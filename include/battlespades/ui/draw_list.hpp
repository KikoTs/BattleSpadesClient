#pragma once

#include "battlespades/ui/design_canvas.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace battlespades::ui {

/** Coordinate system used by a renderer-neutral draw command. */
enum class DrawSpace : std::uint8_t {
    window_pixels,
    design_pixels,
};

/** Top-left-origin rectangle. Fractional values preserve retail eighth-pixels. */
struct DrawRect final {
    double x{};
    double y{};
    double width{};
    double height{};

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return width >= 0.0 && height >= 0.0;
    }

    [[nodiscard]] friend constexpr bool operator==(const DrawRect&, const DrawRect&) = default;
};

struct ColorRgba8 final {
    std::uint8_t red{255U};
    std::uint8_t green{255U};
    std::uint8_t blue{255U};
    std::uint8_t alpha{255U};

    [[nodiscard]] friend constexpr bool operator==(const ColorRgba8&, const ColorRgba8&) = default;
};

/**
 * Exact color modulation retained until conversion to renderer uniforms.
 *
 * Retail frequently used a floating 0.7 intensity. Keeping it as per-mille
 * avoids baking an arbitrary 8-bit rounding decision into characterization
 * data. The final component is `(color / 255) * intensity * opacity`.
 */
struct ColorModulation final {
    ColorRgba8 color{};
    std::uint16_t intensity_per_mille{1'000U};
    std::uint16_t opacity_per_mille{1'000U};

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return intensity_per_mille <= 1'000U && opacity_per_mille <= 1'000U;
    }

    [[nodiscard]] friend constexpr bool operator==(const ColorModulation&,
                                                   const ColorModulation&) = default;
};

enum class TextureFilter : std::uint8_t {
    linear,
    nearest,
};

/** Anchor installed by the recovered retail loader before drawing. */
enum class TextureAnchor : std::uint8_t {
    top_left,
    center,
};

enum class SpriteSizing : std::uint8_t {
    stretch,
    cover,
};

/**
 * Fully resolved sprite request suitable for a bgfx quad batch.
 *
 * `destination` is always top-left-origin. `retail_source_anchor` records the
 * compatibility evidence; it has already been resolved into the destination
 * and must not be applied to the quad a second time.
 */
struct SpriteDrawCommand final {
    std::string asset_id;
    DrawRect destination{};
    DrawSpace space{DrawSpace::design_pixels};
    TextureFilter sampling{TextureFilter::linear};
    TextureAnchor retail_source_anchor{TextureAnchor::top_left};
    double retail_source_scale{1.0};
    SpriteSizing sizing{SpriteSizing::stretch};
    ColorModulation modulation{};
    /**
     * Clockwise rotation about the destination centre. Pyglet's Sprite uses
     * clockwise degrees, so retaining that unit avoids a hidden sign/unit
     * conversion at recovered retail call sites.
     */
    double rotation_degrees{};
    /** Optional top-left-origin source rectangle in authored texture pixels. */
    std::optional<DrawRect> source_pixels{};
    /**
     * Optional clip rectangle in the same coordinate space as `destination`.
     *
     * This remains a real raster clip through the renderer. Cropping a quad is
     * not equivalent for rotated sprites such as the minimap view cone.
     */
    std::optional<DrawRect> clip_pixels{};
};

enum class HorizontalTextAlignment : std::uint8_t {
    left,
    center,
    right,
};

enum class VerticalTextAlignment : std::uint8_t {
    top,
    center,
    /**
     * Retail `draw_text_with_alignment_and_size_validation(..., "center")`.
     *
     * The Python client centers the FTGL ascender/negative-descender span,
     * not the font line height. Keeping this distinct avoids half-leading
     * drift in short menu rows while preserving conventional centering for
     * native-only widgets.
     */
    retail_center,
    bottom,
    /** `destination.y` is the glyph baseline, matching retail Font.draw. */
    baseline,
};

enum class TextTransform : std::uint8_t {
    preserve,
    uppercase,
};

enum class TextFit : std::uint8_t {
    none,
    shrink_to_fit,
    /** Keep the authored font size and scale its cached glyph geometry by width. */
    retail_width_scale,
};

enum class TextLayout : std::uint8_t {
    single_line,
    /**
     * Reproduce text.py's split/resize/draw_text_lines helper sequence.
     *
     * `destination.y` is the first top-left-native glyph baseline. The renderer
     * preserves explicit newlines, wraps at spaces with the retail ten-pixel
     * frame inset, shrinks the font one pixel at a time to satisfy the authored
     * height, and advances later baselines by the FTGL line span plus twice the
     * requested line spacing.
     */
    retail_wrapped_lines,
    /** Top-aligned wrapping at the authored size, capped by maximum_lines.
     * Overflow ends in an ellipsis instead of shrinking the entire notice. */
    bounded_wrapped_lines,
};

/**
 * Localization and glyph-layout request consumed by the native text system.
 *
 * Screens submit a localization key, never a pre-rendered glyph texture.
 * FreeType/HarfBuzz resolve the locale-specific font, fitting and glyph atlas.
 */
struct TextDrawCommand final {
    std::string localization_key;
    std::string preferred_font_asset;
    DrawRect destination{};
    DrawSpace space{DrawSpace::design_pixels};
    double requested_font_size_pixels{};
    double line_spacing_pixels{};
    std::uint8_t maximum_lines{1U};
    HorizontalTextAlignment horizontal_alignment{HorizontalTextAlignment::left};
    VerticalTextAlignment vertical_alignment{VerticalTextAlignment::top};
    TextTransform transform{TextTransform::preserve};
    TextFit fit{TextFit::none};
    ColorModulation modulation{};
    /**
     * Post-rasterization scale around the resolved text origin/baseline.
     *
     * Retail animated cached FTGL glyph geometry with Font.scale rather than
     * requesting a differently hinted font each frame. Keep that transform
     * separate from `requested_font_size_pixels` so those authored effects can
     * retain their exact glyph silhouette.
     */
    double geometric_scale{1.0};
    TextLayout layout{TextLayout::single_line};
    /** Rasterize through retail FTTextureGlyph's outside stroke path. */
    bool retail_outline_stroke{false};
};

/**
 * Deferred retail welcome/name plate.
 *
 * Its frame width and safe-edge position depend on shaped localized text, so
 * it deliberately remains one atomic request until font metrics are present.
 */
struct PlayerNamePlateDrawRequest final {
    std::string frame_asset_id;
    std::string welcome_localization_key;
    std::string player_name;
    std::string preferred_font_asset;
    PixelExtent window{};
    TextureFilter frame_sampling{TextureFilter::linear};
    TextureAnchor retail_frame_anchor{TextureAnchor::top_left};
    double retail_frame_source_scale{0.6};
    double requested_font_size_pixels{16.0};
    double frame_base_width_pixels{120.0};
    double frame_height_pixels{31.0};
    double minimum_text_width_pixels{86.0};
    double safe_edge_inset_pixels{10.0};
    double text_center_x_padding_pixels{15.0};
    double text_center_y_adjustment_pixels{-5.0};
    std::uint8_t maximum_player_name_code_points{32U};
    ColorRgba8 welcome_color{255U, 255U, 255U, 255U};
    ColorRgba8 player_name_color{34U, 177U, 76U, 255U};
    bool requires_font_metrics{true};
    /** Window-pixel translation applied by a frontend slide transition. */
    double window_offset_x{};
};

using DrawCommand = std::variant<SpriteDrawCommand, TextDrawCommand, PlayerNamePlateDrawRequest>;

/** Ordered, renderer-neutral command buffer for one UI presentation pass. */
class DrawList final {
public:
    void reserve(std::size_t command_count);
    void clear() noexcept;
    void push(SpriteDrawCommand command);
    void push(TextDrawCommand command);
    void push(PlayerNamePlateDrawRequest command);
    void push(DrawCommand command);

    [[nodiscard]] std::span<const DrawCommand> commands() const noexcept;
    /**
     * Mutable access for bounded post-presentation transforms.
     *
     * Normal screens still build immutable renderer-neutral commands.  The
     * external layout service is the sole production consumer of this view:
     * it adjusts rectangles after a screen has been composed and before the
     * renderer sees it.  Keeping that operation here avoids teaching every
     * recovered retail presentation about editor persistence.
     */
    [[nodiscard]] std::span<DrawCommand> mutable_commands() noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

private:
    std::vector<DrawCommand> commands_;
};

} // namespace battlespades::ui
