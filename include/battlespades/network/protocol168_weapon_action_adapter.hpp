#pragma once

#include "battlespades/world/weapon_runtime.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace battlespades::network {

/**
 * World/session data needed to turn one packet-neutral weapon action into
 * exact Protocol 168 payloads. The runtime deliberately does not raycast;
 * the owning session fills these values from its current authoritative view.
 */
struct WeaponActionWireContext final {
    std::int32_t loop_count{};
    std::int32_t shot_on_world_update{};
    std::uint8_t player_id{};
    std::array<float, 3U> position{};
    std::array<float, 3U> orientation{};
    std::array<float, 3U> velocity{};
    std::int16_t damage{};
    std::int16_t penetration{};
    bool affect_shooter{};
    std::uint8_t face{};
    float yaw{};
    std::uint32_t color{};
    std::array<std::int16_t, 3U> block_line_start{};
    std::array<std::int16_t, 3U> block_line_end{};
    std::vector<std::array<std::int16_t, 3U>> paint_positions;
    std::string prefab_name;
    std::uint8_t prefab_yaw{};
    std::uint8_t prefab_pitch{};
    std::uint8_t prefab_roll{};
    std::int32_t prefab_from_block_index{};
    std::int32_t prefab_to_block_index{};
    bool prefab_add_to_user_blocks{};
    std::uint8_t ugc_item_id{};
    bool ugc_placing{true};
};

struct EncodedWeaponAction final {
    std::vector<std::vector<std::byte>> packets;
    /** True for presentation/input edges that intentionally have no packet. */
    bool local_only{};
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error.empty();
    }
};

/** Convert every WeaponRuntime action to its exact stock packet family. */
[[nodiscard]] EncodedWeaponAction encode_weapon_action(
    const world::WeaponAction& action, const WeaponActionWireContext& context);

} // namespace battlespades::network
