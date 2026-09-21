#pragma once

#include <string_view>

namespace battlespades::network {
// Published crate manifests and existing equipment records are immutable.
// Correct presentation bindings independently, including already awarded Bren
// items and remote appearances from servers using the published v4 catalogue.
[[nodiscard]] inline std::string_view cosmetic_display_slot(std::string_view item,
                                                            std::string_view slot) noexcept {
    if (item == "community-bren-v2") {
        if (slot == "weapon:8:view") return "weapon:61:view";
        if (slot == "weapon:8:world") return "weapon:61:world";
    }
    return slot;
}
[[nodiscard]] inline std::string_view cosmetic_storage_slot(std::string_view item,
                                                            std::string_view slot) noexcept {
    if (item == "community-bren-v2") {
        if (slot == "weapon:61:view") return "weapon:8:view";
        if (slot == "weapon:61:world") return "weapon:8:world";
    }
    return slot;
}
} // namespace battlespades::network
