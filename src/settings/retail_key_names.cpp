#include "battlespades/settings/retail_key_names.hpp"

#include <utility>

namespace battlespades::settings {

std::string_view retail_key_string_id(std::uint32_t scancode) noexcept {
    // One scancode table for the whole client: client_settings.cpp's
    // retail_key_name_id (also used by the Settings -> Controls value text).
    // Two retail facts are applied on top of it here:
    //  * retail's patched pyglet aliases RCTRL/RSHIFT onto LCTRL_REAL/LSHIFT
    //    (aoslib/aosKeys.py), so the right-hand keys read CTRL/SHIFT;
    //  * the grave key's id is QUOTELEFT: english.py's key block defines
    //    QUOTELEFT = 'QUOTE LEFT', while GRAVE = 'Grave' is the tombstone
    //    entity name, not a key label.
    switch (retail_canonical_scancode(scancode)) {
    case 53U:
        return "QUOTELEFT";
    case 224U:
        return "CTRL";
    case 225U:
        return "SHIFT";
    default:
        return retail_key_name_id(scancode);
    }
}

std::string retail_key_name(std::uint32_t scancode, const RetailStringLookup& lookup) {
    const auto id = retail_key_string_id(scancode);
    if (id.empty()) {
        // pyglet.window.key.symbol_string falls back to str(symbol).
        return std::to_string(scancode);
    }
    if (lookup) {
        if (auto text = lookup(id); text.has_value() && !text->empty()) {
            return std::move(*text);
        }
    }
    return std::string{id};
}

std::string retail_binding_name(InputBinding binding, const RetailStringLookup& lookup) {
    switch (binding.kind) {
    case BindingKind::keyboard_scancode:
        return retail_key_name(binding.code, lookup);
    case BindingKind::mouse_button: {
        const std::string_view id = binding.code == 1U   ? "LMB"
                                    : binding.code == 3U ? "RMB"
                                                         : std::string_view{};
        if (id.empty()) {
            return "MOUSE " + std::to_string(binding.code);
        }
        if (lookup) {
            if (auto text = lookup(id); text.has_value() && !text->empty()) {
                return std::move(*text);
            }
        }
        return std::string{id};
    }
    case BindingKind::unbound:
        break;
    }
    return {};
}

bool retail_key_matches(const ControlsSettings& controls, ControlAction action,
                        std::uint32_t scancode) noexcept {
    const auto binding = controls.binding(action);
    return binding.kind == BindingKind::keyboard_scancode &&
           retail_canonical_scancode(binding.code) == retail_canonical_scancode(scancode);
}

} // namespace battlespades::settings
