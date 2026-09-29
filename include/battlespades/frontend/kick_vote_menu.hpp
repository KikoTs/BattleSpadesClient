#pragma once

#include "battlespades/frontend/change_team_menu.hpp"
#include "battlespades/frontend/retail_hud_rules.hpp"
#include "battlespades/ui/draw_list.hpp"
#include "battlespades/ui/geometry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace battlespades::frontend {

/** One roster row KickVotePlayerSelect can highlight. */
struct KickVoteRosterRow final {
    TeamRosterPlayer player;
    /** Wire team of the row's player (0 spectator, 2 team1, 3 team2). */
    std::uint8_t team{};
};

/** Both columns as draw_player_list lays them out (score-sorted + extras). */
using KickVoteRosterColumns = std::array<std::array<std::optional<KickVoteRosterRow>, 16U>, 2U>;

/**
 * Sorted team rows, spectators (and a full team's overflow) in the spare
 * bottom rows exactly as ViewScores.update / KickVotePlayerSelect.update.
 */
[[nodiscard]] KickVoteRosterColumns kick_vote_roster(const ChangeTeamServerState& state);

/** A roster hit: the player and the retail row rectangle (top-left pixels). */
struct KickVoteRosterHit final {
    KickVoteRosterRow row;
    ui::DrawRect bounds{};
};

/**
 * get_highlighted_team_member for both lists: x 77 / 412, width 313, list y
 * 443 (y-up) with height 263 → 16 rows of 263/16 px under a header row.
 */
[[nodiscard]] std::optional<KickVoteRosterHit>
kick_vote_roster_hit(const KickVoteRosterColumns& columns, ui::Point point) noexcept;

/**
 * Retail KickVotePlayerSelect (hud.pyd, IDA): a ChangeTeam-style screen with
 * both rosters, a reason DropBoxControl and a KICK PLAYER button that stays
 * disabled until a row is selected. There are no client-side denials: the
 * server answers with LocalisedMessage(50) and the menu closes 0.5 s later.
 */
class KickVoteMenuModel final {
public:
    /** TIME_TO_SHOW_MENU_IF_CANCELLED (inithud float constant). */
    static constexpr double close_after_denial_seconds{0.5};
    static constexpr ui::Rect reason_bar{84, 481, 200, 30};
    static constexpr ui::Rect reason_button{258, 485, 22, 22};
    static constexpr int reason_row_height{20};
    static constexpr ui::Rect kick_button{334, 477, 130, 39};

    void open(ChangeTeamServerState roster);
    void close() noexcept;
    [[nodiscard]] bool visible() const noexcept { return visible_; }
    /** update(): the rosters are re-sorted from the live teams every frame. */
    void set_roster(ChangeTeamServerState roster);
    [[nodiscard]] const ChangeTeamServerState& roster() const noexcept { return roster_; }
    [[nodiscard]] const KickVoteRosterColumns& columns() const noexcept { return columns_; }

    void pointer_move(std::optional<ui::Point> point);
    void pointer_press(std::optional<ui::Point> point);
    /** Returns the vote to send when KICK PLAYER is released while enabled. */
    [[nodiscard]] std::optional<KickVoteSelection> pointer_release(std::optional<ui::Point> point);

    /** LocalisedMessage(50): the four KICK_DENIED ids start the close countdown. */
    void notify_localised_message(std::string_view string_id) noexcept;
    /** Advance the denial countdown; true when the menu closed this tick. */
    bool tick(double elapsed_seconds) noexcept;

    [[nodiscard]] const std::optional<KickVoteRosterHit>& highlight() const noexcept {
        return highlight_;
    }
    [[nodiscard]] const std::optional<KickVoteRosterHit>& selection() const noexcept {
        return selection_;
    }
    [[nodiscard]] bool kick_enabled() const noexcept { return selection_.has_value(); }
    [[nodiscard]] KickVoteReason reason() const noexcept { return reason_; }
    [[nodiscard]] bool dropdown_open() const noexcept { return dropdown_open_; }
    [[nodiscard]] std::optional<std::size_t> hovered_reason() const noexcept {
        return hovered_reason_;
    }
    [[nodiscard]] std::optional<ui::Point> pointer() const noexcept { return pointer_; }
    [[nodiscard]] bool kick_pressed() const noexcept { return kick_pressed_; }
    [[nodiscard]] bool reason_button_pressed() const noexcept { return reason_pressed_; }
    [[nodiscard]] std::optional<double> close_countdown() const noexcept {
        return close_countdown_;
    }
    /** Rows in the opened reason list: griefing, hacking, abuse. */
    [[nodiscard]] static ui::Rect reason_row(std::size_t index) noexcept;

private:
    ChangeTeamServerState roster_;
    KickVoteRosterColumns columns_{};
    std::optional<KickVoteRosterHit> highlight_;
    std::optional<KickVoteRosterHit> selection_;
    std::optional<ui::Point> pointer_;
    std::optional<std::size_t> hovered_reason_;
    std::optional<double> close_countdown_;
    KickVoteReason reason_{KickVoteReason::griefing};
    bool visible_{};
    bool dropdown_open_{};
    bool kick_pressed_{};
    bool reason_pressed_{};
};

class KickVoteMenuPresentation final {
public:
    [[nodiscard]] ui::DrawList build(const KickVoteMenuModel& model) const;
};

} // namespace battlespades::frontend
