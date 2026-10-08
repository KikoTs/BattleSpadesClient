#include "battlespades/frontend/create_match_menu.hpp"
#include "battlespades/frontend/create_match_presentation.hpp"
#include "battlespades/frontend/menu_status.hpp"

#include <algorithm>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using namespace battlespades::frontend;
using battlespades::ui::InputAction;
using battlespades::ui::InputEvent;
using battlespades::ui::InputPhase;

void expect(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error{std::string{message}};
}

template <typename Effect>
const Effect* find_effect(const std::vector<CreateMatchEffect>& effects) {
    for (const auto& effect : effects) {
        if (const auto* result = std::get_if<Effect>(&effect)) return result;
    }
    return nullptr;
}

const CreateMatchRowPresentation& row(const CreateMatchMenuPresentation& frame,
                                      std::string_view key) {
    const auto found = std::find_if(frame.rows.begin(), frame.rows.end(), [&](const auto& item) {
        return item.stable_key == key;
    });
    if (found == frame.rows.end()) throw std::runtime_error{"row was not present"};
    return *found;
}

const battlespades::ui::TextDrawCommand* text_command(
    const battlespades::ui::DrawList& list,
    std::string_view key) {
    for (const auto& command : list.commands()) {
        const auto* text = std::get_if<battlespades::ui::TextDrawCommand>(&command);
        if (text != nullptr && text->localization_key == key) return text;
    }
    return nullptr;
}

bool has_sprite(const battlespades::ui::DrawList& list, std::string_view asset) {
    return std::ranges::any_of(list.commands(), [&](const auto& command) {
        const auto* sprite = std::get_if<battlespades::ui::SpriteDrawCommand>(&command);
        return sprite != nullptr && sprite->asset_id == asset;
    });
}

void catalogs_recover_every_retail_playlist_and_rule() {
    const auto modes = retail_create_match_modes();
    expect(modes.size() == 10U,
           "Create Match must expose the ten non-random, non-tutorial, non-UGC playlists");
    expect(modes.front().mode_key == "ctf" && modes[1].mode_key == "cctf" &&
               modes.back().mode_key == "zom",
           "English mode rows must retain the retail localized sort order");
    const auto tdm = std::find_if(modes.begin(), modes.end(), [](const auto& mode) {
        return mode.mode_key == "tdm";
    });
    expect(tdm != modes.end() && tdm->retail_playlist_id == 9U &&
               tdm->maps.size() == 16U && tdm->default_match_minutes == 15U,
           "TDM playlist id, map set, and duration must match playlists/tdm.txt");
    const auto classic = std::find_if(modes.begin(), modes.end(), [](const auto& mode) {
        return mode.mode_key == "cctf";
    });
    expect(classic != modes.end() && classic->family == CreateMatchModeFamily::classic &&
               classic->maps.size() == 7U && classic->default_match_minutes == 90U,
           "Classic CTF must retain its classic family, seven maps, and 90-minute duration");

    const auto rules = retail_create_match_rules();
    expect(rules.size() == 98U,
           "the catalog must include every rule reachable through GAME_RULES_NAMES");
    const auto disguise = std::find_if(rules.begin(), rules.end(), [](const auto& rule) {
        return rule.rule_key == "RULE_ENABLE_EQUIPMENT_DISGUISE";
    });
    expect(disguise != rules.end() && disguise->category_key == "EQUIPMENT" &&
               disguise->default_value == "ON",
           "the Engineer disguise equipment rule must not be dropped");
}

void default_match_settings_geometry_and_values_match_retail() {
    CreateMatchMenuModel menu;
    const auto frame = menu.presentation();
    expect(frame.page == CreateMatchPage::match_settings &&
               frame.panel_title_key == "MATCH_SETTINGS",
           "Create Match must open on the Match Settings lobby panel");
    expect(frame.outer_frame == battlespades::ui::Rect{25, 5, 750, 589} &&
               frame.title_bounds == battlespades::ui::Rect{120, 25, 560, 50} &&
               frame.player_panel == battlespades::ui::Rect{56, 95, 340, 270} &&
               frame.content_panel == battlespades::ui::Rect{400, 95, 340, 355},
           "large frame, title, player list, and settings panel must use recovered geometry");
    expect(frame.lobby_name_field == battlespades::ui::Rect{76, 111, 210, 30} &&
               frame.player_count_bar == battlespades::ui::Rect{56, 367, 340, 20} &&
               frame.chat_panel == battlespades::ui::Rect{56, 390, 340, 116} &&
               frame.action_panel == battlespades::ui::Rect{401, 452, 340, 54} &&
               frame.navigation_bar == battlespades::ui::Rect{54, 541, 695, 32},
           "lobby title edit, count strip, chat, action strip, and navbar must not be approximated");
    expect(frame.rows.size() == 6U,
           "normal Match Settings must contain privacy, mode, players, length, map, and rules");
    expect(row(frame, "PRIVACY").value_text == "OPEN" &&
               row(frame, "PLAYLIST").value_text == "TDM_TITLE" &&
               row(frame, "MAX_PLAYERS").value_text == "12" &&
               row(frame, "MATCH_LENGTH").value_text == "15" &&
               row(frame, "MAP_ROTATION_FILENAME").value_text == "AncientEgypt" &&
               row(frame, "GAME_RULES").value_text == "DEFAULT",
           "default lobby metadata must match the retail lobby-creation callback");
    const auto button = [&](std::string_view key) {
        return std::find_if(frame.buttons.begin(), frame.buttons.end(), [&](const auto& item) {
            return item.stable_key == key;
        });
    };
    const auto defaults = button("DEFAULTS");
    const auto start = button("START_GAME");
    const auto invite = button("INVITE");
    const auto back = button("BACK");
    const auto local = button("START_LOCAL");
    expect(frame.buttons.size() == 5U && defaults != frame.buttons.end() &&
               defaults->bounds == battlespades::ui::Rect{410, 415, 80, 30} &&
               start != frame.buttons.end() &&
               start->bounds == battlespades::ui::Rect{575, 456, 162, 50} &&
               local != frame.buttons.end() &&
               local->bounds == battlespades::ui::Rect{405, 456, 162, 50} &&
               invite != frame.buttons.end() &&
               invite->bounds == battlespades::ui::Rect{300, 109, 80, 30} &&
               back != frame.buttons.end() && back->label_key == "LEAVE_LOBBY",
           "Local and online starts must share the existing action panel without overlap");
    expect(frame.show_defaults_help && !frame.show_content_frame &&
               frame.defaults_help == battlespades::ui::Rect{492, 415, 238, 28},
           "Match Settings uses row frames without a fake enclosing panel and shows reset help");
}

void nested_panels_emit_forward_and_backward_navigation() {
    CreateMatchMenuModel menu;
    expect(menu.activate_row("PLAYLIST"), "Mode menu-link must open Choose Game Mode");
    auto effects = menu.take_effects();
    const auto* forward = find_effect<CreateMatchPageChangedEffect>(effects);
    expect(forward != nullptr && forward->from == CreateMatchPage::match_settings &&
               forward->to == CreateMatchPage::choose_game_mode &&
               forward->direction == CreateMatchNavigationDirection::forward,
           "opening a nested picker must request an in-lobby right-panel replacement");
    const auto picker = menu.presentation();
    expect(picker.rows.size() == 10U && !picker.show_scrollbar &&
               picker.rows.back().bounds.y + picker.rows.back().bounds.height <=
                   picker.action_panel.y,
           "all ten retail modes must fit the 285 px list without a fake scrollbar or overlap");

    expect(menu.activate_row("cctf"), "Classic CTF must be selectable");
    expect(menu.configuration().retail_playlist_id == 2U &&
               menu.configuration().map_name == "Classic" &&
               menu.configuration().match_minutes == 90U,
           "mode selection must atomically repair incompatible map and default duration");
    expect(menu.activate_done(), "Confirm must return from the nested mode panel");
    effects = menu.take_effects();
    const auto* backward = find_effect<CreateMatchPageChangedEffect>(effects);
    expect(menu.page() == CreateMatchPage::match_settings && backward != nullptr &&
               backward->direction == CreateMatchNavigationDirection::backward,
           "nested Confirm must restore the Match Settings right panel");

    expect(menu.activate_done(), "Start Game must emit a lobby route command");
    effects = menu.take_effects();
    const auto* start = find_effect<CreateMatchRouteEffect>(effects);
    expect(start != nullptr && start->action == CreateMatchRouteAction::start_game &&
               start->configuration.retail_playlist_id == 2U,
           "Start Game must carry the complete current lobby configuration");
}

void local_host_options_are_explicit_and_travel_with_start() {
    CreateMatchMenuModel menu;
    expect(menu.set_scroll(3U), "extended host settings must be reachable by scrolling");
    auto frame = menu.presentation();
    expect(row(frame, "BOTS").value_text == "0" &&
               row(frame, "BOT_DIFFICULTY").value_text == "mixed" &&
               row(frame, "SERVER_PORT").value_text == "27015",
           "Create Match must expose bot population, difficulty, and preferred UDP port");
    expect(menu.adjust_row("BOTS", 1) &&
               menu.adjust_row("BOT_DIFFICULTY", -1) &&
               menu.adjust_row("SERVER_PORT", 1),
           "all client-owned host controls must be adjustable");
    expect(menu.configuration().bot_count == 2U &&
               menu.configuration().bot_difficulty == "hard" &&
               menu.configuration().server_port == 32887U,
           "host controls must update the authoritative lobby snapshot");
    expect(menu.activate_done(), "Start Game must remain available after host options change");
    const auto effects = menu.take_effects();
    const auto* start = find_effect<CreateMatchRouteEffect>(effects);
    expect(start != nullptr && start->configuration.bot_count == 2U &&
               start->configuration.bot_difficulty == "hard" &&
               start->configuration.server_port == 32887U,
           "the hidden server launcher must receive every selected host option");
}

void map_picker_categories_selection_and_scrolling_are_stable() {
    CreateMatchMenuModel menu;
    expect(menu.open_page(CreateMatchPage::choose_map), "Map picker must open from settings");
    auto frame = menu.presentation();
    expect(frame.panel_title_key == "MAPS" && frame.show_scrollbar &&
               frame.rows.front().stable_key == "STANDARD" &&
               frame.rows.front().kind == CreateMatchRowKind::category,
           "standard TDM maps must live below the expandable Standard pack");
    expect(menu.mouse_wheel(-3), "long map lists must support wheel scrolling");
    expect(menu.presentation().first_visible_row == 3U,
           "map scroll position must use deterministic row indices");
    expect(menu.activate_row("TokyoNeon"), "any map in the selected playlist must be selectable");
    expect(menu.configuration().map_name == "TokyoNeon",
           "map selection must update authoritative lobby metadata");
    expect(menu.set_category_expanded("STANDARD", false), "map pack must collapse");
    frame = menu.presentation();
    expect(frame.rows.size() == 1U && frame.rows.front().stable_key == "STANDARD" &&
               !frame.rows.front().expanded && frame.first_visible_row == 0U,
           "collapsing a pack must remove children and repair the scroll position");
}

void saved_and_subscribed_maps_are_filtered_by_mode_and_selectable() {
    CreateMatchMenuModel menu;
    menu.set_custom_maps({
        CreateMatchCustomMap{"Custommap_1", "Desert", "Kiril", {"TDM", "ctf"}, false},
        CreateMatchCustomMap{"Custommap_2", "Zombie Fort", "", {"zom"}, false},
        CreateMatchCustomMap{"Subscribed_7", "Workshop Arena", "Someone", {"tdm"}, true},
        CreateMatchCustomMap{"../escape", "Bad", "", {"tdm"}, false},
    });
    expect(menu.custom_maps().size() == 3U, "path-like custom stems must be dropped");
    expect(menu.open_page(CreateMatchPage::choose_map), "map picker must open");
    auto frame = menu.presentation();
    expect(frame.rows.size() > 5U && frame.rows[0].stable_key == "SAVED_MAPS" &&
               frame.rows[0].kind == CreateMatchRowKind::category &&
               frame.rows[1].stable_key == "SAVED_MAPS/Custommap_1" &&
               frame.rows[1].label_key == "Desert" && frame.rows[1].value_text == "Kiril" &&
               frame.rows[2].stable_key == "SUBSCRIBED_MAPS" &&
               frame.rows[3].stable_key == "SUBSCRIBED_MAPS/Subscribed_7" &&
               frame.rows[4].stable_key == "STANDARD",
           "retail packs order: SAVED_MAPS, SUBSCRIBED_MAPS, then the stock pack, TDM-only");
    static_cast<void>(menu.take_effects());
    expect(menu.activate_row("SAVED_MAPS/Custommap_1"), "a listed saved map must be selectable");
    const auto& chosen = menu.configuration();
    expect(chosen.custom_map && !chosen.subscribed_map && chosen.map_name == "Custommap_1" &&
               chosen.map_title == "Desert" && chosen.map_author == "Kiril",
           "Custom_UGC_Map, its author and the file stem travel with the snapshot");
    expect(find_effect<CreateMatchConfigurationChangedEffect>(menu.take_effects()) != nullptr,
           "choosing a custom map must publish the configuration");
    expect(!menu.activate_row("SAVED_MAPS/Custommap_2"),
           "a map whose tags lack the mode must not be selectable");

    // Switching to CTF keeps the map (its tags include ctf); Zombie drops it.
    expect(menu.open_page(CreateMatchPage::choose_game_mode) && menu.activate_row("ctf"),
           "CTF must be selectable");
    expect(menu.configuration().custom_map && menu.configuration().map_name == "Custommap_1",
           "a custom map valid for the new mode stays selected");
    expect(menu.activate_row("zom"), "Zombie must be selectable");
    expect(!menu.configuration().custom_map && menu.configuration().map_name != "Custommap_1",
           "a custom map without the new mode's tag falls back to a stock map");
}

void lobby_refresh_preserves_manual_scroll_and_focus() {
    for(const auto page:{CreateMatchPage::match_settings,CreateMatchPage::choose_map,
                         CreateMatchPage::game_rules}) {
        CreateMatchMenuModel menu;
        if(page!=CreateMatchPage::match_settings)expect(menu.open_page(page),"scroll refresh page must open");
        const auto before_refresh = menu.presentation();
        const auto& focus = before_refresh.focused_key;
        expect(menu.mouse_wheel(-2),"wheel must move down before lobby refresh");
        const auto scrolled=menu.presentation().first_visible_row;
        const auto snapshot=menu.configuration();
        static_cast<void>(menu.take_effects());
        for(int tick=0;tick<60;++tick) {
            menu.apply_authoritative_configuration(snapshot);
            expect(menu.presentation().first_visible_row==scrolled,
                   "unchanged lobby snapshots must not pull the list back to keyboard focus");
        }
        auto changed=snapshot;changed.match_minutes=30U;
        menu.apply_authoritative_configuration(changed);
        expect(menu.configuration().match_minutes==30U,"changed server values must still arrive");
        expect(menu.presentation().first_visible_row==scrolled&&menu.presentation().focused_key==focus,
               "background settings updates must preserve manual scrolling and valid keyboard focus");
        expect(menu.take_effects().empty(),"background updates must not echo edits or UI sounds");
        expect(menu.mouse_wheel(1)&&menu.presentation().first_visible_row==scrolled-1U,
               "wheel up must still work after synchronization");
    }
}

void lobby_refresh_preserves_scrollbar_drag_and_repairs_removed_rows() {
    CreateMatchMenuModel menu;
    expect(menu.open_page(CreateMatchPage::game_rules),"rules page must open");
    menu.pointer_press(battlespades::ui::Point{719,300});
    const auto dragged=menu.presentation().first_visible_row;
    expect(dragged>0U,"scrollbar drag must move into the list");
    menu.apply_authoritative_configuration(menu.configuration());
    expect(menu.presentation().first_visible_row==dragged,"refresh must not undo a scrollbar drag");
    menu.pointer_move(battlespades::ui::Point{719,350});
    expect(menu.presentation().first_visible_row>dragged,"drag must continue across a refresh");
    menu.pointer_release(battlespades::ui::Point{719,350});

    expect(menu.open_page(CreateMatchPage::choose_map),"map page must open");
    expect(menu.set_focus("TokyoNeon"),"focus a map unavailable in the Mafia playlist");
    auto changed=menu.configuration();changed.retail_playlist_id=8U;
    menu.apply_authoritative_configuration(changed);
    const auto frame=menu.presentation();
    expect(frame.first_visible_row<=frame.maximum_scroll&&frame.first_visible_row==0U,
           "a shorter playlist must clamp the old scroll position");
    expect(frame.focused_key&&std::ranges::any_of(frame.rows,[&](const auto& row){return row.stable_key==*frame.focused_key;}),
           "removed map focus must move to an available visible row");

    expect(menu.set_focus("BACK"),"footer keyboard focus precondition");
    changed=menu.configuration();changed.match_minutes=30U;
    menu.apply_authoritative_configuration(changed);
    expect(menu.presentation().focused_key=="BACK","server changes must not steal footer keyboard focus");
}

void game_rules_filter_by_mode_family_and_enabled_classes() {
    CreateMatchMenuModel menu;
    expect(menu.open_page(CreateMatchPage::game_rules), "rules panel must open");
    auto frame = menu.presentation();
    expect(frame.rows.front().stable_key == "tdm" &&
               frame.rows.front().kind == CreateMatchRowKind::category &&
               row(frame, "RULE_TDM_SCORE_TARGET").value_text == "200",
           "the active mode category must sort before General, Classes, Weapons, Equipment");
    expect(frame.content_panel == battlespades::ui::Rect{400, 95, 340, 325} &&
               frame.scrollbar_bounds == battlespades::ui::Rect{708, 155, 22, 255},
           "Rules must retain BaseSquadLobbyMenu's deliberately shortened panel and scrollbar");
    expect(menu.mouse_wheel(-3) && menu.presentation().first_visible_row == 3U,
           "Rules must consume wheel input and advance the visible window");
    menu.pointer_press(battlespades::ui::Point{719, 350});
    expect(menu.presentation().first_visible_row > 3U,
           "Rules scrollbar track must support direct pointer scrolling");
    menu.pointer_release(battlespades::ui::Point{719, 350});
    expect(menu.set_rule_value("RULE_ENABLE_CLASS_COMMANDO", "OFF"),
           "standard class rules must be adjustable");
    expect(!menu.set_rule_value("RULE_ENABLE_WEAPON_MINIGUN", "OFF"),
           "equipment for a disabled sole owning class must become disabled");
    expect(menu.set_rule_value("RULE_ENABLE_CLASS_MARKSMAN", "OFF"),
           "Marksman must be disableable");
    expect(!menu.set_rule_value("RULE_ENABLE_WEAPON_KNIFE", "OFF"),
           "Knife must disable only after both Soldier and Marksman are disabled");
    menu.activate_defaults();
    expect(menu.configuration().rule_overrides.empty(),
           "Game Rules Defaults must clear every explicit lobby override");

    CreateMatchMenuModel classic;
    expect(classic.open_page(CreateMatchPage::choose_game_mode), "mode picker precondition");
    expect(classic.activate_row("cctf"), "classic mode selection precondition");
    expect(classic.activate_done(), "return from mode picker");
    expect(classic.open_page(CreateMatchPage::game_rules), "classic rules must open");
    frame = classic.presentation();
    expect(frame.rows.front().stable_key == "cctf" &&
               row(frame, "RULE_CTF_ENABLE_SHOOT_WITH_INTEL").value_text == "ON" &&
               std::none_of(frame.rows.begin(), frame.rows.end(), [](const auto& item) {
                   return item.stable_key == "CLASSES";
               }),
           "Classic CTF must apply playlist overrides and hide unavailable standard classes");
    expect(classic.set_rule_value("RULE_CTF_ENABLE_SHOOT_WITH_INTEL", "OFF"),
           "Classic CTF aliases must still mutate the unique CTF rule id");
}

void semantic_input_reveals_focus_and_routes_cancel() {
    CreateMatchMenuModel menu;
    expect(menu.open_page(CreateMatchPage::game_rules), "rules panel precondition");
    static_cast<void>(menu.take_effects());
    for (int index{}; index < 15; ++index) {
        expect(menu.handle(InputEvent{InputAction::navigate_down, InputPhase::pressed}),
               "down navigation must advance through expanded rule rows");
    }
    expect(menu.presentation().first_visible_row > 0U,
           "keyboard focus must remain visible by scrolling the long rules list");
    expect(menu.handle(InputEvent{InputAction::cancel, InputPhase::pressed}),
           "Cancel must return from a nested panel");
    expect(menu.page() == CreateMatchPage::match_settings,
           "nested Cancel must restore Match Settings");
    static_cast<void>(menu.take_effects());
    expect(menu.handle(InputEvent{InputAction::cancel, InputPhase::pressed}),
           "Cancel at lobby root must request leaving");
    const auto effects = menu.take_effects();
    const auto* leave = find_effect<CreateMatchRouteEffect>(effects);
    expect(leave != nullptr && leave->action == CreateMatchRouteAction::leave_lobby,
           "root Cancel must emit Leave Lobby rather than silently discarding metadata");
}

void pointer_controls_match_retail_hit_targets_and_clamping() {
    CreateMatchMenuModel slider;
    auto frame = slider.presentation();
    const auto privacy = row(frame, "PRIVACY");
    slider.pointer_release({privacy.control_bounds.x + 12,
                            privacy.control_bounds.y + privacy.control_bounds.height / 2});
    expect(slider.configuration().privacy == CreateMatchPrivacy::friends_only,
           "only the left slider arrow should move Open back to Friends");
    frame = slider.presentation();
    const auto friends = row(frame, "PRIVACY");
    slider.pointer_release({friends.control_bounds.x + friends.control_bounds.width - 12,
                            friends.control_bounds.y + friends.control_bounds.height / 2});
    expect(slider.configuration().privacy == CreateMatchPrivacy::open,
           "the right slider arrow should move Friends to Open");
    static_cast<void>(slider.take_effects());
    frame = slider.presentation();
    const auto open = row(frame, "PRIVACY");
    slider.pointer_release({open.control_bounds.x + open.control_bounds.width - 12,
                            open.control_bounds.y + open.control_bounds.height / 2});
    expect(slider.configuration().privacy == CreateMatchPrivacy::open &&
               slider.take_effects().empty(),
           "retail SliderOption clamps at its last value instead of wrapping to Invite");

    CreateMatchMenuModel menu_link;
    frame = menu_link.presentation();
    const auto mode = row(frame, "PLAYLIST");
    menu_link.pointer_move(battlespades::ui::Point{
        mode.control_bounds.x + mode.control_bounds.width - 12,
        mode.control_bounds.y + mode.control_bounds.height / 2});
    expect(row(menu_link.presentation(), "PLAYLIST").visual_state ==
               CreateMatchVisualState::hovered,
           "the Mode edit control must visibly react before it is clicked");
    menu_link.pointer_release({mode.control_bounds.x + 20,
                               mode.control_bounds.y + mode.control_bounds.height / 2});
    expect(menu_link.page() == CreateMatchPage::match_settings,
           "clicking a MenuOptionControl value bar must not open its nested panel");
    menu_link.pointer_release({mode.control_bounds.x + mode.control_bounds.width - 12,
                               mode.control_bounds.y + mode.control_bounds.height / 2});
    expect(menu_link.page() == CreateMatchPage::choose_game_mode,
           "the retail edit square is the MenuOptionControl's actual hit target");

    CreateMatchMenuModel invite;
    frame = invite.presentation();
    const auto invite_button = std::find_if(
        frame.buttons.begin(), frame.buttons.end(),
        [](const auto& button) { return button.stable_key == "INVITE"; });
    expect(invite_button != frame.buttons.end() && invite_button->enabled,
           "Invite must be visible and enabled while the lobby has capacity");
    invite.pointer_release({invite_button->bounds.x + invite_button->bounds.width / 2,
                            invite_button->bounds.y + invite_button->bounds.height / 2});
    const auto invite_effects = invite.take_effects();
    const auto* platform = find_effect<CreateMatchPlatformActionEffect>(invite_effects);
    expect(platform != nullptr &&
               platform->action == CreateMatchPlatformAction::invite_friends,
           "Invite must cross a typed platform boundary rather than starting a match");
}

void lobby_name_players_and_draw_translation_are_renderer_neutral() {
    CreateMatchMenuModel menu;
    expect(!menu.set_lobby_name(""), "empty lobby names must fail closed");
    expect(!menu.set_lobby_name(std::string(20U, 'a')),
           "retail lobby names must retain their 19-code-point limit");
    expect(menu.set_lobby_name("Kiko's Match"), "valid lobby names must be accepted");
    menu.set_players({
        {1U, "Kiko", "TEAM1_COLOR", true, true, false},
        {2U, "Builder", "TEAM2_COLOR", false, false, false},
    });
    const auto frame = menu.presentation();
    expect(frame.lobby_name == "Kiko's Match" && frame.players.size() == 2U,
           "lobby presentation must own immutable name and player data");

    CreateMatchPresentation renderer;
    const auto draw_list = renderer.build(frame, CreateMatchPresentationContext{});
    expect(draw_list.size() > 35U,
           "Create Match translation must produce a complete ordered shared DrawList");
    const auto* title = text_command(draw_list, "MATCH_LOBBY");
    const auto* panel = text_command(draw_list, "MATCH_SETTINGS");
    const auto* mode = text_command(draw_list, "MODE");
    const auto* start = text_command(draw_list, "START_GAME");
    expect(title != nullptr &&
               title->preferred_font_asset == create_match_presentation_assets::row_font &&
               title->horizontal_alignment ==
                   battlespades::ui::HorizontalTextAlignment::center,
           "retail Match Lobby title must use the centred block face");
    expect(panel != nullptr && mode != nullptr &&
               panel->preferred_font_asset == create_match_presentation_assets::title_font &&
               mode->preferred_font_asset == create_match_presentation_assets::title_font,
           "right-panel and settings labels must use retail's handwritten face");
    expect(start != nullptr &&
               start->preferred_font_asset == create_match_presentation_assets::row_font,
           "Start Game must use retail's block button face");
    const auto* help = text_command(draw_list, "RESET_OPTIONS");
    expect(help != nullptr && help->requested_font_size_pixels == 12.0 &&
               help->preferred_font_asset == "fonts/A750-Sans-Medium.ttf" &&
               help->layout == battlespades::ui::TextLayout::bounded_wrapped_lines &&
               help->maximum_lines == 2U && help->fit == battlespades::ui::TextFit::none &&
               help->destination.height >= 32.0 &&
               help->destination.y + help->destination.height < frame.action_panel.y,
           "Defaults help must fit two readable lines above the action panel without shrinking");
    const auto has_dark_settings_canvas = std::ranges::any_of(
        draw_list.commands(), [&](const auto& command) {
            const auto* sprite = std::get_if<battlespades::ui::SpriteDrawCommand>(&command);
            return sprite != nullptr &&
                   sprite->asset_id == create_match_presentation_assets::white_pixel &&
                   sprite->destination == battlespades::ui::DrawRect{
                       static_cast<double>(frame.content_panel.x),
                       static_cast<double>(frame.content_panel.y),
                       static_cast<double>(frame.content_panel.width),
                       static_cast<double>(frame.content_panel.height)} &&
                   sprite->modulation.color ==
                       battlespades::ui::ColorRgba8{27U, 30U, 1U, 255U};
        });
    expect(has_dark_settings_canvas,
           "the right-side lobby panel must retain retail's dark-green background");

    const auto playlist = row(frame, "PLAYLIST");
    menu.pointer_move(battlespades::ui::Point{
        playlist.control_bounds.x + playlist.control_bounds.width - 12,
        playlist.control_bounds.y + playlist.control_bounds.height / 2});
    const auto hovered = renderer.build(menu.presentation(), CreateMatchPresentationContext{});
    expect(has_sprite(hovered, create_match_presentation_assets::square_button_hover),
           "hovering an edit affordance must use the visible retail hover sprite");
    bool rejected{};
    try {
        static_cast<void>(renderer.build(
            frame,
            CreateMatchPresentationContext{{0, 600}, 1'000U}));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected, "invalid render extents must fail closed before a partial frame is emitted");
}

void lobby_chat_is_editable_bounded_and_renderer_visible() {
    CreateMatchMenuModel menu;
    const auto initial = menu.presentation();
    menu.pointer_release({initial.chat_input.x + 10, initial.chat_input.y + 10});
    expect(menu.chat_focused(), "clicking the lobby chat input must start text entry");
    expect(menu.append_chat_text("Привет 日本"),
           "lobby chat must accept valid Cyrillic and Japanese UTF-8");
    expect(menu.erase_chat_code_point() && menu.append_chat_text("語"),
           "backspace must erase one complete UTF-8 code point");
    expect(menu.submit_chat(), "non-empty lobby chat must emit a send action");
    const auto effects = menu.take_effects();
    const auto* chat = find_effect<CreateMatchChatEffect>(effects);
    expect(chat != nullptr && chat->message == "Привет 日語",
           "the chat action must preserve the exact UTF-8 payload");

    std::vector<CreateMatchChatLine> lines;
    for (int index{}; index < 40; ++index) {
        lines.push_back({"Player", "line " + std::to_string(index)});
    }
    menu.set_chat_lines(std::move(lines));
    const auto frame = menu.presentation();
    expect(frame.chat_lines.size() == 32U && frame.chat_lines.front().message == "line 8",
           "the lobby chat history must remain bounded while retaining newest lines");
    CreateMatchPresentation renderer;
    const auto draw = renderer.build(frame, CreateMatchPresentationContext{});
    expect(text_command(draw, "Player: line 39") != nullptr,
           "the newest authoritative lobby message must be visible in the panel");
}

void host_member_actions_follow_authority_and_stable_roster_ids() {
    CreateMatchMenuModel menu;
    menu.set_players({{1U, "Host", "TEAM_NEUTRAL", true, true, false},
                      {2U, "Guest", "TEAM_NEUTRAL", false, false, false}});
    expect(!menu.request_member_action(2U, true), "Offline draft has no server member actions");
    menu.set_member_management_enabled(true);
    expect(!menu.request_member_action(1U, true), "Host cannot kick itself");
    expect(menu.request_member_action(2U, false), "Host can assign guest team");
    const auto events = menu.take_effects();
    const auto* assignment = std::get_if<CreateMatchMemberEffect>(&events.back());
    expect(assignment && assignment->account_id == 2U && assignment->team == 2U,
           "Assignment contains the stable account and retail team");
    expect(menu.presentation().players[1].team_key == "TEAM_NEUTRAL",
           "Roster waits for authoritative response");
    menu.set_host_authority(false);
    expect(!menu.request_member_action(2U, true), "Member cannot kick another member");
    menu.set_host_authority(true);
    menu.pointer_press(battlespades::ui::Point{245, 188});
    menu.set_players({{1U, "Host", "TEAM_NEUTRAL", true, true, false},
                      {3U, "Replacement", "TEAM_NEUTRAL", false, false, false}});
    menu.pointer_release({245, 188});
    expect(menu.take_effects().empty(), "Roster replacement cannot redirect a kick click");
    std::vector<CreateMatchPlayer> roster;
    for (std::uint64_t id = 1U; id <= 24U; ++id) {
        roster.push_back({id, std::to_string(id), "TEAM_NEUTRAL", id == 1U, id == 1U, false});
    }
    menu.set_players(std::move(roster));
    menu.pointer_release({260, 377});
    menu.pointer_release({260, 377});
    expect(menu.presentation().first_visible_player == 16U, "Every roster member is reachable");
    expect(std::ranges::any_of(menu.presentation().buttons, [](const auto& button) {
        return button.stable_key == "KICK:24";
    }), "Last member has an action");
    const auto frame = menu.presentation();
    for (std::size_t i{}; i < frame.buttons.size(); ++i) {
        for (std::size_t j = i + 1U; j < frame.buttons.size(); ++j) {
            const auto a = frame.buttons[i].bounds, b = frame.buttons[j].bounds;
            expect(a.x + a.width <= b.x || b.x + b.width <= a.x ||
                   a.y + a.height <= b.y || b.y + b.height <= a.y,
                   "Lobby controls must not overlap, including Invite and roster paging");
        }
    }
}

void local_start_is_explicit_and_never_splits_a_group() {
    CreateMatchMenuModel menu;
    expect(menu.adjust_row("BOTS", 1), "Solo host can select bots");
    static_cast<void>(menu.take_effects());
    expect(menu.set_focus("START_LOCAL") && menu.activate_focused(),
           "Local Match must work with keyboard activation");
    const auto effects = menu.take_effects();
    const auto* local = find_effect<CreateMatchRouteEffect>(effects);
    expect(local && local->action == CreateMatchRouteAction::start_local_game &&
               local->configuration.bot_count == 2U,
           "Local route must retain the configured match without requesting online hosting");
    // Match the solo-host debug fixture and the real window click that must
    // reach START_LOCAL after the design-canvas transform.
    menu.set_players({{0U, "Local host", "TEAM_NEUTRAL", true, true, false}});
    menu.pointer_move(battlespades::ui::Point{480, 480});
    menu.pointer_press(battlespades::ui::Point{480, 480});
    menu.pointer_release({480, 480});
    const auto pointer_effects = menu.take_effects();
    const auto* pointer_local = find_effect<CreateMatchRouteEffect>(pointer_effects);
    expect(pointer_local && pointer_local->action == CreateMatchRouteAction::start_local_game &&
               pointer_local->configuration.bot_count == 2U,
           "clicking the visible Local Match button must emit the same configured local route as keyboard activation");
    menu.set_players({{1U, "Host", "TEAM_NEUTRAL", true, true, false},
                      {2U, "Friend", "TEAM_NEUTRAL", false, false, false}});
    expect(!menu.activate_local_game(), "A group must not be silently split into a local match");
    expect(std::ranges::none_of(menu.presentation().buttons, [](const auto& button) {
        return button.stable_key == "START_LOCAL";
    }), "Group hosting must keep the online start control");
    menu.set_players({{2U, "Member", "TEAM_NEUTRAL", false, true, false}});
    menu.set_host_authority(false);
    expect(!menu.activate_local_game(), "A non-host must not start the lobby");
    const CreateMatchPresentation presentation;
    const auto draw = presentation.build(menu.presentation(),
        {{800, 600}, 1'000U, "The public relay is unavailable. Your settings are preserved."});
    expect(text_command(draw, "The public relay is unavailable. Your settings are preserved.") != nullptr,
           "Hosting failure details must be visible in the lobby");

    CreateMatchMenuModel layout;
    for (const auto page : {CreateMatchPage::match_settings, CreateMatchPage::choose_game_mode,
                            CreateMatchPage::choose_map, CreateMatchPage::game_rules}) {
        static_cast<void>(layout.open_page(page));
        const auto maximum = layout.presentation().maximum_scroll;
        for (std::size_t offset{}; offset <= maximum; ++offset) {
            static_cast<void>(layout.set_scroll(offset));
            const auto frame = layout.presentation();
            for (const auto& row : frame.rows) {
                for (const auto& button : frame.buttons) {
                    const auto a = row.bounds;
                    const auto b = button.bounds;
                    expect(a.x + a.width <= b.x || b.x + b.width <= a.x ||
                               a.y + a.height <= b.y || b.y + b.height <= a.y,
                           "visible lobby rows must not intercept buttons on any picker page or scroll position");
                }
            }
        }
    }
}

void published_match_joins_without_starting_another_host() {
    CreateMatchMenuModel menu;
    menu.set_players({{1U, "Host", "TEAM_NEUTRAL", true, false, true},
                      {2U, "Member", "TEAM_NEUTRAL", false, true, false}});
    menu.set_host_authority(false);
    expect(!menu.activate_done(), "a member without a published match cannot launch it");
    menu.set_match_join_available(true);
    auto frame = menu.presentation();
    const auto join = std::ranges::find(frame.buttons, std::string{"JOIN_GAME"},
                                        &CreateMatchButtonPresentation::stable_key);
    expect(join != frame.buttons.end() && join->enabled && join->label_key == "JOIN GAME",
           "a published lobby must expose an enabled Join Game action to members");
    expect(!row(frame, "PRIVACY").enabled && !menu.adjust_row("MAX_PLAYERS", 1),
           "rejoining must not grant ownership of lobby settings");
    expect(std::ranges::none_of(frame.buttons, [](const auto& button) {
        return button.stable_key == "START_LOCAL" || button.stable_key == "START_GAME";
    }) && !menu.activate_local_game(), "a published match cannot start another local host");

    menu.pointer_press(battlespades::ui::Point{650, 480});
    menu.pointer_release({650, 480});
    auto effects = menu.take_effects();
    auto* route = find_effect<CreateMatchRouteEffect>(effects);
    expect(route && route->action == CreateMatchRouteAction::join_game,
           "clicking Join Game must emit the existing-match route, never Start Game");
    expect(menu.set_focus("JOIN_GAME") && menu.activate_focused(),
           "the same rejoin action must be reachable by keyboard");
    effects = menu.take_effects();
    route = find_effect<CreateMatchRouteEffect>(effects);
    expect(route && route->action == CreateMatchRouteAction::join_game,
           "keyboard rejoin must not create a new host either");

    menu.set_match_join_available(true, true);
    frame = menu.presentation();
    const auto busy_join = std::ranges::find(frame.buttons, std::string{"JOIN_GAME"},
                                             &CreateMatchButtonPresentation::stable_key);
    expect(busy_join != frame.buttons.end() && !busy_join->enabled && busy_join->label_key == "JOIN GAME" &&
               !menu.activate_done() && !menu.activate_focused(),
           "resolving a published match must retain Join Game while blocking duplicate activation");
    menu.pointer_press(battlespades::ui::Point{650, 480});
    menu.pointer_release({650, 480});
    expect(!find_effect<CreateMatchRouteEffect>(menu.take_effects()),
           "a busy Join Game button must ignore repeated clicks");
    menu.set_match_join_available(true);

    expect(menu.open_page(CreateMatchPage::choose_map), "members may inspect the map page");
    static_cast<void>(menu.take_effects());
    expect(menu.activate_done() && menu.page() == CreateMatchPage::match_settings,
           "nested Confirm must return to settings even when a match is joinable");
    effects = menu.take_effects();
    expect(find_effect<CreateMatchPageChangedEffect>(effects) &&
               !find_effect<CreateMatchRouteEffect>(effects),
           "nested Confirm must never join or start a match");

    menu.pointer_press(battlespades::ui::Point{650, 480});
    menu.set_match_join_available(false);
    menu.pointer_release({650, 480});
    frame = menu.presentation();
    expect(std::ranges::none_of(frame.buttons, [](const auto& button) {
        return button.stable_key == "JOIN_GAME";
    }) && !find_effect<CreateMatchRouteEffect>(menu.take_effects()) && !menu.activate_done(),
           "withdrawn publication must remove Join Game and reject a stale member click");

    menu.set_players({{2U, "Owner", "TEAM_NEUTRAL", true, true, false}});
    menu.set_host_authority(true);
    menu.set_match_join_available(true);
    expect(!menu.activate_local_game(), "even a solo owner must reuse the published match");
}

void lobby_status_stays_between_actions_and_navigation() {
    const CreateMatchMenuModel menu;
    const auto snapshot = menu.presentation();
    const std::string message =
        "The lobby could not connect. Your settings are preserved; retry when your connection is available.";
    for (const auto extent : {battlespades::ui::PixelExtent{800, 600},
                              battlespades::ui::PixelExtent{1280, 720},
                              battlespades::ui::PixelExtent{1024, 768}}) {
        const auto draw = CreateMatchPresentation{}.build(snapshot, {extent, 1'000U, message});
        const auto* notice = text_command(draw, message);
        expect(notice != nullptr &&
                   notice->layout == battlespades::ui::TextLayout::bounded_wrapped_lines &&
                   notice->maximum_lines == 1U && notice->requested_font_size_pixels == 13.0,
               "lobby messages must stay readable and ellipsize instead of shrinking");
        const auto& bounds = notice->destination;
        expect(bounds.y > snapshot.action_panel.y + snapshot.action_panel.height &&
                   bounds.y + bounds.height < snapshot.navigation_bar.y &&
                   bounds.x >= snapshot.player_panel.x &&
                   bounds.x + bounds.width <= snapshot.content_panel.x + snapshot.content_panel.width,
               "lobby status must occupy the free gap above navigation, clear of all controls");
    }

    battlespades::ui::DrawList notices;
    append_menu_status(notices, {790.0, 590.0, 340.0, 58.0}, "STATUS",
                       message + "\nprivate diagnostic log line");
    expect(!notices.empty(), "a notice anchored at the canvas edge must move into view");
    for (const auto& command : notices.commands()) {
        std::visit([](const auto& value) {
            if constexpr (requires { value.destination; }) {
                const auto& bounds = value.destination;
                expect(bounds.x >= 8.0 && bounds.y >= 8.0 &&
                           bounds.x + bounds.width <= 792.0 &&
                           bounds.y + bounds.height <= 592.0,
                       "notice surfaces and glyph bounds must stay inside safe canvas edges");
            }
            if constexpr (requires { value.localization_key; }) {
                expect(value.localization_key.find("diagnostic") == std::string::npos,
                       "diagnostic tails must not enter the visible lobby notice");
            }
        }, command);
    }
    const auto previous_size = notices.size();
    append_menu_status(notices, {0, 0, 0, 0}, "STATUS", message);
    append_menu_status(notices, {std::numeric_limits<double>::infinity(), 0, 340, 58},
                       "STATUS", message);
    expect(notices.size() == previous_size, "invalid notice geometry must emit no draw commands");
}

struct TestCase final {
    std::string_view name;
    std::function<void()> body;
};

} // namespace

int main() {
    const std::vector<TestCase> tests{
        {"published_match_joins_without_starting_another_host",
         published_match_joins_without_starting_another_host},
        {"lobby_status_stays_between_actions_and_navigation",
         lobby_status_stays_between_actions_and_navigation},
        {"local_start_is_explicit_and_never_splits_a_group",
         local_start_is_explicit_and_never_splits_a_group},
        {"host_member_actions_follow_authority_and_stable_roster_ids",
         host_member_actions_follow_authority_and_stable_roster_ids},
        {"catalogs_recover_every_retail_playlist_and_rule",
         catalogs_recover_every_retail_playlist_and_rule},
        {"default_match_settings_geometry_and_values_match_retail",
         default_match_settings_geometry_and_values_match_retail},
        {"nested_panels_emit_forward_and_backward_navigation",
         nested_panels_emit_forward_and_backward_navigation},
        {"local_host_options_are_explicit_and_travel_with_start",
         local_host_options_are_explicit_and_travel_with_start},
        {"saved_and_subscribed_maps_are_filtered_by_mode_and_selectable",
         saved_and_subscribed_maps_are_filtered_by_mode_and_selectable},
        {"map_picker_categories_selection_and_scrolling_are_stable",
         map_picker_categories_selection_and_scrolling_are_stable},
        {"lobby_refresh_preserves_manual_scroll_and_focus",lobby_refresh_preserves_manual_scroll_and_focus},
        {"lobby_refresh_preserves_scrollbar_drag_and_repairs_removed_rows",lobby_refresh_preserves_scrollbar_drag_and_repairs_removed_rows},
        {"game_rules_filter_by_mode_family_and_enabled_classes",
         game_rules_filter_by_mode_family_and_enabled_classes},
        {"semantic_input_reveals_focus_and_routes_cancel",
         semantic_input_reveals_focus_and_routes_cancel},
        {"pointer_controls_match_retail_hit_targets_and_clamping",
         pointer_controls_match_retail_hit_targets_and_clamping},
        {"lobby_name_players_and_draw_translation_are_renderer_neutral",
         lobby_name_players_and_draw_translation_are_renderer_neutral},
        {"lobby_chat_is_editable_bounded_and_renderer_visible",
         lobby_chat_is_editable_bounded_and_renderer_visible},
    };

    std::size_t failures{};
    for (const auto& test : tests) {
        try {
            test.body();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
        }
    }
    std::cout << tests.size() - failures << '/' << tests.size() << " tests passed\n";
    return failures == 0U ? 0 : 1;
}
