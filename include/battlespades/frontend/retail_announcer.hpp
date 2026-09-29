#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::frontend {

/**
 * Retail "announcer" presentation decisions.
 *
 * AoS 1.x shipped no voiced announcer bank. What players hear as the
 * announcer is a handful of 2D stingers and big-message banners raised by
 * the client from packets it already has:
 *
 * - `GameScene.process_packet_kill_action` (gameScene.pyd 0x10194940,
 *   BattleSpades `docs/KILLFEED_RETAIL.md`): multikill banners and the
 *   domination / revenge banners with their `ks_*` stingers.
 * - `SelectTeam.on_start` (`aoslib/scenes/ingame_menus/selectTeam.py:18-27`):
 *   the match-start stinger.
 *
 * The functions here are pure so the retail branch structure can be pinned
 * by unit tests; the frontend only localises and plays what they return.
 */

/** One big message (and optional stinger) raised by a KillAction. */
struct KillAnnouncement final {
    /** Localisation id (KILL2..KILL5, KILLM, YOU_/THEY_ARE_DOMINATING, ...). */
    std::string_view string_id;
    /** Replaces `{0}` in the localised template; empty when unused. */
    std::string parameter;
    /** Sound stem played 2D with the banner; empty for none. */
    std::string_view stinger;
};

struct KillAnnouncementInput final {
    bool local_is_killer{};
    bool local_is_victim{};
    /** A suicide never takes the local-kill branch (line 3688). */
    bool killer_is_victim{};
    /** False when the killer is absent from the roster (`if killer:`). */
    bool killer_known{};
    std::uint8_t kill_count{};
    bool domination{};
    bool revenge{};
    std::string victim_name;
    std::string killer_name;
};

/** Retail stinger stems, lengths measured from the shipped Ogg files. */
inline constexpr std::string_view domination_stinger{"ks_domination"}; // 1.84 s
inline constexpr std::string_view revenge_stinger{"ks_revenge"};       // 2.03 s

/**
 * Banners in retail order: revenge, then domination, then (local killer
 * only) the multikill line. The server owns kill_count and both flags; the
 * client decides nothing about them.
 *
 * Both stingers are interned exactly once in gameScene.pyd and the handler
 * calls a sound after each of the four relationship banners (KILLFEED_RETAIL
 * decompile), so the victim hears the same stinger as the killer. The
 * multikill banners carry no sound.
 */
[[nodiscard]] inline std::vector<KillAnnouncement>
kill_action_announcements(const KillAnnouncementInput& input) {
    std::vector<KillAnnouncement> result;
    if (!input.killer_known) {
        return result;
    }
    if (input.local_is_killer && !input.killer_is_victim) {
        if (input.revenge) {
            result.push_back({"YOU_GOT_REVENGE", input.victim_name, revenge_stinger});
        }
        if (input.domination) {
            result.push_back({"YOU_ARE_DOMINATING", input.victim_name, domination_stinger});
        }
        switch (input.kill_count) {
        case 0U:
        case 1U:
            break;
        case 2U:
            result.push_back({"KILL2", {}, {}});
            break;
        case 3U:
            result.push_back({"KILL3", {}, {}});
            break;
        case 4U:
            result.push_back({"KILL4", {}, {}});
            break;
        case 5U:
            result.push_back({"KILL5", {}, {}});
            break;
        default:
            result.push_back({"KILLM", std::to_string(input.kill_count), {}});
            break;
        }
    } else if (input.local_is_victim) {
        if (input.revenge) {
            result.push_back({"THEY_GOT_REVENGE", input.killer_name, revenge_stinger});
        }
        if (input.domination) {
            result.push_back({"THEY_ARE_DOMINATING", input.killer_name, domination_stinger});
        }
    }
    return result;
}

/** Replace every `{0}` in a localised template. */
[[nodiscard]] inline std::string format_announcement(std::string text,
                                                     std::string_view parameter) {
    constexpr std::string_view marker{"{0}"};
    std::size_t offset{};
    while ((offset = text.find(marker, offset)) != std::string::npos) {
        text.replace(offset, marker.size(), parameter);
        offset += parameter.size();
    }
    return text;
}

/**
 * `running_local_player_kills`: how many times each player has killed the
 * local player since the local player last killed them. Retail feeds it to
 * the death camera's set_killer_info (DEATHCAM_STREAK_FOR_ORIENTATION = 2,
 * DEATHCAM_STREAK_FOR_POSITION = 3).
 */
class RunningLocalKills final {
public:
    /** Apply one KillAction; returns the killer's streak on a local death. */
    std::uint16_t apply(const KillAnnouncementInput& input,
                        std::uint8_t victim_id,
                        std::uint8_t killer_id) noexcept {
        if (!input.killer_known) {
            return 0U;
        }
        if (input.local_is_killer && !input.killer_is_victim) {
            counts_[victim_id] = 0U;
            return 0U;
        }
        if (input.local_is_victim) {
            auto& count = counts_[killer_id];
            if (count != std::numeric_limits<std::uint16_t>::max()) {
                ++count;
            }
            return count;
        }
        return 0U;
    }
    /** A player id is reused only after PlayerLeft. */
    void forget(std::uint8_t player_id) noexcept { counts_[player_id] = 0U; }
    void clear() noexcept { counts_ = {}; }
    [[nodiscard]] std::uint16_t count(std::uint8_t player_id) const noexcept {
        return counts_[player_id];
    }

private:
    std::array<std::uint16_t, 256U> counts_{};
};

/** What `SelectTeam.on_start` plays when the team menu opens. */
struct MatchStartStinger final {
    std::string_view stem;
    /** Non-classic modes then (re)start `secondary_menu_bed_001`. */
    bool start_secondary_bed{};
};

/**
 * `selectTeam.py:21-27`. The stinger plays in MUSIC_AUDIO_ZONE, so its
 * volume is scaled by the music slider; classic mode has no bed.
 */
[[nodiscard]] constexpr MatchStartStinger match_start_stinger(bool classic) noexcept {
    return classic ? MatchStartStinger{"classic_mu_start_game", false}
                   : MatchStartStinger{"mu_start_game", true};
}

/**
 * Fires once per SelectTeam.on_start: on every transition INTO the team
 * menu, not while it stays open. Retail re-enters the scene when the player
 * backs out of class selection, so the stinger replays then as well.
 */
class ScreenEntryEdge final {
public:
    /** Returns true on the frame `active` first becomes true. */
    bool update(bool active) noexcept {
        const bool entered = active && !was_active_;
        was_active_ = active;
        return entered;
    }
    void reset() noexcept { was_active_ = false; }

private:
    bool was_active_{};
};

/**
 * The match-start stinger belongs to SelectTeam.on_start only: the team
 * pick after joining a match (selectTeam.py:18-27), including re-entry when
 * the player backs out of the initial class pick. The in-game ChangeTeam
 * menu (`.` or the pause menu, changeTeam.py) plays no stinger, so once the
 * local player has been in the world the gate stays shut until the next
 * match (new InitialInfo) calls `new_match`.
 */
class MatchStartStingerGate final {
public:
    /**
     * `team_menu_active`: the team menu is on screen this frame.
     * `in_world`: the local player is in the playable world this frame.
     * Returns true on the frame the stinger must play.
     */
    bool update(bool team_menu_active, bool in_world) noexcept {
        if (in_world) {
            entered_world_ = true;
        }
        const bool entered = entry_.update(team_menu_active);
        return entered && !entered_world_;
    }
    void new_match() noexcept {
        entry_.reset();
        entered_world_ = false;
    }

private:
    ScreenEntryEdge entry_;
    bool entered_world_{};
};

} // namespace battlespades::frontend
