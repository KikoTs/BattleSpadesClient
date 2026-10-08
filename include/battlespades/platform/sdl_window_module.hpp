#pragma once

#include "battlespades/platform/window_port.hpp"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace battlespades::platform {

/** Construction settings for the SDL3 desktop window. */
struct SdlWindowConfig final {
    std::string title{"BattleSpadesClient"};
    WindowExtent initial_extent{1'280U, 720U};
    std::size_t max_events_per_tick{256U};
    bool resizable{true};
    bool high_pixel_density{true};
    /** Hidden windows support deterministic smoke tests without UI flicker. */
    bool initially_hidden{false};
    /** Production rendering requires a native handle; dummy-driver tests do not. */
    bool require_native_handle{true};
    /** Retail run.py rejects interactive windows smaller than 320x240. */
    WindowExtent minimum_extent{320U, 240U};
};

/** Pure validation helper, usable by tests without initializing SDL. */
[[nodiscard]] bool valid_sdl_window_config(const SdlWindowConfig& config) noexcept;

/** One timed event of a developer input script, in ms from the first tick. */
struct ScriptedInputStep final {
    std::uint64_t at_ms{};
    WindowEvent event{};
};

/**
 * Developer input script (`BATTLESPADES_INPUT_SCRIPT=<file>`).
 *
 * Live key/gadget checks need input that does not depend on the window owning
 * the desktop foreground (a firewall prompt or another window can hold it
 * indefinitely). Each non-empty, non-`#` line is `<delay_ms> <verb> [args]`,
 * the delay counted from the previous line:
 *   key <sdl_scancode> down|up       keyboard event (repeat=false)
 *   tap <sdl_scancode> [hold_ms]     down, then up after hold_ms (default 80)
 *   click <x> <y> [left|right]       move + press + release, logical coords
 *   button <left|right> down|up      mouse button at the scripted position
 *   look <dx> <dy>                   relative mouse motion (captured look)
 *   wheel <dy>                       mouse wheel notch(es)
 *   window <focus_lost|focus_gained|minimized|restored|maximized>
 *   quit                             close request
 * The window module appends due steps after the SDL queue each tick, so they
 * reach the frontend on the same path as real input. Unset, it costs one
 * branch per tick. Unknown verbs are skipped; the result is sorted by time.
 */
[[nodiscard]] std::vector<ScriptedInputStep> parse_input_script(std::istream& source);

/**
 * Main-thread SDL3 video, window, and input owner.
 *
 * `start`, `tick`, and `stop` must run on the same main thread, matching the
 * RuntimeModule contract. The adapter acquires exactly one SDL video subsystem
 * reference and releases exactly that reference. If it bootstrapped SDL, it
 * also performs the final process-wide SDL_Quit after all SDL subsystems have
 * stopped. Register this module before other SDL adapters so reverse-order
 * application cleanup leaves it as SDL's final owner.
 *
 * Input events are retained in their SDL queue order and bounded by
 * `max_events_per_tick`. Excess events remain in SDL's queue for the next tick,
 * preventing unbounded work in the fixed-step runtime.
 */
class SdlWindowModule final : public WindowPort {
public:
    explicit SdlWindowModule(SdlWindowConfig config = {});
    ~SdlWindowModule() override;

    SdlWindowModule(const SdlWindowModule&) = delete;
    SdlWindowModule& operator=(const SdlWindowModule&) = delete;
    SdlWindowModule(SdlWindowModule&&) = delete;
    SdlWindowModule& operator=(SdlWindowModule&&) = delete;

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] bool start() override;
    [[nodiscard]] core::TickDecision tick(const core::TickContext& context) override;
    void stop() noexcept override;

    [[nodiscard]] WindowExtent logical_extent() const noexcept override;
    [[nodiscard]] WindowExtent drawable_extent() const noexcept override;
    [[nodiscard]] NativeWindowHandle native_handle() const noexcept override;
    [[nodiscard]] MouseState mouse_state() const noexcept override;
    [[nodiscard]] std::span<const WindowEvent> events() const noexcept override;
    [[nodiscard]] std::span<const DisplayMode> display_modes() const noexcept override;
    [[nodiscard]] bool is_fullscreen() const noexcept override;
    [[nodiscard]] FullscreenKind fullscreen_kind() const noexcept override;
    [[nodiscard]] std::uint32_t current_refresh_rate_millihertz() const noexcept override;
    using WindowPort::apply_display_mode;
    [[nodiscard]] bool apply_display_mode(WindowExtent extent, bool fullscreen,
                                          FullscreenKind kind) override;
    [[nodiscard]] bool set_image_cursor(const std::filesystem::path& image_path,
                                        std::uint32_t hot_x,
                                        std::uint32_t hot_y) override;
    [[nodiscard]] bool open_external_url(std::string_view url) override;
    [[nodiscard]] bool set_relative_mouse_mode(bool enabled) override;
    [[nodiscard]] bool relative_mouse_mode() const noexcept override;
    [[nodiscard]] bool set_text_input_enabled(bool enabled) override;
    [[nodiscard]] std::string clipboard_text() override;

    /** Last startup/runtime failure; empty when the latest operation succeeded. */
    [[nodiscard]] std::string_view last_error() const noexcept;

    /** SDL window ID for diagnostics and black-box event injection tests. */
    [[nodiscard]] std::uint32_t platform_window_id() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace battlespades::platform
