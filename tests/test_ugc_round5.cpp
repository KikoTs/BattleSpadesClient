// Round 5 Map Creator helpers: marker edit modes, VXL capacity counters,
// the ScreenshotHud crop/scale + PNG encode, and the Status Tab/HUD panels.
#include "battlespades/frontend/ugc_preview_writer.hpp"
#include "battlespades/frontend/ugc_status_presentation.hpp"
#include "battlespades/world/entity_catalog.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

using namespace battlespades;

void marker_modes_follow_get_ugc_mode() {
    // Crate drop points are MODE_NORMAL, the bomb MODE_OCCUPATION, zones keep
    // the mode they were placed in (tdm = 6, ctf = 8).
    for (std::uint8_t item = 0U; item <= 2U; ++item) {
        expect(world::ugc_item_edit_mode(item, 8U) == world::ugc_mode_normal,
               "crate drop points are shared MODE_NORMAL markers");
    }
    expect(world::ugc_item_edit_mode(3U, 6U) == world::ugc_mode_occupation,
           "the OCC bomb always belongs to Occupation");
    expect(world::ugc_item_edit_mode(9U, 8U) == 8U, "zones keep their placement mode");
    expect(world::ugc_item_visible_in_mode(0U, 6U), "MODE_NORMAL is drawn in every mode");
    expect(world::ugc_item_visible_in_mode(6U, 6U), "the edited mode is drawn");
    expect(!world::ugc_item_visible_in_mode(8U, 6U), "another mode's zones are hidden");
}

[[nodiscard]] std::vector<std::byte> single_floor_vxl() {
    // One solid colored voxel at z = 239 in every column (N=0, S=E=239).
    std::vector<std::byte> bytes;
    bytes.reserve(512U * 512U * 8U);
    for (std::size_t column = 0U; column < 512U * 512U; ++column) {
        constexpr std::array<std::uint8_t, 8U> column_bytes{0U, 239U, 239U, 0U, 40U, 60U, 80U, 255U};
        for (const std::uint8_t value : column_bytes) {
            bytes.push_back(static_cast<std::byte>(value));
        }
    }
    return bytes;
}

void capacity_counters_track_solids_and_chunks() {
    const auto bytes = single_floor_vxl();
    auto loaded = world::VxlMap::load(bytes);
    expect(static_cast<bool>(loaded), "the synthetic floor map loads");
    auto& map = *loaded.map;
    expect(map.solid_voxels() == 512U * 512U, "one solid per column");
    expect(map.non_empty_chunks() == 32U * 32U, "the floor touches one chunk layer");
    expect(map.is_space_to_add_blocks(), "a flat floor is far below capacity");
    expect(map.set_voxel(5U, 5U, 100U, world::VxlColor{1U, 2U, 3U, 255U}),
           "a floating voxel can be added");
    expect(map.non_empty_chunks() == 32U * 32U + 1U, "a new chunk becomes non-empty");
    expect(map.set_voxel(6U, 5U, 100U, world::VxlColor{1U, 2U, 3U, 255U}),
           "a neighbour can be added");
    expect(map.non_empty_chunks() == 32U * 32U + 1U, "the same chunk is counted once");
    expect(map.clear_voxel(5U, 5U, 100U) && map.clear_voxel(6U, 5U, 100U),
           "the voxels can be removed again");
    expect(map.non_empty_chunks() == 32U * 32U, "an emptied chunk stops counting");
    expect(map.solid_voxels() == 512U * 512U, "solids return to the floor count");
}

void screenshot_preview_crops_the_centre_square() {
    // 8x4 frame: left half red, right half blue; the centre 4x4 square spans
    // columns 2..5, so the 4x4 result is half red, half blue.
    std::vector<std::uint8_t> rgba(8U * 4U * 4U, 0U);
    for (std::uint32_t y = 0U; y < 4U; ++y) {
        for (std::uint32_t x = 0U; x < 8U; ++x) {
            const auto index = static_cast<std::size_t>((y * 8U + x) * 4U);
            rgba[index] = x < 4U ? 255U : 0U;
            rgba[index + 2U] = x < 4U ? 0U : 255U;
            rgba[index + 3U] = 255U;
        }
    }
    std::uint32_t extent{};
    const auto square = frontend::ugc_screenshot_preview_rgba(rgba, 8U, 4U, extent);
    expect(extent == 4U && square.size() == 4U * 4U * 4U, "a small frame keeps its size");
    expect(square[0U] == 255U && square[2U] == 0U, "the crop starts in the red half");
    expect(square[(3U * 4U) + 0U] == 0U && square[(3U * 4U) + 2U] == 255U,
           "the crop ends in the blue half");

    std::vector<std::uint8_t> large(1024U * 768U * 4U, 128U);
    const auto scaled = frontend::ugc_screenshot_preview_rgba(large, 1024U, 768U, extent);
    expect(extent == 512U && scaled.size() == 512U * 512U * 4U,
           "larger captures are scaled to the retail 512 square");
    expect(scaled[3U] == 255U, "the preview is opaque");

    std::vector<std::byte> png;
    std::string error;
    expect(frontend::encode_png_rgba8(square, 4U, 4U, png, error), "the preview encodes");
    expect(png.size() > 8U && png[1U] == std::byte{'P'} && png[2U] == std::byte{'N'},
           "the encoded preview is a PNG");
}

[[nodiscard]] std::size_t count_text(const ui::DrawList& list, std::string_view key) {
    std::size_t count{};
    for (const auto& command : list.commands()) {
        if (const auto* text = std::get_if<ui::TextDrawCommand>(&command);
            text != nullptr && text->localization_key == key) {
            ++count;
        }
    }
    return count;
}

void status_tab_and_incomplete_panel() {
    frontend::UgcStatusTabState state;
    state.player_names = {"Builder", "Guest"};
    state.mode_title = "Team Deathmatch";
    state.map_title = "Desert-1";
    const auto spawns = frontend::ugc_loadout_objective("UGC_OBJECTIVE_TEAM1_SPAWN_POINTS", 0);
    const auto zone = frontend::ugc_loadout_objective("UGC_OBJECTIVE_TEAM1_ZONE", 1);
    expect(spawns.has_value() && zone.has_value(), "the objective catalog resolves");
    state.objectives = {*zone, *spawns};
    const auto tab = frontend::build_ugc_status_tab(state);
    expect(count_text(tab, "UGC_TAB_TITLE") == 1U, "the Status headline is drawn");
    expect(count_text(tab, "UGC_TAB_PLAYERLIST_TITLE") == 1U, "the Players panel is drawn");
    expect(count_text(tab, "UGC_TAB_MODEOPTIONS_TITLE") == 1U, "the Map Config panel is drawn");
    expect(count_text(tab, "UGC_TAB_OBJECTIVES_TITLE") == 1U, "the objectives panel is drawn");
    expect(count_text(tab, "LITERAL|Builder") == 1U && count_text(tab, "LITERAL|Guest") == 1U,
           "every editor is listed");
    expect(count_text(tab, "MODE") == 1U && count_text(tab, "LITERAL|Team Deathmatch") == 1U,
           "the edited mode row is shown");
    expect(count_text(tab, "UGC_MAP_NAME") == 1U && count_text(tab, "LITERAL|Desert-1") == 1U,
           "the project title row is shown");

    ui::DrawList hud;
    const std::vector<frontend::UgcLoadoutObjective> objectives{*zone, *spawns};
    const auto height = frontend::append_ugc_incomplete_objectives(hud, objectives);
    expect(height > 0.0, "an incomplete objective shows the HUD panel");
    expect(count_text(hud, "UGC_INCOMPLETE_OBJECTIVES_TITLE") == 1U,
           "the HUD panel lists only incomplete objectives");
    expect(count_text(hud, "UGC_OBJECTIVE_ROW|UGC_OBJECTIVE_TEAM1_SPAWN_POINTS|10") == 1U &&
               count_text(hud, "UGC_OBJECTIVE_ROW|UGC_OBJECTIVE_TEAM1_ZONE|1") == 0U,
           "complete objectives are omitted from the HUD panel");
    ui::DrawList empty;
    const std::vector<frontend::UgcLoadoutObjective> complete{*zone};
    expect(frontend::append_ugc_incomplete_objectives(empty, complete) == 0.0 &&
               empty.commands().empty(),
           "nothing is drawn once every objective is complete");
}

} // namespace

int main() {
    try {
        marker_modes_follow_get_ugc_mode();
        capacity_counters_track_solids_and_chunks();
        screenshot_preview_crops_the_centre_square();
        status_tab_and_incomplete_panel();
    } catch (const std::exception& error) {
        std::cerr << "ugc round5 test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "ugc round5 tests passed\n";
    return 0;
}
