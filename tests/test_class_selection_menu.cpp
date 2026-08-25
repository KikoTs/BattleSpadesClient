#include "battlespades/frontend/class_selection_menu.hpp"
#include "battlespades/frontend/class_selection_presentation.hpp"

#include <algorithm>
#include <array>
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
        expect(find_text(draw, "Commando") != nullptr,
               "class byte 0 must retain the retail Commando UI name");
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
                sprite->destination == battlespades::ui::DrawRect{67.0, 119.0, 137.0, 128.0}) {
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

        constexpr std::array<std::uint8_t, 7U> full_roster{0U, 1U, 2U, 3U, 12U, 16U, 17U};
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

        std::cout << "class selection menu: roster, loadout and prefab ids passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "class selection menu failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
