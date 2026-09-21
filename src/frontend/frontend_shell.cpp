#include "battlespades/frontend/frontend_shell.hpp"

#include <cmath>

namespace battlespades::frontend {

bool FrontendShellModel::start(ui::ScreenId root) noexcept {
    if (!root.is_valid() || active_.has_value()) {
        return false;
    }

    active_ = root;
    return true;
}

bool FrontendShellModel::navigate(ui::ScreenId target, NavigationDirection direction) noexcept {
    if (!target.is_valid()) {
        return false;
    }
    if (!active_.has_value()) {
        return start(target);
    }
    if (*active_ == target) {
        return true;
    }

    previous_ = active_;
    active_ = target;
    direction_ = direction;
    active_offset_ = direction == NavigationDirection::back ? -1.0 : 1.0;
    return true;
}

bool FrontendShellModel::navigate_immediate(ui::ScreenId target) noexcept {
    if (!target.is_valid()) {
        return false;
    }
    active_ = target;
    previous_.reset();
    direction_ = NavigationDirection::forward;
    active_offset_ = 0.0;
    return true;
}

void FrontendShellModel::tick() noexcept {
    if (!active_.has_value() || active_offset_ == 0.0) {
        return;
    }

    active_offset_ += (0.0 - active_offset_) / interpolation_divisor;
    const auto distance = std::abs(active_offset_);
    if (previous_.has_value() && distance < release_previous_threshold) {
        // menuScene.py releases old_menu here but deliberately does not set
        // current_x to zero. Keeping the residual avoids a visible 3-4 pixel
        // snap at 800x600.
        previous_.reset();
    }
    if (distance < settled_threshold) {
        active_offset_ = 0.0;
    }
}

std::optional<ui::ScreenId> FrontendShellModel::active() const noexcept {
    return active_;
}

std::optional<ui::ScreenId> FrontendShellModel::previous() const noexcept {
    return previous_;
}

double FrontendShellModel::active_offset() const noexcept {
    return active_offset_;
}

double FrontendShellModel::previous_offset() const noexcept {
    if (!previous_.has_value()) {
        return 0.0;
    }
    // Retail translates the menu pair by current_x before placing the old
    // menu one additional width away. Returning a fixed +/-1 hid the outgoing
    // screen immediately instead of moving it beside the incoming screen.
    return active_offset_ + (direction_ == NavigationDirection::back ? 1.0 : -1.0);
}

bool FrontendShellModel::accepts_input() const noexcept {
    return active_.has_value() && std::abs(active_offset_) < input_gate_threshold;
}

bool FrontendShellModel::transitioning() const noexcept {
    return previous_.has_value();
}

} // namespace battlespades::frontend
