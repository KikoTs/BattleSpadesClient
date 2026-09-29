#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
 * Small, pure retail HUD rules shared by the frontend and its tests: the
 * disconnect-reason strings, radar detection, POIFocus look angles, score and
 * award localization identifiers, and the vote-kick initiation flow.
 */
namespace battlespades::frontend {

/**
 * Retail localization key for a server ENet DISCONNECT reason
 * (shared.constants.DISCONNECT), as GameManager.set_big_text_message maps
 * it; reasons without a branch read CONNECTION_CLOSED. Empty only for 18.
 *
 * Kick reasons 23-25 return the A958 template ("You have been kicked for {0}
 * until the end of the current match."); fill it with
 * retail_disconnect_kick_reason_key(). 18 (ERROR_MATCH_ENDED) is not a
 * failure: the caller reconnects for the next map.
 */
[[nodiscard]] std::string_view retail_disconnect_status_key(std::uint32_t reason) noexcept;

/** KICK_REASON_* key for disconnect reasons 23/24/25, else empty. */
[[nodiscard]] std::string_view retail_disconnect_kick_reason_key(std::uint32_t reason) noexcept;

/** Replace retail "{0}", "{1}", ... placeholders in order. */
[[nodiscard]] std::string format_retail_template(std::string_view text,
                                                 const std::vector<std::string>& values);

/**
 * RadarStationEntity.can_detect_player: 3D squared distance between the
 * station and the player's network position strictly below A1900² (250).
 */
inline constexpr double retail_radar_station_range{250.0};
[[nodiscard]] bool retail_radar_detects(double station_x, double station_y, double station_z,
                                        double player_x, double player_y,
                                        double player_z) noexcept;

/** Retail look angles in degrees: yaw 0 faces -x, positive pitch looks down. */
struct RetailLookAngles final {
    double yaw_degrees{};
    double pitch_degrees{};
};

/** Angles that aim from `from` at `to`; nullopt when the points coincide. */
[[nodiscard]] std::optional<RetailLookAngles>
retail_look_at_angles(double from_x, double from_y, double from_z, double to_x, double to_y,
                      double to_z) noexcept;

/**
 * shared.constants.SCORE_REASON_CODES localization identifier for a SetScore
 * reason ordinal. Profile totals and unknown ordinals return empty, matching
 * the HUD labels that are never shown.
 */
[[nodiscard]] std::string_view retail_score_reason_key(std::uint8_t reason) noexcept;

/** shared.constants.GAME_STAT_TYPES identifier for a GameStats(67) ordinal. */
[[nodiscard]] std::string_view retail_game_stat_award_key(std::int32_t stat_type) noexcept;

/** InitiateKickMessage(48) reason byte (server/voting.py KICK_*). */
enum class KickVoteReason : std::uint8_t {
    griefing = 0U,
    hacking = 1U,
    abuse = 2U,
    cancel = 3U,
};

/** KICK_REASON_* key for one wire reason; empty for cancel. */
[[nodiscard]] std::string_view retail_kick_reason_key(KickVoteReason reason) noexcept;

/** Client-side denials hud.pyd raises before any packet is sent. */
enum class KickVoteDenial : std::uint8_t {
    none,
    spectator,
    not_enough_players,
    vote_in_progress,
    vote_too_soon,
};

/** Retail string key for a denial; empty for none. */
[[nodiscard]] std::string_view retail_kick_denial_key(KickVoteDenial denial) noexcept;

/** Minimum team size named by KICK_NOT_ENOUGH_PLAYERS ("at least 3"). */
inline constexpr std::size_t retail_kick_minimum_team_players{3U};
/** server/voting.py VOTE_COOLDOWN between one player's kick votes. */
inline constexpr double retail_kick_vote_cooldown_seconds{60.0};

struct KickVoteContext final {
    bool local_is_spectator{};
    /** Players on the local team, the local player included. */
    std::size_t local_team_players{};
    bool vote_in_progress{};
    /** Seconds since this client last started a kick vote, if ever. */
    std::optional<double> seconds_since_last_start{};
};

[[nodiscard]] KickVoteDenial evaluate_kick_vote(const KickVoteContext& context) noexcept;

/** Seconds still to wait for VOTE_TOO_SOON's "{0}", rounded up, at least 1. */
[[nodiscard]] std::int32_t kick_vote_wait_seconds(double seconds_since_last_start) noexcept;

/** Wire bytes of InitiateKickMessage(48): id, player_id, target_id, reason. */
[[nodiscard]] std::array<std::byte, 4U> encode_initiate_kick(std::uint8_t player_id,
                                                              std::uint8_t target_id,
                                                              KickVoteReason reason) noexcept;

struct KickVoteCandidate final {
    std::uint8_t player_id{};
    std::string name;
};

struct KickVoteSelection final {
    std::uint8_t target_id{};
    KickVoteReason reason{KickVoteReason::griefing};
};

/**
 * KickVotePlayerSelect: SELECT_PLAYER_TO_KICK lists the other players, then
 * the three kick_reasons. Number keys pick a row directly; arrows + Enter move
 * and confirm; Escape backs out one step.
 */
class KickVoteSelectModel final {
public:
    enum class Stage : std::uint8_t { closed, player, reason };

    void open(std::vector<KickVoteCandidate> candidates);
    void close() noexcept;
    [[nodiscard]] bool visible() const noexcept { return stage_ != Stage::closed; }
    [[nodiscard]] Stage stage() const noexcept { return stage_; }
    [[nodiscard]] const std::vector<KickVoteCandidate>& candidates() const noexcept {
        return candidates_;
    }
    [[nodiscard]] std::size_t highlighted() const noexcept { return highlighted_; }
    [[nodiscard]] std::optional<std::uint8_t> chosen_target() const noexcept { return target_; }

    /** Rows shown in the current stage. */
    [[nodiscard]] std::size_t row_count() const noexcept;
    void move(int delta) noexcept;
    /** Choose row `index`; returns the finished selection after the reason. */
    [[nodiscard]] std::optional<KickVoteSelection> choose(std::size_t index);
    [[nodiscard]] std::optional<KickVoteSelection> confirm() { return choose(highlighted_); }
    /** Escape: reason -> player list -> closed. */
    void back() noexcept;

private:
    Stage stage_{Stage::closed};
    std::vector<KickVoteCandidate> candidates_;
    std::size_t highlighted_{};
    std::optional<std::uint8_t> target_;
};

/** Weapon.show_crosshair ordinals (shared/constants.py *_CROSSHAIR). */
inline constexpr int retail_never_crosshair{0};
inline constexpr int retail_zoomed_crosshair{1};
inline constexpr int retail_unzoomed_crosshair{2};
inline constexpr int retail_always_crosshair{3};
inline constexpr int retail_has_ammo_crosshair{4};

/**
 * GameScene.draw's reticle predicate (gameScene.pyd 0x10138b70, block
 * ~0x1013fbcd-0x10140408):
 *
 *     sc == ALWAYS or (sc == ZOOMED and zoom) or (sc == UNZOOMED and not zoom)
 *     or (sc == HAS_AMMO and weapon_object.get_has_enough_ammo())
 *
 * NEVER (bomb, diamond, intel, fake pistol, null tool) never draws. A tool
 * with no recovered ordinal inherits Tool.show_crosshair = HAS_AMMO.
 */
[[nodiscard]] bool retail_crosshair_visible(std::optional<int> show_crosshair, bool zoomed,
                                            bool has_enough_ammo) noexcept;


} // namespace battlespades::frontend
