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
#include <variant>
#include <vector>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace battlespades::render {
namespace {

/**
 * bgfx callback that keeps bgfx's stub behaviour (fatal aborts, no trace or
 * shader cache) and additionally receives requestScreenShot() pixels, which
 * bgfx delivers on its render thread.
 */
class ScreenshotCallback final : public bgfx::CallbackI {
public:
    void fatal(const char* file_path, std::uint16_t line, bgfx::Fatal::Enum code,
               const char* message) override {
        std::fprintf(stderr, "bgfx fatal 0x%08x at %s:%u: %s\n", static_cast<unsigned>(code),
                     file_path != nullptr ? file_path : "?", static_cast<unsigned>(line),
                     message != nullptr ? message : "");
        std::abort();
    }
    void traceVargs(const char*, std::uint16_t, const char*, va_list) override {}
    void profilerBegin(const char*, std::uint32_t, const char*, std::uint16_t) override {}
    void profilerBeginLiteral(const char*, std::uint32_t, const char*, std::uint16_t) override {}
    void profilerEnd() override {}
    std::uint32_t cacheReadSize(std::uint64_t) override { return 0U; }
    bool cacheRead(std::uint64_t, void*, std::uint32_t) override { return false; }
    void cacheWrite(std::uint64_t, const void*, std::uint32_t) override {}
    void screenShot(const char*, std::uint32_t width, std::uint32_t height, std::uint32_t pitch,
                    const void* data, std::uint32_t size, bool yflip) override {
        if (data == nullptr || width == 0U || height == 0U ||
            static_cast<std::uint64_t>(pitch) * height > size || pitch < width * 4U) {
            return;
        }
        BgfxUiRenderer::BackbufferCapture capture;
        capture.width = width;
        capture.height = height;
        capture.rgba.resize(static_cast<std::size_t>(width) * height * 4U);
        const auto* source = static_cast<const std::uint8_t*>(data);
        for (std::uint32_t row = 0U; row < height; ++row) {
            const std::uint32_t source_row = yflip ? height - 1U - row : row;
            const auto* in = source + static_cast<std::size_t>(source_row) * pitch;
            auto* out = capture.rgba.data() + static_cast<std::size_t>(row) * width * 4U;
            for (std::uint32_t column = 0U; column < width; ++column) {
                // bgfx screenshots are BGRA8.
                out[column * 4U + 0U] = in[column * 4U + 2U];
                out[column * 4U + 1U] = in[column * 4U + 1U];
                out[column * 4U + 2U] = in[column * 4U + 0U];
                out[column * 4U + 3U] = 255U;
            }
        }
        const std::scoped_lock lock{mutex_};
        pending_ = std::move(capture);
    }
    void captureBegin(std::uint32_t, std::uint32_t, std::uint32_t, bgfx::TextureFormat::Enum,
                      bool) override {}
    void captureEnd() override {}
    void captureFrame(const void*, std::uint32_t) override {}

    [[nodiscard]] std::optional<BgfxUiRenderer::BackbufferCapture> take() {
        const std::scoped_lock lock{mutex_};
        auto capture = std::move(pending_);
        pending_.reset();
        return capture;
    }

private:
    std::mutex mutex_;
    std::optional<BgfxUiRenderer::BackbufferCapture> pending_;
};

/** One process-wide callback: bgfx is a singleton and outlives no renderer. */
ScreenshotCallback& screenshot_callback() {
    static ScreenshotCallback callback;
    return callback;
}

// View 1 between the clear and the sprite layers belongs to WorldRenderer;
// see render_views.hpp for the global ordering contract.
constexpr bgfx::ViewId clear_view_id{backdrop_clear_view_id};
constexpr bgfx::ViewId window_view_id{ui_window_view_id};
constexpr bgfx::ViewId ui_view_id{ui_canvas_view_id};
constexpr bgfx::ViewId window_overlay_view_id{ui_window_overlay_view_id};
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
    // Always on, at init and at every reset alike: it only sets the device
    // maximum that BGFX_SAMPLER_*_ANISOTROPIC samplers use, so the Texture
    // Filtering setting switches per draw and never changes these flags (a
    // flag change can recreate the D3D swap chain the Steam overlay holds).
    std::uint32_t flags = BGFX_RESET_MAXANISOTROPY;
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
        bool updatable{};
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

        bgfx::setViewRect(window_overlay_view_id,
                          0U,
                          0U,
                          static_cast<std::uint16_t>(drawable_width),
                          static_cast<std::uint16_t>(drawable_height));
        bgfx::setViewClear(window_overlay_view_id, BGFX_CLEAR_NONE);
        bgfx::setViewMode(window_overlay_view_id, bgfx::ViewMode::Sequential);

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
        bgfx::setViewTransform(window_overlay_view_id, view.data(), window_projection.data());
        bgfx::touch(clear_view_id);
        bgfx::touch(window_view_id);
        bgfx::touch(ui_view_id);
        bgfx::touch(window_overlay_view_id);
        canvas_submitted = false;
        return true;
    }

    [[nodiscard]] bool submit_geometry(const UiGeometry& draw) {
        const auto* texture = find(draw.texture);
        if (!texture || !draw.mesh) return fail("markup geometry references released resources");
        const auto& mesh = *draw.mesh;
        if (mesh.vertices.empty() || mesh.indices.empty()) return true;
        std::optional<UiRect> clip;
        if (draw.clip) {
            const ui::DesignCanvas canvas{static_cast<std::int32_t>(design.width),
                                          static_cast<std::int32_t>(design.height)};
            const auto viewport = canvas.viewport({static_cast<std::int32_t>(drawable.width),
                                                   static_cast<std::int32_t>(drawable.height)});
            if (!viewport) return true;
            const auto scale = static_cast<float>(viewport->scale);
            clip = intersect({static_cast<float>(viewport->x) + draw.clip->x * scale,
                              static_cast<float>(viewport->y) + draw.clip->y * scale,
                              draw.clip->width * scale, draw.clip->height * scale},
                             {0, 0, static_cast<float>(drawable.width), static_cast<float>(drawable.height)});
            if (!clip) return true;
        }
        bgfx::TransientVertexBuffer vertices{};
        bgfx::TransientIndexBuffer indices{};
        if (!bgfx::allocTransientBuffers(&vertices, vertex_layout,
                static_cast<std::uint32_t>(mesh.vertices.size()), &indices,
                static_cast<std::uint32_t>(mesh.indices.size()), true)) {
            ++dropped_draws;
            return true;
        }
        auto* output = reinterpret_cast<UiVertex*>(vertices.data);
        const auto& m = draw.transform;
        for (std::size_t i{}; i < mesh.vertices.size(); ++i) {
            const auto& v = mesh.vertices[i];
            const float x = v.x + draw.x, y = v.y + draw.y;
            const float w = m[3] * x + m[7] * y + m[15];
            const float inverse = std::abs(w) > 0.00001F ? 1.0F / w : 1.0F;
            output[i] = {(m[0]*x + m[4]*y + m[12])*inverse,
                         (m[1]*x + m[5]*y + m[13])*inverse, 0, v.u, v.v, v.abgr};
        }
        std::memcpy(indices.data, mesh.indices.data(), mesh.indices.size()*sizeof(std::uint32_t));
        bgfx::setVertexBuffer(0, &vertices);
        bgfx::setIndexBuffer(&indices);
        bgfx::setTexture(0, sampler, texture->native);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_MSAA |
                       BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA));
        if (clip) {
            const auto x = std::floor(clip->x), y = std::floor(clip->y);
            bgfx::setScissor(static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y),
                static_cast<std::uint16_t>(std::ceil(clip->x + clip->width) - x),
                static_cast<std::uint16_t>(std::ceil(clip->y + clip->height) - y));
        }
        bgfx::submit(ui_view_id, program);
        canvas_submitted = true;
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
            ++dropped_draws;
            return true;
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
        bgfx::setState(BGFX_STATE_WRITE_RGB | (sprite.additive
            ? BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE)
            : BGFX_STATE_BLEND_ALPHA) | BGFX_STATE_MSAA);
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
        // Draw-list order wins: a window-pixel sprite queued after canvas
        // content goes to the overlay layer above the canvas.
        const auto view = sprite.space != UiDrawSpace::window_pixels ? ui_view_id
                          : canvas_submitted                         ? window_overlay_view_id
                                                                     : window_view_id;
        if (view == ui_view_id) canvas_submitted = true;
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
    // Keep markup and sprite submissions interleaved. Separate queues put a
    // later tooltip/backdrop underneath every earlier markup draw.
    std::vector<std::variant<UiSprite, UiGeometry>> draws{};
    bgfx::FrameBufferHandle preview_framebuffer{bgfx::kInvalidHandle};
    bgfx::TextureHandle preview_depth{bgfx::kInvalidHandle};
    bgfx::VertexBufferHandle preview_vertices{bgfx::kInvalidHandle};
    bgfx::IndexBufferHandle preview_indices{bgfx::kInvalidHandle};
    UiTextureInfo preview_texture{}, preview_white{};
    std::thread::id owner_thread{};
    std::string error{};
    /** Last sample count asked for; config holds the one actually in force. */
    std::uint8_t requested_multisample{};
    bool initialized{false};
    bool frame_open{false};
    std::size_t dropped_draws{};
    bool canvas_submitted{};
    std::size_t last_dropped_draws{};
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
    if (config.drawable_extent.width > UINT16_MAX || config.drawable_extent.height > UINT16_MAX ||
        config.design_extent.width > static_cast<std::uint32_t>(INT32_MAX) ||
        config.design_extent.height > static_cast<std::uint32_t>(INT32_MAX)) {
        return impl_->fail("drawable or design extent exceeds the UI coordinate limits");
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
    init.callback = &screenshot_callback();
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
    // The default (BGFX_CONFIG_MAX_FRAME_LATENCY = 3) lets the DXGI/Vulkan
    // queue fill 2-3 frames ahead under VSync because 60.000 Hz ticks never
    // match a 59.94 Hz panel: +30-50 ms of input latency. One queued frame
    // (plus bgfx's own render thread) keeps VSync latency near retail.
    init.resolution.maxFrameLatency =
        static_cast<std::uint8_t>(std::clamp<int>(config.max_frame_latency, 1, 3));

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
    impl_->requested_multisample = config.multisample_samples;
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
    bgfx::setViewName(window_overlay_view_id, "BattleSpades UI window overlay");
    bgfx::setDebug(BGFX_DEBUG_NONE);
    impl_->error.clear();
    return true;
}

void BgfxUiRenderer::shutdown() noexcept {
    if (!impl_->initialized) {
        return;
    }

    impl_->draws.clear();
    if (bgfx::isValid(impl_->preview_framebuffer)) bgfx::destroy(impl_->preview_framebuffer);
    if (bgfx::isValid(impl_->preview_depth)) bgfx::destroy(impl_->preview_depth);
    if (bgfx::isValid(impl_->preview_vertices)) bgfx::destroy(impl_->preview_vertices);
    if (bgfx::isValid(impl_->preview_indices)) bgfx::destroy(impl_->preview_indices);
    impl_->preview_framebuffer = BGFX_INVALID_HANDLE;
    impl_->preview_depth = BGFX_INVALID_HANDLE;
    impl_->preview_vertices = BGFX_INVALID_HANDLE;
    impl_->preview_indices = BGFX_INVALID_HANDLE;
    impl_->preview_texture = {};
    impl_->preview_white = {};
    impl_->frame_open = false;
    impl_->dropped_draws = 0U;
    impl_->last_dropped_draws = 0U;
    impl_->free_slots.clear();
    for (std::size_t index{}; index < impl_->textures.size(); ++index) {
        auto& texture = impl_->textures[index];
        if (bgfx::isValid(texture.native)) {
            bgfx::destroy(texture.native);
            texture.native = BGFX_INVALID_HANDLE;
            if (++texture.generation == 0U) texture.generation = 1U;
        }
        texture.canonical_path.clear();
        texture.extent = {};
        texture.references = 0U;
        texture.updatable = false;
        impl_->free_slots.push_back(static_cast<std::uint32_t>(index));
    }
    impl_->texture_cache.clear();
    // Preserve the slot generations across initialize/shutdown cycles. A
    // texture retained by a frontend cache must not alias a new GPU resource
    // when a graphics backend is restarted.
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

std::size_t BgfxUiRenderer::last_frame_dropped_draws() const noexcept {
    return impl_->last_dropped_draws;
}

bool BgfxUiRenderer::resize(UiExtent drawable_extent) {
    if (!impl_->initialized || !impl_->check_thread()) {
        return impl_->fail("cannot resize an uninitialized bgfx UI renderer");
    }
    if (impl_->frame_open) {
        return impl_->fail("cannot resize while a UI frame is open");
    }
    if (drawable_extent.width > UINT16_MAX || drawable_extent.height > UINT16_MAX) {
        return impl_->fail("drawable extent exceeds bgfx's 16-bit view rectangle limit");
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

    // A sample-count change on Direct3D replaces the swap chain, which is
    // fatal while the Steam overlay holds the old one; keep the startup count
    // there and let the caller report the change as pending a restart.
    impl_->requested_multisample = multisample_samples;
    const auto applied_samples = multisample_change_is_live(active_backend())
                                     ? multisample_samples
                                     : impl_->config.multisample_samples;
    if (impl_->config.vertical_sync == vertical_sync &&
        impl_->config.multisample_samples == applied_samples) {
        impl_->error.clear();
        return true;
    }

    impl_->config.vertical_sync = vertical_sync;
    impl_->config.multisample_samples = applied_samples;
    if (impl_->drawable.is_valid()) {
        bgfx::reset(impl_->drawable.width, impl_->drawable.height, reset_flags(impl_->config));
    }
    impl_->error.clear();
    return true;
}

bool BgfxUiRenderer::multisample_restart_pending() const noexcept {
    return impl_->initialized && impl_->requested_multisample != impl_->config.multisample_samples;
}

std::uint8_t BgfxUiRenderer::active_multisample_samples() const noexcept {
    return impl_->initialized ? impl_->config.multisample_samples : std::uint8_t{};
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

    const auto texture_limit = std::min<std::uint32_t>(UINT16_MAX, bgfx::getCaps()->limits.maxTextureSize);
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
    slot.updatable = false;
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
    const auto texture_limit = std::min<std::uint32_t>(UINT16_MAX, bgfx::getCaps()->limits.maxTextureSize);
    if (!extent.is_valid() || extent.width > texture_limit || extent.height > texture_limit) {
        impl_->fail("RGBA8 texture extent exceeds the graphics backend's texture limit");
        return std::nullopt;
    }

    const auto required_size =
        static_cast<std::uint64_t>(extent.width) * static_cast<std::uint64_t>(extent.height) * 4U;
    if (required_size != static_cast<std::uint64_t>(pixels.size()) ||
        required_size > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
        impl_->fail("RGBA8 pixels must contain exactly width * height * 4 bytes");
        return std::nullopt;
    }

    // Runtime textures support update_texture_rgba8. Initial data passed into
    // createTexture2D makes a D3D11 immutable resource; later thumbnail/minimap
    // updates then appear successful but never change its pixels. Allocate a
    // mutable texture first and upload its initial contents through the update.
    const auto native = bgfx::createTexture2D(static_cast<std::uint16_t>(extent.width),
                                              static_cast<std::uint16_t>(extent.height),
                                              false,
                                              1U,
                                              bgfx::TextureFormat::RGBA8,
                                              texture_flags(filter),
                                              nullptr);
    if (!bgfx::isValid(native)) {
        impl_->fail("bgfx could not create an RGBA8 runtime texture");
        return std::nullopt;
    }
    bgfx::updateTexture2D(native,0U,0U,0U,0U,static_cast<std::uint16_t>(extent.width),
        static_cast<std::uint16_t>(extent.height),
        bgfx::copy(pixels.data(),static_cast<std::uint32_t>(pixels.size())));

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
    slot.updatable = true;
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
    if (!slot->updatable) {
        return impl_->fail("only runtime RGBA8 textures can be updated");
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
    if (texture == impl_->preview_texture.texture || texture == impl_->preview_white.texture) {
        return impl_->fail("model preview textures are owned by the renderer");
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
        slot->updatable = false;
        ++slot->generation;
        if (slot->generation == 0U) {
            slot->generation = 1U;
        }
        impl_->free_slots.push_back(texture.index);
    }
    impl_->error.clear();
    return true;
}

std::optional<UiTextureInfo> BgfxUiRenderer::set_model_preview(const world::ChunkMesh& mesh) {
    if (!impl_->initialized || !impl_->check_thread() || impl_->frame_open ||
        mesh.vertices.empty() || mesh.indices.empty() || mesh.vertices.size() > 1'000'000U ||
        mesh.indices.size() > 3'000'000U || mesh.indices.size() % 3U != 0U ||
        std::ranges::any_of(mesh.indices, [&](auto index) { return index >= mesh.vertices.size(); }) ||
        std::ranges::any_of(mesh.vertices, [](const auto& vertex) {
            return !std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z);
        })) {
        impl_->fail("model preview requires a valid bounded triangle mesh outside an open frame");
        return std::nullopt;
    }
    for (std::size_t axis{}; axis < 3U; ++axis) {
        if (!std::isfinite(mesh.minimum[axis]) || !std::isfinite(mesh.maximum[axis]) ||
            mesh.minimum[axis] > mesh.maximum[axis]) {
            impl_->fail("model preview bounds must be finite and ordered");
            return std::nullopt;
        }
    }
    if (!bgfx::isValid(impl_->preview_framebuffer)) {
        constexpr UiExtent extent{640U, 320U};
        const std::vector<std::uint8_t> empty(extent.width * extent.height * 4U, 0U);
        const std::array<std::uint8_t,4U> white{255,255,255,255};
        const auto texture = create_texture_rgba8(empty, extent, TextureFilter::linear);
        const auto pixel = create_texture_rgba8(white, {1,1}, TextureFilter::nearest);
        if (!texture || !pixel) {
            if (texture) static_cast<void>(release_texture(texture->texture));
            if (pixel) static_cast<void>(release_texture(pixel->texture));
            return std::nullopt;
        }
        const auto color = bgfx::createTexture2D(640, 320, false, 1, bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        const auto depth_format=bgfx::isTextureValid(0,false,1,bgfx::TextureFormat::D24S8,BGFX_TEXTURE_RT_WRITE_ONLY)
            ?bgfx::TextureFormat::D24S8:bgfx::TextureFormat::D32F;
        const auto depth = bgfx::createTexture2D(640,320,false,1,depth_format,BGFX_TEXTURE_RT_WRITE_ONLY);
        bgfx::FrameBufferHandle framebuffer=BGFX_INVALID_HANDLE;
        if (bgfx::isValid(color) && bgfx::isValid(depth)) {
            const std::array attachments{color,depth};
            framebuffer=bgfx::createFrameBuffer(2,attachments.data(),false);
        }
        if (!bgfx::isValid(framebuffer)) {
            if (bgfx::isValid(color)) bgfx::destroy(color);
            if (bgfx::isValid(depth)) bgfx::destroy(depth);
            static_cast<void>(release_texture(texture->texture));
            static_cast<void>(release_texture(pixel->texture));
            return std::nullopt;
        }
        auto* slot=impl_->find(texture->texture);
        bgfx::destroy(slot->native); slot->native=color; slot->updatable=false;
        impl_->preview_texture=*texture; impl_->preview_white=*pixel;
        impl_->preview_depth=depth; impl_->preview_framebuffer=framebuffer;
    }
    std::array<float,3U> center{};
    float diameter{};
    for (std::size_t i{}; i<3U; ++i) {
        center[i] = (mesh.minimum[i] + mesh.maximum[i]) * 0.5F;
        diameter = std::max(diameter, mesh.maximum[i] - mesh.minimum[i]);
    }
    const auto scale = std::min(3.4F / std::max(0.01F,diameter),
                               1.65F / std::max(0.01F,mesh.maximum[1]-mesh.minimum[1]));
    std::vector<UiVertex> vertices;
    vertices.reserve(mesh.vertices.size());
    for (const auto& v : mesh.vertices) {
        std::uint32_t color = 0xff000000U;
        for (std::uint32_t channel{}; channel<3U; ++channel) {
            const auto linear = static_cast<float>((v.abgr >> (channel*8U)) & 255U) / 255.0F;
            const auto light=0.8F+0.1F*static_cast<float>(v.face%3U);
            const auto lit = std::clamp(std::pow(linear*light, 1.0F/2.2F)*255.0F, 0.0F, 255.0F);
            color |= static_cast<std::uint32_t>(lit) << (channel*8U);
        }
        vertices.push_back({(v.x-center[0])*scale, (v.y-center[1])*scale,
                            (v.z-center[2])*scale, 0.5F, 0.5F, color});
    }
    const auto vb = bgfx::createVertexBuffer(bgfx::copy(vertices.data(),
        static_cast<std::uint32_t>(vertices.size()*sizeof(UiVertex))), impl_->vertex_layout);
    const auto ib = bgfx::createIndexBuffer(bgfx::copy(mesh.indices.data(),
        static_cast<std::uint32_t>(mesh.indices.size()*sizeof(std::uint32_t))), BGFX_BUFFER_INDEX32);
    if (!bgfx::isValid(vb) || !bgfx::isValid(ib)) {
        if (bgfx::isValid(vb)) bgfx::destroy(vb);
        if (bgfx::isValid(ib)) bgfx::destroy(ib);
        return std::nullopt;
    }
    if (bgfx::isValid(impl_->preview_vertices)) bgfx::destroy(impl_->preview_vertices);
    if (bgfx::isValid(impl_->preview_indices)) bgfx::destroy(impl_->preview_indices);
    impl_->preview_vertices = vb;
    impl_->preview_indices = ib;
    return impl_->preview_texture;
}

void BgfxUiRenderer::render_model_preview(float yaw, float pitch, float zoom) {
    if (!impl_->initialized || !impl_->check_thread() || !bgfx::isValid(impl_->preview_vertices) ||
        !std::isfinite(yaw) || !std::isfinite(pitch) || !std::isfinite(zoom)) return;
    std::array<float,16U> model{}, view{}, projection{};
    const float size = 1.05F / std::clamp(zoom, 0.65F, 1.6F);
    bx::mtxRotateXY(model.data(), pitch, yaw);
    bx::mtxIdentity(view.data());
    bx::mtxOrtho(projection.data(), -size * 640.0F/320.0F, size * 640.0F/320.0F,
                  -size, size, -4.0F, 4.0F, 0.0F, bgfx::getCaps()->homogeneousDepth);
    if (bgfx::getCaps()->originBottomLeft) projection[5]=-projection[5];
    bgfx::setViewFrameBuffer(inventory_preview_view_id, impl_->preview_framebuffer);
    bgfx::setViewRect(inventory_preview_view_id, 0, 0, 640, 320);
    bgfx::setViewClear(inventory_preview_view_id, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x00000000U);
    bgfx::setViewTransform(inventory_preview_view_id, view.data(), projection.data());
    bgfx::setTransform(model.data());
    bgfx::setVertexBuffer(0, impl_->preview_vertices);
    bgfx::setIndexBuffer(impl_->preview_indices);
    bgfx::setTexture(0, impl_->sampler, impl_->find(impl_->preview_white.texture)->native);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
    bgfx::submit(inventory_preview_view_id, impl_->program);
}

bool BgfxUiRenderer::begin_frame() {
    if (!impl_->initialized || !impl_->check_thread()) {
        return impl_->fail("cannot begin a frame before renderer initialization");
    }
    if (impl_->frame_open) {
        return impl_->fail("a UI frame is already open");
    }
    impl_->draws.clear();
    if (!impl_->configure_views()) {
        return false;
    }
    impl_->frame_open = true;
    impl_->dropped_draws = 0U;
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
    impl_->draws.emplace_back(sprite);
    impl_->error.clear();
    return true;
}

bool BgfxUiRenderer::draw(const ViewModelSprite& sprite) {
    if(!impl_->frame_open||!impl_->check_thread())return impl_->fail("viewmodel sprite requires an open frame");
    const auto* texture=impl_->find(sprite.texture);
    if(!texture)return impl_->fail("viewmodel sprite references a stale texture");
    if(!std::ranges::all_of(sprite.position,[](float v){return std::isfinite(v);})||
       !std::isfinite(sprite.radius)||sprite.radius<=0||!std::isfinite(sprite.rotation))
        return impl_->fail("invalid viewmodel sprite geometry");
    if(sprite.position[2]>=-.01F)return true;
    if(bgfx::getAvailTransientVertexBuffer(4,impl_->vertex_layout)<4||bgfx::getAvailTransientIndexBuffer(6)<6) {
        ++impl_->dropped_draws;
        impl_->error.clear();
        return true;
    }
    bgfx::TransientVertexBuffer vb;bgfx::TransientIndexBuffer ib;
    bgfx::allocTransientVertexBuffer(&vb,4,impl_->vertex_layout);bgfx::allocTransientIndexBuffer(&ib,6);
    auto* vertices=reinterpret_cast<UiVertex*>(vb.data);
    const float c=std::cos(sprite.rotation),s=std::sin(sprite.rotation);
    constexpr std::array<std::array<float,2>,4> corners{{{-1,1},{1,1},{1,-1},{-1,-1}}};
    constexpr std::array<std::array<float,2>,4> uv{{{0,0},{1,0},{1,1},{0,1}}};
    for(std::size_t i=0;i<4;++i){const auto x=corners[i][0]*sprite.radius,y=corners[i][1]*sprite.radius;
        vertices[i]={sprite.position[0]+c*x-s*y,sprite.position[1]+s*x+c*y,sprite.position[2],uv[i][0],uv[i][1],pack_abgr(sprite.tint)};}
    constexpr std::array<std::uint16_t,6> indices{0,1,2,0,2,3};std::memcpy(ib.data,indices.data(),sizeof(indices));
    std::array<float,16> identity{};bx::mtxIdentity(identity.data());bgfx::setTransform(identity.data());
    bgfx::setScissor(UINT16_MAX);bgfx::setVertexBuffer(0,&vb);bgfx::setIndexBuffer(&ib);
    bgfx::setTexture(0,impl_->sampler,texture->native);
    bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_DEPTH_TEST_LESS|BGFX_STATE_MSAA|
        (sprite.additive?BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,BGFX_STATE_BLEND_ONE):BGFX_STATE_BLEND_ALPHA));
    bgfx::submit(view_model_view_id,impl_->program);
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

bool BgfxUiRenderer::draw(const UiGeometry& geometry) {
    if (!impl_->frame_open || !impl_->check_thread() || !geometry.mesh ||
        !impl_->find(geometry.texture)) return impl_->fail("invalid markup draw state");
    if (geometry.mesh->vertices.size() > 1'000'000U || geometry.mesh->indices.size() > 3'000'000U)
        return impl_->fail("markup geometry is too large");
    if (!std::isfinite(geometry.x) || !std::isfinite(geometry.y) ||
        std::ranges::any_of(geometry.transform,[](float v){return !std::isfinite(v);}) ||
        std::ranges::any_of(geometry.mesh->vertices, [](const auto& vertex) {
            return !std::isfinite(vertex.x) || !std::isfinite(vertex.y) ||
                   !std::isfinite(vertex.z) || !std::isfinite(vertex.u) || !std::isfinite(vertex.v);
        }) ||
        std::ranges::any_of(geometry.mesh->indices,[&](auto i){return i>=geometry.mesh->vertices.size();}))
        return impl_->fail("markup geometry contains invalid coordinates or indices");
    if (geometry.clip) {
        if (!is_finite(*geometry.clip))
            return impl_->fail("markup clip rectangle must be finite");
        // RmlUi can retain geometry beneath an empty scissor while a panel is
        // hidden or an overflow intersection collapses. This is a successful
        // draw with no visible pixels, not a malformed frame.
        if (!geometry.clip->has_area()) {
            impl_->error.clear();
            return true;
        }
    }
    impl_->draws.emplace_back(geometry);
    impl_->error.clear();
    return true;
}

bool BgfxUiRenderer::request_backbuffer_capture() {
    if (!impl_->initialized) {
        return impl_->fail("bgfx UI renderer is not initialized");
    }
    if (!impl_->check_thread()) {
        return false;
    }
    if (bgfx::getRendererType() == bgfx::RendererType::Noop) {
        return impl_->fail("the active bgfx backend cannot capture the backbuffer");
    }
    bgfx::requestScreenShot(BGFX_INVALID_HANDLE, "screenshot");
    return true;
}

std::optional<BgfxUiRenderer::BackbufferCapture> BgfxUiRenderer::take_backbuffer_capture() {
    return screenshot_callback().take();
}

bool BgfxUiRenderer::end_frame() {
    if (!impl_->frame_open || !impl_->check_thread()) {
        return impl_->fail("end_frame requires an open UI frame on the renderer thread");
    }

    bool submitted = true;
    for (const auto& draw : impl_->draws) {
        const auto* sprite = std::get_if<UiSprite>(&draw);
        if (!(sprite ? impl_->submit_sprite(*sprite)
                     : impl_->submit_geometry(std::get<UiGeometry>(draw)))) {
            submitted = false;
            break;
        }
    }
    impl_->draws.clear();
    impl_->frame_open = false;
    impl_->last_dropped_draws = impl_->dropped_draws;
    static_cast<void>(bgfx::frame());
    if (submitted) {
        impl_->error.clear();
    }
    return submitted;
}

} // namespace battlespades::render
