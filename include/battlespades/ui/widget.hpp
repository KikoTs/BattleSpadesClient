#pragma once

#include "battlespades/ui/geometry.hpp"

#include <cstdint>

namespace battlespades::ui {

/** Stable widget identity. Zero is reserved as an invalid/unassigned value. */
struct WidgetId final {
    std::uint32_t value{};

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return value != 0U;
    }

    [[nodiscard]] friend constexpr bool operator==(const WidgetId&, const WidgetId&) = default;
};

/** State which determines whether a widget may own keyboard/controller focus. */
struct WidgetState final {
    bool visible{true};
    bool enabled{true};
    bool focusable{true};

    [[nodiscard]] constexpr bool accepts_focus() const noexcept {
        return visible && enabled && focusable;
    }

    [[nodiscard]] friend constexpr bool operator==(const WidgetState&,
                                                   const WidgetState&) = default;
};

/** Renderer-neutral widget data used by hit testing and focus navigation. */
struct Widget final {
    WidgetId id{};
    Rect bounds{};
    WidgetState state{};

    [[nodiscard]] constexpr bool accepts_focus() const noexcept {
        return state.accepts_focus();
    }

    [[nodiscard]] friend constexpr bool operator==(const Widget&, const Widget&) = default;
};

} // namespace battlespades::ui
