#pragma once

#include "battlespades/core/runtime_module.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::platform {

struct WindowExtent final {
    std::uint32_t width{};
    std::uint32_t height{};

    [[nodiscard]] friend constexpr bool operator==(const WindowExtent&,
                                                   const WindowExtent&) = default;
};

/** One usable exclusive-fullscreen mode reported by the active display. */
struct DisplayMode final {
    WindowExtent extent{};
    /** Refresh rate in millihertz; zero means the platform did not report it. */
    std::uint32_t refresh_rate_millihertz{};

    [[nodiscard]] friend constexpr bool operator==(const DisplayMode&, const DisplayMode&) = default;
};

/**
 * How a fullscreen window covers the display.
 *
 * `exclusive` switches the display mode (retail behaviour; slow alt-tab and
 * SDL minimises it on focus loss). `borderless` covers the desktop at its
 * current mode, so alt-tab is instant and the window never minimises.
 */
enum class FullscreenKind : std::uint8_t {
    exclusive,
    borderless,
};

/** Desktop window systems supported by bgfx's PlatformData boundary. */
enum class NativeWindowSystem : std::uint8_t {
    unavailable,
    win32,
    cocoa,
    x11,
    wayland,
};

/**
 * Native handles required to attach bgfx to a window created by the platform
 * adapter. `window` maps to bgfx::PlatformData::nwh and `display` maps to
 * bgfx::PlatformData::ndt. The pointers are borrowed and remain valid only
 * while the WindowPort is started.
 */
struct NativeWindowHandle final {
    NativeWindowSystem system{NativeWindowSystem::unavailable};
    void* window{};
    void* display{};

    [[nodiscard]] constexpr bool valid() const noexcept {
        if (window == nullptr) {
            return false;
        }
        switch (system) {
        case NativeWindowSystem::win32:
        case NativeWindowSystem::cocoa:
            return true;
        case NativeWindowSystem::x11:
        case NativeWindowSystem::wayland:
            return display != nullptr;
        case NativeWindowSystem::unavailable:
            return false;
        }
        return false;
    }
};

enum class MouseButton : std::uint8_t {
    unknown,
    left,
    middle,
    right,
    extra_1,
    extra_2,
};

[[nodiscard]] constexpr std::uint32_t mouse_button_mask(MouseButton button) noexcept {
    switch (button) {
    case MouseButton::left:
        return 1U << 0U;
    case MouseButton::middle:
        return 1U << 1U;
    case MouseButton::right:
        return 1U << 2U;
    case MouseButton::extra_1:
        return 1U << 3U;
    case MouseButton::extra_2:
        return 1U << 4U;
    case MouseButton::unknown:
        return 0U;
    }
    return 0U;
}

/** Latest mouse state in logical window coordinates, not drawable pixels. */
struct MouseState final {
    float x{};
    float y{};
    std::uint32_t pressed_buttons{};

    [[nodiscard]] constexpr bool pressed(MouseButton button) const noexcept {
        const auto mask = mouse_button_mask(button);
        return mask != 0U && (pressed_buttons & mask) != 0U;
    }
};

enum class WindowEventType : std::uint8_t {
    close_requested,
    /** Logical client-area extent changed. */
    resized,
    /** Physical drawable extent changed, usually due to display DPI. */
    drawable_resized,
    /** Window entered the platform-minimized state; extents retain their last usable value. */
    minimized,
    /** Window returned from a minimized or maximized state. */
    restored,
    /** Window entered the maximized state (may follow a minimize directly). */
    maximized,
    focus_gained,
    focus_lost,
    mouse_entered,
    mouse_left,
    key_pressed,
    key_released,
    /** UTF-8 text committed by the active platform text-input method. */
    text_input,
    mouse_moved,
    mouse_button_pressed,
    mouse_button_released,
    mouse_wheel,
};

/**
 * One ordered platform event.
 *
 * Coordinates are expressed in logical window units. Keyboard values preserve
 * SDL3's layout-independent scancode and layout-dependent keycode as unsigned
 * integers without exposing SDL headers to engine code. The modifier mask is
 * the SDL3 key-modifier bitset; a later input mapper converts these raw values
 * into semantic gameplay/UI actions.
 */
struct WindowEvent final {
    WindowEventType type{};
    std::uint64_t timestamp_ns{};
    /** Current logical client-area extent for resize events. */
    WindowExtent extent{};
    /** Current physical pixel extent for resize events. */
    WindowExtent drawable_extent{};
    std::uint32_t scancode{};
    std::uint32_t keycode{};
    std::uint16_t modifiers{};
    bool repeated{};
    std::string text{};
    float mouse_x{};
    float mouse_y{};
    float mouse_delta_x{};
    float mouse_delta_y{};
    MouseButton mouse_button{MouseButton::unknown};
    std::uint8_t click_count{};
};

/**
 * Backend-neutral window boundary. The planned SDL3 adapter will implement it;
 * the core never includes SDL headers or owns SDL-specific handles.
 */
class WindowPort : public core::RuntimeModule {
public:
    /** Client-area extent in logical window coordinates. */
    [[nodiscard]] virtual WindowExtent logical_extent() const noexcept = 0;

    /** Client-area extent in physical pixels, suitable for renderer reset. */
    [[nodiscard]] virtual WindowExtent drawable_extent() const noexcept = 0;

    /** Borrowed native handles used to initialize the renderer. */
    [[nodiscard]] virtual NativeWindowHandle native_handle() const noexcept = 0;

    /** Latest mouse state in logical window coordinates. */
    [[nodiscard]] virtual MouseState mouse_state() const noexcept = 0;

    /**
     * Events collected during the most recent tick, in SDL queue order. The
     * span is invalidated by the next tick or stop call.
     */
    [[nodiscard]] virtual std::span<const WindowEvent> events() const noexcept = 0;

    /**
     * Between ticks: removes the pointer-motion events queued at the head of
     * the platform queue, up to the first event of any other kind, and returns
     * them in order. Render-only frames use this to turn the camera with the
     * mouse at display rate instead of at the 60 Hz tick rate.
     *
     * Only the leading run is taken, so no motion ever overtakes a key or
     * button event: the next tick sees exactly the same event order, and the
     * simulation reads the same accumulated look angles, as if every event had
     * waited for it. The span is invalidated by the next call, tick or stop.
     */
    [[nodiscard]] virtual std::span<const WindowEvent> take_leading_mouse_motion() {
        return {};
    }

    /** Snapshot of display modes suitable for the Graphics/Resolution row. */
    [[nodiscard]] virtual std::span<const DisplayMode> display_modes() const noexcept = 0;

    /** True while the native window uses an SDL fullscreen mode. */
    [[nodiscard]] virtual bool is_fullscreen() const noexcept = 0;

    /** Kind of the active fullscreen state; meaningless while windowed. */
    [[nodiscard]] virtual FullscreenKind fullscreen_kind() const noexcept = 0;

    /**
     * Refresh rate of the display that currently holds the window, in
     * millihertz; zero when the platform does not report one.
     */
    [[nodiscard]] virtual std::uint32_t current_refresh_rate_millihertz() const noexcept = 0;

    /**
     * Applies a retail resolution/fullscreen pair on the window owner thread.
     * A borderless fullscreen request ignores `extent` and covers the desktop.
     * Implementations fail closed and retain a diagnostic through last_error().
     */
    [[nodiscard]] virtual bool apply_display_mode(WindowExtent extent, bool fullscreen,
                                                  FullscreenKind kind) = 0;

    /** Retail two-value form: fullscreen means exclusive. */
    [[nodiscard]] bool apply_display_mode(WindowExtent extent, bool fullscreen) {
        return apply_display_mode(extent, fullscreen, FullscreenKind::exclusive);
    }

    /**
     * Replaces the desktop cursor with an authored image. Hotspot coordinates
     * use the conventional top-left image origin. The platform owns the
     * resulting cursor until it is replaced or the window stops.
     */
    [[nodiscard]] virtual bool set_image_cursor(const std::filesystem::path& image_path,
                                                std::uint32_t hot_x,
                                                std::uint32_t hot_y) {
        static_cast<void>(image_path);
        static_cast<void>(hot_x);
        static_cast<void>(hot_y);
        return false;
    }

    /**
     * Opens an explicitly user-activated HTTP(S) destination in the desktop
     * handler. Backends that cannot provide this operation fail closed.
     */
    [[nodiscard]] virtual bool open_external_url(std::string_view url) {
        static_cast<void>(url);
        return false;
    }

    /**
     * Captures the mouse for first-person look: the cursor hides, pointer
     * motion arrives as `mouse_delta_x/y` and the absolute position freezes.
     * Gameplay sessions own enabling this; menus require it released.
     * Backends without capture support fail closed.
     */
    [[nodiscard]] virtual bool set_relative_mouse_mode(bool enabled) {
        static_cast<void>(enabled);
        return false;
    }

    /** True while relative mouse capture is active. */
    [[nodiscard]] virtual bool relative_mouse_mode() const noexcept {
        return false;
    }

    /**
     * Enables committed UTF-8 text events for chat/edit fields.
     *
     * This is deliberately separate from key presses: keyboard layouts, dead
     * keys and IMEs cannot be reconstructed correctly from a keycode.
     */
    [[nodiscard]] virtual bool set_text_input_enabled(bool enabled) {
        static_cast<void>(enabled);
        return false;
    }

    /** Reads UTF-8 text only in response to an explicit paste command. */
    [[nodiscard]] virtual std::string clipboard_text() { return {}; }
};

} // namespace battlespades::platform
