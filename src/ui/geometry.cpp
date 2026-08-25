#include "battlespades/ui/geometry.hpp"

#include <cstdint>

namespace battlespades::ui {

bool Rect::contains(Point point) const noexcept {
    if (!is_valid()) {
        return false;
    }

    // Widen before addition so layouts near the int32 boundary remain defined.
    const auto left = static_cast<std::int64_t>(x);
    const auto top = static_cast<std::int64_t>(y);
    const auto right = left + static_cast<std::int64_t>(width);
    const auto bottom = top + static_cast<std::int64_t>(height);
    const auto point_x = static_cast<std::int64_t>(point.x);
    const auto point_y = static_cast<std::int64_t>(point.y);

    return point_x >= left && point_x < right && point_y >= top && point_y < bottom;
}

} // namespace battlespades::ui
