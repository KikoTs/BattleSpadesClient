#pragma once

#include "battlespades/network/protocol168_runtime.hpp"
#include "battlespades/world/terrain_effects.hpp"

#include <array>
#include <optional>

namespace battlespades::frontend {

/**
 * Translate the retail ExplodeCorpse(36) edge into the shared VFX pipeline.
 *
 * Keeping this adapter outside the native frontend makes the actual decoded
 * packet-to-particle route testable. A zero effect flag is cleanup only and
 * deliberately returns no impact; a non-zero flag preserves the retained
 * floating-point corpse position instead of snapping the burst to a voxel.
 */
[[nodiscard]] std::optional<world::TerrainImpactEvent> make_corpse_explosion_impact(
    const network::ExplodeCorpsePacket& packet,
    std::array<float, 3U> retained_position,
    world::VxlColor team_color);

/**
 * Warm airburst layered under a jetpack corpse explosion.
 *
 * The normal red Character.explode_corpse burst remains intact. This second
 * event supplies the rocketpack's larger smoke trails and transient light at
 * the exact authoritative airborne corpse position.
 */
[[nodiscard]] world::TerrainImpactEvent make_jetpack_death_airburst(
    std::array<float, 3U> retained_position) noexcept;

} // namespace battlespades::frontend
