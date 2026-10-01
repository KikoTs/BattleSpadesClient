#include "battlespades/frontend/class_selection_menu.hpp"
#include "battlespades/frontend/class_selection_presentation.hpp"
#include "battlespades/world/class_catalog.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <variant>

namespace {

void expect(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error{message};
}

[[nodiscard]] const battlespades::ui::TextDrawCommand*
find_text(const battlespades::ui::DrawList& draw, std::string_view value) {
    for (const auto& command : draw.commands()) {
        const auto* text = std::get_if<battlespades::ui::TextDrawCommand>(&command);
        if (text != nullptr && text->localization_key == value)
            return text;
    }
    return nullptr;
}

[[nodiscard]] bool has_sprite(const battlespades::ui::DrawList& draw, std::string_view asset) {
    return std::ranges::any_of(draw.commands(), [asset](const auto& command) {
        const auto* sprite = std::get_if<battlespades::ui::SpriteDrawCommand>(&command);
        return sprite != nullptr && sprite->asset_id == asset;
    });
}

} // namespace

int main() {
    using battlespades::frontend::ClassSelectionAction;
    using battlespades::frontend::ClassSelectionMenuModel;
    try {
        {
            // Zombie mode: retail GameClass.build_class_loadout appends the
            // Flare Block to every non-mafia class, the zombie included, but
            // the server commits the infected kit (hands 24, zombie prefab
            // 28, prefab 23). The first live life must come from that
            // CreatePlayer: the zombie has no FPS arms to hold a block, and
            // the server refuses tool 22 and PlaceFlareBlock from it.
            namespace world = battlespades::world;
            const auto requested = world::automatic_class_selection(4U, {});
            expect(std::ranges::find(requested.loadout, world::flare_block_tool) !=
                       requested.loadout.end(),
                   "retail build_class_loadout gives the zombie request a Flare Block");
            const world::ClassSelection committed{4U, {24U, 28U, 23U}, {}, {}};
            const auto spawn = world::live_spawn_selection(committed, &requested);
            expect(spawn.class_id == 4U && spawn.loadout == committed.loadout,
                   "the first live life uses the server-committed zombie loadout");
            expect(std::ranges::find(spawn.loadout, world::flare_block_tool) ==
                       spawn.loadout.end(),
                   "a zombie never spawns holding the Flare Block");
            const auto* zombie = world::find_class_definition(4U);
            expect(zombie != nullptr && zombie->first_person_arm_assets[0U].empty() &&
                       zombie->first_person_arm_assets[1U].empty(),
                   "CLASS_FPS_ARMS[CLASS_ZOMBIE] is empty: no class hands exist");
            const world::ClassSelection empty{};
            expect(world::live_spawn_selection(empty, &requested).loadout == requested.loadout,
                   "without a committed loadout the request is the fallback");
        }
        ClassSelectionMenuModel menu;
        constexpr std::array<std::uint8_t, 3U> advertised{0U, 12U, 17U};
        menu.configure(advertised, 1U, 0U);
        expect(menu.classes().size() == advertised.size() && menu.team() == 1U,
               "class menu must retain the server-advertised roster and team");

        auto selection = menu.selection();
        expect(selection.class_id == 0U && selection.prefabs.size() == 3U,
               "the initial soldier selection must be complete");
        expect(std::ranges::find(selection.loadout, 23U) != selection.loadout.end() &&
                   std::ranges::find(selection.loadout, 22U) == selection.loadout.end(),
               "class submit must resolve constructs to tool 23, not flares");

        menu.cycle_class(1);
        expect(menu.take_audio_cue() == battlespades::frontend::ClassSelectionAudioCue::scroll,
               "class changes must emit retail menu_scrollA");
        selection = menu.selection();
        expect(std::ranges::find(selection.loadout, 68U) != selection.loadout.end(),
               "Engineer jetpack equipment id must be preserved in packet 13");
        menu.cycle_group(3U, 1);
        selection = menu.selection();
        expect(selection.class_id == 12U &&
                   std::ranges::find(selection.loadout, 64U) != selection.loadout.end() &&
                   std::ranges::find(selection.loadout, 68U) == selection.loadout.end(),
               "Engineer equipment choice must survive the atomic selection");
        const auto submit = menu.click({610, 480});
        expect(submit == ClassSelectionAction::submit, "SELECT must publish the class transaction");
        expect(menu.take_audio_cue() == battlespades::frontend::ClassSelectionAudioCue::confirm,
               "SELECT must emit retail menu_confirmA");

        menu.configure(advertised, 1U, 0U);
        expect(menu.click({60, 550}) == ClassSelectionAction::back &&
                   menu.take_audio_cue() == battlespades::frontend::ClassSelectionAudioCue::back,
               "class Back must emit retail menu_backA");

        menu.configure(advertised, 1U, 0U);
        battlespades::frontend::ClassSelectionPresentation presentation;
        const auto draw = presentation.build(menu, battlespades::ui::PixelExtent{800, 600});
        expect(find_text(draw, "SOLDIER") != nullptr,
               "class byte 0 must use CLASS_NAMES' SOLDIER id (\"Commando\")");
        expect(battlespades::frontend::class_selection_item_icon(72U) ==
                   "png/ui/weapons/parachute.png",
               "Commando parachute must not reuse the generic jetpack icon");
        expect(battlespades::frontend::class_selection_item_icon(67U) ==
                       "png/ui/weapons/jetpack2.png" &&
                   battlespades::frontend::class_selection_item_icon(68U) ==
                       "png/ui/weapons/jetpack_engineer.png",
               "the two recovered jetpacks must keep their distinct art");
        std::size_t empty_cells{};
        bool recovered_selected_frame{};
        for (const auto& command : draw.commands()) {
            const auto* sprite = std::get_if<battlespades::ui::SpriteDrawCommand>(&command);
            if (sprite == nullptr)
                continue;
            if (sprite->asset_id == "png/high/white.png" &&
                sprite->modulation.color == battlespades::ui::ColorRgba8{46U, 44U, 35U, 255U}) {
                ++empty_cells;
            }
            if (sprite->asset_id == "png/ui/in_game_menus/select_class/class_selected_frame.png" &&
                std::abs(sprite->destination.x - 61.0) < 0.001 &&
                std::abs(sprite->destination.y - 120.8) < 0.001 &&
                std::abs(sprite->destination.width - 176.0) < 0.001 &&
                std::abs(sprite->destination.height - 158.4) < 0.001) {
                recovered_selected_frame = true;
            }
        }
        expect(empty_cells == 36U, "SelectClass must retain all 24 loadout and 12 prefab cells");
        expect(recovered_selected_frame,
               "selected class border must retain its measured retail bounds");

        menu.cycle_class(1);
        const auto engineer_draw =
            presentation.build(menu, battlespades::ui::PixelExtent{800, 600});
        expect(has_sprite(engineer_draw, "prefabs/prefab_platform.png"),
               "Engineer must expose its recovered prefab catalog");
        menu.cycle_class(1);
        const auto medic_draw = presentation.build(menu, battlespades::ui::PixelExtent{800, 600});
        expect(!has_sprite(medic_draw, "prefabs/prefab_platform.png") &&
                   has_sprite(medic_draw, "prefabs/prefab_supersmallwall.png"),
               "prefab cells must rebuild from the selected class, not stay static");

        constexpr std::array<std::uint8_t, 4U> vip_classes{6U, 7U, 8U, 9U};
        menu.configure(vip_classes, 2U, 6U);
        const auto vip_draw = presentation.build(menu, battlespades::ui::PixelExtent{800, 600});
        for (const auto& command : vip_draw.commands()) {
            const auto* sprite = std::get_if<battlespades::ui::SpriteDrawCommand>(&command);
            expect(sprite == nullptr || !sprite->asset_id.empty(),
                   "VIP SelectClass must never submit an empty texture path");
        }

        {
            // BattleSpades TDM / Zombie survivor roster (BS server/class_data.py
            // BATTLESPADES_TEAM_CLASSES): the stock six plus the Rocketeer,
            // restored 2026-10-01. Its equipment row is Glide (67) first, then
            // the Jump pack (66), and the packet keeps the chosen pack id.
            constexpr std::array<std::uint8_t, 7U> battlespades_roster{
                0U, 1U, 12U, 3U, 2U, 16U, 17U};
            menu.configure(battlespades_roster, 2U, 2U);
            expect(menu.classes().size() == 7U && menu.selected_class() == 2U,
                   "the Rocketeer must be pickable from the advertised roster");
            auto rocketeer = menu.selection();
            expect(rocketeer.class_id == 2U &&
                       std::ranges::find(rocketeer.loadout, 67U) != rocketeer.loadout.end() &&
                       std::ranges::find(rocketeer.loadout, 66U) == rocketeer.loadout.end(),
                   "the Rocketeer's default equipment is the Glider pack");
            menu.cycle_group(3U, 1);
            rocketeer = menu.selection();
            expect(rocketeer.class_id == 2U &&
                       std::ranges::find(rocketeer.loadout, 66U) != rocketeer.loadout.end() &&
                       std::ranges::find(rocketeer.loadout, 67U) == rocketeer.loadout.end(),
                   "the Jump pack must be the Rocketeer's alternative equipment");
            const auto rocketeer_draw = presentation.build(menu, {800, 600});
            // CLASS_NAMES[ROCKETEER] is the string id ENGINEER (english.py:
            // ENGINEER = u'Rocketeer'); the Engineer itself uses ENGINEER2.
            expect(find_text(rocketeer_draw, "ENGINEER") != nullptr &&
                       has_sprite(rocketeer_draw, "png/ui/weapons/jetpack.png") &&
                       has_sprite(rocketeer_draw, "png/ui/weapons/jetpack2.png"),
                   "the Rocketeer card must show its name and both pack icons");
        }

        constexpr std::array<std::uint8_t, 7U> full_roster{0U, 1U, 2U, 3U, 12U, 16U, 17U};
        // Exercise both retail card layouts and smaller server rosters. The
        // former bug drew a four-card background with five-card hit targets.
        for (std::size_t count = 1U; count <= full_roster.size(); ++count) {
            menu.configure(std::span{full_roster}.first(count), 2U, 255U);
            expect(menu.classes().size() == count,
                   "an unavailable current class must never be appended to the server roster");
            const bool four = count <= 4U;
            const auto layout_draw = presentation.build(menu, {800, 600});
            expect(has_sprite(layout_draw, four
                       ? "png/ui/in_game_menus/select_class/class_background_frame.png"
                       : "png/ui/in_game_menus/select_class/class_background_frame_5.png"),
                   "class background must agree with the server roster size");
            for (std::size_t index{}; index < std::min(count, menu.classes_per_page()); ++index) {
                const int x = (four ? 81 : 85) + static_cast<int>(index) * (four ? 167 : 134);
                const int y = four ? 132 : 131;
                const int size = four ? 136 : 107;
                expect(menu.class_card_bounds(index) == battlespades::ui::Rect{x, y, size, size},
                       "render/input geometry must retain the original four/five-card constants");
                static_cast<void>(menu.click({x + size / 2, y + size / 2}));
                expect(menu.selected_class() == full_roster[index],
                       "clicking a displayed class must select its own server class id");
            }
        }

        constexpr std::array<std::uint8_t, 4U> restricted{12U, 12U, 255U, 0U};
        menu.configure(restricted, 2U, 17U);
        expect(menu.classes().size() == 2U && menu.selected_class() == 12U,
               "invalid and duplicate entries must not invent unavailable classes");
        menu.configure({}, 2U, 255U);
        expect(menu.classes().size() == 1U && menu.selected_class() == 0U,
               "an empty malformed roster must retain a usable fallback");

        // The server's default rules disable the flare block (tool 22), so
        // the constructs table starts with the class prefabs.
        battlespades::world::ClassSelectionRules no_flare;
        no_flare.disabled_tools = {22U};
        menu.configure(advertised, 2U, 12U, no_flare);
        const auto options = battlespades::world::class_prefab_options(12U);
        static_cast<void>(menu.click({480, 365})); // Fourth construct, platform.
        expect(menu.selected_prefabs().size() == 3U &&
                   menu.selected_prefabs()[0] == options[1] &&
                   menu.selected_prefabs()[1] == options[2] &&
                   menu.selected_prefabs()[2] == options[3],
               "a fourth construct click must replace the oldest selected construct");
        static_cast<void>(menu.click({515, 325})); // Remove tower.
        static_cast<void>(menu.click({560, 325})); // Remove barrier.
        expect(menu.selected_prefabs().size() == 1U &&
                   menu.selection().prefabs == std::vector<std::string>{std::string{options[3]}},
               "the submitted loadout must preserve an intentional single construct");
        static_cast<void>(menu.click({480, 365}));
        expect(menu.selected_prefabs().size() == 1U,
               "retail construct selection must keep its final selected item");
        menu.cycle_group(3U, 1);
        const auto engineer_selection = menu.selection();
        menu.select_visible_class(1U); // Re-select Engineer.
        expect(menu.selection().prefabs == engineer_selection.prefabs &&
                   menu.selection().loadout == engineer_selection.loadout,
               "clicking the current class must not reset its loadout");
        menu.cycle_class(1);
        menu.cycle_class(-1);
        expect(menu.selection().prefabs == engineer_selection.prefabs &&
                   menu.selection().loadout == engineer_selection.loadout,
               "browsing another class must preserve the previous class choices");
        menu.configure(advertised, 2U, 12U, no_flare);
        menu.restore_playing_loadout(2U, 12U, engineer_selection.loadout, engineer_selection.prefabs);
        expect(menu.selection().prefabs == engineer_selection.prefabs &&
                   menu.selection().loadout == engineer_selection.loadout,
               "reopening SelectClass must restore the authoritative loadout and constructs");
        expect(battlespades::world::default_class_selection(12U).prefabs.size() == 3U,
               "fresh default loadouts must continue to supply three constructs");

        menu.configure(full_roster, 2U, 0U);
        expect(menu.visible_class_offset() == 0U,
               "seven-class strip must begin at its first retail viewport");
        menu.scroll_classes(1);
        expect(menu.visible_class_offset() == 1U && menu.selected_class_index() == 0U,
               "class scrollbar must move one slot without changing selection");
        menu.scroll_classes(1);
        menu.scroll_classes(1);
        expect(menu.visible_class_offset() == 2U,
               "class scrollbar must clamp at the two hidden classes");
        menu.select_visible_class(4U);
        expect(menu.selected_class_index() == 6U && menu.visible_class_offset() == 2U,
               "last visible class must remain selectable after scrolling");
        menu.cycle_class(-1);
        expect(menu.selected_class_index() == 5U && menu.visible_class_offset() == 2U,
               "keyboard class changes must not jerk an already visible viewport");
        for (int step{}; step < 5; ++step)
            menu.cycle_class(-1);
        expect(menu.selected_class_index() == 0U && menu.visible_class_offset() == 0U,
               "cycling back to the first class must reveal it one slot at a time");

        battlespades::frontend::ClassSelectionAppearance appearance;
        appearance.class_icons[0]="runtime/cosmetic/test-head";
        appearance.class_portraits[0]="runtime/cosmetic/test-body";
        appearance.weapon_icons[battlespades::world::find_class_definition(0)->item_groups[1].front()]="runtime/cosmetic/test-smg";
        const auto skinned=battlespades::frontend::ClassSelectionPresentation{}.build(menu,{800,600},true,appearance);
        for(const auto* expected:{"runtime/cosmetic/test-head","runtime/cosmetic/test-body","runtime/cosmetic/test-smg"}){
            bool found=false;for(const auto& command:skinned.commands())if(const auto* sprite=std::get_if<battlespades::ui::SpriteDrawCommand>(&command))found=found||sprite->asset_id==expected;
            expect(found,"Class selection must use the current cosmetic head, body and weapon icons");
        }
        for(const auto& command:skinned.commands())if(const auto* portrait=std::get_if<battlespades::ui::SpriteDrawCommand>(&command);portrait&&portrait->asset_id=="runtime/cosmetic/test-body"){
            expect(portrait->source_pixels.has_value(),"Generated class portraits need an aspect-correct source rectangle");
            expect(std::abs(portrait->source_pixels->width/portrait->source_pixels->height-portrait->destination.width/portrait->destination.height)<.00001,
                "Generated class portraits must not stretch the player model");
        }
        std::cout << "class selection menu: roster, loadout, cosmetic icons and prefab ids passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "class selection menu failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
