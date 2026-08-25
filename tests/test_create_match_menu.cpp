#include "battlespades/frontend/create_match_menu.hpp"
#include "battlespades/frontend/create_match_presentation.hpp"

#include <algorithm>
#include <exception>
#include <functional>
#include <iostream>
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
    expect(frame.buttons.size() == 4U && defaults != frame.buttons.end() &&
               defaults->bounds == battlespades::ui::Rect{410, 415, 80, 30} &&
               start != frame.buttons.end() &&
               start->bounds == battlespades::ui::Rect{405, 456, 332, 50} &&
               invite != frame.buttons.end() &&
               invite->bounds == battlespades::ui::Rect{300, 109, 80, 30} &&
               back != frame.buttons.end() && back->label_key == "LEAVE_LOBBY",
           "Defaults, glowing Start, Invite, and Leave Lobby must occupy retail controls");
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

struct TestCase final {
    std::string_view name;
    std::function<void()> body;
};

} // namespace

int main() {
    const std::vector<TestCase> tests{
        {"catalogs_recover_every_retail_playlist_and_rule",
         catalogs_recover_every_retail_playlist_and_rule},
        {"default_match_settings_geometry_and_values_match_retail",
         default_match_settings_geometry_and_values_match_retail},
        {"nested_panels_emit_forward_and_backward_navigation",
         nested_panels_emit_forward_and_backward_navigation},
        {"local_host_options_are_explicit_and_travel_with_start",
         local_host_options_are_explicit_and_travel_with_start},
        {"map_picker_categories_selection_and_scrolling_are_stable",
         map_picker_categories_selection_and_scrolling_are_stable},
        {"game_rules_filter_by_mode_family_and_enabled_classes",
         game_rules_filter_by_mode_family_and_enabled_classes},
        {"semantic_input_reveals_focus_and_routes_cancel",
         semantic_input_reveals_focus_and_routes_cancel},
        {"pointer_controls_match_retail_hit_targets_and_clamping",
         pointer_controls_match_retail_hit_targets_and_clamping},
        {"lobby_name_players_and_draw_translation_are_renderer_neutral",
         lobby_name_players_and_draw_translation_are_renderer_neutral},
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
