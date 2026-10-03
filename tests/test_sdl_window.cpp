#include "battlespades/platform/sdl_window_module.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using battlespades::core::TickContext;
using battlespades::core::TickDecision;
using battlespades::platform::MouseButton;
using battlespades::platform::SdlWindowConfig;
using battlespades::platform::SdlWindowModule;
using battlespades::platform::WindowEvent;
using battlespades::platform::WindowEventType;
using battlespades::platform::WindowExtent;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] bool has_event(const SdlWindowModule& window, WindowEventType type) {
    const auto events = window.events();
    return std::ranges::any_of(events,
                               [type](const WindowEvent& event) { return event.type == type; });
}

void push_window_event(SDL_EventType type, SDL_WindowID window_id) {
    SDL_Event event{};
    event.window.type = type;
    event.window.timestamp = SDL_GetTicksNS();
    event.window.windowID = window_id;
    expect(SDL_PushEvent(&event), "SDL rejected injected window event");
}

void sdl_window_preserves_lifecycle_and_input_events() {
    auto invalid_minimum = SdlWindowConfig{};
    invalid_minimum.minimum_extent = {invalid_minimum.initial_extent.width + 1U,
                                      invalid_minimum.initial_extent.height};
    expect(!battlespades::platform::valid_sdl_window_config(invalid_minimum),
           "minimum extent larger than startup extent must be rejected");

    expect(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "SDL_VIDEODRIVER", "dummy", true),
           "could not select SDL dummy video driver");

    SdlWindowConfig config{
        .title = "BattleSpades SDL event test",
        .initial_extent = WindowExtent{320U, 240U},
        .max_events_per_tick = 64U,
        .resizable = true,
        .high_pixel_density = true,
        .initially_hidden = true,
        .require_native_handle = false,
        .minimum_extent = WindowExtent{320U, 240U},
    };
    SdlWindowModule window{config};
    expect(window.start(),
           std::string{"SDL window startup failed: "} + std::string{window.last_error()});
    expect(window.logical_extent() == WindowExtent{320U, 240U},
           "logical startup extent must be retained");
    expect(window.drawable_extent().width > 0U && window.drawable_extent().height > 0U,
           "drawable startup extent must be usable");
    expect(!window.is_fullscreen(), "dummy test window must start windowed");
    expect(window.apply_display_mode(WindowExtent{400U, 300U}, false),
           std::string{"windowed resolution apply failed: "} + std::string{window.last_error()});
    expect(window.logical_extent() == WindowExtent{400U, 300U},
           "windowed resolution must update the logical extent immediately");
    expect(!window.set_image_cursor("missing-retail-cursor.png", 6U, 4U),
           "missing cursor images must fail closed");
    expect(window.set_image_cursor(
               std::filesystem::path{AOS_TEST_ASSET_ROOT} / "png/ui/cursor.png", 6U, 4U),
           std::string{"retail image cursor failed: "} + std::string{window.last_error()});
    expect(!window.set_image_cursor(
               std::filesystem::path{AOS_TEST_ASSET_ROOT} / "png/ui/cursor.png", 64U, 4U),
           "cursor hotspots outside the 64x64 retail image must fail closed");

    // Drain driver-created startup events before injecting the ordered test batch.
    expect(window.tick(TickContext{}) == TickDecision::continue_running,
           "startup events must not stop the window");

    const auto window_id = static_cast<SDL_WindowID>(window.platform_window_id());
    expect(window_id != 0U, "started SDL window must expose an event ID");

    // An event for a different SDL window must never leak through this adapter.
    push_window_event(SDL_EVENT_WINDOW_MINIMIZED, window_id + 1U);
    push_window_event(SDL_EVENT_WINDOW_MINIMIZED, window_id);
    push_window_event(SDL_EVENT_WINDOW_RESTORED, window_id);
    push_window_event(SDL_EVENT_WINDOW_MOUSE_ENTER, window_id);
    push_window_event(SDL_EVENT_WINDOW_MOUSE_LEAVE, window_id);
    push_window_event(SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED, window_id);

    SDL_Event motion{};
    motion.motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.timestamp = SDL_GetTicksNS();
    motion.motion.windowID = window_id;
    motion.motion.state = SDL_BUTTON_LMASK;
    motion.motion.x = 81.25F;
    motion.motion.y = 42.5F;
    motion.motion.xrel = 3.0F;
    motion.motion.yrel = -2.0F;
    expect(SDL_PushEvent(&motion), "SDL rejected injected mouse motion");

    SDL_Event button{};
    button.button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    button.button.timestamp = SDL_GetTicksNS();
    button.button.windowID = window_id;
    button.button.button = SDL_BUTTON_LEFT;
    button.button.down = true;
    button.button.clicks = 1U;
    button.button.x = 81.25F;
    button.button.y = 42.5F;
    expect(SDL_PushEvent(&button), "SDL rejected injected mouse button");

    expect(window.set_text_input_enabled(true),
           std::string{"SDL text input failed: "} +
               std::string{window.last_error()});
    SDL_Event committed_text{};
    committed_text.text.type = SDL_EVENT_TEXT_INPUT;
    committed_text.text.timestamp = SDL_GetTicksNS();
    committed_text.text.windowID = window_id;
    committed_text.text.text = ":";
    expect(SDL_PushEvent(&committed_text),
           "SDL rejected injected committed text");

    expect(window.tick(TickContext{}) == TickDecision::continue_running,
           "ordinary lifecycle/input events must not stop the window");
    expect(has_event(window, WindowEventType::minimized),
           "minimize event must cross the platform boundary");
    expect(has_event(window, WindowEventType::restored),
           "restore event must cross the platform boundary");
    expect(has_event(window, WindowEventType::mouse_entered),
           "mouse-enter event must cross the platform boundary");
    expect(has_event(window, WindowEventType::mouse_left),
           "mouse-leave event must cross the platform boundary");
    expect(has_event(window, WindowEventType::drawable_resized),
           "display-scale transition must invalidate the drawable extent");
    expect(has_event(window, WindowEventType::mouse_moved),
           "mouse motion must cross the platform boundary");
    expect(has_event(window, WindowEventType::mouse_button_pressed),
           "mouse press must cross the platform boundary");
    expect(has_event(window, WindowEventType::text_input),
           "committed UTF-8 text must cross the platform boundary");

    const auto motion_event = std::ranges::find_if(window.events(), [](const WindowEvent& event) {
        return event.type == WindowEventType::mouse_moved;
    });
    expect(motion_event != window.events().end(), "mouse motion event must be inspectable");
    expect(motion_event->mouse_x == 81.25F && motion_event->mouse_y == 42.5F,
           "mouse coordinates must remain in logical window units");
    expect(motion_event->mouse_delta_x == 3.0F && motion_event->mouse_delta_y == -2.0F,
           "mouse deltas must be preserved");

    const auto button_event = std::ranges::find_if(window.events(), [](const WindowEvent& event) {
        return event.type == WindowEventType::mouse_button_pressed;
    });
    expect(button_event != window.events().end(), "mouse button event must be inspectable");
    expect(button_event->mouse_button == MouseButton::left && button_event->click_count == 1U,
           "mouse button identity and click count must be preserved");
    const auto text_event =
        std::ranges::find_if(window.events(), [](const WindowEvent& event) {
            return event.type == WindowEventType::text_input;
        });
    expect(text_event != window.events().end() &&
               text_event->text == ":",
           "committed text bytes must be retained");
    // The dummy video driver uses an isolated clipboard; never replace the desktop clipboard.
    expect(SDL_SetClipboardText("127.0.0.1:27015"), "dummy clipboard must accept fixture text");
    expect(window.clipboard_text() == "127.0.0.1:27015",
           "explicit paste must read the complete host:port through the platform boundary");
    expect(window.set_text_input_enabled(false),
           "SDL text input must stop cleanly");

    for (const auto direction : {SDL_MOUSEWHEEL_NORMAL, SDL_MOUSEWHEEL_FLIPPED}) {
        SDL_Event wheel{};
        wheel.wheel.type = SDL_EVENT_MOUSE_WHEEL;
        wheel.wheel.windowID = window_id;
        wheel.wheel.direction = direction;
        const float sign = direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0F : 1.0F;
        wheel.wheel.x = 0.25F * sign;
        wheel.wheel.y = 1.5F * sign;
        wheel.wheel.mouse_x = 81.25F;
        wheel.wheel.mouse_y = 42.5F;
        expect(SDL_PushEvent(&wheel), "SDL rejected injected wheel event");
    }
    expect(window.tick(TickContext{}) == TickDecision::continue_running,
           "Wheel direction normalization must not stop the window");
    std::size_t wheel_count{};
    for (const auto& event : window.events()) {
        if (event.type != WindowEventType::mouse_wheel) continue;
        ++wheel_count;
        expect(event.mouse_delta_x == 0.25F && event.mouse_delta_y == 1.5F &&
                   event.mouse_x == 81.25F && event.mouse_y == 42.5F,
               "Normal and flipped wheels must share fractional deltas and pointer coordinates");
    }
    expect(wheel_count == 2U, "Both wheel directions must cross the platform boundary");

    // Render-only frames take the motion at the head of the queue to turn the
    // camera between ticks, but never motion queued behind a button or key:
    // the next tick must see the remaining events in their original order.
    {
        const auto push_motion = [window_id](float dx) {
            SDL_Event motion{};
            motion.motion.type = SDL_EVENT_MOUSE_MOTION;
            motion.motion.timestamp = SDL_GetTicksNS();
            motion.motion.windowID = window_id;
            motion.motion.xrel = dx;
            expect(SDL_PushEvent(&motion), "SDL rejected injected look motion");
        };
        push_motion(1.0F);
        push_motion(2.0F);
        SDL_Event press{};
        press.button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        press.button.timestamp = SDL_GetTicksNS();
        press.button.windowID = window_id;
        press.button.button = SDL_BUTTON_LEFT;
        press.button.down = true;
        press.button.clicks = 1U;
        expect(SDL_PushEvent(&press), "SDL rejected injected fire press");
        push_motion(4.0F);

        const auto early = window.take_leading_mouse_motion();
        expect(early.size() == 2U && early[0U].mouse_delta_x == 1.0F &&
                   early[1U].mouse_delta_x == 2.0F,
               "the leading look motion must be taken in order");
        expect(window.take_leading_mouse_motion().empty(),
               "motion behind a button press must wait for the tick");
        expect(window.tick(TickContext{}) == TickDecision::continue_running,
               "the tick after early look must continue");
        std::vector<WindowEvent> events;
        for (const auto& event : window.events()) {
            if (event.type == WindowEventType::mouse_moved ||
                event.type == WindowEventType::mouse_button_pressed) {
                events.push_back(event);
            }
        }
        expect(events.size() == 2U && events[0U].type == WindowEventType::mouse_button_pressed &&
                   events[1U].type == WindowEventType::mouse_moved &&
                   events[1U].mouse_delta_x == 4.0F,
               "the tick must receive the press and the motion behind it, and nothing twice");
    }

    push_window_event(SDL_EVENT_WINDOW_CLOSE_REQUESTED, window_id);
    expect(window.tick(TickContext{}) == TickDecision::stop,
           "native close request must stop the application loop");
    expect(has_event(window, WindowEventType::close_requested),
           "native close request must remain observable during shutdown");

    window.stop();
    expect(window.clipboard_text().empty(), "stopped windows must not read the clipboard");
    expect(window.logical_extent() == WindowExtent{}, "stop must clear logical extent");
    expect(window.drawable_extent() == WindowExtent{}, "stop must clear drawable extent");
    expect(window.events().empty(), "stop must clear retained events");
    expect(window.platform_window_id() == 0U, "stop must invalidate the SDL window ID");
}

} // namespace

int main() {
    try {
        sdl_window_preserves_lifecycle_and_input_events();
        std::cout << "[PASS] sdl_window_preserves_lifecycle_and_input_events\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
