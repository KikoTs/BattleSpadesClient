#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace battlespades::audio {

/** One retail entity-placement cue bank and its semitone bounds. */
struct EntitySoundDefinition final {
    std::string_view group;
    std::array<float, 2U> pitch_semitones{};
};

/** Resolve CreateEntity placement audio without inventing a server action. */
[[nodiscard]] constexpr EntitySoundDefinition
retail_entity_place_sound(std::uint8_t type_id, bool submerged) noexcept {
    constexpr std::array<float, 2U> classic_pitch{-0.8F, 0.8F};
    constexpr std::array<float, 2U> medpack_pitch{-0.1F, 0.1F};
    constexpr std::array<float, 2U> mine_attach_pitch{-0.2F, 0.2F};
    constexpr std::array<float, 2U> fixed_pitch{0.0F, 0.0F};
    switch (type_id) {
    case 8U:
        return {"turret_place", classic_pitch};
    case 9U:
        return {"landmine_place", classic_pitch};
    case 10U:
        return {"dynamite_place", classic_pitch};
    case 30U:
        return {"AoS_soundfx_PLAYER_medic_item_medi_pack_place_001-003", medpack_pitch};
    case 36U:
        return {"AoS_soundfx_PLAYER_marksman_item_RADAR_place_001", fixed_pitch};
    case 37U:
        return {submerged
                    ? "AoS_soundfx_PLAYER_engineer__wpn_mine_launcher_attach_mine_water_001-003"
                    : "AoS_soundfx_PLAYER_engineer_wpn_mine_launcher_attach_mine_001",
                submerged ? fixed_pitch : mine_attach_pitch};
    case 38U:
        return {"AoS_soundfx_PLAYER_miner_wpn_C4_place_001-003", fixed_pitch};
    default:
        return {{}, fixed_pitch};
    }
}

} // namespace battlespades::audio
