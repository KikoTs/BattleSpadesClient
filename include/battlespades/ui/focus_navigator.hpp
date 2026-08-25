#pragma once

#include "battlespades/ui/widget.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace battlespades::ui {

enum class FocusDirection {
    up,
    down,
    left,
    right,
};

/**
 * Owns focus for one screen using stable widget IDs.
 *
 * Widget registration order is the final tie-breaker and the linear tab order.
 * Replacing the layout preserves focus by identity when that widget remains
 * eligible. All operations are synchronous and intended for the fixed UI tick.
 */
class FocusNavigator final {
public:
    /** Replaces the layout transactionally; invalid or duplicate IDs fail closed. */
    [[nodiscard]] bool set_widgets(std::span<const Widget> widgets);

    [[nodiscard]] std::span<const Widget> widgets() const noexcept;
    [[nodiscard]] std::optional<WidgetId> focused() const noexcept;

    /** Focuses an eligible widget, or returns false without changing focus. */
    [[nodiscard]] bool set_focused(WidgetId id) noexcept;

    /** Updates state and repairs focus if the current widget becomes ineligible. */
    [[nodiscard]] bool set_widget_state(WidgetId id, WidgetState state) noexcept;

    /** Moves spatially without wrapping; no candidate leaves the focus unchanged. */
    [[nodiscard]] std::optional<WidgetId> move(FocusDirection direction) noexcept;

    /** Moves in registration order and wraps at either end. */
    [[nodiscard]] std::optional<WidgetId> advance(bool backwards = false) noexcept;

private:
    [[nodiscard]] std::optional<std::size_t> index_of(WidgetId id) const noexcept;
    [[nodiscard]] std::optional<std::size_t> first_focusable() const noexcept;
    void repair_focus() noexcept;

    std::vector<Widget> widgets_;
    std::optional<WidgetId> focused_;
};

} // namespace battlespades::ui
