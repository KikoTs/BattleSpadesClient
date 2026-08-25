#pragma once

#include "battlespades/render/texture_quality.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::render {

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

/** Stable cache handle. The generation prevents use-after-release on slot reuse. */
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

    /** Resets the backbuffer. A zero extent records a minimized window. */
    [[nodiscard]] bool resize(UiExtent drawable_extent);

    /**
     * Applies presentation options without rebuilding renderer resources.
     *
     * This is a main-thread operation and may not run between begin_frame()
     * and end_frame(). The requested flags are retained for every later
     * resize, matching the retail Settings menu's immediate VSync behavior.
     */
    [[nodiscard]] bool set_presentation_options(bool vertical_sync,
                                                std::uint8_t multisample_samples);

    /**
     * Loads a PNG below asset_root and retains one reference to its cache entry.
     * Cache identity includes the sampling mode because retail deliberately
     * mixes nearest and linear filtering for different UI textures.
     */
    [[nodiscard]] std::optional<UiTextureInfo> load_texture(const std::filesystem::path& asset_path,
                                                            TextureFilter filter);

    /** Creates an immutable RGBA8 texture from tightly packed top-left-origin pixels. */
    [[nodiscard]] std::optional<UiTextureInfo> create_texture_rgba8(
        std::span<const std::uint8_t> pixels, UiExtent extent, TextureFilter filter);

    /**
     * Replaces every pixel of an existing runtime texture.
     *
     * This render-thread operation is intentionally forbidden mid-frame.
     * Asset-cache textures and runtime textures share the same checked handle
     * model, but only the caller owns deciding which may be updated.
     */
    [[nodiscard]] bool update_texture_rgba8(UiTexture texture,
                                            std::span<const std::uint8_t> pixels);

    /** Releases one cache reference. Textures may not be released mid-frame. */
    [[nodiscard]] bool release_texture(UiTexture texture);

    /** Starts a frame and clears the prior draw list. */
    [[nodiscard]] bool begin_frame();
    [[nodiscard]] bool draw(const UiSprite& sprite);
    [[nodiscard]] bool draw(const UiThreeSlice& three_slice);

    /** Submits the frame and advances bgfx even when the draw list is empty. */
    [[nodiscard]] bool end_frame();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace battlespades::render
