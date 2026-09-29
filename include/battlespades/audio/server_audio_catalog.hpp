#pragma once

#include "battlespades/audio/audio_port.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace battlespades::audio {

/** Retail `shared.constants_audio.HEARING_DISTANCE`. */
inline constexpr float retail_hearing_distance{50.0F};

/**
 * Call-site tag for positional cues after the retail 50-block allocation gate.
 *
 * Retail mixes every world cue identically: `DEFAULT_ATTENUATION` rolloff,
 * reference distance 1 and no occlusion (the only audio raycast in retail is
 * the reverb probe). The tag keeps call sites readable, but both values
 * resolve to the same retail mix.
 */
enum class SpatialSoundProfile : std::uint8_t {
    ordinary,
    weapon_report,
};

/**
 * `Tool.play_sound` volume (tool.py:99-100): the local player's shot, loops
 * and tails play 2D at 0.5.
 */
[[nodiscard]] constexpr float local_weapon_report_gain() noexcept {
    return 0.5F;
}

/** Observers hear the same `Tool.play_sound` 0.5, attenuated by distance. */
[[nodiscard]] constexpr float remote_weapon_report_gain() noexcept {
    return 0.5F;
}

/** Retail `AL_ROLLOFF_FACTOR` = DEFAULT_ATTENUATION 0.15 for every world cue. */
[[nodiscard]] constexpr float
spatial_rolloff(SpatialSoundProfile /*profile*/) noexcept {
    return 0.15F;
}

/**
 * True when a positional cue is close enough for retail to allocate it.
 *
 * `MediaManager.play` performs this squared-distance rejection before creating
 * an audio player. OpenAL's `AL_MAX_DISTANCE` is not a substitute: the clamped
 * distance models retain a quiet but audible source forever beyond that point.
 */
[[nodiscard]] inline bool
within_retail_hearing_distance(SoundPosition listener,
                               SoundPosition source) noexcept {
    if (!std::isfinite(listener.x) || !std::isfinite(listener.y) ||
        !std::isfinite(listener.z) || !std::isfinite(source.x) ||
        !std::isfinite(source.y) || !std::isfinite(source.z)) {
        return false;
    }
    const float x = source.x - listener.x;
    const float y = source.y - listener.y;
    const float z = source.z - listener.z;
    return (x * x) + (y * y) + (z * z) <=
           retail_hearing_distance * retail_hearing_distance;
}

/**
 * Resolve Protocol 168 PlaySound(23)'s byte through retail `SOUND_MAP`.
 *
 * A numbered family is intentionally returned unexpanded; callers use
 * `sound_group_stems` and choose one member per playback, matching
 * `MediaManager.get_sound_name`. Empty entries are real retail holes and must
 * stay silent rather than indexing an unrelated asset.
 */
[[nodiscard]] inline std::string_view
server_sound_group(std::uint8_t sound_id) noexcept {
    constexpr std::array<std::string_view, 61U> groups{{
        "snowcan_impact",
        "snowcan_build",
        "event_positive",
        "event_negative",
        "diamond_appear",
        "diamond_disappear",
        "diamond_dropinbase",
        "VIP_yoursisdead",
        "VIP_killedtheirs",
        "airstrike_siren_oneshot",
        "airstrike_flyby",
        "airstrike_flyby_space",
        "classic_pickup",
        "crate",
        "healthcrate",
        "crate_blocks",
        "bomb_pickup",
        "bomb_explode_water",
        "bomb_explode",
        "diamond_pickup",
        "flag_returned",
        "dynamite_place",
        "bomb_drop",
        "diamond_drop",
        "cratedrop_flyby_pos_ww",
        "cratedrop_flyby_pos",
        "cratedrop_flyby_space_pos",
        "tutorial_complete",
        "zombie_become",
        "zombie_timer_countdown",
        "turret_place",
        "landmine_place",
        "prefabbuild",
        "hitground",
        "hitground_crowbar_damage",
        "hitground_knife_damage",
        "hitground_pickaxe",
        "hitground_super",
        "hitground_zombie",
        "hitwater",
        "hitwater_super",
        "hitwater_crowbar",
        "hitwater_knife",
        "hitwater_pickaxe",
        "hitwater_zomb",
        "ugc_place",
        "build",
        "ugc_colour_singleblock_001-003",
        "hitground_riotstick_damage",
        {},
        {},
        {},
        "AoS_soundfx_PLAYER_medic_item_medi_pack_exhausted_001",
        "AoS_soundfx_PLAYER_medic_item_medi_pack_place_001-003",
        "hitground_riotshield_damage",
        {},
        "AoS_soundfx_PLAYER_marksman_item_RADAR_place_001",
        "AoS_soundfx_PLAYER_miner_wpn_C4_place_001-003",
        "AoS_soundfx_PLAYER_miner_wpn_C4_detonate_001",
        "AoS_soundfx_PLAYER_engineer_wpn_mine_launcher_attach_mine_001",
        "AoS_soundfx_PLAYER_engineer__wpn_mine_launcher_attach_mine_water_001-003",
    }};
    return sound_id < groups.size() ? groups[sound_id] : std::string_view{};
}

} // namespace battlespades::audio
