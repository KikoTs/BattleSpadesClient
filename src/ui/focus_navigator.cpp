#include "battlespades/ui/focus_navigator.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <tuple>
#include <utility>

namespace battlespades::ui {
namespace {

struct DoubledCenter final {
    std::int64_t x{};
    std::int64_t y{};
};

[[nodiscard]] DoubledCenter doubled_center(const Rect& rect) noexcept {
    return DoubledCenter{
        static_cast<std::int64_t>(rect.x) * 2 + static_cast<std::int64_t>(rect.width),
        static_cast<std::int64_t>(rect.y) * 2 + static_cast<std::int64_t>(rect.height),
    };
}

[[nodiscard]] std::uint64_t magnitude(std::int64_t value) noexcept {
    // Center differences fit in 33 bits, so negation cannot reach INT64_MIN.
    return static_cast<std::uint64_t>(value < 0 ? -value : value);
}

struct DirectionalDistance final {
    std::uint64_t primary{};
    std::uint64_t perpendicular{};
};

[[nodiscard]] std::optional<DirectionalDistance> directional_distance(
    DoubledCenter origin, DoubledCenter candidate, FocusDirection direction) noexcept {
    const auto delta_x = candidate.x - origin.x;
    const auto delta_y = candidate.y - origin.y;

    switch (direction) {
    case FocusDirection::up:
        if (delta_y >= 0) {
            return std::nullopt;
        }
        return DirectionalDistance{magnitude(delta_y), magnitude(delta_x)};
    case FocusDirection::down:
        if (delta_y <= 0) {
            return std::nullopt;
        }
        return DirectionalDistance{magnitude(delta_y), magnitude(delta_x)};
    case FocusDirection::left:
        if (delta_x >= 0) {
            return std::nullopt;
        }
        return DirectionalDistance{magnitude(delta_x), magnitude(delta_y)};
    case FocusDirection::right:
        if (delta_x <= 0) {
            return std::nullopt;
        }
        return DirectionalDistance{magnitude(delta_x), magnitude(delta_y)};
    }

    return std::nullopt;
}

} // namespace

bool FocusNavigator::set_widgets(std::span<const Widget> widgets) {
    std::vector<Widget> replacement;
    replacement.reserve(widgets.size());

    for (const auto& widget : widgets) {
        if (!widget.id.is_valid() || !widget.bounds.is_valid()) {
            return false;
        }
        if (std::ranges::any_of(replacement, [&widget](const Widget& existing) {
                return existing.id == widget.id;
            })) {
            return false;
        }
        replacement.push_back(widget);
    }

    widgets_ = std::move(replacement);
    repair_focus();
    return true;
}

std::span<const Widget> FocusNavigator::widgets() const noexcept {
    return widgets_;
}

std::optional<WidgetId> FocusNavigator::focused() const noexcept {
    return focused_;
}

bool FocusNavigator::set_focused(WidgetId id) noexcept {
    const auto index = index_of(id);
    if (!index.has_value() || !widgets_[*index].accepts_focus()) {
        return false;
    }

    focused_ = id;
    return true;
}

bool FocusNavigator::set_widget_state(WidgetId id, WidgetState state) noexcept {
    const auto index = index_of(id);
    if (!index.has_value()) {
        return false;
    }

    widgets_[*index].state = state;
    repair_focus();
    return true;
}

std::optional<WidgetId> FocusNavigator::move(FocusDirection direction) noexcept {
    if (!focused_.has_value()) {
        repair_focus();
        return focused_;
    }

    const auto origin_index = index_of(*focused_);
    if (!origin_index.has_value()) {
        repair_focus();
        return focused_;
    }

    const auto origin = doubled_center(widgets_[*origin_index].bounds);
    auto best_index = std::numeric_limits<std::size_t>::max();
    auto best_score = std::tuple{
        true,
        std::numeric_limits<std::uint64_t>::max(),
        std::numeric_limits<std::uint64_t>::max(),
        std::numeric_limits<std::size_t>::max(),
    };

    for (std::size_t index = 0U; index < widgets_.size(); ++index) {
        const auto& candidate = widgets_[index];
        if (!candidate.accepts_focus() || index == *origin_index) {
            continue;
        }

        const auto distance =
            directional_distance(origin, doubled_center(candidate.bounds), direction);
        if (!distance.has_value()) {
            continue;
        }

        // Prefer the requested 90-degree cone, then nearest Manhattan distance.
        const bool outside_cone = distance->perpendicular > distance->primary;
        const auto score = std::tuple{
            outside_cone,
            distance->primary + distance->perpendicular,
            distance->perpendicular,
            index,
        };
        if (score < best_score) {
            best_score = score;
            best_index = index;
        }
    }

    if (best_index != std::numeric_limits<std::size_t>::max()) {
        focused_ = widgets_[best_index].id;
    }
    return focused_;
}

std::optional<WidgetId> FocusNavigator::advance(bool backwards) noexcept {
    if (widgets_.empty()) {
        focused_.reset();
        return focused_;
    }

    const auto current = focused_.has_value() ? index_of(*focused_) : std::nullopt;
    for (std::size_t offset = 1U; offset <= widgets_.size(); ++offset) {
        std::size_t index{};
        if (backwards) {
            const auto start = current.value_or(0U);
            index = (start + widgets_.size() - (offset % widgets_.size())) % widgets_.size();
        } else {
            const auto start = current.value_or(widgets_.size() - 1U);
            index = (start + offset) % widgets_.size();
        }

        if (widgets_[index].accepts_focus()) {
            focused_ = widgets_[index].id;
            return focused_;
        }
    }

    focused_.reset();
    return focused_;
}

std::optional<std::size_t> FocusNavigator::index_of(WidgetId id) const noexcept {
    const auto iterator = std::ranges::find(widgets_, id, &Widget::id);
    if (iterator == widgets_.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(widgets_.begin(), iterator));
}

std::optional<std::size_t> FocusNavigator::first_focusable() const noexcept {
    const auto iterator = std::ranges::find_if(widgets_, &Widget::accepts_focus);
    if (iterator == widgets_.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(widgets_.begin(), iterator));
}

void FocusNavigator::repair_focus() noexcept {
    if (focused_.has_value()) {
        const auto index = index_of(*focused_);
        if (index.has_value() && widgets_[*index].accepts_focus()) {
            return;
        }
    }

    const auto index = first_focusable();
    focused_ = index.has_value() ? std::optional{widgets_[*index].id} : std::nullopt;
}

} // namespace battlespades::ui
