#pragma once

#include <cstdint>

namespace battlespades::ui {

/** A point in renderer-independent integer layout units. */
struct Point final {
    std::int32_t x{};
    std::int32_t y{};

    [[nodiscard]] friend constexpr bool operator==(const Point&, const Point&) = default;
};

/**
 * A half-open rectangle: [x, x + width) by [y, y + height).
 *
 * Integer layout keeps hit testing and focus navigation identical in headless
 * tests and every rendering backend. Negative extents are invalid.
 */
struct Rect final {
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t width{};
    std::int32_t height{};

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return width >= 0 && height >= 0;
    }

    [[nodiscard]] bool contains(Point point) const noexcept;

    [[nodiscard]] friend constexpr bool operator==(const Rect&, const Rect&) = default;
};

} // namespace battlespades::ui
