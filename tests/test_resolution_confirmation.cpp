#include "battlespades/frontend/resolution_confirmation.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using battlespades::frontend::ResolutionConfirmationAction;
using battlespades::frontend::ResolutionConfirmationModel;
using battlespades::ui::Point;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] constexpr Point design_point(int x, int top_y) noexcept {
    return {x * ResolutionConfirmationModel::subpixels_per_pixel,
            top_y * ResolutionConfirmationModel::subpixels_per_pixel};
}

void confirmation_is_bounded_and_requires_a_complete_click() {
    ResolutionConfirmationModel model;
    expect(model.seconds_remaining() == 15U, "confirmation must begin at 15 seconds");

    model.pointer_press(design_point(200, 460));
    model.pointer_move(design_point(500, 460));
    expect(!model.pointer_release(design_point(500, 460)).has_value(),
           "dragging between buttons must not activate either action");

    model.pointer_press(design_point(200, 460));
    expect(model.keep_pressed(), "keep button must expose its pressed visual state");
    expect(model.pointer_release(design_point(200, 460)) == ResolutionConfirmationAction::keep,
           "complete click inside Keep must emit keep");
}

void timeout_reverts_once_and_restart_is_fresh() {
    ResolutionConfirmationModel model;
    expect(!model.tick(std::chrono::seconds{14}).has_value(),
           "confirmation must remain active before timeout");
    expect(model.seconds_remaining() == 1U, "countdown must use a ceiling display");
    expect(model.tick(std::chrono::seconds{1}) == ResolutionConfirmationAction::revert,
           "timeout must fail safe to revert");
    expect(!model.tick(std::chrono::seconds{1}).has_value(),
           "timeout action must be emitted at most once");

    model.restart();
    expect(model.seconds_remaining() == 15U, "restart must restore the complete timeout");
    model.pointer_press(design_point(500, 460));
    expect(model.pointer_release(design_point(500, 460)) == ResolutionConfirmationAction::revert,
           "Revert button must emit an immediate revert");
}

} // namespace

int main() {
    try {
        confirmation_is_bounded_and_requires_a_complete_click();
        timeout_reverts_once_and_restart_is_fresh();
        std::cout << "[PASS] resolution confirmation model\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
