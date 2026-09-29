#pragma once

#include "battlespades/world/player_movement.hpp"

#include <cstdint>
#include <optional>

namespace battlespades::world {

class VxlMap;

/**
 * One stock ExplosionDamageManager handler's push arguments.
 *
 * The stock client pushes its own character while processing the terrain
 * Damage(37) packet: `handle_damage` dispatches on the packet type alone
 * through the `damage_functions` table (types 7-13, 15, 16, 18-24, 30, 33,
 * 37-41), and the server applies the same impulse on the matching third
 * accepted ClientData frame (BattleSpades server/main.py
 * STOCK_EXPLOSION_DAMAGE_TYPES, docs/PROTOCOL.md "Snowball Damage/Destroy
 * ordering"). Radius and knockback come from server/weapons_retail.py
 * RETAIL_EXPLOSIONS.
 */
struct RetailBlastSpec final {
    double radius{};
    double knockback_min{};
    double knockback_max{};
};

/** The push a Damage(37) of `damage_type` predicts, or nullopt when none. */
[[nodiscard]] std::optional<RetailBlastSpec>
retail_blast_for_damage_type(std::uint8_t damage_type) noexcept;

/**
 * handle_explosion_damage's velocity impulse on a character whose eye is at
 * `eye`: falloff (R^2 - d^2) / R^2 measured to the body centre (0.75 below
 * the eye standing, 1.25 crouched), magnitude kmin + falloff * (kmax - kmin)
 * scaled by the three weighted sight rays (0.5/0.3/0.2, each from 10% to
 * 110% of the explosion->point vector), direction explosion -> eye.
 * `map` may be null (no occlusion).
 */
[[nodiscard]] std::optional<Vec3> retail_blast_impulse(const VxlMap* map, Vec3 explosion,
                                                       Vec3 eye, bool crouched,
                                                       const RetailBlastSpec& spec) noexcept;

} // namespace battlespades::world
