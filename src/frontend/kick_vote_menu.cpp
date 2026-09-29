#include "battlespades/frontend/kick_vote_menu.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace battlespades::frontend {
namespace {

constexpr ui::ColorRgba8 menu_color{244U, 236U, 187U, 255U};
constexpr ui::ColorRgba8 black{0U, 0U, 0U, 255U};
/** DROP_BOX_BACKGROUND_COLOUR, ROW_GREY_COLOUR, ROW_DARK_GREY_COLOUR. */
constexpr ui::ColorRgba8 dropdown_grey{34U, 32U, 33U, 255U};
constexpr ui::ColorRgba8 row_grey{57U, 53U, 44U, 255U};
constexpr ui::ColorRgba8 row_dark{24U, 21U, 14U, 255U};

constexpr double list_width{313.0};
constexpr double list_height{263.0};
constexpr double row_height{list_height / 16.0};
/** get_highlighted_team_member: y -= 10 + list_y_size, in retail y-up. */
constexpr double header_up{443.0 - 10.0 - row_height};
constexpr std::array<double, 2U> column_x{77.0, 412.0};

[[nodiscard]] std::vector<TeamRosterPlayer> sorted(std::vector<TeamRosterPlayer> players) {
    std::ranges::stable_sort(players, [](const TeamRosterPlayer& left,
                                         const TeamRosterPlayer& right) {
        if (left.score != right.score) return left.score > right.score;
        if (left.player_id != right.player_id) return left.player_id < right.player_id;
        return left.name < right.name;
    });
    return players;
}

[[nodiscard]] bool inside(ui::Point point, ui::Rect rect) noexcept {
    return rect.contains(point);
}

[[nodiscard]] ui::SpriteDrawCommand sprite(std::string asset, ui::DrawRect bounds,
                                           ui::ColorRgba8 tint = {},
                                           std::uint16_t intensity = 1'000U) {
    return {std::move(asset), bounds, ui::DrawSpace::design_pixels,
            ui::TextureFilter::linear, ui::TextureAnchor::top_left, 0.64,
            ui::SpriteSizing::stretch, ui::ColorModulation{tint, intensity, 1'000U}};
}

[[nodiscard]] ui::SpriteDrawCommand solid(ui::DrawRect bounds, ui::ColorRgba8 tint) {
    auto command = sprite("png/high/white.png", bounds, tint);
    command.sampling = ui::TextureFilter::nearest;
    return command;
}

[[nodiscard]] ui::TextDrawCommand text(std::string value, ui::DrawRect bounds, double size,
                                       ui::HorizontalTextAlignment alignment,
                                       ui::ColorRgba8 tint, std::string font,
                                       ui::TextTransform transform,
                                       std::uint16_t intensity = 1'000U) {
    return {std::move(value), std::move(font), bounds, ui::DrawSpace::design_pixels,
            size, 0.0, 1U, alignment, ui::VerticalTextAlignment::retail_center, transform,
            ui::TextFit::retail_width_scale, ui::ColorModulation{tint, intensity, 1'000U}};
}

[[nodiscard]] ui::DrawRect draw_rect(ui::Rect rect) noexcept {
    return {static_cast<double>(rect.x), static_cast<double>(rect.y),
            static_cast<double>(rect.width), static_cast<double>(rect.height)};
}

[[nodiscard]] std::string team_suffix(std::uint8_t team) {
    // KickVotePlayerSelect.draw: white for spectators, blue TEAM1, green TEAM2.
    return team == 2U ? "blue" : team == 3U ? "green" : "white";
}

} // namespace

KickVoteRosterColumns kick_vote_roster(const ChangeTeamServerState& state) {
    KickVoteRosterColumns columns{};
    const std::array teams{sorted(state.team1_players), sorted(state.team2_players)};
    const auto spectators = sorted(state.spectator_players);
    std::array<std::vector<KickVoteRosterRow>, 2U> extras;
    if (teams[0U].size() >= 16U || teams[1U].size() >= 16U) {
        const std::size_t full_team = teams[0U].size() >= 16U ? 0U : 1U;
        auto& destination = extras[1U - full_team];
        for (const auto& player : spectators) destination.push_back({player, 0U});
        for (std::size_t index{16U}; index < teams[full_team].size(); ++index) {
            destination.push_back({teams[full_team][index],
                                   static_cast<std::uint8_t>(full_team + 2U)});
        }
    } else if (!spectators.empty()) {
        const auto left_space = 16U - teams[0U].size();
        const auto right_space = 16U - teams[1U].size();
        auto left_count = spectators.size() / 2U;
        if (left_count > left_space) left_count = left_space;
        else if (spectators.size() - left_count > right_space) {
            left_count = spectators.size() - right_space;
        }
        for (std::size_t index{}; index < spectators.size(); ++index) {
            extras[index < left_count ? 0U : 1U].push_back({spectators[index], 0U});
        }
    }
    for (std::size_t column{}; column < 2U; ++column) {
        const auto team = static_cast<std::uint8_t>(column + 2U);
        for (std::size_t row{}; row < 16U && row < teams[column].size(); ++row) {
            columns[column][row] = KickVoteRosterRow{teams[column][row], team};
        }
        const auto first_extra = 16 - static_cast<std::ptrdiff_t>(extras[column].size());
        for (std::size_t index{}; index < extras[column].size(); ++index) {
            const auto row = first_extra + static_cast<std::ptrdiff_t>(index);
            if (row < 0 || row >= 16) continue;
            auto& slot = columns[column][static_cast<std::size_t>(row)];
            if (!slot.has_value()) slot = extras[column][index];
        }
    }
    return columns;
}

std::optional<KickVoteRosterHit>
kick_vote_roster_hit(const KickVoteRosterColumns& columns, ui::Point point) noexcept {
    const double mouse_x = static_cast<double>(point.x);
    const double mouse_up = 600.0 - static_cast<double>(point.y);
    for (std::size_t column{}; column < 2U; ++column) {
        const double x = column_x[column];
        if (mouse_x < x || mouse_x > x + list_width) continue;
        if (mouse_up > header_up || mouse_up < header_up - (list_height + row_height)) continue;
        const auto index = static_cast<int>(std::round((header_up - mouse_up) / row_height));
        const auto player_index = index - 1;
        if (player_index < 0 || player_index >= 16) return std::nullopt;
        const auto& slot = columns[column][static_cast<std::size_t>(player_index)];
        if (!slot.has_value()) return std::nullopt;
        // (x, y - list_y_size * index, width, list_y_size): the y is the row
        // centre used to blit the centre-anchored hover/highlight art.
        const double centre_down = 600.0 - (header_up - row_height * static_cast<double>(index));
        return KickVoteRosterHit{*slot, {x, centre_down - row_height * 0.5, list_width, row_height}};
    }
    return std::nullopt;
}

void KickVoteMenuModel::open(ChangeTeamServerState roster) {
    *this = KickVoteMenuModel{};
    visible_ = true;
    set_roster(std::move(roster));
}

void KickVoteMenuModel::close() noexcept {
    visible_ = false;
    dropdown_open_ = false;
    highlight_.reset();
    selection_.reset();
    close_countdown_.reset();
    kick_pressed_ = false;
    reason_pressed_ = false;
}

void KickVoteMenuModel::set_roster(ChangeTeamServerState roster) {
    roster_ = std::move(roster);
    columns_ = kick_vote_roster(roster_);
}

ui::Rect KickVoteMenuModel::reason_row(std::size_t index) noexcept {
    // ListPanelBase.initialise_ui(x, y - 1, width, rows * 20): the list opens
    // one pixel below the title bar.
    return {reason_bar.x, reason_bar.y + reason_bar.height + 1 +
                              static_cast<int>(index) * reason_row_height,
            reason_bar.width, reason_row_height};
}

void KickVoteMenuModel::pointer_move(std::optional<ui::Point> point) {
    pointer_ = point;
    hovered_reason_.reset();
    if (!point.has_value()) {
        highlight_.reset();
        return;
    }
    if (dropdown_open_) {
        for (std::size_t index{}; index < 3U; ++index) {
            if (inside(*point, reason_row(index))) hovered_reason_ = index;
        }
    }
    highlight_ = kick_vote_roster_hit(columns_, *point);
}

void KickVoteMenuModel::pointer_press(std::optional<ui::Point> point) {
    kick_pressed_ = false;
    reason_pressed_ = false;
    if (!visible_ || !point.has_value()) return;
    // DropBoxControl.on_mouse_press: a press outside the open list closes it.
    bool over_dropdown = inside(*point, reason_bar);
    if (dropdown_open_) {
        for (std::size_t index{}; index < 3U; ++index) {
            over_dropdown = over_dropdown || inside(*point, reason_row(index));
        }
        if (!over_dropdown) dropdown_open_ = false;
    }
    if (inside(*point, reason_button)) reason_pressed_ = true;
    if (inside(*point, kick_button) && kick_enabled()) kick_pressed_ = true;
    // on_mouse_press: selection = get_hover_item(); calculate_options().
    if (!over_dropdown) {
        if (const auto hit = kick_vote_roster_hit(columns_, *point); hit.has_value()) {
            selection_ = hit;
        } else if (!inside(*point, kick_button)) {
            selection_.reset();
        }
    }
}

std::optional<KickVoteSelection>
KickVoteMenuModel::pointer_release(std::optional<ui::Point> point) {
    const bool kick = std::exchange(kick_pressed_, false);
    const bool reason_button_down = std::exchange(reason_pressed_, false);
    if (!visible_ || !point.has_value()) return std::nullopt;
    if (reason_button_down && inside(*point, reason_button)) {
        dropdown_open_ = !dropdown_open_;
        return std::nullopt;
    }
    if (dropdown_open_) {
        for (std::size_t index{}; index < 3U; ++index) {
            if (inside(*point, reason_row(index))) {
                reason_ = static_cast<KickVoteReason>(index);
                dropdown_open_ = false;
                return std::nullopt;
            }
        }
    }
    if (kick && inside(*point, kick_button) && selection_.has_value()) {
        return KickVoteSelection{selection_->row.player.player_id, reason_};
    }
    return std::nullopt;
}

void KickVoteMenuModel::notify_localised_message(std::string_view string_id) noexcept {
    if (!visible_) return;
    if (string_id == "KICK_DENIED_REASON_VOTE_TOO_SOON" ||
        string_id == "KICK_DENIED_REASON_VOTE_IN_PROGRESS" ||
        string_id == "KICK_DENIED_FOR_SPECTATOR" || string_id == "KICK_NOT_ENOUGH_PLAYERS") {
        close_countdown_ = close_after_denial_seconds;
    }
}

bool KickVoteMenuModel::tick(double elapsed_seconds) noexcept {
    if (!visible_ || !close_countdown_.has_value()) return false;
    *close_countdown_ -= std::max(0.0, elapsed_seconds);
    if (*close_countdown_ > 0.0) return false;
    close();
    return true;
}

ui::DrawList KickVoteMenuPresentation::build(const KickVoteMenuModel& model) const {
    ui::DrawList list;
    if (!model.visible()) return list;
    list.reserve(200U);
    // change_team_frame.blit(400, 300): the ChangeTeam content frame.
    list.push(sprite("png/ui/in_game_menus/change_team_content_frames.png",
                     {49.0, 62.0, 702.0, 476.0}));
    // title_font.draw(SELECT_PLAYER_TO_KICK.upper(), 400, 489, center=True).
    list.push(text("SELECT_PLAYER_TO_KICK", {100.0, 78.0, 600.0, 58.0}, 46.0,
                   ui::HorizontalTextAlignment::center, menu_color, "fonts/Spades.ttf",
                   ui::TextTransform::uppercase));
    append_retail_player_lists(list, model.roster(), 26.0, 263.0, true);

    const auto pointer = model.pointer();
    // DropBoxControl title bar: MenuOptionControl with a down_arrow SquareButton.
    const auto bar = draw_rect(KickVoteMenuModel::reason_bar);
    list.push(solid(bar, black));
    const double button_size = static_cast<double>(KickVoteMenuModel::reason_button.width);
    list.push(solid({bar.x + 4.0, bar.y + 4.0, bar.width - 8.0 - button_size - 2.0, button_size},
                    dropdown_grey));
    const bool button_hovered =
        pointer.has_value() && KickVoteMenuModel::reason_button.contains(*pointer);
    std::string square = "png/ui/common_elements/buttons/button_square";
    if (model.reason_button_pressed() && button_hovered) square += "_press";
    else if (button_hovered) square += "_hover";
    square += ".png";
    list.push(sprite(std::move(square), draw_rect(KickVoteMenuModel::reason_button)));
    list.push(sprite("png/ui/common_elements/scroll_bar/scroll_bar_arrow_down.png",
                     draw_rect(KickVoteMenuModel::reason_button)));
    list.push(text(std::string{retail_kick_reason_key(model.reason())},
                   {bar.x + 14.0, bar.y, bar.width - 28.0 - button_size - 2.0, bar.height}, 18.0,
                   ui::HorizontalTextAlignment::left, menu_color, "fonts/Spades.ttf",
                   ui::TextTransform::uppercase));

    // KICK PLAYER TextButton(334, 123, 130, 39): disabled until a row is picked.
    const auto kick = draw_rect(KickVoteMenuModel::kick_button);
    const bool kick_hovered =
        pointer.has_value() && KickVoteMenuModel::kick_button.contains(*pointer);
    const bool enabled = model.kick_enabled();
    const std::string state = !enabled ? ""
                              : model.kick_pressed() && kick_hovered ? "press_"
                              : kick_hovered                         ? "hover_"
                                                                     : "";
    const auto intensity = enabled ? std::uint16_t{1'000U} : std::uint16_t{700U};
    const double cap = 60.0 / 97.0 * kick.height + 1.0;
    const auto part = [&](std::string_view side) {
        return "png/ui/common_elements/buttons/button_large_" + state + std::string{side} + ".png";
    };
    list.push(sprite(part("left"), {kick.x, kick.y, cap, kick.height}, {}, intensity));
    list.push(sprite(part("mid"), {kick.x + cap, kick.y, kick.width - cap * 2.0, kick.height}, {},
                     intensity));
    list.push(sprite(part("right"), {kick.x + kick.width - cap, kick.y, cap, kick.height}, {},
                     intensity));
    list.push(text("KICK_PLAYER", {kick.x + 10.0, kick.y + 1.0, kick.width - 20.0, kick.height - 2.0},
                   28.0, ui::HorizontalTextAlignment::center, {20U, 20U, 20U, 255U},
                   "fonts/Spades.ttf", ui::TextTransform::uppercase, intensity));

    if (model.dropdown_open()) {
        const auto first = KickVoteMenuModel::reason_row(0U);
        list.push(solid({static_cast<double>(first.x), static_cast<double>(first.y),
                         static_cast<double>(first.width),
                         static_cast<double>(KickVoteMenuModel::reason_row_height) * 3.0},
                        black));
        for (std::size_t index{}; index < 3U; ++index) {
            const auto row = draw_rect(KickVoteMenuModel::reason_row(index));
            list.push(solid(row, index % 2U == 0U ? row_grey : row_dark));
            if (model.hovered_reason() == index ||
                static_cast<std::size_t>(model.reason()) == index) {
                list.push(sprite("png/ui/common_elements/buttons/highlight_line.png", row));
            }
            list.push(text(std::string{retail_kick_reason_key(static_cast<KickVoteReason>(index))},
                           {row.x + 14.0, row.y, row.width - 28.0, row.height}, 11.0,
                           ui::HorizontalTextAlignment::left, menu_color,
                           "fonts/A750-Sans-Medium.ttf", ui::TextTransform::preserve));
        }
    }

    // Hover then selection art, centred on the row (hover 313x16, highlight 321x25).
    if (const auto& hover = model.highlight(); hover.has_value()) {
        const double centre_x = hover->bounds.x + hover->bounds.width * 0.5;
        const double centre_y = hover->bounds.y + hover->bounds.height * 0.5;
        list.push(sprite("png/ui/common_elements/buttons/hover_scoreboard_" +
                             team_suffix(hover->row.team) + ".png",
                         {centre_x - 156.5, centre_y - 8.0, 313.0, 16.0}));
    }
    if (const auto& selected = model.selection(); selected.has_value()) {
        const double centre_x = selected->bounds.x + selected->bounds.width * 0.5;
        const double centre_y = selected->bounds.y + selected->bounds.height * 0.5;
        list.push(sprite("png/ui/common_elements/buttons/highlight_scoreboard_" +
                             team_suffix(selected->row.team) + ".png",
                         {centre_x - 160.5, centre_y - 12.5, 321.0, 25.0}));
    }
    return list;
}

} // namespace battlespades::frontend
