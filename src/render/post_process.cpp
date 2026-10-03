#include "post_process.hpp"

#include "battlespades/render/render_views.hpp"

#include <bx/math.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <utility>
#include <vector>

namespace battlespades::render {
namespace {

[[nodiscard]] std::vector<std::uint8_t> read_shader(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        return {};
    }
    return {std::istreambuf_iterator<char>{input}, {}};
}

[[nodiscard]] bgfx::ProgramHandle load_program(const std::filesystem::path& root,
                                               const char* vertex, const char* fragment) {
    const auto vs_bytes = read_shader(root / vertex);
    const auto fs_bytes = read_shader(root / fragment);
    if (vs_bytes.empty() || fs_bytes.empty()) {
        return BGFX_INVALID_HANDLE;
    }
    const auto vs = bgfx::createShader(
        bgfx::copy(vs_bytes.data(), static_cast<std::uint32_t>(vs_bytes.size())));
    const auto fs = bgfx::createShader(
        bgfx::copy(fs_bytes.data(), static_cast<std::uint32_t>(fs_bytes.size())));
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
        if (bgfx::isValid(vs)) bgfx::destroy(vs);
        if (bgfx::isValid(fs)) bgfx::destroy(fs);
        return BGFX_INVALID_HANDLE;
    }
    return bgfx::createProgram(vs, fs, true);
}

[[nodiscard]] bool format_renderable(bgfx::TextureFormat::Enum format, bool sampled) noexcept {
    const auto support = bgfx::getCaps()->formats[format];
    return (support & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0U &&
           (!sampled || (support & BGFX_CAPS_FORMAT_TEXTURE_2D) != 0U);
}

[[nodiscard]] PostExtent halved(PostExtent extent) noexcept {
    return {std::max<std::uint32_t>(extent.width / 2U, 1U),
            std::max<std::uint32_t>(extent.height / 2U, 1U)};
}

template <typename Handle>
void destroy_handle(Handle& handle) noexcept {
    if (bgfx::isValid(handle)) {
        bgfx::destroy(handle);
        handle = BGFX_INVALID_HANDLE;
    }
}

constexpr std::uint64_t pass_state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
constexpr std::uint32_t linear_clamp = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
constexpr std::uint32_t point_clamp =
    BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT |
    BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;

} // namespace

PostProcessor::~PostProcessor() {
    shutdown();
}

void PostProcessor::set_shader_root(std::filesystem::path backend_root) {
    root_ = std::move(backend_root);
}

void PostProcessor::shutdown() noexcept {
    release_targets();
    for (auto* program : {&ssao_, &world_, &bright_, &down_, &up_, &composite_, &rcas_}) {
        destroy_handle(*program);
    }
    for (auto* uniform : {&s_colour_, &s_depth_, &s_ao_, &s_low_, &s_bloom_, &u_texel_,
                          &u_proj_, &u_depth_mode_, &u_ao_, &u_flags_, &u_ao_texel_,
                          &u_reproject_, &u_bloom_, &u_mode_, &u_grade_, &u_sharp_,
                          &u_cvd_[0U], &u_cvd_[1U], &u_cvd_[2U]}) {
        destroy_handle(*uniform);
    }
    destroy_handle(triangle_);
    attempted_ = false;
    caps_ = {};
    previous_.reset();
}

void PostProcessor::release(Target& target) noexcept {
    // Targets own their texture through the framebuffer.
    destroy_handle(target.framebuffer);
    target.texture = BGFX_INVALID_HANDLE;
    target.extent = {};
}

void PostProcessor::release_targets() noexcept {
    destroy_handle(world_with_depth_);
    destroy_handle(scene_);
    destroy_handle(scene_colour_);
    destroy_handle(scene_depth_);
    scene_extent_ = {};
    release(world_colour_);
    release(ao_);
    for (auto& target : bloom_down_) release(target);
    for (auto& target : bloom_up_) release(target);
    release(output_);
}

bool PostProcessor::load() {
    attempted_ = true;
    caps_ = {};
    if (root_.empty() || bgfx::getRendererType() == bgfx::RendererType::Noop) {
        return false;
    }
    if (format_renderable(bgfx::TextureFormat::RGBA16F, true)) {
        colour_format_ = bgfx::TextureFormat::RGBA16F;
    } else if (format_renderable(bgfx::TextureFormat::RGBA8, true)) {
        colour_format_ = bgfx::TextureFormat::RGBA8;
    } else {
        return false;
    }
    if (!format_renderable(bgfx::TextureFormat::RGBA8, true)) {
        return false;
    }
    depth_readable_ = false;
    for (const auto format : {bgfx::TextureFormat::D24S8, bgfx::TextureFormat::D32F,
                              bgfx::TextureFormat::D24, bgfx::TextureFormat::D16}) {
        if (format_renderable(format, true)) {
            depth_format_ = format;
            depth_readable_ = true;
            break;
        }
    }
    if (!depth_readable_) {
        bool found = false;
        for (const auto format : {bgfx::TextureFormat::D24S8, bgfx::TextureFormat::D32F,
                                  bgfx::TextureFormat::D16}) {
            if (format_renderable(format, false)) {
                depth_format_ = format;
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }

    ssao_ = load_program(root_, "vs_post.bin", "fs_post_ssao.bin");
    world_ = load_program(root_, "vs_post.bin", "fs_post_world.bin");
    bright_ = load_program(root_, "vs_post.bin", "fs_post_bright.bin");
    down_ = load_program(root_, "vs_post.bin", "fs_post_down.bin");
    up_ = load_program(root_, "vs_post.bin", "fs_post_up.bin");
    composite_ = load_program(root_, "vs_post.bin", "fs_post_composite.bin");
    rcas_ = load_program(root_, "vs_post.bin", "fs_post_rcas.bin");
    if (!bgfx::isValid(composite_) || !bgfx::isValid(rcas_)) {
        // Without the resolve nothing can reach the screen.
        shutdown();
        attempted_ = true;
        return false;
    }

    s_colour_ = bgfx::createUniform("s_postColor", bgfx::UniformType::Sampler);
    s_depth_ = bgfx::createUniform("s_postDepth", bgfx::UniformType::Sampler);
    s_ao_ = bgfx::createUniform("s_postAo", bgfx::UniformType::Sampler);
    s_low_ = bgfx::createUniform("s_postLow", bgfx::UniformType::Sampler);
    s_bloom_ = bgfx::createUniform("s_postBloom", bgfx::UniformType::Sampler);
    u_texel_ = bgfx::createUniform("u_postTexel", bgfx::UniformType::Vec4);
    u_proj_ = bgfx::createUniform("u_postProj", bgfx::UniformType::Vec4);
    u_depth_mode_ = bgfx::createUniform("u_postDepthMode", bgfx::UniformType::Vec4);
    u_ao_ = bgfx::createUniform("u_postAo", bgfx::UniformType::Vec4);
    u_flags_ = bgfx::createUniform("u_postFlags", bgfx::UniformType::Vec4);
    u_ao_texel_ = bgfx::createUniform("u_postAoTexel", bgfx::UniformType::Vec4);
    u_reproject_ = bgfx::createUniform("u_postReproject", bgfx::UniformType::Mat4);
    u_bloom_ = bgfx::createUniform("u_postBloom", bgfx::UniformType::Vec4);
    u_mode_ = bgfx::createUniform("u_postMode", bgfx::UniformType::Vec4);
    u_grade_ = bgfx::createUniform("u_postGrade", bgfx::UniformType::Vec4);
    u_cvd_[0U] = bgfx::createUniform("u_postCvd0", bgfx::UniformType::Vec4);
    u_cvd_[1U] = bgfx::createUniform("u_postCvd1", bgfx::UniformType::Vec4);
    u_cvd_[2U] = bgfx::createUniform("u_postCvd2", bgfx::UniformType::Vec4);
    u_sharp_ = bgfx::createUniform("u_postSharp", bgfx::UniformType::Vec4);

    // One clip-space triangle covering the screen. Texture v follows the
    // backend's render-target origin so every pass samples upright.
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3U, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2U, bgfx::AttribType::Float)
        .end();
    const bool bottom_left = bgfx::getCaps()->originBottomLeft;
    std::array<float, 15U> vertices{};
    constexpr std::array<std::array<float, 2U>, 3U> corners{{{-1.0F, -1.0F}, {3.0F, -1.0F}, {-1.0F, 3.0F}}};
    for (std::size_t index{}; index < corners.size(); ++index) {
        const auto x = corners[index][0U];
        const auto y = corners[index][1U];
        vertices[index * 5U + 0U] = x;
        vertices[index * 5U + 1U] = y;
        vertices[index * 5U + 2U] = 0.0F;
        vertices[index * 5U + 3U] = (x + 1.0F) * 0.5F;
        vertices[index * 5U + 4U] = bottom_left ? (y + 1.0F) * 0.5F : (1.0F - y) * 0.5F;
    }
    triangle_ = bgfx::createVertexBuffer(
        bgfx::copy(vertices.data(), static_cast<std::uint32_t>(sizeof(vertices))), layout);
    if (!bgfx::isValid(triangle_)) {
        shutdown();
        attempted_ = true;
        return false;
    }

    caps_.chain = true;
    caps_.bloom = bgfx::isValid(bright_) && bgfx::isValid(down_) && bgfx::isValid(up_);
    caps_.ambient_occlusion = depth_readable_ && bgfx::isValid(ssao_) && bgfx::isValid(world_);
    caps_.motion_blur = depth_readable_ && bgfx::isValid(world_);
    caps_.edge_adaptive_upscale = true;
    return true;
}

PostCapabilities PostProcessor::capabilities() {
    if (!attempted_) {
        static_cast<void>(load());
    }
    return caps_;
}

bool PostProcessor::ensure(Target& target, PostExtent extent, bgfx::TextureFormat::Enum format) {
    if (bgfx::isValid(target.framebuffer) && target.extent == extent) {
        return true;
    }
    release(target);
    const auto texture = bgfx::createTexture2D(
        static_cast<std::uint16_t>(extent.width), static_cast<std::uint16_t>(extent.height), false,
        1U, format, BGFX_TEXTURE_RT | linear_clamp);
    if (!bgfx::isValid(texture)) {
        return false;
    }
    target.framebuffer = bgfx::createFrameBuffer(1U, &texture, true);
    if (!bgfx::isValid(target.framebuffer)) {
        bgfx::TextureHandle doomed = texture;
        bgfx::destroy(doomed);
        return false;
    }
    target.texture = texture;
    target.extent = extent;
    return true;
}

bool PostProcessor::ensure_scene(PostExtent extent, bool world_pass) {
    if (!(bgfx::isValid(scene_) && scene_extent_ == extent)) {
        destroy_handle(world_with_depth_);
        destroy_handle(scene_);
        destroy_handle(scene_colour_);
        destroy_handle(scene_depth_);
        scene_extent_ = {};
        const auto width = static_cast<std::uint16_t>(extent.width);
        const auto height = static_cast<std::uint16_t>(extent.height);
        scene_colour_ = bgfx::createTexture2D(width, height, false, 1U, colour_format_,
                                              BGFX_TEXTURE_RT | linear_clamp);
        scene_depth_ = bgfx::createTexture2D(
            width, height, false, 1U, depth_format_,
            depth_readable_ ? (BGFX_TEXTURE_RT | point_clamp) : BGFX_TEXTURE_RT_WRITE_ONLY);
        if (!bgfx::isValid(scene_colour_) || !bgfx::isValid(scene_depth_)) {
            destroy_handle(scene_colour_);
            destroy_handle(scene_depth_);
            return false;
        }
        const std::array attachments{scene_colour_, scene_depth_};
        scene_ = bgfx::createFrameBuffer(2U, attachments.data(), false);
        if (!bgfx::isValid(scene_)) {
            destroy_handle(scene_colour_);
            destroy_handle(scene_depth_);
            return false;
        }
        scene_extent_ = extent;
    }
    if (!world_pass) {
        return true;
    }
    const bool recreated = !(bgfx::isValid(world_colour_.framebuffer) && world_colour_.extent == extent);
    if (recreated) {
        destroy_handle(world_with_depth_);
    }
    if (!ensure(world_colour_, extent, colour_format_)) {
        return false;
    }
    if (!bgfx::isValid(world_with_depth_)) {
        const std::array attachments{world_colour_.texture, scene_depth_};
        world_with_depth_ = bgfx::createFrameBuffer(2U, attachments.data(), false);
    }
    return bgfx::isValid(world_with_depth_);
}

PostProcessor::Frame PostProcessor::begin(const PostSettings& settings, PostExtent drawable) {
    passes_ = 0U;
    const auto unbind = [] {
        bgfx::setViewFrameBuffer(world_view_id, BGFX_INVALID_HANDLE);
        bgfx::setViewFrameBuffer(view_model_view_id, BGFX_INVALID_HANDLE);
        bgfx::setViewFrameBuffer(composite_view_id, BGFX_INVALID_HANDLE);
    };
    if (!settings.active() || drawable.width == 0U || drawable.height == 0U) {
        if (bound_) {
            unbind();
            bound_ = false;
        }
        return {};
    }
    if (!capabilities().chain) {
        return {};
    }
    const bool world_pass =
        (settings.ambient_occlusion != AmbientOcclusion::off && caps_.ambient_occlusion) ||
        (settings.motion_blur > 0.0F && caps_.motion_blur);
    const auto scene = post_scene_extent(
        drawable, settings.render_scale,
        std::min<std::uint32_t>(bgfx::getCaps()->limits.maxTextureSize, 16384U));
    if (!ensure_scene(scene, world_pass)) {
        if (bound_) {
            unbind();
            bound_ = false;
        }
        return {};
    }
    bgfx::setViewFrameBuffer(world_view_id, scene_);
    Frame frame{true, world_pass, scene, view_model_view_id};
    if (world_pass) {
        bgfx::setViewFrameBuffer(view_model_view_id, BGFX_INVALID_HANDLE);
        bgfx::setViewFrameBuffer(post_view_model_view_id, world_with_depth_);
        frame.view_model_view = post_view_model_view_id;
    } else {
        bgfx::setViewFrameBuffer(view_model_view_id, scene_);
    }
    bound_ = true;
    return frame;
}

void PostProcessor::bind(std::uint8_t stage, bgfx::UniformHandle sampler,
                         bgfx::TextureHandle texture, bool point) {
    bgfx::setTexture(stage, sampler, texture, point ? point_clamp : linear_clamp);
}

void PostProcessor::pass(std::uint16_t view, bgfx::FrameBufferHandle framebuffer,
                         PostExtent extent, bgfx::ProgramHandle program) {
    bgfx::setViewName(view, "BattleSpades post");
    bgfx::setViewFrameBuffer(view, framebuffer);
    bgfx::setViewRect(view, 0U, 0U, static_cast<std::uint16_t>(extent.width),
                      static_cast<std::uint16_t>(extent.height));
    bgfx::setViewClear(view, BGFX_CLEAR_NONE);
    bgfx::setViewMode(view, bgfx::ViewMode::Default);
    bgfx::setViewTransform(view, nullptr, nullptr);
    bgfx::setVertexBuffer(0U, triangle_);
    bgfx::setState(pass_state);
    bgfx::submit(view, program);
    ++passes_;
}

void PostProcessor::note_camera(const Camera& camera) noexcept {
    previous_ = camera;
}

void PostProcessor::finish(const Frame& frame, const PostSettings& settings, PostExtent drawable,
                           const Camera& camera) {
    if (!frame.active) {
        return;
    }
    const auto* caps = bgfx::getCaps();
    const std::array<float, 4U> depth_mode{caps->homogeneousDepth ? 1.0F : 0.0F,
                                           caps->originBottomLeft ? 1.0F : -1.0F, 0.0F, 0.0F};
    const std::array<float, 4U> projection{camera.tan_half_fov_x, camera.tan_half_fov_y,
                                           camera.near_plane, camera.far_plane};
    const auto scene = frame.scene;
    bgfx::TextureHandle source = scene_colour_;

    if (frame.world_pass) {
        const bool ao_on =
            settings.ambient_occlusion != AmbientOcclusion::off && caps_.ambient_occlusion;
        std::array<float, 16U> reproject{};
        bx::mtxIdentity(reproject.data());
        bool blur_on = settings.motion_blur > 0.0F && caps_.motion_blur;
        if (blur_on) {
            if (previous_.has_value() &&
                !camera_cut(previous_->eye, camera.eye, previous_->forward, camera.forward)) {
                std::array<float, 16U> inverse{};
                bx::mtxInverse(inverse.data(), camera.view_projection.data());
                bx::mtxMul(reproject.data(), inverse.data(), previous_->view_projection.data());
            } else {
                // A cut or the first frame: nothing to blur against.
                blur_on = false;
            }
        }
        PostExtent ao_extent = scene;
        if (ao_on) {
            const auto parameters = ambient_occlusion_parameters(settings.ambient_occlusion);
            ao_extent = parameters.half_resolution ? halved(scene) : scene;
            if (ensure(ao_, ao_extent, bgfx::TextureFormat::RGBA8)) {
                const std::array<float, 4U> ao{parameters.radius,
                                               static_cast<float>(parameters.samples),
                                               parameters.strength, 0.05F};
                const std::array<float, 4U> texel{1.0F / static_cast<float>(scene.width),
                                                  1.0F / static_cast<float>(scene.height),
                                                  0.0F, 0.0F};
                bind(1U, s_depth_, scene_depth_, true);
                bgfx::setUniform(u_proj_, projection.data());
                bgfx::setUniform(u_depth_mode_, depth_mode.data());
                bgfx::setUniform(u_ao_, ao.data());
                bgfx::setUniform(u_texel_, texel.data());
                pass(post_ssao_view_id, ao_.framebuffer, ao_extent, ssao_);
            }
        }
        const bool ao_ready = ao_on && bgfx::isValid(ao_.framebuffer);
        const std::array<float, 4U> flags{ao_ready ? 1.0F : 0.0F, blur_on ? 1.0F : 0.0F,
                                          std::clamp(settings.motion_blur, 0.0F, 1.0F), 0.0F};
        const std::array<float, 4U> ao_texel{1.0F / static_cast<float>(ao_extent.width),
                                             1.0F / static_cast<float>(ao_extent.height), 0.0F,
                                             0.0F};
        bind(0U, s_colour_, scene_colour_);
        bind(1U, s_depth_, scene_depth_, true);
        // Every declared sampler gets a live texture, even when its branch
        // is off: Vulkan and Metal validate unbound descriptors.
        bind(2U, s_ao_, ao_ready ? ao_.texture : scene_colour_);
        bgfx::setUniform(u_proj_, projection.data());
        bgfx::setUniform(u_depth_mode_, depth_mode.data());
        bgfx::setUniform(u_flags_, flags.data());
        bgfx::setUniform(u_ao_texel_, ao_texel.data());
        bgfx::setUniform(u_reproject_, reproject.data());
        pass(post_world_view_id, world_colour_.framebuffer, scene, world_);
        source = world_colour_.texture;
    }

    bgfx::TextureHandle bloom = BGFX_INVALID_HANDLE;
    const float bloom_intensity = std::clamp(settings.bloom_intensity, 0.0F, 1.0F);
    if (settings.bloom && caps_.bloom && bloom_intensity > 0.0F) {
        std::array<PostExtent, 4U> extents{};
        extents[0U] = halved(scene);
        for (std::size_t level{1U}; level < extents.size(); ++level) {
            extents[level] = halved(extents[level - 1U]);
        }
        bool ready = true;
        for (std::size_t level{}; level < extents.size(); ++level) {
            ready = ready && ensure(bloom_down_[level], extents[level], colour_format_);
        }
        for (std::size_t level{}; level < bloom_up_.size(); ++level) {
            ready = ready && ensure(bloom_up_[level], extents[level], colour_format_);
        }
        if (ready) {
            const auto texel_of = [](PostExtent extent) {
                return std::array<float, 4U>{1.0F / static_cast<float>(extent.width),
                                             1.0F / static_cast<float>(extent.height), 0.0F, 0.0F};
            };
            std::uint16_t view = post_bloom_view_id_base;
            const std::array<float, 4U> threshold{0.72F, 0.25F, 0.0F, 0.0F};
            auto texel = texel_of(scene);
            bind(0U, s_colour_, source);
            bgfx::setUniform(u_texel_, texel.data());
            bgfx::setUniform(u_bloom_, threshold.data());
            pass(view++, bloom_down_[0U].framebuffer, extents[0U], bright_);
            for (std::size_t level{1U}; level < extents.size(); ++level) {
                texel = texel_of(extents[level - 1U]);
                bind(0U, s_colour_, bloom_down_[level - 1U].texture);
                bgfx::setUniform(u_texel_, texel.data());
                pass(view++, bloom_down_[level].framebuffer, extents[level], down_);
            }
            for (std::size_t level = bloom_up_.size(); level-- > 0U;) {
                const bool deepest = level + 1U == bloom_up_.size();
                const auto& low = deepest ? bloom_down_[level + 1U] : bloom_up_[level + 1U];
                texel = texel_of(extents[level + 1U]);
                bind(0U, s_colour_, bloom_down_[level].texture);
                bind(1U, s_low_, low.texture);
                bgfx::setUniform(u_texel_, texel.data());
                pass(view++, bloom_up_[level].framebuffer, extents[level], up_);
            }
            bloom = bloom_up_[0U].texture;
        }
    }

    const bool sharpen = settings.sharpness > 0.0F && ensure(output_, drawable, bgfx::TextureFormat::RGBA8);
    const bool upscale = scene.width < drawable.width || scene.height < drawable.height;
    const float gamma = std::clamp(settings.gamma, 0.5F, 3.0F);
    const std::array<float, 4U> source_texel{
        1.0F / static_cast<float>(scene.width), 1.0F / static_cast<float>(scene.height),
        static_cast<float>(scene.width), static_cast<float>(scene.height)};
    const std::array<float, 4U> mode{
        upscale && settings.upscale == UpscaleFilter::edge_adaptive && caps_.edge_adaptive_upscale
            ? 1.0F
            : 0.0F,
        0.0F, 0.0F, 0.0F};
    const std::array<float, 4U> grade{
        std::clamp(settings.brightness, -0.5F, 0.5F), 1.0F / gamma,
        bgfx::isValid(bloom) ? bloom_intensity * 0.6F : 0.0F,
        settings.color_vision != ColorVisionMode::off ? 1.0F : 0.0F};
    const auto correction = color_vision_correction(settings.color_vision);
    bind(0U, s_colour_, source);
    bind(1U, s_bloom_, bgfx::isValid(bloom) ? bloom : source);
    bgfx::setUniform(u_texel_, source_texel.data());
    bgfx::setUniform(u_mode_, mode.data());
    bgfx::setUniform(u_grade_, grade.data());
    for (std::size_t row{}; row < 3U; ++row) {
        const std::array<float, 4U> values{correction[row][0U], correction[row][1U],
                                           correction[row][2U], 0.0F};
        bgfx::setUniform(u_cvd_[row], values.data());
    }
    if (sharpen) {
        pass(post_upscale_view_id, output_.framebuffer, drawable, composite_);
        const std::array<float, 4U> texel{1.0F / static_cast<float>(drawable.width),
                                          1.0F / static_cast<float>(drawable.height), 0.0F, 0.0F};
        const std::array<float, 4U> sharp{std::clamp(settings.sharpness, 0.0F, 1.0F), 0.0F, 0.0F,
                                          0.0F};
        bind(0U, s_colour_, output_.texture, true);
        bgfx::setUniform(u_texel_, texel.data());
        bgfx::setUniform(u_sharp_, sharp.data());
        pass(composite_view_id, BGFX_INVALID_HANDLE, drawable, rcas_);
    } else {
        pass(composite_view_id, BGFX_INVALID_HANDLE, drawable, composite_);
    }
}

} // namespace battlespades::render
