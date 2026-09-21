#pragma once

#include "battlespades/ui/draw_list.hpp"
#include "battlespades/ui/geometry.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace battlespades::frontend {

enum class ChangeTeamAction : std::uint8_t {
    team1 = 2U,
    team2 = 3U,
    spectator = 0U,
    back = 255U,
};

/** One row in the stock two-column team roster. */
struct TeamRosterPlayer final {
    std::string name;
    std::int32_t score{};
    std::uint16_t ping{};
    std::uint8_t class_id{};
    bool dead{};
    /** Stable tie-breaker used by retail's score-sorted roster. */
    std::uint8_t player_id{};
    /** CreatePlayer.demo_player selects the amber scoreboard text branch. */
    bool demo_player{};
    /** Retail replaces the class icon with its crown for VIP/reveal state. */
    bool high_minimap_visibility{};
    /** CreatePlayer language selects Tuffy for Russian, Polish and Turkish. */
    std::uint8_t local_language{};
    /** KillAction relationship markers relative to the local player. */
    bool dominating_local_player{};
    bool dominated_by_local_player{};
    /** Steam-lobby owner marker; local hosted matches map this to the host. */
    bool lobby_host{};
};

/** Live roster and lock state consumed by retail's ChangeTeam screen. */
struct ChangeTeamServerState final {
    std::string team1_name{"Blue Team"};
    std::string team2_name{"Green Team"};
    std::array<std::uint8_t, 3U> team1_color{44U, 117U, 179U};
    std::array<std::uint8_t, 3U> team2_color{92U, 174U, 74U};
    std::vector<TeamRosterPlayer> team1_players;
    std::vector<TeamRosterPlayer> team2_players;
    /** ViewScores uses spare bottom rows for spectators, in neutral grey. */
    std::vector<TeamRosterPlayer> spectator_players;
    std::int32_t team1_score{};
    std::int32_t team2_score{};
    std::int32_t score_limit{};
    bool team1_show_score{true};
    bool team2_show_score{true};
    bool team1_show_max_score{true};
    bool team2_show_max_score{true};
    std::uint8_t current_team{};
    std::optional<std::uint8_t> forced_team;
    bool team1_locked{};
    bool team2_locked{};
    bool spectator_enabled{};
    bool spectator_locked{};
    bool lock_team_swap{};
    bool lock_spectator_swap{};
    /** SelectTeam uses the full 750x589 join frame, not ChangeTeam's pause frame. */
    bool initial_join{};
};

/**
 * Exact in-match team-choice interaction boundary. The model never changes a
 * roster itself; it emits one wire-team choice and waits for ChangeTeam(77)
 * lifecycle packets to replace the local player.
 */
class ChangeTeamMenuModel final {
public:
    void configure(ChangeTeamServerState state);
    void pointer_move(std::optional<ui::Point> point) noexcept;
    void pointer_press(std::optional<ui::Point> point) noexcept;
    [[nodiscard]] std::optional<ChangeTeamAction>
    pointer_release(std::optional<ui::Point> point) noexcept;

    [[nodiscard]] const ChangeTeamServerState& state() const noexcept {
        return state_;
    }
    [[nodiscard]] std::optional<ChangeTeamAction> hovered() const noexcept {
        return hovered_;
    }
    [[nodiscard]] std::optional<ChangeTeamAction> pressed() const noexcept {
        return pressed_;
    }
    [[nodiscard]] bool visible(ChangeTeamAction action) const noexcept;
    [[nodiscard]] bool enabled(ChangeTeamAction action) const noexcept;
    [[nodiscard]] static ui::Rect bounds(ChangeTeamAction action,
                                         bool initial_join = false) noexcept;

private:
    [[nodiscard]] std::optional<ChangeTeamAction>
    hit_test(std::optional<ui::Point> point) const noexcept;

    ChangeTeamServerState state_{};
    std::optional<ChangeTeamAction> hovered_;
    std::optional<ChangeTeamAction> pressed_;
};

class ChangeTeamPresentation final {
public:
    [[nodiscard]] ui::DrawList build(const ChangeTeamMenuModel& model,
                                     ui::PixelExtent window) const;
};

/**
 * Resolve retail ViewScores/ViewGameStats' authored end-of-round message.
 *
 * `message_id` is ShowTextMessage(73)'s selector. `winner_team` is the
 * authoritative score winner retained by ViewGameStats; when absent, the
 * current team scores provide the same fallback used by ViewScores.
 */
[[nodiscard]] std::string retail_match_result_message(
    const ChangeTeamServerState& state,
    std::optional<std::uint8_t> message_id,
    std::int32_t winner_team = 0);

/** Hold-TAB overlay recovered from ViewScores and draw_player_list. */
class ScoreboardPresentation final {
public:
    [[nodiscard]] ui::DrawList build(const ChangeTeamServerState& state,
                                     std::string mode_title,
                                     ui::PixelExtent window,
                                     std::optional<std::uint8_t> message_id =
                                         std::nullopt,
                                     std::int32_t winner_team = 0) const;
};

} // namespace battlespades::frontend
