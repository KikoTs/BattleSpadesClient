#pragma once

#include "battlespades/ui/screen_stack.hpp"

#include <optional>

namespace battlespades::frontend {

enum class NavigationDirection {
    forward,
    back,
};

/**
 * Deterministic state for the retail frontend's horizontal screen transition.
 *
 * Screen objects and their caches belong to the future FrontendController.
 * This model owns only the active/previous identities, draw offsets, and input
 * gate, making transition behavior executable in headless tests.
 */
class FrontendShellModel final {
public:
    [[nodiscard]] bool start(ui::ScreenId root) noexcept;
    [[nodiscard]] bool
    navigate(ui::ScreenId target,
             NavigationDirection direction = NavigationDirection::forward) noexcept;

    /**
     * Changes the active route without retaining or translating the old one.
     *
     * Gameplay overlays use this boundary: Escape must reveal Pause over the
     * live world immediately, while navigation inside Pause still uses the
     * recovered horizontal menu transition.
     */
    [[nodiscard]] bool navigate_immediate(ui::ScreenId target) noexcept;

    /** Advances the retail interpolation once on the fixed UI tick. */
    void tick() noexcept;

    [[nodiscard]] std::optional<ui::ScreenId> active() const noexcept;
    [[nodiscard]] std::optional<ui::ScreenId> previous() const noexcept;
    [[nodiscard]] double active_offset() const noexcept;
    /** Absolute draw offset of the retained screen, in window widths. */
    [[nodiscard]] double previous_offset() const noexcept;
    [[nodiscard]] bool accepts_input() const noexcept;
    [[nodiscard]] bool transitioning() const noexcept;

private:
    static constexpr double interpolation_divisor{10.0};
    static constexpr double release_previous_threshold{0.005};
    // Retail drops the old menu at 0.005 but continues interpolating the
    // current menu. Clamp only once the residual is visually sub-pixel.
    static constexpr double settled_threshold{0.000001};
    static constexpr double input_gate_threshold{0.5};

    std::optional<ui::ScreenId> active_;
    std::optional<ui::ScreenId> previous_;
    NavigationDirection direction_{NavigationDirection::forward};
    double active_offset_{};
};

} // namespace battlespades::frontend
