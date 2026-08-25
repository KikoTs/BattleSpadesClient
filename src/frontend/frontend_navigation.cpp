#include "battlespades/frontend/frontend_navigation.hpp"

#include <vector>

namespace battlespades::frontend {

FrontendNavigationModel::FrontendNavigationModel(std::size_t maximum_depth)
    : routes_{maximum_depth} {}

bool FrontendNavigationModel::start(FrontendScreen root) noexcept {
    const auto id = screen_id(root);
    if (!routes_.empty() || !routes_.apply(ui::ScreenCommand::reset(id))) {
        return false;
    }
    if (shell_.start(id)) {
        return true;
    }
    static_cast<void>(routes_.apply(ui::ScreenCommand::pop()));
    return false;
}

bool FrontendNavigationModel::push(FrontendScreen child) noexcept {
    if (!shell_.accepts_input()) {
        return false;
    }
    const auto id = screen_id(child);
    if (!routes_.apply(ui::ScreenCommand::push(id))) {
        return false;
    }
    if (shell_.navigate(id, NavigationDirection::forward)) {
        return true;
    }
    static_cast<void>(routes_.apply(ui::ScreenCommand::pop()));
    return false;
}

bool FrontendNavigationModel::pop() noexcept {
    if (!shell_.accepts_input() || routes_.size() <= 1U) {
        return false;
    }
    const auto screens = routes_.screens();
    const auto target = screens[screens.size() - 2U];
    const auto popped = screens.back();
    if (!routes_.apply(ui::ScreenCommand::pop())) {
        return false;
    }
    if (shell_.navigate(target, NavigationDirection::back)) {
        return true;
    }
    static_cast<void>(routes_.apply(ui::ScreenCommand::push(popped)));
    return false;
}

bool FrontendNavigationModel::replace(FrontendScreen target,
                                      NavigationDirection direction) noexcept {
    if (!shell_.accepts_input()) {
        return false;
    }
    const auto before = routes_.top();
    const auto id = screen_id(target);
    if (!routes_.apply(ui::ScreenCommand::replace(id))) {
        return false;
    }
    if (shell_.navigate(id, direction)) {
        return true;
    }
    if (before.has_value()) {
        static_cast<void>(routes_.apply(ui::ScreenCommand::replace(*before)));
    }
    return false;
}

bool FrontendNavigationModel::reset(FrontendScreen root,
                                    NavigationDirection direction) noexcept {
    if (!shell_.accepts_input()) {
        return false;
    }
    const auto source_routes = routes_.screens();
    const std::vector<ui::ScreenId> old_routes{source_routes.begin(), source_routes.end()};
    const auto id = screen_id(root);
    if (!routes_.apply(ui::ScreenCommand::reset(id))) {
        return false;
    }
    if (shell_.navigate(id, direction)) {
        return true;
    }
    if (!old_routes.empty()) {
        static_cast<void>(routes_.apply(ui::ScreenCommand::reset(old_routes.front())));
        for (std::size_t index{1U}; index < old_routes.size(); ++index) {
            static_cast<void>(routes_.apply(ui::ScreenCommand::push(old_routes[index])));
        }
    }
    return false;
}

void FrontendNavigationModel::tick() noexcept {
    shell_.tick();
}

std::optional<FrontendScreen> FrontendNavigationModel::active() const noexcept {
    const auto active = shell_.active();
    return active.has_value() ? frontend_screen(*active) : std::nullopt;
}

std::optional<FrontendScreen> FrontendNavigationModel::previous() const noexcept {
    const auto previous = shell_.previous();
    return previous.has_value() ? frontend_screen(*previous) : std::nullopt;
}

std::span<const ui::ScreenId> FrontendNavigationModel::stack() const noexcept {
    return routes_.screens();
}

std::size_t FrontendNavigationModel::depth() const noexcept {
    return routes_.size();
}

const FrontendShellModel& FrontendNavigationModel::shell() const noexcept {
    return shell_;
}

} // namespace battlespades::frontend
