#pragma once

#include "battlespades/frontend/ugc_loadout_menu.hpp"
#include "battlespades/ui/draw_list.hpp"

#include <span>
#include <string>
#include <vector>

namespace battlespades::frontend {

/**
 * Data behind hud.pyd's Map Creator Tab screen (ObjectivesPlayersList).
 *
 * `mode_title` is the localised MODE_MAP_TITLES[manager.ugc_mode]; `map_title` is the
 * lobby's MAP_ROTATION_NEW_TITLE (the project title). Objectives arrive in
 * packet-68 order and are drawn sorted by UGC_OBJECTIVES_TYPES priority.
 */
struct UgcStatusTabState final {
    std::vector<std::string> player_names;
    std::string mode_title;
    std::string map_title;
    std::vector<UgcLoadoutObjective> objectives;
};

/**
 * ObjectivesPlayersList.draw (hud.pyd 0x1007b6e0) + initialize (0x10079c00),
 * in the retail 800x600 design space: ugc_tab_frame at 0.8 alpha, the
 * UGC_TAB_TITLE "Status" headline, and three ListPanelBase panels converted
 * from Pyglet's bottom-left origin -- Players (404, 451, 327, 341), Map Config
 * (68, 451, 327, 120) and UGCObjectivesListPanel (68, 352, 327, 242).
 */
[[nodiscard]] ui::DrawList build_ugc_status_tab(const UgcStatusTabState& state);

/**
 * The HUD's incomplete-objectives list (hud.pyd HUD.__init__ 0x1007d940):
 * UGCObjectivesListPanel(10, height - 10, 320, 300, row_height=30,
 * has_header, enable_background_resizing, frame_padding_height=0,
 * transparent_items, show_game_settings_text, only_show_incomplete) in window
 * pixels. Nothing is drawn when every objective is complete. Returns the
 * panel's drawn height (0 when hidden) so the host's settings hint can sit
 * 25 px below it exactly like UGCObjectivesListPanel.draw.
 */
double append_ugc_incomplete_objectives(ui::DrawList& list,
                                        std::span<const UgcLoadoutObjective> objectives);

} // namespace battlespades::frontend
