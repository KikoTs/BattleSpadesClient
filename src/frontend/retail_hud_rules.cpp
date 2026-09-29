#include "battlespades/frontend/retail_hud_rules.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace battlespades::frontend {

std::string_view retail_disconnect_status_key(std::uint32_t reason) noexcept {
    // shared.constants.DISCONNECT ordinals. Every identifier except the
    // A-numbered ones is referenced by stock gamemanager.pyd; A940/A953/A954/
    // A955/A958/A961 are the obfuscated ERROR_BANNED/DLC_LOCKED/CONTENT_LOCKED/
    // AFK_TIMEOUT/TEMP_BANNED/HOST_HAS_LEFT strings (constants.py:3459-3481,
    // english.py). The 23-25 template choice is best evidence: gamemanager
    // reads A962-A964 but english.py has no strings of that name, and A958
    // is the only kick template with a reason slot (VERIFY in IDA).
    switch (reason) {
    case 1U: return "A940";                        // ERROR_BANNED
    case 2U: return "KICKED_ERROR";                // ERROR_KICKED
    case 3U: return "SERVER_OUT_OF_DATE";          // ERROR_SERVER_OUT_OF_DATE
    case 4U: return "SERVERFULL_ERROR";            // ERROR_FULL
    case 5U: return "NO_STEAM_CONNECTION";         // ERROR_NOSTEAM
    case 6U: return "NO_VAC_CONNECTION";           // ERROR_NOVAC
    case 7U: return "NO_VALID_LICENSE";            // ERROR_NOLICENSE
    case 8U: return "INVALID_SESSION_TICKET";      // ERROR_NOTICKET
    case 9U: return "YOU_HAVE_BEEN_VAC_BANNED";    // ERROR_VACBANNED
    case 10U: return "CLIENT_OUT_OF_DATE";         // ERROR_CLIENT_OUT_OF_DATE
    // gamemanager.pyd set_big_text_message (0x10018810): a connect timeout
    // reads "Unable to connect to server", not ERROR_TIMEOUT.
    case 11U: return "UNABLE_TO_CONNECT_TO_SERVER"; // ERROR_TIMEOUT
    case 12U: return "RANKED_CONNECTION";          // ERROR_RANKED_SERVER
    case 13U: return "INVALID_DATA";               // ERROR_DATA
    case 14U: return "A953";                       // ERROR_DLC_LOCKED
    case 15U: return "A954";                       // ERROR_CONTENT_LOCKED
    case 16U: return "A955";                       // ERROR_AFK_TIMEOUT
    case 17U: return "CUSTOM_CONNECTION";          // ERROR_CUSTOM_SERVER
    // Retail shows strings.A958 UNFORMATTED for a temp ban (the literal
    // "{0}" stays in the text: a stock bug kept for parity).
    case 19U: return "A958";                       // ERROR_TEMP_BANNED
    // INVALID_DEMO_CONTENT only when SteamIsDemoRunning(); there is no demo.
    case 21U: return "INVALID_SESSION_TICKET";     // ERROR_DEMO_VERSION
    case 22U: return "A961";                       // ERROR_HOST_HAS_LEFT
    case 23U: case 24U: case 25U: return "A958";   // ERROR_KICK_*
    case 26U: return "LOBBY_ERROR_CLOSED";
    case 27U: return "LOBBY_ERROR_LOBBY_FULL";
    case 28U: return "LOBBY_ERROR_UNKNOWN";
    case 29U: return "LOBBY_ERROR_SERVER_CONNECTION_FAILED";
    case 30U: return "UGC_LOBBY_PUBLISHING_ERROR";
    // 18 MATCH_ENDED is the rotation reconnect, never a failure string.
    case 18U: return {};
    // set_big_text_message has no branch for 0 UNDEFINED, 20
    // CONTROL_ALREADY_BOUND (A959 needs key-binding arguments a disconnect
    // lacks), 31 UNKNOWN or anything newer: strings.CONNECTION_CLOSED.
    default: return "CONNECTION_CLOSED";
    }
}

std::string_view retail_disconnect_kick_reason_key(std::uint32_t reason) noexcept {
    switch (reason) {
    case 23U: return "KICK_REASON_GRIEFING";
    case 24U: return "KICK_REASON_HACKING";
    case 25U: return "KICK_REASON_ABUSE";
    default: return {};
    }
}

std::string format_retail_template(std::string_view text, const std::vector<std::string>& values) {
    std::string result{text};
    for (std::size_t index{}; index < values.size(); ++index) {
        const auto token = "{" + std::to_string(index) + "}";
        for (auto at = result.find(token); at != std::string::npos;
             at = result.find(token, at + values[index].size())) {
            result.replace(at, token.size(), values[index]);
        }
    }
    return result;
}

bool retail_radar_detects(double station_x, double station_y, double station_z, double player_x,
                          double player_y, double player_z) noexcept {
    const auto dx = player_x - station_x;
    const auto dy = player_y - station_y;
    const auto dz = player_z - station_z;
    const auto squared = dx * dx + dy * dy + dz * dz;
    return std::isfinite(squared) &&
           squared < retail_radar_station_range * retail_radar_station_range;
}

std::optional<RetailLookAngles> retail_look_at_angles(double from_x, double from_y, double from_z,
                                                      double to_x, double to_y,
                                                      double to_z) noexcept {
    const auto dx = to_x - from_x;
    const auto dy = to_y - from_y;
    const auto dz = to_z - from_z;
    const auto horizontal = std::hypot(dx, dy);
    if (!std::isfinite(horizontal) || !std::isfinite(dz) ||
        (horizontal < 1.0e-9 && std::abs(dz) < 1.0e-9)) {
        return std::nullopt;
    }
    constexpr double degrees{180.0 / std::numbers::pi};
    // Inverse of TutorialWorldSession's orientation convention: yaw 0 looks
    // along -x, and map z grows downward so a lower target has positive pitch.
    return RetailLookAngles{std::atan2(-dy, -dx) * degrees, std::atan2(dz, horizontal) * degrees};
}

std::string_view retail_score_reason_key(std::uint8_t reason) noexcept {
    // shared.constants.SCORE_REASON_CODES for the ordinals the HUD shows.
    // Profile totals (35, 47, 48, 63-68, 73-75, 83+) and the empty
    // TEABAG_SCORE_REASON (221) have no popup.
    static constexpr std::array<std::string_view, 83U> keys{
        "",                     "TDM_Kill",           "TDM_Suicide",
        "TDM_Headshot",         "TDM_Melee",          "TDM_Assist",
        "TDM_TeamKill",         "TDM_Revenge",        "TDM_Distract",
        "TDM_Payback",          "TDM_Reload",         "TDM_Defend",
        "VIP_Survive",          "VIP_Escort",         "VIP_KillEnemyVIP",
        "VIP_Distract",         "VIP_Kill",           "VIP_Assault",
        "VIP_Assault_Enemy",    "VIP_Defend",         "TC_Occupy",
        "TC_Claim",             "TC_Control",         "TC_Defend",
        "TC_Assault",           "TC_Contend",         "OCC_Occupy",
        "OCC_Carry",            "OCC_Boom",           "OCC_Distract",
        "OCC_Carrier_Defend",   "OCC_Defend",         "OCC_Assault",
        "OCC_Survive",          "OCC_Intercept",      "",
        "Occ_Disposal",         "Occ_Intercept_Disposal", "DIA_Capture",
        "DIA_Uncover",          "DIA_Carry",          "DIA_Escort",
        "DIA_Distract",         "DIA_Carrier_Defend", "DIA_Defend",
        "DIA_Assault",          "DIA_Intercept",      "",
        "",                     "CTF_Capture",        "CTF_Carry",
        "CTF_Escort",           "CTF_Claim",          "CTF_Distract",
        "CTF_Defend",           "CTF_Assault",        "CTF_Assault_Enemy",
        "CTF_Carrier_Defend",   "CTF_Intercept",      "ZOM_Survive",
        "ZOM_LastMan",          "ZOM_KillSurvivor",   "ZOM_LastManZombieKill",
        "",                     "",                   "",
        "",                     "",                   "",
        "DEM_Destroy",          "DEM_Repair",         "DEM_Defend",
        "DEM_Assault",          "",                   "",
        "",                     "MH_Occupy",          "MH_First",
        "MH_Claim",             "MH_Control",         "MH_Defend",
        "MH_Assault",           "MH_Contest",
    };
    return reason < keys.size() ? keys[reason] : std::string_view{};
}

std::string_view retail_game_stat_award_key(std::int32_t stat_type) noexcept {
    static constexpr std::array<std::string_view, 30U> keys{
        "MOST_DistanceTravelled",  "MOST_TimeInAir",         "MOST_HealthCratesCollected",
        "MOST_AmmoCratesCollected", "MOST_BlockCratesCollected", "MOST_Kills",
        "MOST_KillsAtLowHealth",   "MOST_Teabags",           "MOST_Headshots",
        "MOST_BlocksPlaced",       "MOST_BlocksDestroyed",   "BIGGEST_KillStreak",
        "BIGGEST_CollapsingObject", "BIGGEST_RangedKill",    "MOST_MeleeKills",
        "MOST_BrainsEaten",        "MOST_Distractions",      "MOST_Defends",
        "MOST_Assists",            "MOST_AirStrikesSurvived", "MOST_DamageTaken",
        "HIGHEST_Block",           "MOST_HeadshotsReceived", "MOST_SnipersKilled",
        "FEWEST_ShotsFired",       "MOST_Suicides",          "MOST_KillSteals",
        "MOST_TimeOnFire",         "MOST_Dominated",         "MOST_Dominations",
    };
    if (stat_type < 0 || static_cast<std::size_t>(stat_type) >= keys.size()) return {};
    return keys[static_cast<std::size_t>(stat_type)];
}

std::string_view retail_kick_reason_key(KickVoteReason reason) noexcept {
    switch (reason) {
    case KickVoteReason::griefing: return "KICK_REASON_GRIEFING";
    case KickVoteReason::hacking: return "KICK_REASON_HACKING";
    case KickVoteReason::abuse: return "KICK_REASON_ABUSE";
    case KickVoteReason::cancel: break;
    }
    return {};
}

std::string_view retail_kick_denial_key(KickVoteDenial denial) noexcept {
    switch (denial) {
    case KickVoteDenial::spectator: return "KICK_DENIED_FOR_SPECTATOR";
    case KickVoteDenial::not_enough_players: return "KICK_NOT_ENOUGH_PLAYERS";
    case KickVoteDenial::vote_in_progress: return "KICK_DENIED_REASON_VOTE_IN_PROGRESS";
    case KickVoteDenial::vote_too_soon: return "KICK_DENIED_REASON_VOTE_TOO_SOON";
    case KickVoteDenial::none: break;
    }
    return {};
}

KickVoteDenial evaluate_kick_vote(const KickVoteContext& context) noexcept {
    // Order follows the strongest condition first: a spectator never reaches
    // the team-size test, and a running ballot outranks the cooldown.
    if (context.local_is_spectator) return KickVoteDenial::spectator;
    if (context.local_team_players < retail_kick_minimum_team_players)
        return KickVoteDenial::not_enough_players;
    if (context.vote_in_progress) return KickVoteDenial::vote_in_progress;
    if (context.seconds_since_last_start.has_value() &&
        *context.seconds_since_last_start < retail_kick_vote_cooldown_seconds)
        return KickVoteDenial::vote_too_soon;
    return KickVoteDenial::none;
}

std::int32_t kick_vote_wait_seconds(double seconds_since_last_start) noexcept {
    const auto remaining = retail_kick_vote_cooldown_seconds -
                           (std::isfinite(seconds_since_last_start) ? seconds_since_last_start : 0.0);
    return std::max(1, static_cast<std::int32_t>(std::ceil(remaining)));
}

std::array<std::byte, 4U> encode_initiate_kick(std::uint8_t player_id, std::uint8_t target_id,
                                               KickVoteReason reason) noexcept {
    return {std::byte{48U}, static_cast<std::byte>(player_id), static_cast<std::byte>(target_id),
            static_cast<std::byte>(reason)};
}

void KickVoteSelectModel::open(std::vector<KickVoteCandidate> candidates) {
    candidates_ = std::move(candidates);
    highlighted_ = 0U;
    target_.reset();
    stage_ = Stage::player;
}

void KickVoteSelectModel::close() noexcept {
    stage_ = Stage::closed;
    candidates_.clear();
    highlighted_ = 0U;
    target_.reset();
}

std::size_t KickVoteSelectModel::row_count() const noexcept {
    switch (stage_) {
    case Stage::player: return candidates_.size();
    case Stage::reason: return 3U;
    case Stage::closed: break;
    }
    return 0U;
}

void KickVoteSelectModel::move(int delta) noexcept {
    const auto rows = row_count();
    if (rows == 0U) return;
    const auto size = static_cast<long long>(rows);
    const auto next = ((static_cast<long long>(highlighted_) + delta) % size + size) % size;
    highlighted_ = static_cast<std::size_t>(next);
}

std::optional<KickVoteSelection> KickVoteSelectModel::choose(std::size_t index) {
    if (index >= row_count()) return std::nullopt;
    if (stage_ == Stage::player) {
        target_ = candidates_[index].player_id;
        stage_ = Stage::reason;
        highlighted_ = 0U;
        return std::nullopt;
    }
    if (stage_ == Stage::reason && target_.has_value()) {
        const KickVoteSelection selection{*target_, static_cast<KickVoteReason>(index)};
        close();
        return selection;
    }
    return std::nullopt;
}

void KickVoteSelectModel::back() noexcept {
    if (stage_ == Stage::reason) {
        stage_ = Stage::player;
        target_.reset();
        highlighted_ = 0U;
        return;
    }
    close();
}

bool retail_crosshair_visible(std::optional<int> show_crosshair, bool zoomed,
                              bool has_enough_ammo) noexcept {
    switch (show_crosshair.value_or(retail_has_ammo_crosshair)) {
    case retail_always_crosshair:
        return true;
    case retail_zoomed_crosshair:
        return zoomed;
    case retail_unzoomed_crosshair:
        return !zoomed;
    case retail_has_ammo_crosshair:
        return has_enough_ammo;
    default:
        return false;
    }
}

} // namespace battlespades::frontend
