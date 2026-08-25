#include "battlespades/ui/focus_navigator.hpp"
#include "battlespades/ui/geometry.hpp"
#include "battlespades/ui/input.hpp"
#include "battlespades/ui/screen_stack.hpp"
#include "battlespades/ui/widget.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using battlespades::ui::FocusDirection;
using battlespades::ui::FocusNavigator;
using battlespades::ui::Point;
using battlespades::ui::Rect;
using battlespades::ui::ScreenCommand;
using battlespades::ui::ScreenId;
using battlespades::ui::ScreenStack;
using battlespades::ui::Widget;
using battlespades::ui::WidgetId;
using battlespades::ui::WidgetState;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] Widget widget(std::uint32_t id, Rect bounds, WidgetState state = {}) {
    return Widget{WidgetId{id}, bounds, state};
}

void rectangles_use_half_open_integer_bounds() {
    const Rect bounds{10, 20, 30, 40};

    expect(bounds.contains(Point{10, 20}), "top-left must be inside");
    expect(bounds.contains(Point{39, 59}), "last covered point must be inside");
    expect(!bounds.contains(Point{40, 59}), "right edge must be outside");
    expect(!bounds.contains(Point{39, 60}), "bottom edge must be outside");
    expect(!Rect{0, 0, -1, 1}.contains(Point{0, 0}), "invalid rect must reject hits");

    const Rect boundary{
        std::numeric_limits<std::int32_t>::max() - 1,
        0,
        2,
        1,
    };
    expect(boundary.contains(Point{std::numeric_limits<std::int32_t>::max(), 0}),
           "hit testing must widen boundary arithmetic");
}

void released_input_does_not_trigger_an_action() {
    using battlespades::ui::InputAction;
    using battlespades::ui::InputEvent;
    using battlespades::ui::InputPhase;

    expect(InputEvent{InputAction::activate, InputPhase::pressed}.triggers_action(),
           "press should trigger");
    expect(InputEvent{InputAction::navigate_down, InputPhase::repeated}.triggers_action(),
           "repeat should trigger");
    expect(!InputEvent{InputAction::cancel, InputPhase::released}.triggers_action(),
           "release should not trigger");
}

void widget_layout_validation_is_transactional() {
    FocusNavigator navigator;
    const std::array initial{
        widget(1U, Rect{0, 0, 10, 10}),
        widget(2U, Rect{0, 20, 10, 10}),
    };
    expect(navigator.set_widgets(initial), "valid widget layout should install");
    expect(navigator.focused() == WidgetId{1U}, "first eligible widget should focus");

    const std::array duplicate{
        widget(3U, Rect{0, 0, 10, 10}),
        widget(3U, Rect{0, 20, 10, 10}),
    };
    expect(!navigator.set_widgets(duplicate), "duplicate identity must fail closed");
    expect(navigator.widgets().size() == 2U, "failed layout must preserve old widgets");
    expect(navigator.focused() == WidgetId{1U}, "failed layout must preserve focus");
}

void focus_is_preserved_by_identity_and_repaired_on_disable() {
    FocusNavigator navigator;
    const std::array initial{
        widget(10U, Rect{0, 0, 10, 10}),
        widget(20U, Rect{0, 20, 10, 10}),
    };
    expect(navigator.set_widgets(initial), "layout should install");
    expect(navigator.set_focused(WidgetId{20U}), "second widget should accept focus");

    const std::array reordered{
        widget(20U, Rect{50, 20, 10, 10}),
        widget(10U, Rect{50, 0, 10, 10}),
    };
    expect(navigator.set_widgets(reordered), "updated layout should install");
    expect(navigator.focused() == WidgetId{20U}, "layout must preserve focus by ID");

    expect(navigator.set_widget_state(WidgetId{20U}, WidgetState{true, false, true}),
           "known widget state should update");
    expect(navigator.focused() == WidgetId{10U}, "disabled focus must repair predictably");
}

void directional_focus_prefers_the_requested_cone() {
    FocusNavigator navigator;
    const std::array widgets{
        widget(1U, Rect{0, 0, 10, 10}),
        widget(2U, Rect{40, 0, 10, 10}),
        widget(3U, Rect{2, 100, 10, 10}),
        widget(4U, Rect{80, 0, 10, 10}, WidgetState{true, false, true}),
    };
    expect(navigator.set_widgets(widgets), "spatial layout should install");
    expect(navigator.move(FocusDirection::right) == WidgetId{2U},
           "aligned right candidate should beat a steep diagonal");
    expect(navigator.move(FocusDirection::right) == WidgetId{2U},
           "disabled candidate must be skipped and focus must not wrap");
}

void linear_focus_wraps_and_skips_ineligible_widgets() {
    FocusNavigator navigator;
    const std::array widgets{
        widget(1U, Rect{0, 0, 10, 10}),
        widget(2U, Rect{0, 10, 10, 10}, WidgetState{false, true, true}),
        widget(3U, Rect{0, 20, 10, 10}),
    };
    expect(navigator.set_widgets(widgets), "linear layout should install");
    expect(navigator.advance() == WidgetId{3U}, "next must skip hidden widget");
    expect(navigator.advance() == WidgetId{1U}, "next must wrap");
    expect(navigator.advance(true) == WidgetId{3U}, "previous must wrap backwards");
}

void screen_stack_batches_are_bounded_and_transactional() {
    ScreenStack stack{2U};
    expect(stack.apply(ScreenCommand::push(ScreenId{1U})), "root screen should push");
    expect(stack.apply(ScreenCommand::push(ScreenId{2U})), "overlay should push");
    expect(stack.top() == ScreenId{2U}, "overlay should be on top");
    expect(!stack.apply(ScreenCommand::push(ScreenId{3U})), "depth overflow must fail closed");

    const std::array invalid_batch{
        ScreenCommand::pop(),
        ScreenCommand::push(ScreenId{}),
    };
    expect(!stack.apply(invalid_batch), "invalid batch must fail");
    expect(stack.size() == 2U, "failed batch must not partially pop");
    expect(stack.top() == ScreenId{2U}, "failed batch must preserve the top");

    const std::array valid_batch{
        ScreenCommand::pop(),
        ScreenCommand::replace(ScreenId{4U}),
        ScreenCommand::reset(ScreenId{5U}),
    };
    expect(stack.apply(valid_batch), "valid batch should apply in order");
    expect(stack.size() == 1U, "reset should leave one screen");
    expect(stack.top() == ScreenId{5U}, "reset target should be on top");
}

struct TestCase final {
    std::string_view name;
    std::function<void()> body;
};

} // namespace

int main() {
    const std::vector<TestCase> tests{
        {"rectangles_use_half_open_integer_bounds", rectangles_use_half_open_integer_bounds},
        {"released_input_does_not_trigger_an_action", released_input_does_not_trigger_an_action},
        {"widget_layout_validation_is_transactional", widget_layout_validation_is_transactional},
        {"focus_is_preserved_by_identity_and_repaired_on_disable",
         focus_is_preserved_by_identity_and_repaired_on_disable},
        {"directional_focus_prefers_the_requested_cone",
         directional_focus_prefers_the_requested_cone},
        {"linear_focus_wraps_and_skips_ineligible_widgets",
         linear_focus_wraps_and_skips_ineligible_widgets},
        {"screen_stack_batches_are_bounded_and_transactional",
         screen_stack_batches_are_bounded_and_transactional},
    };

    std::size_t failures{};
    for (const auto& test : tests) {
        try {
            test.body();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
        }
    }

    std::cout << tests.size() - failures << '/' << tests.size() << " tests passed\n";
    return failures == 0U ? 0 : 1;
}
