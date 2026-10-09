#pragma once
#include "battlespades/world/player_movement.hpp"
namespace battlespades::world {
/** Original point grenade collision; position has +176 Z, velocity is units/s.
 * Returns the pre-bounce speed in original velocity units (zero without a bounce). */
[[nodiscard]] double
step_classic_grenade(Vec3& position, Vec3& velocity, const VxlMap& map, double dt) noexcept;
/** 0.75/0.76 movement. Input is immediate, positions are offset by +176 Z
 * only at the renderer boundary; arithmetic runs in classic float32 space. */
[[nodiscard]] MovementStepResult step_classic_player(PlayerMovementState& state,
                                                     PlayerInputState input,
                                                     const VxlMap* map,
                                                     double dt,
                                                     bool aiming,
                                                     bool& jump_held) noexcept;
} // namespace battlespades::world
