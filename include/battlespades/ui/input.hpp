#pragma once

namespace battlespades::ui {

/** Semantic UI actions produced by keyboard, mouse, or controller adapters. */
enum class InputAction {
    navigate_up,
    navigate_down,
    navigate_left,
    navigate_right,
    focus_next,
    focus_previous,
    activate,
    cancel,
};

enum class InputPhase {
    pressed,
    repeated,
    released,
};

/**
 * One ordered input event for a fixed UI tick.
 *
 * Platform adapters own key-repeat timing. The UI consumes pressed/repeated
 * events in their supplied order and never consults a wall clock.
 */
struct InputEvent final {
    InputAction action{InputAction::activate};
    InputPhase phase{InputPhase::pressed};

    [[nodiscard]] constexpr bool triggers_action() const noexcept {
        return phase != InputPhase::released;
    }

    [[nodiscard]] friend constexpr bool operator==(const InputEvent&, const InputEvent&) = default;
};

} // namespace battlespades::ui
