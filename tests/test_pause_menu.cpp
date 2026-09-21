#include "battlespades/frontend/pause_menu.hpp"
#include "battlespades/frontend/change_team_menu.hpp"
#include "battlespades/frontend/live_client_policy.hpp"
#include "battlespades/world/class_catalog.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <variant>

namespace {

void expect(bool value, const char* message) {
    if (!value) throw std::runtime_error{message};
}

[[nodiscard]] const battlespades::ui::TextDrawCommand* find_text(
    const battlespades::ui::DrawList& draw, std::string_view value) {
    for (const auto& command : draw.commands()) {
        const auto* text =
            std::get_if<battlespades::ui::TextDrawCommand>(&command);
        if (text != nullptr && text->localization_key == value) return text;
    }
    return nullptr;
}

[[nodiscard]] const battlespades::ui::SpriteDrawCommand* find_sprite(
    const battlespades::ui::DrawList& draw, std::string_view asset) {
    for (const auto& command : draw.commands()) {
        const auto* sprite =
            std::get_if<battlespades::ui::SpriteDrawCommand>(&command);
        if (sprite != nullptr && sprite->asset_id == asset) return sprite;
    }
    return nullptr;
}

[[nodiscard]] const battlespades::ui::SpriteDrawCommand* find_sprite_at(
    const battlespades::ui::DrawList& draw, std::string_view asset,
    battlespades::ui::DrawRect bounds) {
    for (const auto& command : draw.commands()) {
        const auto* sprite =
            std::get_if<battlespades::ui::SpriteDrawCommand>(&command);
        if (sprite != nullptr && sprite->asset_id == asset &&
            sprite->destination == bounds) {
            return sprite;
        }
    }
    return nullptr;
}

[[nodiscard]] const battlespades::ui::SpriteDrawCommand* find_sprite_near(
    const battlespades::ui::DrawList& draw, std::string_view asset,
    battlespades::ui::DrawRect bounds, double epsilon = 1.0e-9) {
    for (const auto& command : draw.commands()) {
        const auto* sprite =
            std::get_if<battlespades::ui::SpriteDrawCommand>(&command);
        if (sprite != nullptr && sprite->asset_id == asset &&
            std::abs(sprite->destination.x - bounds.x) <= epsilon &&
            std::abs(sprite->destination.y - bounds.y) <= epsilon &&
            std::abs(sprite->destination.width - bounds.width) <= epsilon &&
            std::abs(sprite->destination.height - bounds.height) <= epsilon) {
            return sprite;
        }
    }
    return nullptr;
}

} // namespace

int main() {
    using namespace battlespades::frontend;
    try {
        PauseMenuServerState normal;
        normal.mode_type = 6U;
        normal.player_team = 2U;
        normal.player_class = 1U;
        normal.available_class_count = 4U;
        const auto enabled = pause_menu_environment_for(normal);
        expect(enabled.show_class_change && enabled.allow_class_change &&
                   enabled.show_team_change && enabled.allow_team_change,
               "normal match menu must expose server-permitted choices");

        normal.active_team_locks_class = true;
        normal.lock_team_swap = true;
        const auto locked = pause_menu_environment_for(normal);
        expect(!locked.allow_class_change && !locked.allow_team_change,
               "StateData locks must disable pause actions");

        normal.active_team_locks_class = false;
        normal.lock_team_swap = false;
        normal.map_ended = true;
        const auto scores = pause_menu_environment_for(normal);
        expect(scores.show_class_change && scores.show_team_change &&
                   !scores.allow_class_change && !scores.allow_team_change,
               "server score state keeps selectors visible but disabled");

        normal = {};
        normal.mode_type = 12U;
        normal.ugc_mode = true;
        normal.player_team = 2U;
        const auto ugc = pause_menu_environment_for(normal);
        expect(!ugc.show_class_change && !ugc.show_team_change &&
                   ugc.show_constructs && ugc.allow_constructs &&
                   ugc.show_game_data && ugc.allow_game_data,
               "UGC mode must replace ordinary selectors with both libraries");
        PauseMenuModel ugc_menu{ugc};
        expect(ugc_menu.action_bounds(PauseMenuAction::constructs) ==
                       ugc_menu.action_bounds(PauseMenuAction::change_class) &&
                   ugc_menu.action_bounds(PauseMenuAction::game_data) ==
                       ugc_menu.action_bounds(PauseMenuAction::change_team),
               "retail UGC library actions must reuse the class/team slots");
        const battlespades::ui::Point construct_point{300, 250};
        expect(ugc_menu.pointer_release(construct_point) == std::nullopt,
               "a release without a matching press must stay inert");
        ugc_menu.pointer_press(construct_point);
        expect(ugc_menu.pointer_release(construct_point) ==
                   PauseMenuAction::constructs,
               "UGC Construct Library pause action must be clickable");

        normal = {};
        normal.mode_type = 7U;
        normal.player_team = 2U;
        normal.player_class = 10U;
        normal.available_class_count = 4U;
        const auto vip = pause_menu_environment_for(normal);
        expect(!vip.show_class_change && !vip.show_team_change,
               "VIP boss must not escape server-owned role assignment");

        normal.mode_type = 10U;
        const auto tutorial = pause_menu_environment_for(normal);
        expect(!tutorial.show_class_change && !tutorial.show_team_change,
               "Tutorial hides both multiplayer selectors");

        expect(live_world_simulation_allowed(true, true, false),
               "an Escape overlay must not suspend the live world");
        expect(!live_world_simulation_allowed(true, true, true) &&
                   !live_world_simulation_allowed(true, false, false),
               "window suspension or route teardown must stop local simulation");
        expect(!should_present_local_death(false, true, 0) &&
                   should_present_local_death(true, true, 0) &&
                   !should_present_local_death(true, false, 0),
               "only a confirmed living CreatePlayer may cross into death");
        expect(is_local_create_player_boundary(12U, std::uint8_t{12U}, true) &&
                   !is_local_create_player_boundary(7U, std::uint8_t{12U}, true) &&
                   !is_local_create_player_boundary(12U, std::nullopt, true) &&
                   !is_local_create_player_boundary(12U, std::uint8_t{12U}, false),
               "only the owner's CreatePlayer may complete its respawn generation");
        expect(!should_send_change_team(true) &&
                   should_send_change_team(false),
               "initial SelectTeam must stage packet 15, not ChangeTeam(77)");
        expect(offline_developer_tools_allowed(false, false, false),
               "developer tools must remain available in the offline lab");
        expect(!offline_developer_tools_allowed(true, false, false) &&
                   !offline_developer_tools_allowed(false, true, false) &&
                   !offline_developer_tools_allowed(false, false, true),
               "any active or pending authoritative session must disable "
               "developer tools");

        ChangeTeamMenuModel teams;
        ChangeTeamServerState team_state;
        team_state.current_team = 2U;
        team_state.spectator_enabled = true;
        team_state.team1_players = {
            {"A"}, {"B"}, {"C"}, {"D"}};
        team_state.team2_players = {{"E"}};
        team_state.lock_team_swap = true;
        teams.configure(team_state);
        expect(!teams.enabled(ChangeTeamAction::team1),
               "current team must not emit a redundant ChangeTeam request");
        expect(teams.enabled(ChangeTeamAction::team2),
               "a locked swap may repair a two-player imbalance");
        expect(teams.enabled(ChangeTeamAction::spectator),
               "server-enabled spectator remains selectable");

        team_state.team1_name = "TEAM1_COLOR";
        team_state.team2_name = "TEAM2_COLOR";
        teams.configure(team_state);
        expect(teams.state().team1_name == "Blue" &&
                   teams.state().team2_name == "Green",
               "stock StateData localization ids must resolve before rendering");

        team_state.lock_spectator_swap = true;
        team_state.forced_team = std::uint8_t{2U};
        teams.configure(team_state);
        expect(!teams.visible(ChangeTeamAction::team2) &&
                   !teams.enabled(ChangeTeamAction::spectator),
               "forced-team and spectator locks must gate the live screen");

        // Retail TextButton does not honor the supplied `size` parameter: a
        // 49 px control chooses big_button_aldo_font (36 px), while the
        // shorter initial spectator control uses medium_button_aldo_font.
        team_state = {};
        team_state.initial_join = true;
        team_state.spectator_enabled = true;
        team_state.team1_name = "Blue";
        team_state.team2_name = "Green";
        teams.configure(team_state);
        ChangeTeamPresentation team_presentation;
        const auto team_draw =
            team_presentation.build(teams, battlespades::ui::PixelExtent{800, 600});
        const auto* join_blue = find_text(team_draw, "JOIN Blue");
        const auto* spectate = find_text(team_draw, "SPECTATE");
        expect(join_blue != nullptr &&
                   join_blue->requested_font_size_pixels == 36.0 &&
                   spectate != nullptr &&
                   spectate->requested_font_size_pixels == 18.0,
               "SelectTeam button faces must use the recovered retail font sizes");
        const auto* select_first_row = find_sprite_at(
            team_draw, "png/high/white.png",
            {77.0, 166.0, 313.0, 277.0 / 16.0});
        const auto* select_second_row = find_sprite_at(
            team_draw, "png/high/white.png",
            {77.0, 166.0 + 277.0 / 16.0, 313.0, 277.0 / 16.0});
        expect(select_first_row != nullptr && select_second_row != nullptr,
               "SelectTeam must retain its source 277/16 player-row pitch");
        expect(select_first_row->modulation.color ==
                       battlespades::ui::ColorRgba8{36U, 51U, 63U, 255U} &&
                   select_second_row->modulation.color ==
                       battlespades::ui::ColorRgba8{16U, 31U, 43U, 255U},
               "player rows must start with LIST_COLOR2 and use retail "
               "blend_color truncation");

        auto change_team_state = team_state;
        change_team_state.initial_join = false;
        teams.configure(change_team_state);
        const auto change_team_draw = team_presentation.build(
            teams, battlespades::ui::PixelExtent{800, 600});
        expect(find_sprite_at(change_team_draw, "png/high/white.png",
                              {77.0, 192.0, 313.0, 263.0 / 16.0}) != nullptr,
               "ChangeTeam must use its source 263/16 player-row pitch");

        ScoreboardPresentation scoreboard;
        const auto score_draw = scoreboard.build(
            team_state, "TEAM DEATHMATCH!",
            battlespades::ui::PixelExtent{800, 600});
        const auto* score_frame = find_sprite(
            score_draw,
            "png/ui/in_game_menus/view_scores_content_frames.png");
        expect(score_frame != nullptr &&
                   score_frame->destination ==
                       battlespades::ui::DrawRect{49.0, 89.0, 702.0, 421.0} &&
                   score_frame->modulation.opacity_per_mille == 800U,
               "hold-TAB scoreboard must retain the measured retail frame and its 0.8 opacity");
        const auto* mode_title = find_text(score_draw, "TEAM DEATHMATCH!");
        expect(mode_title != nullptr &&
                   mode_title->destination ==
                       battlespades::ui::DrawRect{74.0, 137.0, 652.0, 0.0} &&
                   mode_title->horizontal_alignment ==
                       battlespades::ui::HorizontalTextAlignment::center &&
                   mode_title->fit == battlespades::ui::TextFit::retail_width_scale &&
                   mode_title->vertical_alignment ==
                       battlespades::ui::VerticalTextAlignment::baseline,
               "ViewScores title must use the recovered retail (400,463) baseline");
        const auto authored_score_draw = scoreboard.build(
            team_state, "TEAM DEATHMATCH!",
            battlespades::ui::PixelExtent{800, 600},
            static_cast<std::uint8_t>(2U), 2);
        const auto* message_frame = find_sprite(
            authored_score_draw,
            "png/ui/in_game_menus/score_text_frame.png");
        const auto* message_text =
            find_text(authored_score_draw, "Blue destroyed Green base!");
        expect(message_frame != nullptr &&
                   message_frame->destination ==
                       battlespades::ui::DrawRect{49.0, 511.0, 702.0, 28.0} &&
                   message_text != nullptr &&
                   message_text->destination ==
                       battlespades::ui::DrawRect{64.0, 530.0, 672.0, 0.0} &&
                   message_text->requested_font_size_pixels == 14.0 &&
                   message_text->transform ==
                       battlespades::ui::TextTransform::uppercase &&
                   message_text->horizontal_alignment ==
                       battlespades::ui::HorizontalTextAlignment::center &&
                   message_text->fit == battlespades::ui::TextFit::retail_width_scale &&
                   message_text->vertical_alignment ==
                       battlespades::ui::VerticalTextAlignment::baseline,
               "ShowTextMessage must populate ViewScores' recovered bottom banner");
        team_state.team1_score = 12'345;
        team_state.team2_score = 23'456;
        team_state.score_limit = 99;
        team_state.team1_show_score = false;
        team_state.team2_show_score = true;
        team_state.team2_show_max_score = false;
        const auto authority_draw = scoreboard.build(
            team_state, "TEAM DEATHMATCH!",
            battlespades::ui::PixelExtent{800, 600});
        const auto* hidden_score_blue = find_text(authority_draw, "Blue");
        const auto* shown_score_green = find_text(authority_draw, "Green");
        const auto* green_score = find_text(authority_draw, "23456");
        expect(find_text(authority_draw, "12345") == nullptr &&
                   green_score != nullptr &&
                   find_text(authority_draw, "23456/99") == nullptr,
               "scoreboard team values must obey StateData show-score and show-max flags");
        expect(hidden_score_blue != nullptr && shown_score_green != nullptr &&
                   hidden_score_blue->destination ==
                       battlespades::ui::DrawRect{177.0, 184.0, 180.0, 0.0} &&
                   shown_score_green->destination ==
                       battlespades::ui::DrawRect{467.0, 184.0, 155.0, 0.0} &&
                   green_score->destination ==
                       battlespades::ui::DrawRect{415.0, 184.0, 50.0, 0.0} &&
                   green_score->requested_font_size_pixels == 32.0 &&
                   green_score->vertical_alignment ==
                       battlespades::ui::VerticalTextAlignment::baseline &&
                   green_score->fit ==
                       battlespades::ui::TextFit::retail_width_scale,
               "hidden scores must grant retail's extra team-name width while visible scores share the exact Spades-32 baseline");
        ChangeTeamServerState sorted_state;
        sorted_state.team1_players = {
            {"Low", 1, 10, 0, false, 4},
            {"High", 99, 11, 0, false, 7},
        };
        for (std::uint8_t id{8U}; id < 23U; ++id) {
            sorted_state.team1_players.push_back(
                {"P" + std::to_string(id), static_cast<std::int32_t>(90 - id),
                 12, 0, false, id});
        }
        const auto sorted_draw = scoreboard.build(
            sorted_state, "TEAM DEATHMATCH!",
            battlespades::ui::PixelExtent{800, 600});
        const auto* high = find_text(sorted_draw, "High");
        const auto* sixteenth = find_text(sorted_draw, "P22");
        const auto* excluded = find_text(sorted_draw, "Low");
        const auto* second = find_text(sorted_draw, "P8");
        expect(high != nullptr && high->destination.y == 231.0 &&
                   high->destination.height == 0.0 &&
                   high->vertical_alignment ==
                       battlespades::ui::VerticalTextAlignment::baseline,
               "scoreboard must sort each team by descending score and retain the direct retail row baseline");
        expect(second != nullptr &&
                   second->destination.y == 231.0 + 263.0 / 16.0 &&
                   second->destination.height == 0.0,
               "ViewScores must use the binary-proven 263/16 row pitch");
        const auto* score_first_row = find_sprite_at(
            sorted_draw, "png/high/white.png",
            {77.0, 219.0, 313.0, 263.0 / 16.0});
        const auto* score_last_row = find_sprite_at(
            sorted_draw, "png/high/white.png",
            {77.0, 219.0 + 15.0 * 263.0 / 16.0, 313.0, 263.0 / 16.0});
        expect(score_first_row != nullptr && score_last_row != nullptr &&
                   std::abs((score_last_row->destination.y +
                             score_last_row->destination.height) -
                            482.0) < 1.0e-9,
               "ViewScores sixteen-row body must end at retail top-left y=482");
        expect(sixteenth != nullptr && excluded != nullptr &&
                   std::abs(excluded->destination.x - 468.95) < 1.0e-9 &&
                   excluded->destination.y == 231.0 + 15.0 * 263.0 / 16.0 &&
                   excluded->modulation.color ==
                       battlespades::ui::ColorRgba8{44U, 117U, 179U, 255U},
               "ViewScores must put a team's seventeenth player at the bottom of the other column in its own team colour");
        ChangeTeamServerState spectators_state;
        spectators_state.team1_players = {{"Blue player", 1, 12, 0, false, 1}};
        spectators_state.team2_players = {{"Green player", 1, 12, 0, false, 2}};
        spectators_state.spectator_players = {
            {"Watcher1", 10, 13, 0, false, 3},
            {"Watcher2", 8, 14, 0, false, 4},
            {"Watcher3", 6, 15, 0, false, 5}};
        const auto spectator_draw = scoreboard.build(spectators_state,
            "Team Deathmatch!", {800, 600});
        const auto* watcher1 = find_text(spectator_draw, "Watcher1");
        const auto* watcher2 = find_text(spectator_draw, "Watcher2");
        const auto* watcher3 = find_text(spectator_draw, "Watcher3");
        expect(watcher1 != nullptr && watcher2 != nullptr && watcher3 != nullptr &&
                   std::abs(watcher1->destination.x - 133.95) < 1.0e-9 &&
                   watcher1->destination.y == 231.0 + 15.0 * 263.0 / 16.0 &&
                   std::abs(watcher2->destination.x - 468.95) < 1.0e-9 &&
                   watcher2->destination.y == 231.0 + 14.0 * 263.0 / 16.0 &&
                   watcher3->destination.y == 231.0 + 15.0 * 263.0 / 16.0 &&
                   watcher1->modulation.color ==
                       battlespades::ui::ColorRgba8{194U, 194U, 194U, 255U},
               "retail spectators must split across free bottom rows with neutral text");
        teams.configure(spectators_state);
        expect(find_text(team_presentation.build(teams, {800, 600}), "Watcher1") == nullptr,
               "ChangeTeam must keep its own roster layout without ViewScores extra rows");
        expect(find_sprite(sorted_draw, "png/ui/icons/deuce_head_2.png") != nullptr &&
                   find_sprite(sorted_draw, "png/ui/icons/deuce_head_1.png") != nullptr,
               "scoreboard must retain retail's deliberately swapped team heads");

        ChangeTeamServerState player_state;
        // A non-stock character tint proves draw_player_list keeps retail's
        // fixed UI palette for the roster while tinting only live portraits
        // from Team.color.
        player_state.team1_color = {20U, 80U, 200U};
        player_state.team2_color = {60U, 190U, 70U};
        player_state.team1_players = {
            {"Ordinary", 70, 10, 0, false, 1},
            {"Dead", 60, 11, 0, true, 2},
            {"Demo", 50, 12, 0, false, 3, true},
            {"VIP", 40, 13, 0, false, 4, false, true},
            {"Tuffy", 30, 14, 0, false, 5, false, false, 6},
            {"Dominating", 20, 15, 0, false, 6, false, false, 0, true},
            {"Dominated", 10, 16, 0, false, 7, false, false, 0, false, true},
            {"Host", 0, 17, 0, false, 8, false, false, 0, false, false, true},
        };
        const auto player_draw = scoreboard.build(
            player_state, "TEAM DEATHMATCH!",
            battlespades::ui::PixelExtent{800, 600});
        constexpr double roster_y{219.0};
        constexpr double roster_pitch{263.0 / 16.0};
        const auto* ordinary_class =
            battlespades::world::find_class_definition(0U);
        expect(ordinary_class != nullptr,
               "scoreboard fixture requires the retail class-zero definition");
        const auto* class_icon = find_sprite_near(
            player_draw, ordinary_class->team_icon_assets[0U],
            {89.404, roster_y - 0.596, 16.192, 16.192});
        expect(class_icon != nullptr &&
                   class_icon->modulation.color ==
                       battlespades::ui::ColorRgba8{114U, 150U, 222U, 255U},
               "alive scoreboard portraits must use retail 0.11 scale and the server character-color white blend");
        const auto* ordinary_text = find_text(player_draw, "Ordinary");
        expect(ordinary_text != nullptr && ordinary_text->modulation.color ==
                                              battlespades::ui::ColorRgba8{
                                                  44U, 117U, 179U, 255U},
               "roster text must retain fixed UI_TEAM_COLOURS when character colors differ");
        const auto* death_icon = find_sprite_near(
            player_draw, "png/ui/score_icon_death.png",
            {87.26, roster_y + roster_pitch - 1.74, 20.48, 20.48});
        expect(death_icon != nullptr &&
                   death_icon->modulation.color ==
                       battlespades::ui::ColorRgba8{255U, 0U, 0U, 255U},
               "dead roster rows must use the source-sized red death icon");
        const auto* demo_text = find_text(player_draw, "Demo");
        expect(demo_text != nullptr &&
                   demo_text->modulation.color ==
                       battlespades::ui::ColorRgba8{255U, 194U, 81U, 255U},
               "demo players must use the recovered amber text branch");
        const auto* crown = find_sprite_near(
            player_draw, "png/ui/score_icon_crown.png",
            {88.54, roster_y + 3.0 * roster_pitch + 0.82, 17.92, 15.36});
        expect(crown != nullptr &&
                   crown->modulation.color ==
                       battlespades::ui::ColorRgba8{114U, 150U, 222U, 255U},
               "VIP/high-visibility players must replace the class icon with the crown");
        const auto* tuffy_text = find_text(player_draw, "Tuffy");
        expect(tuffy_text != nullptr &&
                   tuffy_text->preferred_font_asset == "fonts/Tuffy_Bold.ttf" &&
                   tuffy_text->destination.y == 231.0 + 4.0 * roster_pitch &&
                   tuffy_text->vertical_alignment ==
                       battlespades::ui::VerticalTextAlignment::baseline,
               "Russian, Polish and Turkish player rows must use Tuffy Bold without shifting away from the shared retail baseline");
        const auto* domination = find_sprite_near(
            player_draw,
            "png/ui/in_game_menus/select_class/domination.png",
            {113.18, roster_y + 5.0 * roster_pitch + 2.38, 10.24, 10.24});
        expect(domination != nullptr &&
                   domination->modulation.color ==
                       battlespades::ui::ColorRgba8{255U, 255U, 255U, 255U},
               "local domination relationships must render the white source marker");
        const auto* dominated = find_sprite_near(
            player_draw,
            "png/ui/in_game_menus/select_class/dominated.png",
            {113.18, roster_y + 6.0 * roster_pitch + 2.38, 10.24, 10.24});
        expect(dominated != nullptr &&
                   dominated->modulation.color ==
                       battlespades::ui::ColorRgba8{137U, 179U, 44U, 255U},
               "dominated relationships must tint with the opposing UI team color");
        const auto* leader = find_sprite_near(
            player_draw, "png/ui/icons/leader_icon.png",
            {129.47, roster_y + 7.0 * roster_pitch + 3.02, 8.96, 8.96});
        const auto* host_text = find_text(player_draw, "Host");
        expect(leader != nullptr && host_text != nullptr &&
                   host_text->destination.x == 143.91,
               "lobby hosts must render the leader icon and advance the name column");
        std::cout << "pause menu server-state tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
