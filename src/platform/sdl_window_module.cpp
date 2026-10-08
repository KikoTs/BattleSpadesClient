#include "battlespades/platform/sdl_window_module.hpp"

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace battlespades::platform {
namespace {

[[nodiscard]] WindowExtent checked_extent(int width, int height) noexcept {
    if (width <= 0 || height <= 0) {
        return {};
    }
    return {
        .width = static_cast<std::uint32_t>(width),
        .height = static_cast<std::uint32_t>(height),
    };
}

[[nodiscard]] MouseButton translate_mouse_button(std::uint8_t button) noexcept {
    switch (button) {
    case SDL_BUTTON_LEFT:
        return MouseButton::left;
    case SDL_BUTTON_MIDDLE:
        return MouseButton::middle;
    case SDL_BUTTON_RIGHT:
        return MouseButton::right;
    case SDL_BUTTON_X1:
        return MouseButton::extra_1;
    case SDL_BUTTON_X2:
        return MouseButton::extra_2;
    default:
        return MouseButton::unknown;
    }
}

[[nodiscard]] std::uint32_t translate_mouse_buttons(SDL_MouseButtonFlags flags) noexcept {
    std::uint32_t result{};
    if ((flags & SDL_BUTTON_LMASK) != 0U) {
        result |= mouse_button_mask(MouseButton::left);
    }
    if ((flags & SDL_BUTTON_MMASK) != 0U) {
        result |= mouse_button_mask(MouseButton::middle);
    }
    if ((flags & SDL_BUTTON_RMASK) != 0U) {
        result |= mouse_button_mask(MouseButton::right);
    }
    if ((flags & SDL_BUTTON_X1MASK) != 0U) {
        result |= mouse_button_mask(MouseButton::extra_1);
    }
    if ((flags & SDL_BUTTON_X2MASK) != 0U) {
        result |= mouse_button_mask(MouseButton::extra_2);
    }
    return result;
}

[[nodiscard]] NativeWindowHandle extract_native_handle(SDL_Window* window) noexcept {
    const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
    if (properties == 0U) {
        return {};
    }

    if (void* handle =
            SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr)) {
        return {
            .system = NativeWindowSystem::win32,
            .window = handle,
            .display = nullptr,
        };
    }

    if (void* handle =
            SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr)) {
        return {
            .system = NativeWindowSystem::cocoa,
            .window = handle,
            .display = nullptr,
        };
    }

    void* wayland_surface =
        SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
    if (wayland_surface != nullptr) {
        return {
            .system = NativeWindowSystem::wayland,
            .window = wayland_surface,
            .display = SDL_GetPointerProperty(
                properties, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr),
        };
    }

    void* x11_display =
        SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
    const Sint64 x11_window =
        SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    if (x11_display != nullptr && x11_window > 0) {
        return {
            .system = NativeWindowSystem::x11,
            .window = reinterpret_cast<void*>(static_cast<std::uintptr_t>(x11_window)),
            .display = x11_display,
        };
    }

    return {};
}

[[nodiscard]] std::optional<std::string> input_script_path() {
#if defined(_WIN32)
    char* buffer{};
    std::size_t size{};
    if (_dupenv_s(&buffer, &size, "BATTLESPADES_INPUT_SCRIPT") != 0 || buffer == nullptr) {
        return std::nullopt;
    }
    std::string value{buffer};
    std::free(buffer);
#else
    const char* raw = std::getenv("BATTLESPADES_INPUT_SCRIPT");
    std::string value{raw == nullptr ? "" : raw};
#endif
    if (value.empty()) return std::nullopt;
    return value;
}

[[nodiscard]] std::string sdl_error_or(std::string fallback) {
    const char* const error = SDL_GetError();
    if (error != nullptr && error[0] != '\0') {
        return error;
    }
    return fallback;
}

} // namespace

std::vector<ScriptedInputStep> parse_input_script(std::istream& file) {
    std::vector<ScriptedInputStep> steps;
    std::string line;
    std::uint64_t clock{};
    float cursor_x{};
    float cursor_y{};
    const auto button_of = [](const std::string& name) {
        return name == "right" ? MouseButton::right : MouseButton::left;
    };
    while (std::getline(file, line)) {
        std::istringstream fields{line};
        std::uint64_t delay{};
        std::string verb;
        if (line.empty() || line.front() == '#' || !(fields >> delay >> verb)) continue;
        clock += delay;
        if (verb == "key" || verb == "tap") {
            std::uint32_t scancode{};
            std::string state;
            fields >> scancode >> state;
            const bool tap = verb == "tap";
            const bool down = tap || state == "down";
            steps.push_back({clock, WindowEvent{.type = down ? WindowEventType::key_pressed
                                                              : WindowEventType::key_released,
                                                .scancode = scancode}});
            if (tap) {
                std::uint64_t hold{80U};
                if (!state.empty()) {
                    std::istringstream{state} >> hold;
                }
                steps.push_back({clock + hold, WindowEvent{.type = WindowEventType::key_released,
                                                           .scancode = scancode}});
            }
        } else if (verb == "click") {
            std::string button{"left"};
            fields >> cursor_x >> cursor_y >> button;
            steps.push_back({clock, WindowEvent{.type = WindowEventType::mouse_moved,
                                                .mouse_x = cursor_x, .mouse_y = cursor_y}});
            steps.push_back({clock + 30U, WindowEvent{.type = WindowEventType::mouse_button_pressed,
                                                      .mouse_x = cursor_x, .mouse_y = cursor_y,
                                                      .mouse_button = button_of(button),
                                                      .click_count = 1U}});
            steps.push_back({clock + 90U,
                             WindowEvent{.type = WindowEventType::mouse_button_released,
                                         .mouse_x = cursor_x, .mouse_y = cursor_y,
                                         .mouse_button = button_of(button), .click_count = 1U}});
        } else if (verb == "button") {
            std::string button;
            std::string state;
            fields >> button >> state;
            steps.push_back({clock, WindowEvent{.type = state == "up"
                                                            ? WindowEventType::mouse_button_released
                                                            : WindowEventType::mouse_button_pressed,
                                                .mouse_x = cursor_x, .mouse_y = cursor_y,
                                                .mouse_button = button_of(button),
                                                .click_count = 1U}});
        } else if (verb == "look") {
            float dx{};
            float dy{};
            fields >> dx >> dy;
            steps.push_back({clock, WindowEvent{.type = WindowEventType::mouse_moved,
                                                .mouse_x = cursor_x, .mouse_y = cursor_y,
                                                .mouse_delta_x = dx, .mouse_delta_y = dy}});
        } else if (verb == "wheel") {
            float dy{};
            fields >> dy;
            steps.push_back({clock, WindowEvent{.type = WindowEventType::mouse_wheel,
                                                .mouse_x = cursor_x, .mouse_y = cursor_y,
                                                .mouse_delta_y = dy}});
        } else if (verb == "quit") {
            steps.push_back({clock, WindowEvent{.type = WindowEventType::close_requested}});
        } else if (verb == "window") {
            // Window-state transitions (alt-tab, minimise) for focus tests.
            std::string state;
            fields >> state;
            const auto type = state == "focus_lost"   ? WindowEventType::focus_lost
                              : state == "focus_gained" ? WindowEventType::focus_gained
                              : state == "minimized"    ? WindowEventType::minimized
                              : state == "restored"     ? WindowEventType::restored
                              : state == "maximized"    ? WindowEventType::maximized
                                                        : WindowEventType::mouse_entered;
            steps.push_back({clock, WindowEvent{.type = type}});
        }
    }
    std::ranges::stable_sort(steps, {}, &ScriptedInputStep::at_ms);
    return steps;
}


struct SdlWindowModule::Impl final {
    explicit Impl(SdlWindowConfig requested_config) : config{std::move(requested_config)} {}

    [[nodiscard]] bool refresh_extents() noexcept {
        int logical_width{};
        int logical_height{};
        int pixel_width{};
        int pixel_height{};
        if (!SDL_GetWindowSize(window, &logical_width, &logical_height) ||
            !SDL_GetWindowSizeInPixels(window, &pixel_width, &pixel_height)) {
            return false;
        }

        const auto refreshed_logical = checked_extent(logical_width, logical_height);
        const auto refreshed_drawable = checked_extent(pixel_width, pixel_height);

        // Some window systems transiently report a 0x0 surface while a
        // window is minimized. That is not an SDL failure and must not tear
        // down the client. Keep the last renderer-usable extents until the
        // restore/pixel-size event supplies non-zero dimensions again.
        if (refreshed_logical.width != 0U) {
            logical_extent = refreshed_logical;
        }
        if (refreshed_drawable.width != 0U) {
            drawable_extent = refreshed_drawable;
        }
        return true;
    }

    [[nodiscard]] bool belongs_to_window(SDL_WindowID candidate) const noexcept {
        return candidate == window_id;
    }

    void refresh_display_modes() {
        display_modes.clear();
        if (window == nullptr) {
            return;
        }

        const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
        int count{};
        SDL_DisplayMode** const modes = SDL_GetFullscreenDisplayModes(display, &count);
        if (modes == nullptr || count <= 0) {
            return;
        }
        for (int index{}; index < count; ++index) {
            const SDL_DisplayMode* const mode = modes[index];
            if (mode == nullptr || mode->w < 640 || mode->h < 480) {
                continue;
            }
            const auto refresh = mode->refresh_rate > 0.0F
                                     ? static_cast<std::uint32_t>(
                                           std::lround(mode->refresh_rate * 1'000.0F))
                                     : 0U;
            display_modes.push_back(DisplayMode{
                checked_extent(mode->w, mode->h),
                refresh,
            });
        }
        SDL_free(modes);
        std::ranges::sort(display_modes, [](const DisplayMode& left, const DisplayMode& right) {
            if (left.extent.width != right.extent.width) {
                return left.extent.width < right.extent.width;
            }
            if (left.extent.height != right.extent.height) {
                return left.extent.height < right.extent.height;
            }
            return left.refresh_rate_millihertz < right.refresh_rate_millihertz;
        });
        const auto duplicate = std::ranges::unique(display_modes);
        display_modes.erase(duplicate.begin(), duplicate.end());
    }

    void push_window_event(WindowEventType type, const SDL_WindowEvent& source) {
        events.push_back(WindowEvent{
            .type = type,
            .timestamp_ns = source.timestamp,
            .extent = logical_extent,
            .drawable_extent = drawable_extent,
        });
    }

    SdlWindowConfig config;
    SDL_Window* window{};
    SDL_GLContext graphics_context{};
    SDL_Cursor* image_cursor{};
    SDL_WindowID window_id{};
    WindowExtent logical_extent{};
    WindowExtent drawable_extent{};
    NativeWindowHandle native_handle{};
    MouseState mouse_state{};
    std::vector<WindowEvent> events;
    // Pointer motion taken between ticks (take_leading_mouse_motion).
    std::vector<WindowEvent> early_motion;
    std::vector<DisplayMode> display_modes;
    std::string last_error;
    std::thread::id owner_thread{};
    std::vector<ScriptedInputStep> input_script;
    std::size_t input_script_next{};
    std::optional<std::chrono::steady_clock::time_point> input_script_start;
    std::optional<std::pair<float, float>> input_script_cursor;
    bool video_initialized{};
    bool owns_sdl_runtime{};
    bool close_requested{};
};

bool valid_sdl_window_config(const SdlWindowConfig& config) noexcept {
    constexpr std::size_t maximum_event_budget{4'096U};
    constexpr std::uint32_t maximum_sdl_dimension{
        static_cast<std::uint32_t>(std::numeric_limits<int>::max())};

    return !config.title.empty() && config.initial_extent.width > 0U &&
           config.initial_extent.height > 0U && config.minimum_extent.width > 0U &&
           config.minimum_extent.height > 0U &&
           config.minimum_extent.width <= config.initial_extent.width &&
           config.minimum_extent.height <= config.initial_extent.height &&
           config.initial_extent.width <= maximum_sdl_dimension &&
           config.initial_extent.height <= maximum_sdl_dimension &&
           config.minimum_extent.width <= maximum_sdl_dimension &&
           config.minimum_extent.height <= maximum_sdl_dimension &&
           config.max_events_per_tick > 0U && config.max_events_per_tick <= maximum_event_budget;
}

SdlWindowModule::SdlWindowModule(SdlWindowConfig config)
    : impl_{std::make_unique<Impl>(std::move(config))} {}

SdlWindowModule::~SdlWindowModule() {
    stop();
}

std::string_view SdlWindowModule::name() const noexcept {
    return "sdl-window";
}

bool SdlWindowModule::start() {
    if (impl_->window != nullptr || impl_->video_initialized) {
        impl_->last_error = "SDL window module is already started";
        return false;
    }
    if (!valid_sdl_window_config(impl_->config)) {
        impl_->last_error = "invalid SDL window configuration";
        return false;
    }

    impl_->last_error.clear();
    impl_->events.clear();
    impl_->events.reserve(impl_->config.max_events_per_tick);
    impl_->close_requested = false;
    impl_->owner_thread = std::this_thread::get_id();

    // This executable owns main(), so SDL must not install an SDL_main shim.
    SDL_SetMainReady();
#if defined(__linux__)
    // Prefer XWayland, falling back to native Wayland when no X server is
    // available. A normal-priority hint never overrides SDL_VIDEO_DRIVER (or
    // the legacy SDL_VIDEODRIVER) from the environment, so users and CI keep
    // their explicit choice.
    static_cast<void>(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11,wayland"));
#endif
    impl_->owns_sdl_runtime = SDL_WasInit(0) == 0U;
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        impl_->last_error = sdl_error_or("SDL video initialization failed");
        if (impl_->owns_sdl_runtime) {
            SDL_Quit();
            impl_->owns_sdl_runtime = false;
        }
        impl_->owner_thread = {};
        return false;
    }
    impl_->video_initialized = true;

    SDL_WindowFlags flags{};
#if defined(__HAIKU__)
    // Keep the dummy event-test driver usable without an OpenGL device.
    const bool haiku_gl = std::string_view{SDL_GetCurrentVideoDriver()} == "haiku";
    if (haiku_gl) flags |= SDL_WINDOW_OPENGL;
#endif
    if (impl_->config.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (impl_->config.high_pixel_density) {
        flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
    }
    if (impl_->config.initially_hidden) {
        flags |= SDL_WINDOW_HIDDEN;
    }

    impl_->window = SDL_CreateWindow(impl_->config.title.c_str(),
                                     static_cast<int>(impl_->config.initial_extent.width),
                                     static_cast<int>(impl_->config.initial_extent.height),
                                     flags);
    if (impl_->window == nullptr) {
        impl_->last_error = sdl_error_or("SDL window creation failed");
        stop();
        return false;
    }

#if defined(__HAIKU__)
    if (haiku_gl) {
        impl_->graphics_context = SDL_GL_CreateContext(impl_->window);
        if (impl_->graphics_context == nullptr) {
            impl_->last_error = sdl_error_or("SDL OpenGL context creation failed");
            stop();
            return false;
        }
        // CreateContext makes BGLView current and locks it. Hand the lock to
        // bgfx; retaining SDL's lock prevents its wrapper from releasing GL.
        if (!SDL_GL_MakeCurrent(impl_->window, nullptr)) {
            impl_->last_error = sdl_error_or("SDL OpenGL context handoff failed");
            stop();
            return false;
        }
    }
#endif

    if (!SDL_SetWindowMinimumSize(impl_->window,
                                  static_cast<int>(impl_->config.minimum_extent.width),
                                  static_cast<int>(impl_->config.minimum_extent.height))) {
        impl_->last_error = sdl_error_or("SDL window minimum-size setup failed");
        stop();
        return false;
    }

    impl_->window_id = SDL_GetWindowID(impl_->window);
    if (impl_->window_id == 0U || !impl_->refresh_extents() || impl_->logical_extent.width == 0U ||
        impl_->drawable_extent.width == 0U) {
        impl_->last_error = sdl_error_or("SDL window extent query failed");
        stop();
        return false;
    }

    impl_->native_handle = extract_native_handle(impl_->window);
#if defined(__HAIKU__)
    if (impl_->graphics_context != nullptr) {
        impl_->native_handle = {
            .system = NativeWindowSystem::haiku,
            .graphics_context = impl_->graphics_context,
        };
    }
#endif
    if (impl_->config.require_native_handle && !impl_->native_handle.valid()) {
        impl_->last_error = "SDL did not expose a renderer-compatible native window handle";
        stop();
        return false;
    }

    float mouse_x{};
    float mouse_y{};
    const SDL_MouseButtonFlags mouse_buttons = SDL_GetMouseState(&mouse_x, &mouse_y);
    impl_->mouse_state = {
        .x = mouse_x,
        .y = mouse_y,
        .pressed_buttons = translate_mouse_buttons(mouse_buttons),
    };
    impl_->refresh_display_modes();
    if (const auto script = input_script_path(); script.has_value()) {
        std::ifstream file{std::filesystem::path{*script}};
        impl_->input_script = parse_input_script(file);
        impl_->input_script_next = 0U;
        impl_->input_script_start.reset();
    }
    return true;
}

core::TickDecision SdlWindowModule::tick(const core::TickContext&) {
    if (impl_->window == nullptr || impl_->owner_thread != std::this_thread::get_id()) {
        impl_->last_error = impl_->window == nullptr ? "SDL window module is not started"
                                                     : "SDL window ticked from a non-owner thread";
        return core::TickDecision::stop;
    }

    impl_->early_motion.clear();
    impl_->events.clear();
    impl_->close_requested = false;

    SDL_Event source{};
    std::size_t events_polled{};
    while (events_polled < impl_->config.max_events_per_tick && SDL_PollEvent(&source)) {
        ++events_polled;

        if (source.type == SDL_EVENT_QUIT) {
            impl_->events.push_back(WindowEvent{
                .type = WindowEventType::close_requested,
                .timestamp_ns = source.quit.timestamp,
                .extent = impl_->logical_extent,
                .drawable_extent = impl_->drawable_extent,
            });
            impl_->close_requested = true;
            continue;
        }

        switch (source.type) {
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (impl_->belongs_to_window(source.window.windowID)) {
                impl_->push_window_event(WindowEventType::close_requested, source.window);
                impl_->close_requested = true;
            }
            break;
        case SDL_EVENT_WINDOW_RESIZED:
            if (impl_->belongs_to_window(source.window.windowID)) {
                if (!impl_->refresh_extents()) {
                    impl_->last_error = sdl_error_or("SDL window extent refresh failed");
                    return core::TickDecision::stop;
                }
                impl_->push_window_event(WindowEventType::resized, source.window);
            }
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            if (impl_->belongs_to_window(source.window.windowID)) {
                if (!impl_->refresh_extents()) {
                    impl_->last_error = sdl_error_or("SDL drawable extent refresh failed");
                    return core::TickDecision::stop;
                }
                impl_->push_window_event(WindowEventType::drawable_resized, source.window);
            }
            break;
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            if (impl_->belongs_to_window(source.window.windowID)) {
                if (!impl_->refresh_extents()) {
                    impl_->last_error = sdl_error_or("SDL display extent refresh failed");
                    return core::TickDecision::stop;
                }
                // A monitor move can change the backing pixel size without a
                // logical resize. Notify the renderer explicitly even on
                // platforms that omit a separate pixel-size event.
                impl_->push_window_event(WindowEventType::drawable_resized, source.window);
                if (source.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED) {
                    // Resolution choices and exclusive modes follow the display
                    // the window is on.
                    impl_->refresh_display_modes();
                }
            }
            break;
        case SDL_EVENT_WINDOW_MINIMIZED:
            if (impl_->belongs_to_window(source.window.windowID)) {
                impl_->push_window_event(WindowEventType::minimized, source.window);
            }
            break;
        case SDL_EVENT_WINDOW_RESTORED:
            if (impl_->belongs_to_window(source.window.windowID)) {
                if (!impl_->refresh_extents()) {
                    impl_->last_error = sdl_error_or("SDL restored extent refresh failed");
                    return core::TickDecision::stop;
                }
                impl_->push_window_event(WindowEventType::restored, source.window);
            }
            break;
        case SDL_EVENT_WINDOW_MAXIMIZED:
            // Restoring a window that was minimised while maximised reports
            // MAXIMIZED, not RESTORED. The frontend treats it as "visible".
            if (impl_->belongs_to_window(source.window.windowID)) {
                if (!impl_->refresh_extents()) {
                    impl_->last_error = sdl_error_or("SDL maximized extent refresh failed");
                    return core::TickDecision::stop;
                }
                impl_->push_window_event(WindowEventType::maximized, source.window);
            }
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            if (impl_->belongs_to_window(source.window.windowID)) {
                impl_->push_window_event(WindowEventType::focus_gained, source.window);
            }
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            if (impl_->belongs_to_window(source.window.windowID)) {
                impl_->push_window_event(WindowEventType::focus_lost, source.window);
            }
            break;
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
            if (impl_->belongs_to_window(source.window.windowID)) {
                impl_->push_window_event(WindowEventType::mouse_entered, source.window);
            }
            break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            if (impl_->belongs_to_window(source.window.windowID)) {
                impl_->push_window_event(WindowEventType::mouse_left, source.window);
            }
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            if (impl_->belongs_to_window(source.key.windowID)) {
                impl_->events.push_back(WindowEvent{
                    .type = source.type == SDL_EVENT_KEY_DOWN ? WindowEventType::key_pressed
                                                              : WindowEventType::key_released,
                    .timestamp_ns = source.key.timestamp,
                    .scancode = static_cast<std::uint32_t>(source.key.scancode),
                    .keycode = source.key.key,
                    .modifiers = source.key.mod,
                    .repeated = source.key.repeat,
                });
            }
            break;
        case SDL_EVENT_TEXT_INPUT:
            if (impl_->belongs_to_window(source.text.windowID) &&
                source.text.text != nullptr) {
                impl_->events.push_back(WindowEvent{
                    .type = WindowEventType::text_input,
                    .timestamp_ns = source.text.timestamp,
                    .text = source.text.text,
                });
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (impl_->belongs_to_window(source.motion.windowID)) {
                impl_->mouse_state = {
                    .x = source.motion.x,
                    .y = source.motion.y,
                    .pressed_buttons = translate_mouse_buttons(source.motion.state),
                };
                impl_->events.push_back(WindowEvent{
                    .type = WindowEventType::mouse_moved,
                    .timestamp_ns = source.motion.timestamp,
                    .mouse_x = source.motion.x,
                    .mouse_y = source.motion.y,
                    .mouse_delta_x = source.motion.xrel,
                    .mouse_delta_y = source.motion.yrel,
                });
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (impl_->belongs_to_window(source.button.windowID)) {
                impl_->events.push_back(WindowEvent{
                    .type = source.type == SDL_EVENT_MOUSE_BUTTON_DOWN
                                ? WindowEventType::mouse_button_pressed
                                : WindowEventType::mouse_button_released,
                    .timestamp_ns = source.button.timestamp,
                    .mouse_x = source.button.x,
                    .mouse_y = source.button.y,
                    .mouse_button = translate_mouse_button(source.button.button),
                    .click_count = source.button.clicks,
                });
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (impl_->belongs_to_window(source.wheel.windowID)) {
                const float direction = source.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0F : 1.0F;
                impl_->events.push_back(WindowEvent{
                    .type = WindowEventType::mouse_wheel,
                    .timestamp_ns = source.wheel.timestamp,
                    .mouse_x = source.wheel.mouse_x,
                    .mouse_y = source.wheel.mouse_y,
                    .mouse_delta_x = source.wheel.x * direction,
                    .mouse_delta_y = source.wheel.y * direction,
                });
            }
            break;
        default:
            break;
        }
    }

    if (impl_->input_script_next < impl_->input_script.size()) {
        const auto now = std::chrono::steady_clock::now();
        if (!impl_->input_script_start.has_value()) impl_->input_script_start = now;
        const auto elapsed = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - *impl_->input_script_start).count());
        while (impl_->input_script_next < impl_->input_script.size() &&
               impl_->input_script[impl_->input_script_next].at_ms <= elapsed) {
            auto event = impl_->input_script[impl_->input_script_next++].event;
            event.extent = impl_->logical_extent;
            event.drawable_extent = impl_->drawable_extent;
            if (event.type == WindowEventType::mouse_moved ||
                event.type == WindowEventType::mouse_button_pressed) {
                impl_->input_script_cursor = std::pair{event.mouse_x, event.mouse_y};
            }
            impl_->events.push_back(std::move(event));
        }
    }

    float mouse_x{};
    float mouse_y{};
    const SDL_MouseButtonFlags mouse_buttons = SDL_GetMouseState(&mouse_x, &mouse_y);
    impl_->mouse_state = {
        .x = impl_->input_script_cursor.has_value() ? impl_->input_script_cursor->first : mouse_x,
        .y = impl_->input_script_cursor.has_value() ? impl_->input_script_cursor->second : mouse_y,
        .pressed_buttons = translate_mouse_buttons(mouse_buttons),
    };

    return impl_->close_requested ? core::TickDecision::stop : core::TickDecision::continue_running;
}

void SdlWindowModule::stop() noexcept {
    if (impl_ == nullptr) {
        return;
    }

    if (impl_->image_cursor != nullptr) {
        // The active cursor must no longer reference the object we destroy.
        static_cast<void>(SDL_SetCursor(SDL_GetDefaultCursor()));
        SDL_DestroyCursor(impl_->image_cursor);
        impl_->image_cursor = nullptr;
    }

    impl_->early_motion.clear();
    impl_->events.clear();
    impl_->display_modes.clear();
    impl_->mouse_state = {};
    impl_->native_handle = {};
    impl_->logical_extent = {};
    impl_->drawable_extent = {};
    impl_->window_id = 0U;
    impl_->close_requested = false;

    if (impl_->graphics_context != nullptr) {
        static_cast<void>(SDL_GL_MakeCurrent(impl_->window, nullptr));
        static_cast<void>(SDL_GL_DestroyContext(impl_->graphics_context));
        impl_->graphics_context = nullptr;
    }
    if (impl_->window != nullptr) {
        SDL_DestroyWindow(impl_->window);
        impl_->window = nullptr;
    }
    if (impl_->video_initialized) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        impl_->video_initialized = false;
    }
    if (impl_->owns_sdl_runtime) {
        // Other SDL adapters must already be stopped by reverse module order.
        // Never tear down process-wide state that an independently-owned SDL
        // subsystem still uses.
        if (SDL_WasInit(0) == 0U) {
            SDL_Quit();
        }
        impl_->owns_sdl_runtime = false;
    }
    impl_->owner_thread = {};
}

WindowExtent SdlWindowModule::logical_extent() const noexcept {
    return impl_->logical_extent;
}

WindowExtent SdlWindowModule::drawable_extent() const noexcept {
    return impl_->drawable_extent;
}

NativeWindowHandle SdlWindowModule::native_handle() const noexcept {
    return impl_->native_handle;
}

MouseState SdlWindowModule::mouse_state() const noexcept {
    return impl_->mouse_state;
}

std::span<const WindowEvent> SdlWindowModule::events() const noexcept {
    return impl_->events;
}

std::span<const WindowEvent> SdlWindowModule::take_leading_mouse_motion() {
    impl_->early_motion.clear();
    if (impl_->window == nullptr || impl_->owner_thread != std::this_thread::get_id()) {
        return {};
    }
    SDL_PumpEvents();
    constexpr int batch{64};
    std::array<SDL_Event, static_cast<std::size_t>(batch)> queued{};
    // Bounded like tick(): a mouse polling at 8 kHz queues ~60 events in the
    // longest gap between two render-only frames.
    for (std::size_t round{}; round < 8U; ++round) {
        const int peeked =
            SDL_PeepEvents(queued.data(), batch, SDL_PEEKEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST);
        int leading{};
        while (leading < peeked && queued[static_cast<std::size_t>(leading)].type ==
                                       SDL_EVENT_MOUSE_MOTION &&
               impl_->belongs_to_window(
                   queued[static_cast<std::size_t>(leading)].motion.windowID)) {
            ++leading;
        }
        if (leading <= 0) {
            break;
        }
        // Events are only ever appended behind the peeked ones, so the first
        // `leading` motion events in the queue are exactly the run above.
        const int taken = SDL_PeepEvents(queued.data(), leading, SDL_GETEVENT,
                                         SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_MOTION);
        for (int index{}; index < taken; ++index) {
            const auto& motion = queued[static_cast<std::size_t>(index)].motion;
            impl_->mouse_state = {
                .x = motion.x,
                .y = motion.y,
                .pressed_buttons = translate_mouse_buttons(motion.state),
            };
            impl_->early_motion.push_back(WindowEvent{
                .type = WindowEventType::mouse_moved,
                .timestamp_ns = motion.timestamp,
                .mouse_x = motion.x,
                .mouse_y = motion.y,
                .mouse_delta_x = motion.xrel,
                .mouse_delta_y = motion.yrel,
            });
        }
        if (taken < batch || leading < peeked) {
            break;
        }
    }
    return impl_->early_motion;
}

std::span<const DisplayMode> SdlWindowModule::display_modes() const noexcept {
    return impl_->display_modes;
}

bool SdlWindowModule::is_fullscreen() const noexcept {
    return impl_->window != nullptr &&
           (SDL_GetWindowFlags(impl_->window) & SDL_WINDOW_FULLSCREEN) != 0U;
}

FullscreenKind SdlWindowModule::fullscreen_kind() const noexcept {
    // SDL3 reports no fullscreen mode for a desktop (borderless) fullscreen.
    return impl_->window != nullptr && SDL_GetWindowFullscreenMode(impl_->window) == nullptr
               ? FullscreenKind::borderless
               : FullscreenKind::exclusive;
}

std::uint32_t SdlWindowModule::current_refresh_rate_millihertz() const noexcept {
    if (impl_->window == nullptr) {
        return 0U;
    }
    const SDL_DisplayMode* mode = SDL_GetWindowFullscreenMode(impl_->window);
    if (mode == nullptr || (SDL_GetWindowFlags(impl_->window) & SDL_WINDOW_FULLSCREEN) == 0U) {
        const SDL_DisplayID display = SDL_GetDisplayForWindow(impl_->window);
        mode = display != 0U ? SDL_GetCurrentDisplayMode(display) : nullptr;
    }
    if (mode == nullptr || !(mode->refresh_rate > 0.0F) || mode->refresh_rate > 2'000.0F) {
        return 0U;
    }
    return static_cast<std::uint32_t>(std::lround(mode->refresh_rate * 1'000.0F));
}

bool SdlWindowModule::apply_display_mode(WindowExtent extent, bool fullscreen,
                                         FullscreenKind kind) {
    if (impl_->window == nullptr || impl_->owner_thread != std::this_thread::get_id()) {
        impl_->last_error = impl_->window == nullptr
                                ? "SDL window module is not started"
                                : "SDL display mode changed from a non-owner thread";
        return false;
    }
    if (extent.width < impl_->config.minimum_extent.width ||
        extent.height < impl_->config.minimum_extent.height ||
        extent.width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        extent.height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        impl_->last_error = "requested SDL display mode is outside the supported extent";
        return false;
    }

    if (fullscreen && kind == FullscreenKind::borderless) {
        // Desktop fullscreen: a null mode keeps the display's current mode,
        // so alt-tab never triggers a mode switch or an SDL auto-minimise.
        if (!SDL_SetWindowFullscreenMode(impl_->window, nullptr) ||
            !SDL_SetWindowFullscreen(impl_->window, true)) {
            impl_->last_error = sdl_error_or("SDL could not apply borderless fullscreen");
            return false;
        }
    } else if (fullscreen) {
        SDL_DisplayMode closest{};
        const SDL_DisplayID display = SDL_GetDisplayForWindow(impl_->window);
        if (display == 0U ||
            !SDL_GetClosestFullscreenDisplayMode(display,
                                                 static_cast<int>(extent.width),
                                                 static_cast<int>(extent.height),
                                                 0.0F,
                                                 true,
                                                 &closest) ||
            !SDL_SetWindowFullscreenMode(impl_->window, &closest) ||
            !SDL_SetWindowFullscreen(impl_->window, true)) {
            impl_->last_error = sdl_error_or("SDL could not apply the fullscreen display mode");
            return false;
        }
    } else {
        if (!SDL_SetWindowFullscreen(impl_->window, false) ||
            !SDL_SetWindowFullscreenMode(impl_->window, nullptr) ||
            !SDL_SetWindowSize(impl_->window,
                               static_cast<int>(extent.width),
                               static_cast<int>(extent.height))) {
            impl_->last_error = sdl_error_or("SDL could not apply the windowed display mode");
            return false;
        }
    }

    static_cast<void>(SDL_SyncWindow(impl_->window));
    if (!impl_->refresh_extents()) {
        impl_->last_error = sdl_error_or("SDL could not refresh the applied display mode");
        return false;
    }
    impl_->refresh_display_modes();
    impl_->last_error.clear();
    return true;
}

bool SdlWindowModule::set_image_cursor(const std::filesystem::path& image_path,
                                       std::uint32_t hot_x,
                                       std::uint32_t hot_y) {
    if (impl_->window == nullptr || impl_->owner_thread != std::this_thread::get_id()) {
        impl_->last_error = impl_->window == nullptr
                                ? "SDL window module is not started"
                                : "image cursor changed from a non-owner thread";
        return false;
    }
    if (image_path.empty()) {
        impl_->last_error = "image cursor path is empty";
        return false;
    }

    const auto encoded_path = image_path.u8string();
    const std::string utf8_path{reinterpret_cast<const char*>(encoded_path.data()),
                                encoded_path.size()};
    SDL_Surface* const surface = SDL_LoadSurface(utf8_path.c_str());
    if (surface == nullptr) {
        impl_->last_error = sdl_error_or("SDL could not load the image cursor");
        return false;
    }

    const bool hotspot_valid = hot_x < static_cast<std::uint32_t>(surface->w) &&
                               hot_y < static_cast<std::uint32_t>(surface->h);
    if (!hotspot_valid) {
        SDL_DestroySurface(surface);
        impl_->last_error = "image cursor hotspot is outside the image";
        return false;
    }

    SDL_Cursor* const replacement = SDL_CreateColorCursor(
        surface, static_cast<int>(hot_x), static_cast<int>(hot_y));
    SDL_DestroySurface(surface);
    if (replacement == nullptr) {
        impl_->last_error = sdl_error_or("SDL could not create the image cursor");
        return false;
    }
    if (!SDL_SetCursor(replacement)) {
        impl_->last_error = sdl_error_or("SDL could not activate the image cursor");
        SDL_DestroyCursor(replacement);
        return false;
    }

    if (impl_->image_cursor != nullptr) {
        SDL_DestroyCursor(impl_->image_cursor);
    }
    impl_->image_cursor = replacement;
    impl_->last_error.clear();
    return true;
}

bool SdlWindowModule::open_external_url(std::string_view url) {
    if (impl_->window == nullptr || impl_->owner_thread != std::this_thread::get_id()) {
        impl_->last_error = impl_->window == nullptr
                                ? "SDL window module is not started"
                                : "external URL requested from a non-owner thread";
        return false;
    }
    if ((!url.starts_with("https://") && !url.starts_with("http://")) ||
        url.find('\0') != std::string_view::npos) {
        impl_->last_error = "external URL must be a non-empty HTTP(S) address";
        return false;
    }

    const std::string owned_url{url};
    if (!SDL_OpenURL(owned_url.c_str())) {
        impl_->last_error = sdl_error_or("SDL could not open the external URL");
        return false;
    }
    impl_->last_error.clear();
    return true;
}

bool SdlWindowModule::set_relative_mouse_mode(bool enabled) {
    if (impl_->window == nullptr || impl_->owner_thread != std::this_thread::get_id()) {
        impl_->last_error = impl_->window == nullptr
                                ? "SDL window module is not started"
                                : "relative mouse mode changed from a non-owner thread";
        return false;
    }
    if (!SDL_SetWindowRelativeMouseMode(impl_->window, enabled)) {
        impl_->last_error = sdl_error_or("SDL could not change relative mouse mode");
        return false;
    }
    impl_->last_error.clear();
    return true;
}

bool SdlWindowModule::relative_mouse_mode() const noexcept {
    return impl_->window != nullptr && SDL_GetWindowRelativeMouseMode(impl_->window);
}

bool SdlWindowModule::set_text_input_enabled(bool enabled) {
    if (impl_->window == nullptr ||
        impl_->owner_thread != std::this_thread::get_id()) {
        impl_->last_error =
            impl_->window == nullptr
                ? "SDL window module is not started"
                : "text input changed from a non-owner thread";
        return false;
    }
    const bool changed = enabled ? SDL_StartTextInput(impl_->window)
                                 : SDL_StopTextInput(impl_->window);
    if (!changed) {
        impl_->last_error =
            sdl_error_or("SDL could not change text-input state");
        return false;
    }
    impl_->last_error.clear();
    return true;
}

std::string SdlWindowModule::clipboard_text() {
    if (impl_->window == nullptr || impl_->owner_thread != std::this_thread::get_id()) {
        return {};
    }
    const auto text = std::unique_ptr<char, decltype(&SDL_free)>{SDL_GetClipboardText(), SDL_free};
    return text ? std::string{text.get()} : std::string{};
}

std::string_view SdlWindowModule::last_error() const noexcept {
    return impl_->last_error;
}

std::uint32_t SdlWindowModule::platform_window_id() const noexcept {
    return impl_->window_id;
}

} // namespace battlespades::platform
