#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace battlespades::audio {

/** One retail impact bank and its Character.play_sound semitone bounds. */
struct ExplosionSoundDefinition final {
    std::string_view group;
    std::array<float, 2U> pitch_semitones{};
};

/** Resolve the authored dry/water impact bank for one tool. */
[[nodiscard]] constexpr ExplosionSoundDefinition
retail_explosion_sound(std::uint8_t tool_id, bool submerged) noexcept {
    constexpr std::array<float, 2U> classic_pitch{-0.8F, 0.8F};
    constexpr std::array<float, 2U> fixed_pitch{0.0F, 0.0F};
    switch (tool_id) {
    case 12U:
        return {submerged ? "rocketexplode_water" : "rocketexplode", classic_pitch};
    case 13U:
    case 46U:
        return {submerged ? "rocket_trip_explode_water" : "rocket_trip_explode",
                classic_pitch};
    case 14U:
    case 47U:
        return {submerged ? "drillexplode_water" : "drillexplode", classic_pitch};
    case 20U:
        return {submerged ? "turr_rocketexplode_water" : "turr_rocketexplode",
                classic_pitch};
    case 21U:
        return {submerged ? "dynamiteexplode_water" : "dynamiteexplode", classic_pitch};
    case 33U:
        return {submerged ? "molotov_land_water" : "molotov_land_explode", classic_pitch};
    case 54U:
        return {submerged ? "AoS_soundfx_PLAYER_specialist_wpn_chem_bomb_water_001-003"
                          : "AoS_soundfx_PLAYER_specialist_wpn_chem_bomb_explode_001-005",
                fixed_pitch};
    case 55U:
        return {submerged
                    ? "AoS_soundfx_PLAYER_specalist_wpn_GRENADE_LAUNCHER_explode_water_001-005"
                    : "AoS_soundfx_PLAYER_specalist_wpn_GRENADE_LAUNCHER_explode_001-005",
                fixed_pitch};
    case 57U:
        return {submerged
                    ? "AoS_soundfx_PLAYER_specialist_wpn_sticky_grenade_explode_water_001-005"
                    : "AoS_soundfx_PLAYER_specialist_wpn_sticky_grenade_explode_001-005",
                fixed_pitch};
    case 58U:
        return {submerged
                    ? "AoS_soundfx_PLAYER_engineer__wpn_mine_launcher_mine_explode_water_001-004"
                    : "AoS_soundfx_PLAYER_engineer__wpn_mine_launcher_mine_explode_001-004",
                fixed_pitch};
    case 59U:
        return {submerged ? "AoS_soundfx_PLAYER_miner_wpn_C4_explode_water_001-003"
                          : "AoS_soundfx_PLAYER_miner_wpn_C4_explode_001-003",
                fixed_pitch};
    default:
        return {submerged ? "waterexplode" : "explode", classic_pitch};
    }
}

} // namespace battlespades::audio
