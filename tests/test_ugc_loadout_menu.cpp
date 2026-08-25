#include "battlespades/frontend/ugc_loadout_menu.hpp"
#include "battlespades/frontend/ugc_loadout_presentation.hpp"
#include "battlespades/network/protocol168_weapons.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

[[nodiscard]] bool has_sprite(const battlespades::ui::DrawList& draw,
                              std::string_view asset) {
    return std::ranges::any_of(draw.commands(), [asset](const auto& command) {
        const auto* sprite =
            std::get_if<battlespades::ui::SpriteDrawCommand>(&command);
        return sprite != nullptr && sprite->asset_id == asset;
    });
}

[[nodiscard]] const battlespades::ui::TextDrawCommand*
find_text(const battlespades::ui::DrawList& draw, std::string_view key) {
    for (const auto& command : draw.commands()) {
        const auto* text =
            std::get_if<battlespades::ui::TextDrawCommand>(&command);
        if (text != nullptr && text->localization_key == key) return text;
    }
    return nullptr;
}

} // namespace

int main() {
    using namespace battlespades::frontend;
    try {
        expect(ugc_prefab_category("UGC_Prefab_Desert_landscape_1") ==
                   UgcPrefabCategory::landscape,
               "retail landscape prefab must retain its source category");
        expect(ugc_prefab_category("ugc_prefab_spookychurch") ==
                   UgcPrefabCategory::buildings_and_walls,
               "retail building prefab must retain its source category");
        expect(ugc_prefab_category("ugc_prefab_tree_bamboolarge") ==
                   UgcPrefabCategory::nature &&
                   ugc_prefab_category("ugc_prefab_rocket") ==
                       UgcPrefabCategory::props &&
                   ugc_prefab_category("ugc_prefab_roadstraight") ==
                       UgcPrefabCategory::road_rail_and_bridges &&
                   ugc_prefab_category("UGC_prefab_prop3dbillboard") ==
                       UgcPrefabCategory::signs_and_banners,
               "all six source Construct Library categories must resolve");
        expect(!ugc_prefab_category("invented_prefab").has_value(),
               "unknown server strings must not be fabricated into retail tabs");
        expect(ugc_prefab_display_name("UGC_Prefab_Desert_landscape_1") ==
                   std::optional<std::string_view>{"Desert Terrain 1"} &&
                   ugc_prefab_size_localization_key(
                       "UGC_Prefab_Desert_landscape_1") ==
                       std::optional<std::string_view>{"UGC_PREFAB_SIZE_LARGE"},
               "prefab cards must retain the exact retail name and KV6-derived size");
        expect(!ugc_prefab_display_name("invented_prefab").has_value() &&
                   !ugc_prefab_size_localization_key("invented_prefab").has_value(),
               "unknown prefabs must not gain fabricated presentation metadata");

        constexpr std::array<std::uint8_t, 3U> blue_spawns{{7U, 8U, 9U}};
        constexpr std::array<std::uint8_t, 3U> green_bases{{10U, 11U, 12U}};
        constexpr std::array<std::uint8_t, 3U> neutral_bases{{16U, 17U, 18U}};
        constexpr std::array<std::uint8_t, 1U> bomb_spawn{{3U}};
        expect(std::ranges::equal(
                   ugc_loadout_entity_ids_for_objective(
                       "UGC_OBJECTIVE_TEAM1_SPAWN_POINTS"),
                   blue_spawns) &&
                   std::ranges::equal(
                       ugc_loadout_entity_ids_for_objective(
                           "UGC_OBJECTIVE_TEAM2_ZONE"),
                       green_bases) &&
                   std::ranges::equal(
                       ugc_loadout_entity_ids_for_objective(
                           "UGC_OBJECTIVE_DIA_NEUTRAL_ZONES"),
                       neutral_bases) &&
                   std::ranges::equal(
                       ugc_loadout_entity_ids_for_objective(
                           "UGC_OBJECTIVE_BOMB_SPAWNS"),
                       bomb_spawn),
               "packet-68 objective ids must resolve to exact retail entity ids");
        expect(ugc_loadout_entity_ids_for_objective(
                   "UGC_OBJECTIVE_BLOCKCOUNT").empty() &&
                   ugc_loadout_entity_ids_for_objective("INVENTED_OBJECTIVE").empty(),
               "objectives without source entity ids must fail closed");

        std::vector<UgcLoadoutObjective> objective_rows;
        for (const auto& [id, value] :
             std::array<std::pair<std::string_view, std::int32_t>, 3U>{{
                 {"UGC_OBJECTIVE_BLOCKCOUNT", 1'234},
                 {"UGC_OBJECTIVE_TEAM1_ZONE", 0},
                 {"UGC_OBJECTIVE_TEAM1_SPAWN_POINTS", 2},
             }}) {
            const auto row = ugc_loadout_objective(id, value);
            expect(row.has_value(),
                   "every source-authored objective id must resolve its validation rule");
            objective_rows.push_back(*row);
        }
        expect(!ugc_loadout_objective("INVENTED_OBJECTIVE", 9).has_value(),
               "unknown packet-68 objective ids must fail closed");

        std::vector<std::string> prefabs{
            "UGC_Prefab_Desert_landscape_10",
            "UGC_Prefab_Desert_landscape_2",
            "UGC_Prefab_Desert_landscape_1",
            "ugc_prefab_spookychurch",
            "ugc_prefab_tree_bamboolarge",
            "ugc_prefab_rocket",
            "ugc_prefab_roadstraight",
            "UGC_prefab_prop3dbillboard",
            "invented_prefab",
        };
        constexpr std::array<std::uint8_t, 5U> tools{{0U, 3U, 4U, 10U, 18U}};
        const std::array<std::string, 1U> selected_prefabs{{
            "UGC_Prefab_Desert_landscape_1"}};
        constexpr std::array<std::uint8_t, 1U> selected_tools{{0U}};

        UgcLoadoutMenuModel menu;
        menu.configure(UgcLoadoutLibrary::constructs, prefabs, tools,
                       selected_prefabs, selected_tools, 3U, true,
                       objective_rows);
        expect(menu.tabs().size() == 6U && menu.team() == 3U && menu.in_game(),
               "Construct Library must expose only source-backed non-empty tabs");
        expect(menu.current_tab()->items.size() == 3U &&
                   menu.current_tab()->items[0U].choice.prefab_name.ends_with("_1") &&
                   menu.current_tab()->items[1U].choice.prefab_name.ends_with("_2") &&
                   menu.current_tab()->items[2U].choice.prefab_name.ends_with("_10"),
               "prefab cards must use SelectUGC natural numeric ordering");
        expect(menu.inventory().size() == 2U &&
                   menu.inventory()[0U].kind == UgcLoadoutItemKind::prefab &&
                   menu.inventory()[1U].kind == UgcLoadoutItemKind::game_data,
               "existing prefabs then UGC tools must seed one shared backpack");
        expect(menu.objectives().size() == 3U &&
                   menu.objectives()[0U].id ==
                       "UGC_OBJECTIVE_TEAM1_SPAWN_POINTS" &&
                   menu.objectives()[0U].complete() &&
                   menu.objectives()[1U].id == "UGC_OBJECTIVE_TEAM1_ZONE" &&
                   !menu.objectives()[1U].complete() &&
                   menu.objectives()[2U].id == "UGC_OBJECTIVE_BLOCKCOUNT",
               "packet-68 rows must retain retail stable priority ordering and completion");

        const auto first_item = menu.current_tab()->items[1U].choice;
        static_cast<void>(menu.click({70, 150}));
        expect(menu.inventory().size() == 1U &&
                   menu.take_audio_cue() == UgcLoadoutAudioCue::scroll,
               "clicking a selected grid item must remove it and emit menu_scrollA");
        menu.select_item(first_item);
        menu.select_tab(1U);
        menu.select_item(menu.current_tab()->items.front().choice);
        menu.select_tab(2U);
        menu.select_item(menu.current_tab()->items.front().choice);
        menu.select_tab(3U);
        menu.select_item(menu.current_tab()->items.front().choice);
        menu.select_tab(4U);
        menu.select_item(menu.current_tab()->items.front().choice);
        menu.select_tab(5U);
        menu.select_item(menu.current_tab()->items.front().choice);
        expect(menu.inventory().size() == UgcLoadoutMenuModel::maximum_selection &&
                   menu.inventory().front().kind == UgcLoadoutItemKind::prefab &&
                   menu.inventory().front().prefab_name ==
                       "ugc_prefab_spookychurch",
               "sixth selection must evict the oldest shared backpack item");

        menu.open_library(UgcLoadoutLibrary::game_data);
        expect(menu.tabs().size() == 3U &&
                   menu.tabs()[0U].label_key == "UGC_TOOL_CAT_SPAWNS" &&
                   menu.tabs()[1U].label_key == "UGC_TOOL_CAT_BASES" &&
                   menu.tabs()[2U].label_key == "UGC_TOOL_CAT_CRATE_DROPS",
               "Game Data tabs must retain source category order");
        expect(menu.tabs()[0U].items.size() == 1U &&
                   menu.tabs()[0U].items.front().choice.ugc_tool == 4U &&
                   menu.tabs()[1U].items.size() == 3U &&
                   menu.tabs()[2U].items.size() == 1U,
               "objective metadata must strictly gate marker ids");
        expect(menu.inventory().size() == 5U,
               "switching libraries must retain the shared backpack");

        menu.select_item(menu.tabs()[0U].items.front().choice);
        const auto selection = menu.selection();
        constexpr std::array<std::uint8_t, 12U> exact_loadout{{
            5U, 45U, 47U, 48U, 69U, 43U,
            30U, 42U, 41U, 25U, 26U, 30U,
        }};
        expect(selection.class_id == 13U &&
                   std::ranges::equal(selection.loadout, exact_loadout),
               "SelectUGC submit must preserve the exact UGC Builder loadout bytes");
        expect(std::ranges::count(selection.loadout, 30U) == 2,
               "the source-authored duplicate INTEL_TOOL byte must not be normalized away");
        battlespades::network::SetClassLoadoutPacket packet;
        packet.player_id = 12U;
        packet.class_id = selection.class_id;
        packet.instant = true;
        packet.loadout = selection.loadout;
        packet.prefabs = selection.prefabs;
        packet.ugc_tools = selection.ugc_tools;
        const auto decoded = battlespades::network::decode_weapon_packet(
            battlespades::network::encode_packet(packet));
        const auto* round_trip =
            decoded.packet.has_value()
                ? std::get_if<battlespades::network::SetClassLoadoutPacket>(
                      &*decoded.packet)
                : nullptr;
        expect(round_trip != nullptr && round_trip->player_id == 12U &&
                   round_trip->class_id == 13U && round_trip->instant &&
                   round_trip->loadout == selection.loadout &&
                   round_trip->prefabs == selection.prefabs &&
                   round_trip->ugc_tools == selection.ugc_tools,
               "SelectUGC must serialize as the exact complete Protocol 168 packet-13 transaction");

        expect(menu.click({430, 445}) == UgcLoadoutAction::submit &&
                   menu.take_audio_cue() == UgcLoadoutAudioCue::confirm,
               "SELECT must emit menu_confirmA and publish the loadout transaction");
        expect(menu.click({345, 530}) == UgcLoadoutAction::back &&
                   menu.take_audio_cue() == UgcLoadoutAudioCue::back,
               "in-game Back must emit menu_backA");

        std::vector<std::string> scrolling_prefabs;
        for (unsigned index = 1U; index <= 15U; ++index) {
            scrolling_prefabs.push_back(
                "UGC_Prefab_Desert_landscape_" + std::to_string(index));
        }
        menu.configure(UgcLoadoutLibrary::constructs, scrolling_prefabs, {},
                       {}, {}, 2U, false);
        expect(menu.visible_items().size() == 14U &&
                   menu.current_tab()->first_visible_item == 0U,
               "Construct grid must expose fourteen cards per page");
        menu.scroll_rows(1);
        expect(menu.current_tab()->first_visible_item == 7U &&
                   menu.visible_items().size() == 8U,
               "vertical scrollbar must advance one seven-card row");

        menu.configure(UgcLoadoutLibrary::constructs, prefabs, tools,
                       selected_prefabs, selected_tools, 2U, true,
                       objective_rows);
        UgcLoadoutPresentation presentation;
        const auto draw = presentation.build(menu, {800, 600});
        expect(find_text(draw, "PREFABS_MENU") != nullptr &&
                   has_sprite(draw, "png/ui/ugc_tools/pf_template_bg.png") &&
                   has_sprite(draw,
                              "ugc/prefabs/ugc_prefab_desert_landscape_1.png") &&
                   has_sprite(draw, "png/ui/ugc_tools/pf_select_marker.png") &&
                   has_sprite(draw,
                              "png/ui/ugc_tools/prefab_selection_blueprint.png") &&
                   find_text(draw, "UGC_PREFAB_SIZE") != nullptr &&
                   find_text(draw, "UGC_PREFAB_SIZE_LARGE") != nullptr,
               "Construct Library draw list must use retail art, name and size fields");
        menu.open_library(UgcLoadoutLibrary::game_data);
        const auto data_draw = presentation.build(menu, {800, 600});
        expect(find_text(data_draw, "UGC_GAME_DATA") != nullptr &&
                   has_sprite(data_draw,
                              "png/ui/ugc_tools/gdata_template_bg.png") &&
                   has_sprite(data_draw,
                              "png/ui/ugc_tools/ugc_spawngreen_small.png") &&
                   find_text(data_draw, "UGC_TAB_OBJECTIVES_TITLE") != nullptr &&
                   find_text(data_draw,
                             "UGC_OBJECTIVE_ROW|UGC_OBJECTIVE_TEAM1_ZONE|1") !=
                       nullptr &&
                   find_text(data_draw, "UGC_OBJECTIVE_VALUE|0|1") != nullptr,
               "Game Data Library must render source marker art and packet-68 objectives");

        std::cout << "UGC loadout menu: retail libraries, inventory and transaction passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "UGC loadout menu failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
