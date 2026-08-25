#pragma once

#include "battlespades/frontend/class_selection_menu.hpp"
#include "battlespades/ui/draw_list.hpp"

#include <cstdint>
#include <string>

namespace battlespades::frontend {

/** Resolve one retail loadout/equipment id to its SelectClass icon. */
[[nodiscard]] std::string
class_selection_item_icon(std::uint16_t raw_item_id);

/** Stock-asset presentation for the multiplayer SelectClass gate. */
class ClassSelectionPresentation final {
public:
    [[nodiscard]] ui::DrawList build(const ClassSelectionMenuModel& menu,
                                     ui::PixelExtent window,
                                     bool in_game = false) const;
};

} // namespace battlespades::frontend
