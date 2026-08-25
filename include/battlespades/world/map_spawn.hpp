#pragma once

#include "battlespades/world/player_movement.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <cstdint>

namespace battlespades::world {

/** A resolved standing position on an arbitrary map. */
struct MapSpawn final {
    /**
     * Player anchor, not the feet.
     *
     * Feeds straight into TutorialSessionConfig::initial_position. The feet
     * plane sits `player_contact_offset` below this and the eye sits at it.
     */
    Vec3 position{};
    Vec3 orientation{-1.0, 0.0, 0.0};
    /** False when nothing safe was found; `position` is then a high-air fallback. */
    bool derived{};
};

/**
 * Nearest safe standing anchor to a starting column.
 *
 * Loading an arbitrary map has two failure modes, both seen in practice: spawn
 * inside rock and the screen goes black, or spawn over a void column and fall
 * forever. A single `surface_z` probe is not enough to avoid either, because
 * the real ground test samples four body corners at radius 0.45 -- a one-voxel
 * ledge or a pillar top passes a naive centre check and then fails in-game.
 *
 * So this reuses the actual movement predicates rather than re-deriving them,
 * and searches outward in bounded square rings from the start column. The
 * anchor formula `surface_z - player_contact_offset(false, false)` reproduces
 * the authored Training spawn to the digit, so it is a derivation rather than a
 * tuned guess.
 *
 * Never returns a position inside geometry. When nothing passes it reports
 * `derived = false` and a position high in the air: falling onto an unknown map
 * is recoverable and immediately legible, being embedded in rock is not.
 */
[[nodiscard]] MapSpawn resolve_map_spawn(const VxlMap& map,
                                         std::uint32_t start_x = 256U,
                                         std::uint32_t start_y = 256U) noexcept;

} // namespace battlespades::world
