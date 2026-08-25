#pragma once

#include "battlespades/world/terrain_effects.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace battlespades::world {

/**
 * Recreate the terrain-contact half of a server-relayed ShootFeedback(8).
 *
 * The result is presentation only. It never mutates VXL or invents player
 * damage; authoritative Damage(37) and ShootResponse(9) retain those roles.
 */
[[nodiscard]] std::vector<TerrainImpactEvent>
replicated_hitscan_impacts(const VxlMap& map,
                           std::array<float, 3U> origin,
                           std::array<float, 3U> orientation,
                           std::uint8_t tool_id,
                           std::uint8_t seed,
                           bool zoomed);

} // namespace battlespades::world
