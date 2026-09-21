#pragma once

#include "battlespades/frontend/class_selection_menu.hpp"
#include "battlespades/ui/draw_list.hpp"

#include <cstdint>
#include <string>
#include <map>

namespace battlespades::frontend {

/** Resolve one retail loadout/equipment id to its SelectClass icon. */
[[nodiscard]] std::string
class_selection_item_icon(std::uint16_t raw_item_id);

struct ClassSelectionAppearance {
    // Centre of the 256-square generated image, matching the 111x160 portrait slot.
    static constexpr ui::DrawRect portrait_source{39.2,0.0,177.6,256.0};
    std::map<std::uint8_t,std::string> class_icons, class_portraits;
    std::map<std::uint16_t,std::string> weapon_icons;
};
/** Asset presentation for the multiplayer SelectClass gate. */
class ClassSelectionPresentation final {
public:
    [[nodiscard]] ui::DrawList build(const ClassSelectionMenuModel& menu,
                                     ui::PixelExtent window,
                                     bool in_game = false,
                                     const ClassSelectionAppearance& appearance = {}) const;
};

} // namespace battlespades::frontend
