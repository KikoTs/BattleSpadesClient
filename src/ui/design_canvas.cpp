#include "battlespades/ui/design_canvas.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace battlespades::ui {

std::optional<CanvasTextRasterization>
resolve_canvas_text_rasterization(double design_pixel_height,
                                  const CanvasViewport& viewport,
                                  std::uint32_t maximum_pixel_height) noexcept {
    if (!std::isfinite(design_pixel_height) || design_pixel_height <= 0.0 ||
        !std::isfinite(viewport.scale) || viewport.scale <= 0.0 ||
        maximum_pixel_height == 0U) {
        return std::nullopt;
    }

    // Never throw resolution away below the retail 800x600 reference. Above
    // it, rasterize at the physical canvas scale so the GPU does not magnify a
    // small 800x600 glyph texture. The cap matches TextRasterizer's hard limit.
    const auto requested_scale = std::max(1.0, viewport.scale);
    const auto unbounded_height = std::floor(design_pixel_height * requested_scale + 0.5);
    const auto pixel_height = static_cast<std::uint32_t>(std::clamp(
        unbounded_height, 1.0, static_cast<double>(maximum_pixel_height)));
    const auto actual_scale = static_cast<double>(pixel_height) / design_pixel_height;
    return CanvasTextRasterization{
        .pixel_height = pixel_height,
        .design_pixels_per_bitmap_pixel = 1.0 / actual_scale,
    };
}

DesignCanvas::DesignCanvas(std::int32_t width_pixels,
                           std::int32_t height_pixels,
                           std::int32_t subpixels_per_pixel)
    : width_pixels_{width_pixels}, height_pixels_{height_pixels},
      subpixels_per_pixel_{subpixels_per_pixel} {}

bool DesignCanvas::is_valid() const noexcept {
    if (width_pixels_ <= 0 || height_pixels_ <= 0 || subpixels_per_pixel_ <= 0) {
        return false;
    }

    const auto maximum = std::numeric_limits<std::int32_t>::max();
    return width_pixels_ <= maximum / subpixels_per_pixel_ &&
           height_pixels_ <= maximum / subpixels_per_pixel_;
}

std::int32_t DesignCanvas::width() const noexcept {
    return is_valid() ? width_pixels_ * subpixels_per_pixel_ : 0;
}

std::int32_t DesignCanvas::height() const noexcept {
    return is_valid() ? height_pixels_ * subpixels_per_pixel_ : 0;
}

std::int32_t DesignCanvas::subpixels_per_pixel() const noexcept {
    return subpixels_per_pixel_;
}

std::optional<CanvasViewport> DesignCanvas::viewport(PixelExtent window) const noexcept {
    if (!is_valid() || !window.is_valid()) {
        return std::nullopt;
    }

    const auto horizontal_scale =
        static_cast<double>(window.width) / static_cast<double>(width_pixels_);
    const auto vertical_scale =
        static_cast<double>(window.height) / static_cast<double>(height_pixels_);
    const auto scale = std::min(horizontal_scale, vertical_scale);
    const auto scaled_width = static_cast<double>(width_pixels_) * scale;
    const auto scaled_height = static_cast<double>(height_pixels_) * scale;

    // Binary-confirmed GameManager.get_aspect behavior: all four returned
    // pixel values use C/Python int truncation while `scale` remains a double.
    const auto viewport_x =
        static_cast<std::int32_t>((static_cast<double>(window.width) - scaled_width) * 0.5);
    const auto viewport_y =
        static_cast<std::int32_t>((static_cast<double>(window.height) - scaled_height) * 0.5);
    const auto viewport_width = static_cast<std::int32_t>(scaled_width);
    const auto viewport_height = static_cast<std::int32_t>(scaled_height);

    return CanvasViewport{
        .x = viewport_x,
        .y = viewport_y,
        .width = viewport_width,
        .height = viewport_height,
        .scale = scale,
    };
}

std::optional<Point>
DesignCanvas::to_canvas(PixelExtent window, double window_x, double window_y) const noexcept {
    const auto target = viewport(window);
    if (!target.has_value() || !std::isfinite(window_x) || !std::isfinite(window_y)) {
        return std::nullopt;
    }

    // menuScene.transform_mouse subtracts the truncated offset and divides by
    // the original ratio. Test the resulting logical point, not the truncated
    // informational width/height returned beside it.
    const auto logical_x_pixels = (window_x - static_cast<double>(target->x)) / target->scale;
    const auto logical_y_pixels = (window_y - static_cast<double>(target->y)) / target->scale;
    if (logical_x_pixels < 0.0 || logical_y_pixels < 0.0 ||
        logical_x_pixels >= static_cast<double>(width_pixels_) ||
        logical_y_pixels >= static_cast<double>(height_pixels_)) {
        return std::nullopt;
    }

    const auto subpixels = static_cast<double>(subpixels_per_pixel_);
    const auto logical_x = std::floor(logical_x_pixels * subpixels);
    const auto logical_y = std::floor(logical_y_pixels * subpixels);
    return Point{
        static_cast<std::int32_t>(logical_x),
        static_cast<std::int32_t>(logical_y),
    };
}

std::optional<Point> DesignCanvas::to_canvas(PixelExtent window,
                                             Point window_point) const noexcept {
    return to_canvas(
        window, static_cast<double>(window_point.x), static_cast<double>(window_point.y));
}

} // namespace battlespades::ui
