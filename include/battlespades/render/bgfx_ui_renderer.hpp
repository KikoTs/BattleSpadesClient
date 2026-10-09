#pragma once

#include "battlespades/render/texture_quality.hpp"
#include "battlespades/world/chunk_mesh.hpp"

#include <cstdint>
#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::render {

/**
 * Swap-chain queue depth passed to bgfx::Init::resolution.maxFrameLatency.
 * bgfx's default of 3 added 2-3 frames of VSync latency (audit 2026-09-29).
 */
inline constexpr std::uint8_t bgfx_max_frame_latency{1U};

/** Opaque native handles supplied by the platform/window adapter. */
struct NativeWindow final {
    void* display{};
    void* window{};
    void* graphics_context{};
    void* back_buffer{};
    void* depth_stencil{};
    bool wayland{false};
};

enum class GraphicsBackend : std::uint8_t {
    automatic,
    direct3d11,
    direct3d12,
    vulkan,
    opengl,
    metal,
};

/** Stable diagnostic/UI spelling for a bgfx backend request. */
[[nodiscard]] std::string_view graphics_backend_name(GraphicsBackend backend) noexcept;

/**
 * Whether bgfx can change the backbuffer's MSAA sample count on a running
 * renderer.
 *
 * On Direct3D 11 and 12 bgfx::reset does not resize the swap chain for a new
 * sample count: it releases it and creates another on the same window. Any
 * other holder of the old chain -- the Steam overlay hooks it as soon as it is
 * created -- keeps it alive, the second flip-model chain on that HWND is
 * refused, and bgfx raises Fatal::UnableToInitialize ("Failed to create swap
 * chain."), which ends the process. VSync and resolution changes take the
 * ResizeBuffers path on those backends and stay safe. `automatic` is
 * conservatively not live.
 */
[[nodiscard]] constexpr bool multisample_change_is_live(GraphicsBackend backend) noexcept {
    return backend == GraphicsBackend::vulkan || backend == GraphicsBackend::opengl ||
           backend == GraphicsBackend::metal;
}

/**
 * Enumerates the renderers compiled into bgfx on this platform.
 *
 * `automatic` is always first. This query is safe before initialize() and is
 * used to prevent Windows-only APIs appearing in macOS/Linux Settings.
 */
[[nodiscard]] std::vector<GraphicsBackend> supported_graphics_backends();

enum class TextureFilter : std::uint8_t {
    nearest,
    linear,
};

/** Ordered UI layers. Window-pixel sprites are always rendered below the design canvas. */
enum class UiDrawSpace : std::uint8_t {
    window_pixels,
    design_canvas,
};

struct UiExtent final {
    std::uint32_t width{};
    std::uint32_t height{};

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return width > 0U && height > 0U;
    }
};

struct UiRect final {
    float x{};
    float y{};
    float width{};
    float height{};

    [[nodiscard]] constexpr bool has_area() const noexcept {
        return width > 0.0F && height > 0.0F;
    }
};

struct UiColor final {
    std::uint8_t red{255U};
    std::uint8_t green{255U};
    std::uint8_t blue{255U};
    std::uint8_t alpha{255U};
};

/** Stable cache handle. Generation checks survive slot reuse and renderer restarts. */
struct UiTexture final {
    static constexpr std::uint32_t invalid_index{UINT32_MAX};

    std::uint32_t index{invalid_index};
    std::uint32_t generation{};

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return index != invalid_index;
    }

    [[nodiscard]] friend constexpr bool operator==(const UiTexture&, const UiTexture&) = default;
};

struct UiTextureInfo final {
    UiTexture texture{};
    UiExtent extent{};
};

/** CPU-side PNG decode result suitable for background preload workers. */
struct DecodedUiTexture final {
    std::vector<std::uint8_t> rgba8;
    UiExtent extent{};
};

struct UiTextureDecodeResult final {
    std::optional<DecodedUiTexture> texture;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return texture.has_value();
    }
};

/**
 * Reads and decodes one absolute PNG path without touching bgfx state.
 *
 * This function is safe to call on asset worker threads. GPU creation remains
 * a render-thread responsibility through create_texture_rgba8().
 */
[[nodiscard]] UiTextureDecodeResult decode_png_rgba8(const std::filesystem::path& absolute_path);

struct UiSprite final {
    UiTexture texture{};
    UiRect destination{};
    std::optional<UiRect> source_pixels{};
    std::optional<UiRect> clip{};
    UiColor tint{};
    UiDrawSpace space{UiDrawSpace::design_canvas};
    /** Clockwise rotation about `destination`'s centre. */
    float rotation_degrees{};
    bool additive{};
};

/** Resident image billboard in native eye coordinates (right, up, back). */
struct ViewModelSprite final {
    UiTexture texture{};
    std::array<float,3> position{};
    float radius{},rotation{};
    UiColor tint{};
    bool additive{true};
};

struct UiGeometryVertex final {
    float x{}, y{}, z{}, u{}, v{};
    std::uint32_t abgr{0xffffffffU};
};
struct UiGeometryData final {
    std::vector<UiGeometryVertex> vertices;
    std::vector<std::uint32_t> indices;
};
/** Retained markup geometry. Text atlases and images use checked UI handles. */
struct UiGeometry final {
    std::shared_ptr<const UiGeometryData> mesh;
    UiTexture texture;
    float x{}, y{};
    std::array<float,16U> transform{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    /** A finite empty scissor accepts the draw without emitting pixels. */
    std::optional<UiRect> clip;
};

/**
 * Three independently textured horizontal slices.
 *
 * The caps retain their requested widths while the middle stretches. If the
 * destination is narrower than both caps, both caps shrink proportionally and
 * the middle collapses to zero width. This keeps menu buttons deterministic.
 */
struct UiThreeSlice final {
    UiTexture left{};
    UiTexture middle{};
    UiTexture right{};
    UiRect destination{};
    float left_width{};
    float right_width{};
    std::optional<UiRect> clip{};
    UiColor tint{};
    UiDrawSpace space{UiDrawSpace::design_canvas};
};

struct BgfxUiRendererConfig final {
    NativeWindow native_window{};
    UiExtent drawable_extent{};
    UiExtent design_extent{800U, 600U};
    std::filesystem::path asset_root{};
    std::filesystem::path shader_root{};
    GraphicsBackend backend{GraphicsBackend::automatic};
    std::uint32_t clear_rgba{0x000000FFU};
    bool vertical_sync{true};
    /** 0, 2 or 4. Retail exposes 2x and 4x as distinct choices. */
    std::uint8_t multisample_samples{0U};
    /** Startup-only png/low, png/med, or png/high resource root. */
    TextureQualityTier texture_quality{TextureQualityTier::medium};
    bool debug_device{false};
    /**
     * Swap-chain queue depth, 1 .. 3. 1 = at most one frame queued (lowest
     * input latency, the default); 2 smooths GPU-bound frame times at up to a
     * frame of latency. Startup-only (bgfx has no runtime setter).
     */
    std::uint8_t max_frame_latency{bgfx_max_frame_latency};
};

/**
 * Main-thread bgfx owner and renderer for Classic frontend sprites.
 *
 * initialize(), frame submission, texture operations, resize(), and shutdown()
 * must all run on the initializing thread. The platform adapter owns the
 * native window for the entire renderer lifetime. Source rectangles and the
 * 800x600 design canvas use a top-left origin. Draw calls are submitted in
 * insertion order so translucent loose PNGs reproduce retail composition.
 */
class BgfxUiRenderer final {
public:
    BgfxUiRenderer();
    ~BgfxUiRenderer();

    BgfxUiRenderer(const BgfxUiRenderer&) = delete;
    BgfxUiRenderer& operator=(const BgfxUiRenderer&) = delete;
    BgfxUiRenderer(BgfxUiRenderer&&) = delete;
    BgfxUiRenderer& operator=(BgfxUiRenderer&&) = delete;

    [[nodiscard]] bool initialize(const BgfxUiRendererConfig& config);
    void shutdown() noexcept;

    [[nodiscard]] bool is_initialized() const noexcept;
    [[nodiscard]] std::string_view last_error() const noexcept;
    /** The concrete backend selected by bgfx; never `automatic` after init. */
    [[nodiscard]] GraphicsBackend active_backend() const noexcept;
    /** Draws skipped for transient-buffer pressure in the last completed frame. */
    [[nodiscard]] std::size_t last_frame_dropped_draws() const noexcept;

    /** Resets the backbuffer. A zero extent records a minimized window. */
    [[nodiscard]] bool resize(UiExtent drawable_extent);

    /**
     * Applies presentation options without rebuilding renderer resources.
     *
     * This is a main-thread operation and may not run between begin_frame()
     * and end_frame(). The requested flags are retained for every later
     * resize, matching the retail Settings menu's immediate VSync behavior.
     *
     * A new sample count is applied only where multisample_change_is_live()
     * holds for the active backend. Elsewhere the running swap chain keeps
     * its startup sample count, the request is remembered, and
     * multisample_restart_pending() reports it so the caller can say the
     * change takes effect after a restart.
     */
    [[nodiscard]] bool set_presentation_options(bool vertical_sync,
                                                std::uint8_t multisample_samples);

    /** A requested MSAA sample count is waiting for the next launch. */
    [[nodiscard]] bool multisample_restart_pending() const noexcept;
    /** The sample count the running swap chain was actually reset with. */
    [[nodiscard]] std::uint8_t active_multisample_samples() const noexcept;

    /**
     * Loads a PNG below asset_root and retains one reference to its cache entry.
     * Cache identity includes the sampling mode because retail deliberately
     * mixes nearest and linear filtering for different UI textures.
     */
    [[nodiscard]] std::optional<UiTextureInfo> load_texture(const std::filesystem::path& asset_path,
                                                            TextureFilter filter);

    /** Creates an updatable RGBA8 texture. Optional alpha-weighted mipmaps keep small icons smooth. */
    [[nodiscard]] std::optional<UiTextureInfo> create_texture_rgba8(
        std::span<const std::uint8_t> pixels, UiExtent extent, TextureFilter filter,
        bool mipmapped = false);

    /**
     * Replaces every pixel of an existing runtime texture.
     *
     * This render-thread operation is intentionally forbidden mid-frame.
     * Only textures created by create_texture_rgba8() may be updated. Cached
     * PNGs are immutable, and model previews are renderer-owned targets.
     */
    [[nodiscard]] bool update_texture_rgba8(UiTexture texture,
                                            std::span<const std::uint8_t> pixels);

    /** Releases one cache reference. Textures may not be released mid-frame. */
    [[nodiscard]] bool release_texture(UiTexture texture);

    /**
     * Upload once on selection, then orbit the resident model without CPU rasterization.
     * The returned texture is borrowed; the renderer owns it until shutdown.
     */
    [[nodiscard]] std::optional<UiTextureInfo> set_model_preview(const world::ChunkMesh& mesh);
    void render_model_preview(float yaw, float pitch, float zoom);

    /** Starts a frame and clears the prior draw list. */
    [[nodiscard]] bool begin_frame();
    [[nodiscard]] bool draw(const UiSprite& sprite);
    /** Call after WorldRenderer::submit: shares the weapon's depth and HDR target. */
    [[nodiscard]] bool draw(const ViewModelSprite& sprite);
    [[nodiscard]] bool draw(const UiThreeSlice& three_slice);
    [[nodiscard]] bool draw(const UiGeometry& geometry);

    /** Submits the frame and advances bgfx even when the draw list is empty. */
    [[nodiscard]] bool end_frame();

    /** One completed backbuffer capture: top-left-origin RGBA8 pixels. */
    struct BackbufferCapture final {
        std::uint32_t width{};
        std::uint32_t height{};
        std::vector<std::uint8_t> rgba;
    };

    /**
     * Asks bgfx for a copy of the presented backbuffer (the screenshot key).
     * The pixels arrive a frame or two later through take_backbuffer_capture.
     */
    [[nodiscard]] bool request_backbuffer_capture();
    /** The finished capture, if one has arrived since the last call. */
    [[nodiscard]] std::optional<BackbufferCapture> take_backbuffer_capture();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace battlespades::render
