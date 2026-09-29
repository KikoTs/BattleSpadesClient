#include "battlespades/platform/sdl_window_module.hpp"

#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using battlespades::platform::MouseButton;
using battlespades::platform::parse_input_script;
using battlespades::platform::WindowEventType;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

void parses_keys_taps_and_relative_delays() {
    std::istringstream source{"# join\n"
                              "1000 key 30 down\n"
                              "\n"
                              "500 key 30 up\n"
                              "200 tap 29 150\n"};
    const auto steps = parse_input_script(source);
    expect(steps.size() == 4U, "two keys and a tap produce four events");
    expect(steps[0].at_ms == 1000U && steps[0].event.type == WindowEventType::key_pressed &&
               steps[0].event.scancode == 30U && !steps[0].event.repeated,
           "first key press at 1000 ms, not a repeat");
    expect(steps[1].at_ms == 1500U && steps[1].event.type == WindowEventType::key_released,
           "delays accumulate");
    expect(steps[2].at_ms == 1700U && steps[2].event.type == WindowEventType::key_pressed &&
               steps[2].event.scancode == 29U,
           "tap presses");
    expect(steps[3].at_ms == 1850U && steps[3].event.type == WindowEventType::key_released,
           "tap releases after its hold");
}

void parses_mouse_and_window_steps() {
    std::istringstream source{"100 click 612 477\n"
                              "0 button right down\n"
                              "50 look 0 250\n"
                              "0 wheel -1\n"
                              "0 window focus_lost\n"
                              "10 window maximized\n"
                              "0 bogus verb\n"
                              "5 quit\n"};
    const auto steps = parse_input_script(source);
    expect(steps.size() == 9U, "click expands to move/press/release");
    expect(steps[0].event.type == WindowEventType::mouse_moved && steps[0].event.mouse_x == 612.0F &&
               steps[0].event.mouse_y == 477.0F,
           "click moves first");
    bool right_press{};
    bool look{};
    bool wheel{};
    bool focus_lost{};
    bool maximized{};
    bool quit{};
    for (const auto& step : steps) {
        const auto& event = step.event;
        right_press = right_press || (event.type == WindowEventType::mouse_button_pressed &&
                                      event.mouse_button == MouseButton::right &&
                                      event.mouse_x == 612.0F);
        look = look || (event.type == WindowEventType::mouse_moved &&
                        event.mouse_delta_y == 250.0F);
        wheel = wheel || (event.type == WindowEventType::mouse_wheel &&
                          event.mouse_delta_y == -1.0F);
        focus_lost = focus_lost || event.type == WindowEventType::focus_lost;
        maximized = maximized || (event.type == WindowEventType::maximized && step.at_ms == 160U);
        quit = quit || (event.type == WindowEventType::close_requested && step.at_ms == 165U);
    }
    expect(right_press, "button uses the scripted cursor");
    expect(look && wheel, "look and wheel deltas");
    expect(focus_lost && maximized, "window transitions");
    expect(quit, "unknown verbs are skipped without breaking the clock");
    for (std::size_t index = 1U; index < steps.size(); ++index) {
        expect(steps[index - 1U].at_ms <= steps[index].at_ms, "steps are time ordered");
    }
}

} // namespace

int main() {
    try {
        parses_keys_taps_and_relative_delays();
        parses_mouse_and_window_steps();
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
    std::cout << "input script tests passed\n";
    return 0;
}
