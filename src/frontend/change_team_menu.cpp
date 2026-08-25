#include "battlespades/frontend/change_team_menu.hpp"

#include "battlespades/world/class_catalog.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace battlespades::frontend {
namespace {

constexpr std::array<ChangeTeamAction, 4U> actions{
    ChangeTeamAction::team1, ChangeTeamAction::team2,
    ChangeTeamAction::spectator, ChangeTeamAction::back};
constexpr std::size_t team_lock_difference_tolerance{2U};
constexpr ui::ColorRgba8 menu_color{244U, 236U, 187U, 255U};
constexpr ui::ColorRgba8 flat_join_background{112U, 216U, 224U, 255U};
constexpr ui::ColorRgba8 dead_player_color{255U, 0U, 0U, 255U};
constexpr ui::ColorRgba8 demo_player_color{255U, 194U, 81U, 255U};
constexpr ui::ColorRgba8 white{255U, 255U, 255U, 255U};
constexpr std::array<std::uint8_t, 3U> retail_team1_ui{44U, 117U, 179U};
constexpr std::array<std::uint8_t, 3U> retail_team2_ui{137U, 179U, 44U};

[[nodiscard]] constexpr bool roster_uses_tuffy(
    std::uint8_t local_language) noexcept {
    // aoslib.strings.LocalLanguage: Russian=6, Polish=7, Turkish=8.
    return local_language == 6U || local_language == 7U ||
           local_language == 8U;
}

[[nodiscard]] ui::ColorRgba8 color(
    const std::array<std::uint8_t, 3U>& rgb,
    std::uint8_t alpha = 255U) noexcept {
    return {rgb[0U], rgb[1U], rgb[2U], alpha};
}

/** shared.common.blend_color(a, b, f): int(a*(1-f) + b*f). */
[[nodiscard]] ui::ColorRgba8 retail_blend_color(
    ui::ColorRgba8 first,
    const std::array<std::uint8_t, 3U>& second,
    double factor) noexcept {
    const auto component = [factor](std::uint8_t left,
                                    std::uint8_t right) {
        // Python's int() truncates here. Rounding produces visibly darker
        // roster stripes and does not match the retail helper.
        return static_cast<std::uint8_t>(
            static_cast<double>(left) * (1.0 - factor) +
            static_cast<double>(right) * factor);
    };
    return {component(first.red, second[0U]),
            component(first.green, second[1U]),
            component(first.blue, second[2U]), 255U};
}

[[nodiscard]] ui::SpriteDrawCommand sprite(
    std::string asset, ui::DrawRect bounds,
    ui::ColorRgba8 tint = {},
    std::uint16_t intensity = 1'000U,
    std::uint16_t opacity = 1'000U) {
    return {std::move(asset), bounds, ui::DrawSpace::design_pixels,
            ui::TextureFilter::linear, ui::TextureAnchor::top_left, 0.64,
            ui::SpriteSizing::stretch,
            ui::ColorModulation{tint, intensity, opacity}};
}

[[nodiscard]] ui::TextDrawCommand text(
    std::string value, ui::DrawRect bounds, double size,
    ui::HorizontalTextAlignment alignment =
        ui::HorizontalTextAlignment::center,
    ui::ColorRgba8 tint = menu_color,
    std::string font = "fonts/Spades.ttf",
    ui::TextTransform transform = ui::TextTransform::uppercase,
    ui::TextFit fit = ui::TextFit::retail_width_scale,
    std::uint16_t intensity = 1'000U,
    ui::VerticalTextAlignment vertical_alignment =
        ui::VerticalTextAlignment::retail_center) {
    return {std::move(value), std::move(font), bounds,
            ui::DrawSpace::design_pixels, size, 0.0, 1U, alignment,
            vertical_alignment, transform, fit,
            ui::ColorModulation{tint, intensity, 1'000U}};
}

void append_button(ui::DrawList& list, ui::Rect bounds, std::string label,
                   bool hovered, bool pressed, bool enabled,
                   double text_size) {
    const std::string state = pressed ? "press_" : hovered ? "hover_" : "";
    const auto part = [&](std::string_view side) {
        return "png/ui/common_elements/buttons/button_large_" + state +
               std::string{side} + ".png";
    };
    const auto intensity = enabled ? std::uint16_t{1'000U}
                                   : std::uint16_t{650U};
    const double x = static_cast<double>(bounds.x);
    const double y = static_cast<double>(bounds.y);
    const double width = static_cast<double>(bounds.width);
    const double height = static_cast<double>(bounds.height);
    const double cap = 60.0 / 97.0 * height + 1.0;
    list.push(sprite(part("left"), {x, y, cap, height}, {}, intensity));
    list.push(sprite(part("mid"), {x + cap, y, width - cap * 2.0, height},
                     {}, intensity));
    list.push(sprite(part("right"), {x + width - cap, y, cap, height},
                     {}, intensity));
    list.push(text(std::move(label),
                   {x + 10.0, y + 1.0, width - 20.0, height - 2.0},
                   text_size, ui::HorizontalTextAlignment::center,
                   {20U, 20U, 20U, 255U}, "fonts/Spades.ttf",
                   ui::TextTransform::uppercase, ui::TextFit::retail_width_scale,
                   intensity));
}

void append_key_display(ui::DrawList& list, int key, double center_x,
                        double center_y, double size) {
    list.push(sprite("png/ui/icons/key" + std::to_string(key) + ".png",
                     {center_x - size * 0.5, center_y - size * 0.5, size, size}));
}

[[nodiscard]] std::string score_text(std::int32_t score,
                                     std::int32_t limit,
                                     bool show_max_score) {
    if (!show_max_score) return std::to_string(score);
    return std::to_string(score) + "/" + std::to_string(limit);
}

/**
 * Reproduces ingame_menus.draw_player_list.
 *
 * `vertical_offset` is zero for SelectTeam, 26 for ChangeTeam and 53 for
 * ViewScores. SelectTeam passes height 277; both in-match callers pass 263.
 * Keeping the source height as an argument is required because
 * draw_player_list computes every player row as `height / 16.0`.
 */
void append_player_lists(ui::DrawList& list,
                         const ChangeTeamServerState& state,
                         double vertical_offset, double list_height) {
    constexpr double list_width{313.0};
    const double row_height = list_height / 16.0;
    const auto append_team = [&](bool team2) {
        const double x = team2 ? 412.0 : 77.0;
        // draw_player_list deliberately separates the authored HUD palette
        // from Team.color. Roster bands, labels, score and Deuce head use
        // UI_TEAM_COLOURS; only a living class/VIP portrait follows the
        // server-authored character colour.
        const auto& team_ui_color = team2 ? retail_team2_ui : retail_team1_ui;
        const auto& character_color = team2 ? state.team2_color : state.team1_color;
        auto players = team2 ? state.team2_players : state.team1_players;
        std::ranges::stable_sort(players, [](const TeamRosterPlayer& left,
                                            const TeamRosterPlayer& right) {
            if (left.score != right.score) return left.score > right.score;
            if (left.player_id != right.player_id) return left.player_id < right.player_id;
            return left.name < right.name;
        });
        const auto& name = team2 ? state.team2_name : state.team1_name;
        const auto score = team2 ? state.team2_score : state.team1_score;
        const bool show_score = team2 ? state.team2_show_score : state.team1_show_score;
        const bool show_max_score =
            team2 ? state.team2_show_max_score : state.team1_show_max_score;

        const double head_x = team2 ? 637.0 : 119.0;
        const double head_y = 89.0 + vertical_offset;
        // This apparent swap is deliberate in retail draw_player_list:
        // TEAM1 uses deuce_head_2 and TEAM2 uses deuce_head_1.
        const std::string suffix = team2 ? "1" : "2";
        list.push(sprite("png/ui/icons/deuce_head_" + suffix + ".png",
                         {head_x, head_y, 46.0, 44.0}));
        list.push(sprite("png/ui/icons/deuce_head_colour_" + suffix + ".png",
                         {head_x, head_y, 46.0, 44.0}, color(team_ui_color)));

        // draw_player_list starts at x+100. TEAM2 then moves the name left by
        // 45px when the score is shown, but by 70px when it is hidden and
        // grants the name another 25px. The prior fixed rectangle silently
        // lost that no-score branch.
        const double team_name_x = team2
                                       ? (show_score ? 467.0 : 442.0)
                                       : 177.0;
        const double team_name_width = show_score ? 155.0 : 180.0;
        const double score_x = team2 ? 415.0 : 335.0;
        // draw_text_with_size_validation(..., height=30, center_text=True)
        // places both labels at team_y + 30/3. Converting the three retail
        // callers gives one exact top-origin baseline: 131 + caller offset.
        const double team_baseline = 131.0 + vertical_offset;
        list.push(text(name, {team_name_x, team_baseline, team_name_width, 0.0},
                       32.0, ui::HorizontalTextAlignment::center,
                       color(team_ui_color), "fonts/Spades.ttf",
                       ui::TextTransform::uppercase,
                       ui::TextFit::retail_width_scale, 1'000U,
                       ui::VerticalTextAlignment::baseline));
        if (show_score) {
            list.push(text(score_text(score, state.score_limit, show_max_score),
                           {score_x, team_baseline, 50.0, 0.0}, 32.0,
                           ui::HorizontalTextAlignment::center,
                           color(team_ui_color), "fonts/Spades.ttf",
                           ui::TextTransform::uppercase,
                           ui::TextFit::retail_width_scale, 1'000U,
                           ui::VerticalTextAlignment::baseline));
        }

        const double header_baseline = 153.0 + vertical_offset;
        const double name_x = x + list_width * 0.15 + 10.0;
        const double score_column = x + list_width * 0.60 + 10.0;
        const double ping_column = x + list_width * 0.85 + 10.0;
        list.push(text("Name", {name_x, header_baseline, score_column - name_x - 5.0, 0.0},
                       11.0, ui::HorizontalTextAlignment::left,
                       {0U, 0U, 0U, 255U}, "fonts/A750-Sans-Medium.ttf",
                       ui::TextTransform::preserve,
                       ui::TextFit::retail_width_scale, 1'000U,
                       ui::VerticalTextAlignment::baseline));
        list.push(text("Score", {score_column, header_baseline, 65.0, 0.0}, 11.0,
                       ui::HorizontalTextAlignment::left,
                       {0U, 0U, 0U, 255U}, "fonts/A750-Sans-Medium.ttf",
                       ui::TextTransform::preserve,
                       ui::TextFit::retail_width_scale, 1'000U,
                       ui::VerticalTextAlignment::baseline));
        list.push(text("Ping", {ping_column, header_baseline, 47.0, 0.0}, 11.0,
                       ui::HorizontalTextAlignment::left,
                       {0U, 0U, 0U, 255U}, "fonts/A750-Sans-Medium.ttf",
                       ui::TextTransform::preserve,
                       ui::TextFit::retail_width_scale, 1'000U,
                       ui::VerticalTextAlignment::baseline));

        // Retail iterates xrange(17): one header plus sixteen player rows.
        for (std::size_t row{}; row < 16U; ++row) {
            const double row_y = 166.0 + vertical_offset +
                                 static_cast<double>(row) * row_height;
            // The retail loop index includes its header. The first player is
            // therefore i=1 and uses LIST_COLOR2; the second uses LIST_COLOR1.
            const auto background = retail_blend_color(
                row % 2U == 0U ? ui::ColorRgba8{35U, 35U, 35U, 255U}
                               : ui::ColorRgba8{10U, 10U, 10U, 255U},
                team_ui_color, 0.2);
            list.push(sprite("png/high/white.png",
                             {x, row_y, list_width, row_height}, background));
            if (row >= players.size()) continue;

            const auto& player = players[row];
            const auto player_color = player.dead
                                          ? dead_player_color
                                          : player.demo_player
                                                ? demo_player_color
                                                : color(team_ui_color);
            const auto alive_icon_color = retail_blend_color(
                color(character_color), {255U, 255U, 255U}, 0.4);
            const auto* klass = world::find_class_definition(player.class_id);
            const bool ordinary_alive_icon =
                !player.dead && !player.high_minimap_visibility;
            if (player.dead) {
                // draw_player_list anchors the original 32x32 death icon at
                // center (x+20.5,row+8.5) with global image scale 0.64.
                list.push(sprite("png/ui/score_icon_death.png",
                                 {x + 10.26, row_y - 1.74, 20.48, 20.48},
                                 dead_player_color));
            } else if (player.high_minimap_visibility) {
                // VIP/high-visibility players replace their class portrait
                // with the source 28x24 crown at the same retail anchor.
                list.push(sprite("png/ui/score_icon_crown.png",
                                 {x + 11.54, row_y + 0.82, 17.92, 15.36},
                                 alive_icon_color));
            } else if (klass != nullptr) {
                const auto team_index = team2 ? 1U : 0U;
                list.push(sprite(std::string{klass->team_icon_assets[team_index]},
                                 {x + 12.404, row_y - 0.596,
                                  16.192, 16.192},
                                 alive_icon_color));
            }

            const double marker_y = row_y +
                (ordinary_alive_icon ? 2.38 : 3.38);
            if (player.dominating_local_player) {
                list.push(sprite(
                    "png/ui/in_game_menus/select_class/domination.png",
                    {x + 36.18, marker_y, 10.24, 10.24}, white));
            } else if (player.dominated_by_local_player) {
                const auto& other_team_color =
                    team2 ? retail_team1_ui : retail_team2_ui;
                list.push(sprite(
                    "png/ui/in_game_menus/select_class/dominated.png",
                    {x + 36.18, marker_y, 10.24, 10.24},
                    color(other_team_color)));
            }

            double player_name_x = name_x;
            if (player.lobby_host) {
                list.push(sprite("png/ui/icons/leader_icon.png",
                                 {x + 52.47,
                                  row_y + (ordinary_alive_icon ? 3.02 : 4.02),
                                  8.96, 8.96},
                                 white));
                player_name_x = x + 66.91;
            }
            const std::string row_font =
                roster_uses_tuffy(player.local_language)
                    ? "fonts/Tuffy_Bold.ttf"
                    : "fonts/A750-Sans-Medium.ttf";
            // Font.draw receives this baseline directly. It must not be
            // recomputed from each font's metrics: doing that made Tuffy rows
            // visibly jump relative to the score and icon columns.
            const double row_baseline = 178.0 + vertical_offset +
                                        static_cast<double>(row) * row_height;
            list.push(text(player.name,
                           {player_name_x, row_baseline,
                            score_column - player_name_x - 5.0,
                            0.0},
                           11.0, ui::HorizontalTextAlignment::left,
                           player_color, row_font,
                           ui::TextTransform::preserve,
                           ui::TextFit::retail_width_scale, 1'000U,
                           ui::VerticalTextAlignment::baseline));
            list.push(text(std::to_string(player.score),
                           {score_column, row_baseline, 50.0, 0.0}, 11.0,
                           ui::HorizontalTextAlignment::left,
                           player_color, row_font,
                           ui::TextTransform::preserve,
                           ui::TextFit::retail_width_scale, 1'000U,
                           ui::VerticalTextAlignment::baseline));
            list.push(text(std::to_string(player.ping),
                           {ping_column, row_baseline, 45.0, 0.0}, 11.0,
                           ui::HorizontalTextAlignment::left,
                           player_color, row_font,
                           ui::TextTransform::preserve,
                           ui::TextFit::retail_width_scale, 1'000U,
                           ui::VerticalTextAlignment::baseline));
        }
    };
    append_team(false);
    append_team(true);
}

void append_navigation_back(ui::DrawList& list) {
    list.push(sprite("png/ui/common_elements/nav_bar/back_icon.png",
                     {54.0, 543.0, 26.0, 26.0}));
    list.push(text("Back", {82.0, 540.0, 100.0, 34.0}, 22.0,
                   ui::HorizontalTextAlignment::left,
                   {180U, 165U, 75U, 255U}));
}

[[nodiscard]] std::string localize_stock_team_name(std::string value) {
    // StateData carries localization identifiers, not display text, for the
    // stock teams. The Python client resolves these through strings.get_by_id.
    if (value == "TEAM1_COLOR") return "Blue";
    if (value == "TEAM2_COLOR") return "Green";
    if (value == "TEAM_NEUTRAL") return "Neutral";
    return value;
}

} // namespace

void ChangeTeamMenuModel::configure(ChangeTeamServerState state) {
    state.team1_name = localize_stock_team_name(std::move(state.team1_name));
    state.team2_name = localize_stock_team_name(std::move(state.team2_name));
    state_ = std::move(state);
    hovered_.reset();
    pressed_.reset();
}

ui::Rect ChangeTeamMenuModel::bounds(ChangeTeamAction action,
                                     bool initial_join) noexcept {
    if (initial_join) {
        switch (action) {
        case ChangeTeamAction::team1: return {78, 451, 225, 49};
        case ChangeTeamAction::team2: return {497, 451, 225, 49};
        case ChangeTeamAction::spectator: return {327, 464, 110, 36};
        case ChangeTeamAction::back: return {54, 541, 695, 35};
        }
    }
    switch (action) {
    case ChangeTeamAction::team1: return {68, 471, 200, 49};
    case ChangeTeamAction::team2: return {531, 471, 200, 49};
    case ChangeTeamAction::spectator: return {314, 477, 130, 39};
    case ChangeTeamAction::back: return {};
    }
    return {};
}

bool ChangeTeamMenuModel::visible(ChangeTeamAction action) const noexcept {
    if (action == ChangeTeamAction::back) return state_.initial_join;
    if (action == ChangeTeamAction::spectator) {
        return state_.spectator_enabled && !state_.spectator_locked;
    }
    const auto wire_team = static_cast<std::uint8_t>(action);
    if (state_.forced_team.has_value() && *state_.forced_team != wire_team) {
        return false;
    }
    return action == ChangeTeamAction::team1 ? !state_.team1_locked
                                             : !state_.team2_locked;
}

bool ChangeTeamMenuModel::enabled(ChangeTeamAction action) const noexcept {
    if (!visible(action)) return false;
    if (action == ChangeTeamAction::back) return true;
    const auto wire_team = static_cast<std::uint8_t>(action);
    if (!state_.initial_join && wire_team == state_.current_team) return false;
    if (action == ChangeTeamAction::spectator) {
        return !state_.lock_spectator_swap;
    }
    if (state_.initial_join || !state_.lock_team_swap ||
        state_.current_team < 2U) {
        return true;
    }

    // changeTeam.py permits a swap when it repairs a >=2 player imbalance,
    // when the destination is empty, or when the current team was locked.
    if (action == ChangeTeamAction::team1 && state_.current_team == 3U) {
        return state_.team1_players.empty() || state_.team2_locked ||
               state_.team2_players.size() >=
                   state_.team1_players.size() + team_lock_difference_tolerance;
    }
    if (action == ChangeTeamAction::team2 && state_.current_team == 2U) {
        return state_.team2_players.empty() || state_.team1_locked ||
               state_.team1_players.size() >=
                   state_.team2_players.size() + team_lock_difference_tolerance;
    }
    return true;
}

std::optional<ChangeTeamAction> ChangeTeamMenuModel::hit_test(
    std::optional<ui::Point> point) const noexcept {
    if (!point.has_value()) return std::nullopt;
    for (const auto action : actions) {
        if (enabled(action) &&
            bounds(action, state_.initial_join).contains(*point)) {
            return action;
        }
    }
    return std::nullopt;
}

void ChangeTeamMenuModel::pointer_move(
    std::optional<ui::Point> point) noexcept {
    hovered_ = hit_test(point);
    if (pressed_.has_value() && pressed_ != hovered_) pressed_.reset();
}

void ChangeTeamMenuModel::pointer_press(
    std::optional<ui::Point> point) noexcept {
    pressed_ = hit_test(point);
    hovered_ = pressed_;
}

std::optional<ChangeTeamAction> ChangeTeamMenuModel::pointer_release(
    std::optional<ui::Point> point) noexcept {
    const auto released = hit_test(point);
    const auto result = pressed_.has_value() && pressed_ == released
                            ? pressed_
                            : std::nullopt;
    pressed_.reset();
    hovered_ = released;
    return result;
}

ui::DrawList ChangeTeamPresentation::build(
    const ChangeTeamMenuModel& model, ui::PixelExtent window) const {
    static_cast<void>(window);
    ui::DrawList list;
    list.reserve(160U);
    const auto& state = model.state();
    if (state.initial_join) {
        // SelectTeam is rendered after the map has loaded but before a player
        // camera exists. Retail clears that gate to the skydome's cyan.
        list.push(sprite("png/high/white.png", {0.0, 0.0, 800.0, 600.0},
                         flat_join_background));
        list.push(sprite("png/ui/common_elements/frames/ui_frame_large.png",
                         {25.0, 5.0, 750.0, 589.0}));
        list.push(sprite("png/ui/choose_team/choose_team_content_frames.png",
                         {31.0, 5.0, 739.0, 589.0}));
        list.push(text("CHOOSE TEAM", {180.0, 14.0, 440.0, 59.0}, 46.0));
        append_player_lists(list, state, 0.0, 277.0);
        append_key_display(list, 1, 93.0, 120.0, 34.0);
        append_key_display(list, 2, 707.0, 120.0, 34.0);
        if (model.visible(ChangeTeamAction::spectator)) {
            append_key_display(list, 3, 458.0, 481.0, 34.0);
        }
        append_navigation_back(list);
    } else {
        list.push(sprite(
            "png/ui/in_game_menus/change_team_content_frames.png",
            {49.0, 62.0, 702.0, 476.0}));
        list.push(text("CHANGE TEAM", {180.0, 78.0, 440.0, 58.0}, 46.0));
        append_player_lists(list, state, 26.0, 263.0);
        append_key_display(list, 1, 93.0, 145.0, 34.0);
        append_key_display(list, 2, 707.0, 145.0, 34.0);
        if (model.visible(ChangeTeamAction::spectator)) {
            append_key_display(list, 3, 464.0, 496.0, 34.0);
        }
    }

    for (const auto action : actions) {
        if (!model.visible(action) || action == ChangeTeamAction::back) continue;
        std::string label;
        double font_size{20.0};
        switch (action) {
        case ChangeTeamAction::team1:
            label = "JOIN " + state.team1_name;
            // TextButton ignores the constructor's historical size argument.
            // A 49 px button selects big_button_aldo_font (36 px).
            font_size = 36.0;
            break;
        case ChangeTeamAction::team2:
            label = "JOIN " + state.team2_name;
            font_size = 36.0;
            break;
        case ChangeTeamAction::spectator:
            label = "SPECTATE";
            // SelectTeam's 36 px button selects the medium 18 px face.
            // ChangeTeam is 39 px and selects the large face, shrunk to fit.
            font_size = state.initial_join ? 18.0 : 28.0;
            break;
        case ChangeTeamAction::back:
            break;
        }
        append_button(list,
                      ChangeTeamMenuModel::bounds(action, state.initial_join),
                      std::move(label), model.hovered() == action,
                      model.pressed() == action, model.enabled(action),
                      font_size);
    }
    return list;
}

std::string retail_match_result_message(
    const ChangeTeamServerState& state,
    std::optional<std::uint8_t> message_id,
    std::int32_t winner_team) {
    const auto score_winner = [&]() -> std::int32_t {
        if (state.team1_score > state.team2_score) return 2;
        if (state.team2_score > state.team1_score) return 3;
        return winner_team == 2 || winner_team == 3 ? winner_team : 0;
    };
    const auto resolved_winner = [&]() -> std::int32_t {
        if (winner_team == 2 || winner_team == 3) return winner_team;
        return score_winner();
    };
    const auto winning_name = [&](std::int32_t winner) -> const std::string& {
        return winner == 3 ? state.team2_name : state.team1_name;
    };
    const auto losing_name = [&](std::int32_t winner) -> const std::string& {
        return winner == 3 ? state.team1_name : state.team2_name;
    };
    const auto generic_score_title = [&]() {
        const auto winner = resolved_winner();
        if (winner == 2 || winner == 3) return winning_name(winner) + " wins!";
        return std::string{"Draw!"};
    };

    if (!message_id.has_value()) return generic_score_title();
    switch (*message_id) {
    case 0U:
        return "End of map. Next map incoming...";
    case 1U: {
        const auto winner = score_winner();
        if (winner == 2 || winner == 3) return winning_name(winner) + " wins!";
        return "Draw!";
    }
    case 2U: {
        const auto winner = score_winner();
        if (winner == 2 || winner == 3) {
            return winning_name(winner) + " destroyed " + losing_name(winner) +
                   " base!";
        }
        return "Draw!";
    }
    case 3U:
        return "Zombie virus has claimed all survivors!";
    case 4U:
        return "Zombie outbreak contained! Survivors receive a score bonus!";
    case 6U:
        return state.team1_name + " wins!";
    case 7U:
        return state.team2_name + " wins!";
    case 8U:
        return "Draw!";
    case 5U:
    default:
        // OCCUPATION_WIN_MESSAGE has no dedicated branch in either retail
        // set_message implementation; it retains the generic score title.
        return generic_score_title();
    }
}

ui::DrawList ScoreboardPresentation::build(
    const ChangeTeamServerState& state, std::string mode_title,
    ui::PixelExtent window, std::optional<std::uint8_t> message_id,
    std::int32_t winner_team) const {
    static_cast<void>(window);
    ui::DrawList list;
    list.reserve(140U);
    list.push(sprite("png/ui/in_game_menus/view_scores_content_frames.png",
                     {49.0, 89.0, 702.0, 421.0}));
    // ViewScores.draw calls title_font.draw(..., 400, 463, center=True).
    // Preserve that baseline exactly: a width-bearing layout box subtly
    // shifted the title and could shrink long localized mode names.
    list.push(text(std::move(mode_title), {400.0, 137.0, 0.0, 0.0}, 46.0,
                   ui::HorizontalTextAlignment::center, menu_color,
                   "fonts/Spades.ttf", ui::TextTransform::uppercase,
                   ui::TextFit::none, 1'000U,
                   ui::VerticalTextAlignment::baseline));
    append_player_lists(list, state, 53.0, 263.0);
    if (message_id.has_value()) {
        // ViewScores.draw: score_text_frame is a 1098x45 centre-anchored image
        // scaled with int(source * .64), blitted at retail bottom-left
        // (400,75). score_text_font draws the uppercase text at (400,70).
        list.push(sprite("png/ui/in_game_menus/score_text_frame.png",
                         {49.0, 511.0, 702.0, 28.0}));
        list.push(text(retail_match_result_message(state, message_id,
                                                   winner_team),
                       {400.0, 530.0, 0.0, 0.0}, 14.0,
                       ui::HorizontalTextAlignment::center, menu_color,
                       "fonts/Spades.ttf", ui::TextTransform::uppercase,
                       ui::TextFit::none, 1'000U,
                       ui::VerticalTextAlignment::baseline));
    }
    return list;
}

} // namespace battlespades::frontend
