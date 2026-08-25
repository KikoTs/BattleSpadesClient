#pragma once

#include "battlespades/frontend/ugc_loadout_menu.hpp"
#include "battlespades/ui/draw_list.hpp"

namespace battlespades::frontend {

/** Stock-asset presentation for SelectPrefabs and SelectGameData. */
class UgcLoadoutPresentation final {
public:
    [[nodiscard]] ui::DrawList build(const UgcLoadoutMenuModel& menu,
                                     ui::PixelExtent window) const;
};

} // namespace battlespades::frontend
