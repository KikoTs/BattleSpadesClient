#include "battlespades/ui/screen_stack.hpp"

#include <utility>

namespace battlespades::ui {

ScreenStack::ScreenStack(std::size_t maximum_depth) : maximum_depth_{maximum_depth} {
    screens_.reserve(maximum_depth_);
}

std::size_t ScreenStack::size() const noexcept {
    return screens_.size();
}

bool ScreenStack::empty() const noexcept {
    return screens_.empty();
}

std::optional<ScreenId> ScreenStack::top() const noexcept {
    if (screens_.empty()) {
        return std::nullopt;
    }
    return screens_.back();
}

std::span<const ScreenId> ScreenStack::screens() const noexcept {
    return screens_;
}

bool ScreenStack::apply(ScreenCommand command) {
    return apply_to(screens_, command);
}

bool ScreenStack::apply(std::span<const ScreenCommand> commands) {
    auto replacement = screens_;
    for (const auto command : commands) {
        if (!apply_to(replacement, command)) {
            return false;
        }
    }

    screens_ = std::move(replacement);
    return true;
}

bool ScreenStack::apply_to(std::vector<ScreenId>& screens, ScreenCommand command) const {
    switch (command.operation) {
    case ScreenOperation::push:
        if (!command.target.is_valid() || screens.size() >= maximum_depth_) {
            return false;
        }
        screens.push_back(command.target);
        return true;
    case ScreenOperation::pop:
        if (screens.empty()) {
            return false;
        }
        screens.pop_back();
        return true;
    case ScreenOperation::replace:
        if (!command.target.is_valid() || maximum_depth_ == 0U) {
            return false;
        }
        if (screens.empty()) {
            screens.push_back(command.target);
        } else {
            screens.back() = command.target;
        }
        return true;
    case ScreenOperation::reset:
        if (!command.target.is_valid() || maximum_depth_ == 0U) {
            return false;
        }
        screens.assign(1U, command.target);
        return true;
    }

    return false;
}

} // namespace battlespades::ui
