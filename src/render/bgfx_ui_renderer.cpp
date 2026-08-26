#include "battlespades/render/bgfx_ui_renderer.hpp"

#include "battlespades/render/render_views.hpp"
#include "battlespades/ui/design_canvas.hpp"

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace battlespades::render {
namespace {

// View 1 between the clear and the sprite layers belongs to WorldRenderer;
// see render_views.hpp for the global ordering contract.
constexpr bgfx::ViewId clear_view_id{backdrop_clear_view_id};
constexpr bgfx::ViewId window_view_id{ui_window_view_id};
constexpr bgfx::ViewId ui_view_id{ui_canvas_view_id};
constexpr std::uint32_t vertices_per_sprite{4U};
constexpr std::uint32_t indices_per_sprite{6U};

struct UiVertex final {
    float x{};
    float y{};
    float z{};
    float u{};
    float v{};
    std::uint32_t abgr{};
};

static_assert(sizeof(UiVertex) == 24U);

[[nodiscard]] bgfx::RendererType::Enum backend_type(GraphicsBackend backend) noexcept {
    switch (backend) {
    case GraphicsBackend::automatic:
        return bgfx::RendererType::Count;
    case GraphicsBackend::direct3d11:
        return bgfx::RendererType::Direct3D11;
    case GraphicsBackend::direct3d12:
        return bgfx::RendererType::Direct3D12;
    case GraphicsBackend::vulkan:
        return bgfx::RendererType::Vulkan;
    case GraphicsBackend::opengl:
        return bgfx::RendererType::OpenGL;
    case GraphicsBackend::metal:
        return bgfx::RendererType::Metal;
    }
    return bgfx::RendererType::Count;
}

[[nodiscard]] GraphicsBackend backend_from_type(bgfx::RendererType::Enum backend) noexcept {
    switch (backend) {
    case bgfx::RendererType::Direct3D11:
        return GraphicsBackend::direct3d11;
    case bgfx::RendererType::Direct3D12:
        return GraphicsBackend::direct3d12;
    case bgfx::RendererType::Vulkan:
        return GraphicsBackend::vulkan;
    case bgfx::RendererType::OpenGL:
    case bgfx::RendererType::OpenGLES:
        return GraphicsBackend::opengl;
    case bgfx::RendererType::Metal:
        return GraphicsBackend::metal;
    default:
        return GraphicsBackend::automatic;
    }
}

[[nodiscard]] std::string_view shader_directory(bgfx::RendererType::Enum backend) noexcept {
    switch (backend) {
    case bgfx::RendererType::Direct3D11:
    case bgfx::RendererType::Direct3D12:
        return "dx11";
    case bgfx::RendererType::OpenGL:
        return "glsl";
    case bgfx::RendererType::OpenGLES:
        return "essl";
    case bgfx::RendererType::Metal:
        return "metal";
    case bgfx::RendererType::Vulkan:
        return "spirv";
    default:
        return {};
    }
}

[[nodiscard]] std::uint32_t pack_abgr(UiColor color) noexcept {
    return (static_cast<std::uint32_t>(color.alpha) << 24U) |
           (static_cast<std::uint32_t>(color.blue) << 16U) |
           (static_cast<std::uint32_t>(color.green) << 8U) | static_cast<std::uint32_t>(color.red);
}

[[nodiscard]] bool is_finite(UiRect rectangle) noexcept {
    return std::isfinite(rectangle.x) && std::isfinite(rectangle.y) &&
           std::isfinite(rectangle.width) && std::isfinite(rectangle.height);
}

[[nodiscard]] std::optional<UiRect> intersect(UiRect left, UiRect right) noexcept {
    const auto x = std::max(left.x, right.x);
    const auto y = std::max(left.y, right.y);
    const auto far_x = std::min(left.x + left.width, right.x + right.width);
    const auto far_y = std::min(left.y + left.height, right.y + right.height);
    if (far_x <= x || far_y <= y) {
        return std::nullopt;
    }
    return UiRect{x, y, far_x - x, far_y - y};
}

[[nodiscard]] std::uint64_t texture_flags(TextureFilter filter) noexcept {
    auto flags = static_cast<std::uint64_t>(BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    if (filter == TextureFilter::nearest) {
        flags |= static_cast<std::uint64_t>(BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT |
                                            BGFX_SAMPLER_MIP_POINT);
    }
    return flags;
}

[[nodiscard]] std::uint32_t reset_flags(const BgfxUiRendererConfig& config) noexcept {
    std::uint32_t flags = BGFX_RESET_NONE;
    if (config.vertical_sync) {
        flags |= BGFX_RESET_VSYNC;
    }
    // Retail exposes 2x and 4x as distinct choices, so they must map to
    // distinct reset flags; collapsing both to X4 made picking 2x silently
    // cost the same as 4x.
    if (config.multisample_samples >= 4U) {
        flags |= BGFX_RESET_MSAA_X4;
    } else if (config.multisample_samples >= 2U) {
        flags |= BGFX_RESET_MSAA_X2;
    }
    return flags;
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>>
read_binary(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary | std::ios::ate};
    if (!stream) {
        return std::nullopt;
    }

    const auto end = stream.tellg();
    if (end <= std::streampos{0}) {
        return std::nullopt;
    }
    const auto size = static_cast<std::uintmax_t>(end);
    if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::uint32_t>::max()) ||
        size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    stream.seekg(0, std::ios::beg);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        return std::nullopt;
    }
    return bytes;
}

[[nodiscard]] bool is_below_root(const std::filesystem::path& root,
                                 const std::filesystem::path& candidate) {
    const auto relative = candidate.lexically_relative(root);
    if (relative.empty() || relative.is_absolute()) {
        return false;
    }
    const auto first = relative.begin();
    return first != relative.end() && *first != std::filesystem::path{".."};
}

} // namespace

std::string_view graphics_backend_name(GraphicsBackend backend) noexcept {
    switch (backend) {
    case GraphicsBackend::automatic:
        return "Auto";
    case GraphicsBackend::direct3d11:
        return "Direct3D 11";
    case GraphicsBackend::direct3d12:
        return "Direct3D 12";
    case GraphicsBackend::vulkan:
        return "Vulkan";
    case GraphicsBackend::opengl:
        return "OpenGL";
    case GraphicsBackend::metal:
        return "Metal";
    }
    return "Unknown";
}

std::vector<GraphicsBackend> supported_graphics_backends() {
    std::array<bgfx::RendererType::Enum, bgfx::RendererType::Count> supported{};
    const auto count =
        bgfx::getSupportedRenderers(static_cast<std::uint8_t>(supported.size()), supported.data());
    const auto end = supported.begin() + count;
    const auto contains = [&supported, end](bgfx::RendererType::Enum backend) {
        return std::find(supported.begin(), end, backend) != end;
    };

    std::vector<GraphicsBackend> result{GraphicsBackend::automatic};
    constexpr std::array candidates{
        GraphicsBackend::direct3d11,
        GraphicsBackend::direct3d12,
        GraphicsBackend::vulkan,
        GraphicsBackend::opengl,
        GraphicsBackend::metal,
    };
    for (const auto candidate : candidates) {
        const auto native = backend_type(candidate);
        const bool available =
            candidate == GraphicsBackend::opengl
                ? contains(bgfx::RendererType::OpenGL) || contains(bgfx::RendererType::OpenGLES)
                : contains(native);
        if (available) {
            result.push_back(candidate);
        }
    }
    return result;
}

UiTextureDecodeResult decode_png_rgba8(const std::filesystem::path& absolute_path) {
    if (!absolute_path.is_absolute()) {
        return {std::nullopt, "PNG preload path must be absolute"};
    }
    auto extension = absolute_path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (extension != ".png") {
        return {std::nullopt, "UI texture preload accepts PNG assets only"};
    }

    const auto bytes = read_binary(absolute_path);
    if (!bytes.has_value()) {
        return {std::nullopt, "unable to read PNG asset: " + absolute_path.string()};
    }

    bx::DefaultAllocator allocator;
    auto* image = bimg::imageParse(&allocator,
                                   bytes->data(),
                                   static_cast<std::uint32_t>(bytes->size()),
                                   bimg::TextureFormat::RGBA8);
    if (image == nullptr) {
        return {std::nullopt, "bimg could not decode PNG asset: " + absolute_path.string()};
    }

    constexpr auto texture_limit = static_cast<std::uint32_t>(UINT16_MAX);
    const auto supported = image->m_width > 0U && image->m_height > 0U &&
                           image->m_width <= texture_limit && image->m_height <= texture_limit &&
                           image->m_depth == 1U && image->m_numLayers == 1U && !image->m_cubeMap;
    const auto expected_size = static_cast<std::uint64_t>(image->m_width) * image->m_height * 4U;
    if (!supported || expected_size > image->m_size ||
        expected_size > std::numeric_limits<std::size_t>::max()) {
        bimg::imageFree(image);
        return {std::nullopt, "PNG asset is not a supported two-dimensional RGBA8 texture"};
    }

    DecodedUiTexture decoded;
    decoded.extent = UiExtent{image->m_width, image->m_height};
    const auto* first = static_cast<const std::uint8_t*>(image->m_data);
    decoded.rgba8.assign(first, first + static_cast<std::size_t>(expected_size));
    bimg::imageFree(image);
    return {std::move(decoded), {}};
}

struct BgfxUiRenderer::Impl final {
    struct TextureSlot final {
        bgfx::TextureHandle native{bgfx::kInvalidHandle};
        std::filesystem::path canonical_path{};
        UiExtent extent{};
        TextureFilter filter{TextureFilter::linear};
        std::uint32_t generation{1U};
        std::uint32_t references{};
    };

    using CacheKey = std::pair<std::filesystem::path, TextureFilter>;

    bool fail(std::string message) {
        error = std::move(message);
        return false;
    }

    [[nodiscard]] bool check_thread() {
        if (initialized && owner_thread != std::this_thread::get_id()) {
            return fail("bgfx UI renderer used from a thread other than its initializing thread");
        }
        return true;
    }

    [[nodiscard]] TextureSlot* find(UiTexture texture) noexcept {
        if (!texture.is_valid() || texture.index >= textures.size()) {
            return nullptr;
        }
        auto& slot = textures[texture.index];
        if (!bgfx::isValid(slot.native) || slot.generation != texture.generation) {
            return nullptr;
        }
        return &slot;
    }

    [[nodiscard]] const TextureSlot* find(UiTexture texture) const noexcept {
        if (!texture.is_valid() || texture.index >= textures.size()) {
            return nullptr;
        }
        const auto& slot = textures[texture.index];
        if (!bgfx::isValid(slot.native) || slot.generation != texture.generation) {
            return nullptr;
        }
        return &slot;
    }

    [[nodiscard]] std::optional<std::filesystem::path>
    resolve_asset(const std::filesystem::path& relative) {
        if (relative.empty() || relative.is_absolute()) {
            fail("texture path must be relative to the configured asset root");
            return std::nullopt;
        }

        const auto quality_relative =
            texture_quality_asset(relative, config.texture_quality);
        std::error_code error_code;
        const auto candidate =
            std::filesystem::weakly_canonical(asset_root / quality_relative, error_code);
        if (error_code || !is_below_root(asset_root, candidate)) {
            fail("texture path escapes the configured asset root");
            return std::nullopt;
        }
        return candidate;
    }

    [[nodiscard]] bgfx::ShaderHandle load_shader(const std::filesystem::path& path) {
        const auto bytes = read_binary(path);
        if (!bytes.has_value()) {
            fail("unable to read compiled shader: " + path.string());
            return BGFX_INVALID_HANDLE;
        }
        const auto* memory = bgfx::copy(bytes->data(), static_cast<std::uint32_t>(bytes->size()));
        const auto shader = bgfx::createShader(memory);
        if (!bgfx::isValid(shader)) {
            fail("bgfx rejected compiled shader: " + path.string());
        }
        return shader;
    }

    [[nodiscard]] bool create_program() {
        const auto directory = shader_directory(bgfx::getRendererType());
        if (directory.empty()) {
            return fail("the selected bgfx backend has no BattleSpades UI shader variant");
        }
        const auto backend_root = shader_root / directory;
        const auto vertex = load_shader(backend_root / "vs_ui.bin");
        if (!bgfx::isValid(vertex)) {
            return false;
        }
        const auto fragment = load_shader(backend_root / "fs_ui.bin");
        if (!bgfx::isValid(fragment)) {
            bgfx::destroy(vertex);
            return false;
        }

        program = bgfx::createProgram(vertex, fragment, true);
        if (!bgfx::isValid(program)) {
            return fail("bgfx could not link the UI shader program");
        }
        return true;
    }

    [[nodiscard]] bool configure_views() {
        const auto drawable_width = drawable.width;
        const auto drawable_height = drawable.height;
        const auto logical_width = design.width;
        const auto logical_height = design.height;
        if (!drawable.is_valid()) {
            return fail("cannot begin a frame while the drawable is minimized");
        }

        constexpr auto view_limit = static_cast<std::uint32_t>(UINT16_MAX);
        constexpr auto canvas_limit =
            static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
        if (drawable_width > view_limit || drawable_height > view_limit) {
            return fail("drawable extent exceeds bgfx's 16-bit view rectangle limit");
        }
        if (logical_width > canvas_limit || logical_height > canvas_limit) {
            return fail("design extent exceeds the shared canvas coordinate limit");
        }

        const ui::DesignCanvas canvas{static_cast<std::int32_t>(logical_width),
                                      static_cast<std::int32_t>(logical_height)};
        const auto viewport = canvas.viewport(ui::PixelExtent{
            static_cast<std::int32_t>(drawable_width), static_cast<std::int32_t>(drawable_height)});
        if (!viewport.has_value()) {
            return fail("the design canvas could not map the drawable extent");
        }

        bgfx::setViewRect(clear_view_id,
                          0U,
                          0U,
                          static_cast<std::uint16_t>(drawable_width),
                          static_cast<std::uint16_t>(drawable_height));
        bgfx::setViewClear(clear_view_id, BGFX_CLEAR_COLOR, config.clear_rgba);
        bgfx::setViewMode(clear_view_id, bgfx::ViewMode::Sequential);

        bgfx::setViewRect(window_view_id,
                          0U,
                          0U,
                          static_cast<std::uint16_t>(drawable_width),
                          static_cast<std::uint16_t>(drawable_height));
        bgfx::setViewClear(window_view_id, BGFX_CLEAR_NONE);
        bgfx::setViewMode(window_view_id, bgfx::ViewMode::Sequential);

        bgfx::setViewRect(ui_view_id,
                          0U,
                          0U,
                          static_cast<std::uint16_t>(drawable_width),
                          static_cast<std::uint16_t>(drawable_height));
        bgfx::setViewClear(ui_view_id, BGFX_CLEAR_NONE);
        bgfx::setViewMode(ui_view_id, bgfx::ViewMode::Sequential);

        std::array<float, 16U> view{};
        std::array<float, 16U> window_projection{};
        std::array<float, 16U> projection{};
        bx::mtxIdentity(view.data());
        bx::mtxOrtho(window_projection.data(),
                     0.0F,
                     static_cast<float>(drawable_width),
                     static_cast<float>(drawable_height),
                     0.0F,
                     -1.0F,
                     1.0F,
                     0.0F,
                     bgfx::getCaps()->homogeneousDepth);
        // Retail translates by the truncated get_aspect origin, then scales by
        // its unrounded ratio. A full-drawable view plus these logical bounds
        // reproduces that transform without forcing bgfx's integer view rect
        // to choose a second, slightly different scale.
        const auto canvas_left = -static_cast<double>(viewport->x) / viewport->scale;
        const auto canvas_top = -static_cast<double>(viewport->y) / viewport->scale;
        const auto canvas_right =
            (static_cast<double>(drawable_width) - static_cast<double>(viewport->x)) /
            viewport->scale;
        const auto canvas_bottom =
            (static_cast<double>(drawable_height) - static_cast<double>(viewport->y)) /
            viewport->scale;
        bx::mtxOrtho(projection.data(),
                     static_cast<float>(canvas_left),
                     static_cast<float>(canvas_right),
                     static_cast<float>(canvas_bottom),
                     static_cast<float>(canvas_top),
                     -1.0F,
                     1.0F,
                     0.0F,
                     bgfx::getCaps()->homogeneousDepth);
        bgfx::setViewTransform(window_view_id, view.data(), window_projection.data());
        bgfx::setViewTransform(ui_view_id, view.data(), projection.data());
        bgfx::touch(clear_view_id);
        bgfx::touch(window_view_id);
        bgfx::touch(ui_view_id);
        return true;
    }

    [[nodiscard]] bool submit_sprite(const UiSprite& sprite) {
        const auto* texture = find(sprite.texture);
        if (texture == nullptr) {
            return fail("queued sprite references a stale texture handle");
        }

        auto destination = sprite.destination;
        auto source = sprite.source_pixels.value_or(UiRect{
            0.0F,
            0.0F,
            static_cast<float>(texture->extent.width),
            static_cast<float>(texture->extent.height),
        });

        std::optional<UiRect> drawable_clip;
        if (sprite.clip.has_value()) {
            auto clip = *sprite.clip;
            if (sprite.space == UiDrawSpace::design_canvas) {
                const ui::DesignCanvas canvas{static_cast<std::int32_t>(design.width),
                                              static_cast<std::int32_t>(design.height)};
                const auto viewport =
                    canvas.viewport(ui::PixelExtent{static_cast<std::int32_t>(drawable.width),
                                                    static_cast<std::int32_t>(drawable.height)});
                if (!viewport.has_value()) {
                    return fail("sprite clip could not map the design canvas");
                }
                clip = UiRect{
                    static_cast<float>(viewport->x) + clip.x * static_cast<float>(viewport->scale),
                    static_cast<float>(viewport->y) + clip.y * static_cast<float>(viewport->scale),
                    clip.width * static_cast<float>(viewport->scale),
                    clip.height * static_cast<float>(viewport->scale),
                };
            }
            drawable_clip = intersect(clip,
                                      UiRect{0.0F,
                                             0.0F,
                                             static_cast<float>(drawable.width),
                                             static_cast<float>(drawable.height)});
            if (!drawable_clip.has_value()) {
                return true;
            }
        }

        bgfx::TransientVertexBuffer vertex_buffer{};
        bgfx::TransientIndexBuffer index_buffer{};
        if (!bgfx::allocTransientBuffers(&vertex_buffer,
                                         vertex_layout,
                                         vertices_per_sprite,
                                         &index_buffer,
                                         indices_per_sprite)) {
            return fail("bgfx transient UI geometry budget was exhausted");
        }

        const auto inverse_width = 1.0F / static_cast<float>(texture->extent.width);
        const auto inverse_height = 1.0F / static_cast<float>(texture->extent.height);
        const auto u0 = source.x * inverse_width;
        const auto v0 = source.y * inverse_height;
        const auto u1 = (source.x + source.width) * inverse_width;
        const auto v1 = (source.y + source.height) * inverse_height;
        const auto far_x = destination.x + destination.width;
        const auto far_y = destination.y + destination.height;
        const auto color = pack_abgr(sprite.tint);

        // The UI projection is y-down. Applying the standard positive-angle
        // rotation formula therefore rotates clockwise on screen, which is
        // the convention used by pyglet Sprite.rotation in the retail client.
        constexpr float degrees_to_radians{3.14159265358979323846F / 180.0F};
        const auto angle = sprite.rotation_degrees * degrees_to_radians;
        const auto cosine = std::cos(angle);
        const auto sine = std::sin(angle);
        const auto center_x = destination.x + destination.width * 0.5F;
        const auto center_y = destination.y + destination.height * 0.5F;
        const auto rotate = [center_x, center_y, cosine, sine](float x, float y) {
            const auto relative_x = x - center_x;
            const auto relative_y = y - center_y;
            return std::array<float, 2U>{
                center_x + cosine * relative_x - sine * relative_y,
                center_y + sine * relative_x + cosine * relative_y,
            };
        };
        const auto top_left = rotate(destination.x, destination.y);
        const auto top_right = rotate(far_x, destination.y);
        const auto bottom_right = rotate(far_x, far_y);
        const auto bottom_left = rotate(destination.x, far_y);
        const std::array vertices{
            UiVertex{top_left[0U], top_left[1U], 0.0F, u0, v0, color},
            UiVertex{top_right[0U], top_right[1U], 0.0F, u1, v0, color},
            UiVertex{bottom_right[0U], bottom_right[1U], 0.0F, u1, v1, color},
            UiVertex{bottom_left[0U], bottom_left[1U], 0.0F, u0, v1, color},
        };
        constexpr std::array<std::uint16_t, indices_per_sprite> indices{0U, 1U, 2U, 0U, 2U, 3U};
        std::memcpy(vertex_buffer.data, vertices.data(), sizeof(vertices));
        std::memcpy(index_buffer.data, indices.data(), sizeof(indices));

        bgfx::setVertexBuffer(0U, &vertex_buffer);
        bgfx::setIndexBuffer(&index_buffer);
        bgfx::setTexture(0U, sampler, texture->native);
        // Keep the operating-system backbuffer opaque. bgfx's Vulkan
        // swapchain uses VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR when the surface
        // advertises it, so writing a sprite's blended alpha here makes DWM
        // composite the desktop through the game window. RGB still uses
        // straight-alpha blending; retaining the destination alpha preserves
        // the opaque value established by the frame clear.
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_ALPHA | BGFX_STATE_MSAA);
        if (drawable_clip.has_value()) {
            const auto left = std::floor(drawable_clip->x);
            const auto top = std::floor(drawable_clip->y);
            const auto right = std::ceil(drawable_clip->x + drawable_clip->width);
            const auto bottom = std::ceil(drawable_clip->y + drawable_clip->height);
            bgfx::setScissor(static_cast<std::uint16_t>(left),
                             static_cast<std::uint16_t>(top),
                             static_cast<std::uint16_t>(right - left),
                             static_cast<std::uint16_t>(bottom - top));
        }
        const auto view = sprite.space == UiDrawSpace::window_pixels ? window_view_id : ui_view_id;
        bgfx::submit(view, program);
        return true;
    }

    BgfxUiRendererConfig config{};
    UiExtent drawable{};
    UiExtent design{};
    std::filesystem::path asset_root{};
    std::filesystem::path shader_root{};
    bx::DefaultAllocator allocator{};
    bgfx::VertexLayout vertex_layout{};
    bgfx::UniformHandle sampler{bgfx::kInvalidHandle};
    bgfx::ProgramHandle program{bgfx::kInvalidHandle};
    std::vector<TextureSlot> textures{};
    std::vector<std::uint32_t> free_slots{};
    std::map<CacheKey, std::uint32_t> texture_cache{};
    std::vector<UiSprite> sprites{};
    std::thread::id owner_thread{};
    std::string error{};
    bool initialized{false};
    bool frame_open{false};
};

BgfxUiRenderer::BgfxUiRenderer() : impl_{std::make_unique<Impl>()} {}

BgfxUiRenderer::~BgfxUiRenderer() {
    shutdown();
}

bool BgfxUiRenderer::initialize(const BgfxUiRendererConfig& config) {
    if (impl_->initialized) {
        return impl_->fail("bgfx UI renderer is already initialized");
    }
    if (config.native_window.window == nullptr) {
        return impl_->fail("a native window handle is required for UI rendering");
    }
    if (!config.drawable_extent.is_valid() || !config.design_extent.is_valid()) {
        return impl_->fail("drawable and design extents must be non-zero");
    }

    std::error_code error_code;
    const auto asset_root = std::filesystem::weakly_canonical(config.asset_root, error_code);
    if (error_code || !std::filesystem::is_directory(asset_root)) {
        return impl_->fail("configured asset root is not an accessible directory");
    }
    error_code.clear();
    const auto shader_root = std::filesystem::weakly_canonical(config.shader_root, error_code);
    if (error_code || !std::filesystem::is_directory(shader_root)) {
        return impl_->fail("configured shader root is not an accessible directory");
    }

    bgfx::Init init{};
    init.type = backend_type(config.backend);
    init.vendorId = BGFX_PCI_ID_NONE;
    init.debug = config.debug_device;
    init.profile = false;
    init.platformData.ndt = config.native_window.display;
    init.platformData.nwh = config.native_window.window;
    init.platformData.context = config.native_window.graphics_context;
    init.platformData.backBuffer = config.native_window.back_buffer;
    init.platformData.backBufferDS = config.native_window.depth_stencil;
    init.platformData.type = config.native_window.wayland ? bgfx::NativeWindowHandleType::Wayland
                                                          : bgfx::NativeWindowHandleType::Default;
    init.resolution.width = config.drawable_extent.width;
    init.resolution.height = config.drawable_extent.height;
    init.resolution.reset = reset_flags(config);

    impl_->owner_thread = std::this_thread::get_id();
#if defined(__APPLE__)
    // Metal creates and attaches its CAMetalLayer through AppKit. In bgfx's
    // default multithreaded mode the main thread waits for renderer startup,
    // while SwapChainMtl dispatches back to that blocked main thread: a hard
    // semaphore deadlock before the first frame. Calling renderFrame before
    // init is bgfx's documented opt-in to single-threaded rendering, keeping
    // Metal swap-chain creation and all later submissions on the AppKit thread.
    static_cast<void>(bgfx::renderFrame());
#endif
    if (!bgfx::init(init)) {
        impl_->owner_thread = {};
        return impl_->fail("bgfx failed to initialize the requested graphics backend");
    }

    impl_->initialized = true;
    impl_->config = config;
    impl_->drawable = config.drawable_extent;
    impl_->design = config.design_extent;
    impl_->asset_root = asset_root;
    impl_->shader_root = shader_root;
    impl_->vertex_layout.begin(bgfx::getRendererType())
        .add(bgfx::Attrib::Position, 3U, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2U, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4U, bgfx::AttribType::Uint8, true)
        .end();
    impl_->sampler = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(impl_->sampler)) {
        impl_->fail("bgfx could not create the UI texture sampler");
        shutdown();
        return false;
    }
    if (!impl_->create_program()) {
        const auto saved_error = impl_->error;
        shutdown();
        impl_->error = saved_error;
        return false;
    }

    bgfx::setViewName(clear_view_id, "BattleSpades UI clear");
    bgfx::setViewName(window_view_id, "BattleSpades UI window layer");
    bgfx::setViewName(ui_view_id, "BattleSpades Classic UI");
    bgfx::setDebug(BGFX_DEBUG_NONE);
    impl_->error.clear();
    return true;
}

void BgfxUiRenderer::shutdown() noexcept {
    if (!impl_->initialized) {
        return;
    }

    impl_->sprites.clear();
    impl_->frame_open = false;
    for (auto& texture : impl_->textures) {
        if (bgfx::isValid(texture.native)) {
            bgfx::destroy(texture.native);
            texture.native = BGFX_INVALID_HANDLE;
        }
    }
    impl_->texture_cache.clear();
    impl_->free_slots.clear();
    impl_->textures.clear();
    if (bgfx::isValid(impl_->program)) {
        bgfx::destroy(impl_->program);
        impl_->program = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(impl_->sampler)) {
        bgfx::destroy(impl_->sampler);
        impl_->sampler = BGFX_INVALID_HANDLE;
    }
    bgfx::shutdown();

    impl_->initialized = false;
    impl_->owner_thread = {};
    impl_->drawable = {};
    impl_->design = {};
    impl_->asset_root.clear();
    impl_->shader_root.clear();
}

bool BgfxUiRenderer::is_initialized() const noexcept {
    return impl_->initialized;
}

std::string_view BgfxUiRenderer::last_error() const noexcept {
    return impl_->error;
}

GraphicsBackend BgfxUiRenderer::active_backend() const noexcept {
    if (!impl_->initialized) {
        return GraphicsBackend::automatic;
    }
    return backend_from_type(bgfx::getRendererType());
}

bool BgfxUiRenderer::resize(UiExtent drawable_extent) {
    if (!impl_->initialized || !impl_->check_thread()) {
        return impl_->fail("cannot resize an uninitialized bgfx UI renderer");
    }
    if (impl_->frame_open) {
        return impl_->fail("cannot resize while a UI frame is open");
    }
    impl_->drawable = drawable_extent;
    if (!drawable_extent.is_valid()) {
        impl_->error.clear();
        return true;
    }
    bgfx::reset(drawable_extent.width, drawable_extent.height, reset_flags(impl_->config));
    impl_->error.clear();
    return true;
}

bool BgfxUiRenderer::set_presentation_options(bool vertical_sync,
                                              std::uint8_t multisample_samples) {
    if (!impl_->initialized || !impl_->check_thread()) {
        return impl_->fail(!impl_->initialized
                               ? "bgfx UI renderer is not initialized"
                               : "bgfx UI presentation options changed from a non-owner thread");
    }
    if (impl_->frame_open) {
        return impl_->fail("bgfx UI presentation options cannot change during an open frame");
    }

    if (impl_->config.vertical_sync == vertical_sync &&
        impl_->config.multisample_samples == multisample_samples) {
        return true;
    }

    impl_->config.vertical_sync = vertical_sync;
    impl_->config.multisample_samples = multisample_samples;
    if (impl_->drawable.is_valid()) {
        bgfx::reset(impl_->drawable.width, impl_->drawable.height, reset_flags(impl_->config));
    }
    impl_->error.clear();
    return true;
}

std::optional<UiTextureInfo> BgfxUiRenderer::load_texture(const std::filesystem::path& asset_path,
                                                          TextureFilter filter) {
    if (!impl_->initialized || !impl_->check_thread()) {
        impl_->fail("cannot load a texture before the bgfx UI renderer is initialized");
        return std::nullopt;
    }
    if (impl_->frame_open) {
        impl_->fail("cannot load a texture while a UI frame is open");
        return std::nullopt;
    }

    const auto resolved = impl_->resolve_asset(asset_path);
    if (!resolved.has_value()) {
        return std::nullopt;
    }
    auto extension = resolved->extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (extension != ".png") {
        impl_->fail("the Classic UI texture cache accepts PNG assets only");
        return std::nullopt;
    }

    const Impl::CacheKey key{*resolved, filter};
    if (const auto cached = impl_->texture_cache.find(key); cached != impl_->texture_cache.end()) {
        auto& slot = impl_->textures[cached->second];
        if (slot.references == std::numeric_limits<std::uint32_t>::max()) {
            impl_->fail("texture reference count overflow");
            return std::nullopt;
        }
        ++slot.references;
        impl_->error.clear();
        return UiTextureInfo{UiTexture{cached->second, slot.generation}, slot.extent};
    }

    const auto bytes = read_binary(*resolved);
    if (!bytes.has_value()) {
        impl_->fail("unable to read PNG asset: " + resolved->string());
        return std::nullopt;
    }
    auto* image = bimg::imageParse(&impl_->allocator,
                                   bytes->data(),
                                   static_cast<std::uint32_t>(bytes->size()),
                                   bimg::TextureFormat::RGBA8);
    if (image == nullptr) {
        impl_->fail("bimg could not decode PNG asset: " + resolved->string());
        return std::nullopt;
    }

    constexpr auto texture_limit = static_cast<std::uint32_t>(UINT16_MAX);
    if (image->m_width == 0U || image->m_height == 0U || image->m_width > texture_limit ||
        image->m_height > texture_limit || image->m_depth != 1U || image->m_numLayers != 1U ||
        image->m_cubeMap) {
        bimg::imageFree(image);
        impl_->fail("PNG asset is not a supported two-dimensional UI texture");
        return std::nullopt;
    }

    const auto* texture_memory = bgfx::copy(image->m_data, image->m_size);
    const auto native = bgfx::createTexture2D(static_cast<std::uint16_t>(image->m_width),
                                              static_cast<std::uint16_t>(image->m_height),
                                              image->m_numMips > 1U,
                                              1U,
                                              bgfx::TextureFormat::RGBA8,
                                              texture_flags(filter),
                                              texture_memory);
    const UiExtent extent{image->m_width, image->m_height};
    bimg::imageFree(image);
    if (!bgfx::isValid(native)) {
        impl_->fail("bgfx could not create texture for PNG asset: " + resolved->string());
        return std::nullopt;
    }

    std::uint32_t index{};
    if (!impl_->free_slots.empty()) {
        index = impl_->free_slots.back();
        impl_->free_slots.pop_back();
    } else {
        if (impl_->textures.size() >=
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            bgfx::destroy(native);
            impl_->fail("texture cache handle space exhausted");
            return std::nullopt;
        }
        index = static_cast<std::uint32_t>(impl_->textures.size());
        impl_->textures.emplace_back();
    }

    auto& slot = impl_->textures[index];
    slot.native = native;
    slot.canonical_path = *resolved;
    slot.extent = extent;
    slot.filter = filter;
    slot.references = 1U;
    impl_->texture_cache.emplace(key, index);
    impl_->error.clear();
    return UiTextureInfo{UiTexture{index, slot.generation}, extent};
}

std::optional<UiTextureInfo> BgfxUiRenderer::create_texture_rgba8(
    std::span<const std::uint8_t> pixels, UiExtent extent, TextureFilter filter) {
    if (!impl_->initialized || !impl_->check_thread()) {
        impl_->fail("cannot create a texture before the bgfx UI renderer is initialized");
        return std::nullopt;
    }
    if (impl_->frame_open) {
        impl_->fail("cannot create a texture while a UI frame is open");
        return std::nullopt;
    }
    constexpr auto texture_limit = static_cast<std::uint32_t>(UINT16_MAX);
    if (!extent.is_valid() || extent.width > texture_limit || extent.height > texture_limit) {
        impl_->fail("RGBA8 texture extent exceeds bgfx's 16-bit texture limit");
        return std::nullopt;
    }

    const auto required_size =
        static_cast<std::uint64_t>(extent.width) * static_cast<std::uint64_t>(extent.height) * 4U;
    if (required_size != static_cast<std::uint64_t>(pixels.size()) ||
        required_size > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
        impl_->fail("RGBA8 pixels must contain exactly width * height * 4 bytes");
        return std::nullopt;
    }

    const auto* texture_memory =
        bgfx::copy(pixels.data(), static_cast<std::uint32_t>(pixels.size()));
    const auto native = bgfx::createTexture2D(static_cast<std::uint16_t>(extent.width),
                                              static_cast<std::uint16_t>(extent.height),
                                              false,
                                              1U,
                                              bgfx::TextureFormat::RGBA8,
                                              texture_flags(filter),
                                              texture_memory);
    if (!bgfx::isValid(native)) {
        impl_->fail("bgfx could not create an RGBA8 runtime texture");
        return std::nullopt;
    }

    std::uint32_t index{};
    if (!impl_->free_slots.empty()) {
        index = impl_->free_slots.back();
        impl_->free_slots.pop_back();
    } else {
        if (impl_->textures.size() >=
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            bgfx::destroy(native);
            impl_->fail("texture cache handle space exhausted");
            return std::nullopt;
        }
        index = static_cast<std::uint32_t>(impl_->textures.size());
        impl_->textures.emplace_back();
    }

    auto& slot = impl_->textures[index];
    slot.native = native;
    slot.canonical_path.clear();
    slot.extent = extent;
    slot.filter = filter;
    slot.references = 1U;
    impl_->error.clear();
    return UiTextureInfo{UiTexture{index, slot.generation}, extent};
}

bool BgfxUiRenderer::update_texture_rgba8(UiTexture texture, std::span<const std::uint8_t> pixels) {
    if (!impl_->initialized || !impl_->check_thread()) {
        return impl_->fail("cannot update a texture before the bgfx UI renderer is initialized");
    }
    if (impl_->frame_open) {
        return impl_->fail("cannot update a texture while a UI frame is open");
    }
    auto* slot = impl_->find(texture);
    if (slot == nullptr || slot->references == 0U) {
        return impl_->fail("cannot update a stale texture handle");
    }
    const auto required_size = static_cast<std::uint64_t>(slot->extent.width) *
                               static_cast<std::uint64_t>(slot->extent.height) * 4U;
    if (required_size != static_cast<std::uint64_t>(pixels.size()) ||
        required_size > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
        return impl_->fail("updated RGBA8 pixels must match the existing texture extent");
    }
    const auto* memory = bgfx::copy(pixels.data(), static_cast<std::uint32_t>(pixels.size()));
    bgfx::updateTexture2D(slot->native,
                          0U,
                          0U,
                          0U,
                          0U,
                          static_cast<std::uint16_t>(slot->extent.width),
                          static_cast<std::uint16_t>(slot->extent.height),
                          memory);
    impl_->error.clear();
    return true;
}

bool BgfxUiRenderer::release_texture(UiTexture texture) {
    if (!impl_->initialized || !impl_->check_thread()) {
        return impl_->fail("cannot release a texture before renderer initialization");
    }
    if (impl_->frame_open) {
        return impl_->fail("cannot release a texture while a UI frame is open");
    }
    auto* slot = impl_->find(texture);
    if (slot == nullptr || slot->references == 0U) {
        return impl_->fail("cannot release a stale texture handle");
    }

    --slot->references;
    if (slot->references == 0U) {
        if (!slot->canonical_path.empty()) {
            impl_->texture_cache.erase(Impl::CacheKey{slot->canonical_path, slot->filter});
        }
        bgfx::destroy(slot->native);
        slot->native = BGFX_INVALID_HANDLE;
        slot->canonical_path.clear();
        slot->extent = {};
        ++slot->generation;
        if (slot->generation == 0U) {
            slot->generation = 1U;
        }
        impl_->free_slots.push_back(texture.index);
    }
    impl_->error.clear();
    return true;
}

bool BgfxUiRenderer::begin_frame() {
    if (!impl_->initialized || !impl_->check_thread()) {
        return impl_->fail("cannot begin a frame before renderer initialization");
    }
    if (impl_->frame_open) {
        return impl_->fail("a UI frame is already open");
    }
    impl_->sprites.clear();
    if (!impl_->configure_views()) {
        return false;
    }
    impl_->frame_open = true;
    impl_->error.clear();
    return true;
}

bool BgfxUiRenderer::draw(const UiSprite& sprite) {
    if (!impl_->frame_open || !impl_->check_thread()) {
        return impl_->fail("draw requires an open UI frame on the renderer thread");
    }
    const auto* texture = impl_->find(sprite.texture);
    if (texture == nullptr) {
        return impl_->fail("sprite references a stale texture handle");
    }
    if (!is_finite(sprite.destination) || !sprite.destination.has_area()) {
        return impl_->fail("sprite destination must be finite and have positive area");
    }
    if (sprite.source_pixels.has_value()) {
        const auto source = *sprite.source_pixels;
        if (!is_finite(source) || !source.has_area() || source.x < 0.0F || source.y < 0.0F ||
            source.x + source.width > static_cast<float>(texture->extent.width) ||
            source.y + source.height > static_cast<float>(texture->extent.height)) {
            return impl_->fail("sprite source rectangle lies outside its texture");
        }
    }
    if (sprite.clip.has_value() && (!is_finite(*sprite.clip) || !sprite.clip->has_area())) {
        return impl_->fail("sprite clip rectangle must be finite and have positive area");
    }
    if (!std::isfinite(sprite.rotation_degrees)) {
        return impl_->fail("sprite rotation must be finite");
    }
    impl_->sprites.push_back(sprite);
    impl_->error.clear();
    return true;
}

bool BgfxUiRenderer::draw(const UiThreeSlice& three_slice) {
    if (!impl_->frame_open || !impl_->check_thread()) {
        return impl_->fail("three-slice draw requires an open UI frame on the renderer thread");
    }
    if (impl_->find(three_slice.left) == nullptr || impl_->find(three_slice.middle) == nullptr ||
        impl_->find(three_slice.right) == nullptr) {
        return impl_->fail("three-slice references a stale texture handle");
    }
    if (!is_finite(three_slice.destination) || !three_slice.destination.has_area() ||
        !std::isfinite(three_slice.left_width) || !std::isfinite(three_slice.right_width) ||
        three_slice.left_width < 0.0F || three_slice.right_width < 0.0F) {
        return impl_->fail("three-slice geometry must be finite and non-negative");
    }

    auto left_width = three_slice.left_width;
    auto right_width = three_slice.right_width;
    const auto caps_width = left_width + right_width;
    if (caps_width > three_slice.destination.width && caps_width > 0.0F) {
        const auto scale = three_slice.destination.width / caps_width;
        left_width *= scale;
        right_width *= scale;
    }
    const auto middle_width = three_slice.destination.width - left_width - right_width;

    const UiSprite left{
        three_slice.left,
        UiRect{three_slice.destination.x,
               three_slice.destination.y,
               left_width,
               three_slice.destination.height},
        std::nullopt,
        three_slice.clip,
        three_slice.tint,
        three_slice.space,
    };
    const UiSprite middle{
        three_slice.middle,
        UiRect{three_slice.destination.x + left_width,
               three_slice.destination.y,
               middle_width,
               three_slice.destination.height},
        std::nullopt,
        three_slice.clip,
        three_slice.tint,
        three_slice.space,
    };
    const UiSprite right{
        three_slice.right,
        UiRect{three_slice.destination.x + left_width + middle_width,
               three_slice.destination.y,
               right_width,
               three_slice.destination.height},
        std::nullopt,
        three_slice.clip,
        three_slice.tint,
        three_slice.space,
    };

    if (left_width > 0.0F && !draw(left)) {
        return false;
    }
    if (middle_width > 0.0F && !draw(middle)) {
        return false;
    }
    if (right_width > 0.0F && !draw(right)) {
        return false;
    }
    impl_->error.clear();
    return true;
}

bool BgfxUiRenderer::end_frame() {
    if (!impl_->frame_open || !impl_->check_thread()) {
        return impl_->fail("end_frame requires an open UI frame on the renderer thread");
    }

    bool submitted = true;
    for (const auto& sprite : impl_->sprites) {
        if (!impl_->submit_sprite(sprite)) {
            submitted = false;
            break;
        }
    }
    impl_->sprites.clear();
    impl_->frame_open = false;
    static_cast<void>(bgfx::frame());
    if (submitted) {
        impl_->error.clear();
    }
    return submitted;
}

} // namespace battlespades::render
