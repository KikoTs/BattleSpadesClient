#pragma once

#include "battlespades/ui/geometry.hpp"

#include <cstdint>
#include <optional>

namespace battlespades::ui {

/**
 * Window dimensions in the caller's coordinate space: drawable pixels for
 * rendering, or logical window units for pointer input.
 */
struct PixelExtent final {
    std::int32_t width{};
    std::int32_t height{};

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return width > 0 && height > 0;
    }
};

/**
 * The recovered Classic design-canvas transform in window coordinates.
 *
 * The retail `GameManager.get_aspect()` truncates the centered origin and the
 * reported extent to whole window pixels, but retains the unrounded uniform
 * scale for drawing and inverse pointer mapping.  Keeping both values in one
 * object prevents render and input code from choosing different rounding.
 */
struct CanvasViewport final {
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t width{};
    std::int32_t height{};
    double scale{};

    [[nodiscard]] friend constexpr bool operator==(const CanvasViewport&,
                                                   const CanvasViewport&) = default;
};

/**
 * Physical glyph size and inverse scale for one design-space font draw.
 *
 * Retail fonts with resizing enabled are re-rasterized for the active
 * 800x600 canvas scale. The layout remains in design pixels; only the glyph
 * bitmap gains physical resolution, so menu geometry does not move.
 */
struct CanvasTextRasterization final {
    std::uint32_t pixel_height{};
    double design_pixels_per_bitmap_pixel{};

    [[nodiscard]] friend constexpr bool operator==(const CanvasTextRasterization&,
                                                   const CanvasTextRasterization&) = default;
};

/** Resolve a bounded high-resolution glyph bitmap for a design-space size. */
[[nodiscard]] std::optional<CanvasTextRasterization>
resolve_canvas_text_rasterization(double design_pixel_height,
                                  const CanvasViewport& viewport,
                                  std::uint32_t maximum_pixel_height = 256U) noexcept;

/**
 * Maps a fixed logical canvas into arbitrary windows without distorting it.
 *
 * Logical coordinates use a top-left origin and fixed integer subpixels.
 * Letterbox regions deliberately map to no point so they cannot activate UI.
 */
class DesignCanvas final {
public:
    DesignCanvas(std::int32_t width_pixels,
                 std::int32_t height_pixels,
                 std::int32_t subpixels_per_pixel = 1);

    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] std::int32_t width() const noexcept;
    [[nodiscard]] std::int32_t height() const noexcept;
    [[nodiscard]] std::int32_t subpixels_per_pixel() const noexcept;

    [[nodiscard]] std::optional<CanvasViewport> viewport(PixelExtent window) const noexcept;

    /**
     * Converts a window point to logical subpixels using the recovered inverse.
     *
     * The logical canvas is half-open. Points on its right or bottom edge and
     * points in letterbox bars fail closed. Floating-point coordinates are
     * accepted because SDL reports logical mouse positions as floats.
     */
    [[nodiscard]] std::optional<Point>
    to_canvas(PixelExtent window, double window_x, double window_y) const noexcept;

    /** Integer convenience overload for headless UI and tests. */
    [[nodiscard]] std::optional<Point> to_canvas(PixelExtent window,
                                                 Point window_point) const noexcept;

private:
    std::int32_t width_pixels_{};
    std::int32_t height_pixels_{};
    std::int32_t subpixels_per_pixel_{};
};

} // namespace battlespades::ui
