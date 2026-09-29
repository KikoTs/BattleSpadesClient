#pragma once

#include "battlespades/settings/client_settings.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace battlespades::settings {

/**
 * The one retail key-name source shared by in-game hints and Settings.
 *
 * Retail `gui.translate_key` (aoslib/gui.py:250-268) takes the pyglet symbol
 * name, drops a leading `_` (digits) or `NUM_` (keypad), applies
 * KEY_TRANSLATIONS (LCTRL_REAL->CTRL, LSHIFT->SHIFT, PAGEUP->PAGE_UP,
 * PAGEDOWN->PAGE_DOWN, NUMLOCK->NUM_LOCK, CAPSLOCK->CAPS_LOCK) and then
 * looks the result up with `strings.get_by_id`. An id with no string falls
 * back to the id itself. So `,` renders "COMMA", Left Ctrl "CTRL", the left
 * arrow the localized LEFT string ("Left" in English) and Escape "ESCAPE"
 * (english.py has ESC, never ESCAPE).
 *
 * `retail_key_string_id` returns that translated id for an SDL scancode, or
 * an empty view for a key retail had no symbol for. It is built on
 * client_settings' retail_key_name_id table (the Settings value text) plus
 * retail's RCTRL/RSHIFT aliasing and the QUOTELEFT grave id.
 */
[[nodiscard]] std::string_view retail_key_string_id(std::uint32_t scancode) noexcept;

/** Localisation lookup: the translated text for a string id, if any. */
using RetailStringLookup = std::function<std::optional<std::string>(std::string_view)>;

/**
 * translate_key for one scancode: the localized string for its id when the
 * catalogue has one, else the id itself. A key with no retail symbol renders
 * its decimal code (pyglet symbol_string's str(symbol) fallback).
 */
[[nodiscard]] std::string retail_key_name(std::uint32_t scancode,
                                          const RetailStringLookup& lookup);

/**
 * translate_key for a stored binding. Keyboard bindings use retail_key_name;
 * mouse buttons 1 and 3 use the retail LMB/RMB strings. An unbound action
 * (retail `None`, e.g. toggle_hud) has no key name and returns "".
 */
[[nodiscard]] std::string retail_binding_name(InputBinding binding,
                                              const RetailStringLookup& lookup);

/**
 * Retail's patched pyglet aliases RCTRL to LCTRL_REAL and RSHIFT to
 * LSHIFT_REAL (aoslib/aosKeys.py), so the right-hand modifier IS the left
 * one for every binding. Other scancodes are returned unchanged.
 */
[[nodiscard]] constexpr std::uint32_t retail_canonical_scancode(std::uint32_t scancode) noexcept {
    constexpr std::uint32_t left_control{224U};
    constexpr std::uint32_t left_shift{225U};
    constexpr std::uint32_t right_control{228U};
    constexpr std::uint32_t right_shift{229U};
    if (scancode == right_control) return left_control;
    if (scancode == right_shift) return left_shift;
    return scancode;
}

/**
 * Retail `event_key == config.<action>`: true when `scancode` is the key
 * bound to `action`. Mouse and unbound bindings never match a key press.
 */
[[nodiscard]] bool retail_key_matches(const ControlsSettings& controls, ControlAction action,
                                      std::uint32_t scancode) noexcept;

} // namespace battlespades::settings
