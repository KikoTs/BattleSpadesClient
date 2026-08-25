#pragma once

#include "battlespades/world/kv6_model.hpp"
#include "battlespades/world/particle_system.hpp"
#include "battlespades/world/prefab_placement.hpp"
#include "battlespades/world/voxel_collapse.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace battlespades::world {

struct TerrainImpactEvent;

/**
 * Authored particle compositions for terrain feedback.
 *
 * These are pure emitters: they read an event and push spawns, owning no
 * state, so every burst's geometry, colour and count is unit-testable without
 * a renderer. Retail counts and lifetimes are authoritative here: presentation
 * code must not silently stack a second fallback burst on top.
 */

/** One shrinking block image per captured voxel, bounded with the body mesh. */
inline constexpr std::uint32_t falling_particle_maximum{2'048U};
/**
 * Bullet or melee hit that chipped or destroyed a single block.
 *
 * Retail spawns exactly four map-coloured particles offset along the struck
 * face (GameScene.spawn_debris), with no separate dust layer.
 */
void emit_block_break(ParticleSystem& particles, const TerrainImpactEvent& impact);

/**
 * Server-confirmed player impact from ShootResponse(9).
 *
 * Retail emits exactly five dark-red tumbling cubes at the packet position.
 * Keeping this separate from local hitscan effects prevents blood from being
 * fabricated for misses, spawn protection, or server-rejected damage.
 */
void emit_player_blood(ParticleSystem& particles,
                       std::array<float, 3U> position,
                       std::uint32_t seed);

/**
 * Server-confirmed HitEntity(20) spark/chip burst.
 *
 * This is deliberately separate from block debris: the packet position is an
 * exact entity-space impact and has no voxel face or terrain colour.
 */
void emit_entity_hit(ParticleSystem& particles,
                     std::array<float, 3U> position,
                     std::uint32_t seed);

/**
 * Retail supply-crate deletion effect.
 *
 * Crate.delete emits 25 additive 4x4 twinkles at the authoritative entity
 * position. It has no gravity or collision and shrinks over two seconds.
 */
void emit_crate_pickup(ParticleSystem& particles,
                       std::array<float, 3U> position,
                       std::uint32_t seed);

/** Compact construction puff for an authoritative ordinary block placement. */
void emit_block_placement(ParticleSystem& particles,
                          PrefabPlacementCell cell,
                          VxlColor color,
                          std::uint32_t seed);

/**
 * Bounded construction smoke distributed across one atomically committed
 * prefab. Large constructs are sampled evenly instead of flooding the pool.
 */
void emit_prefab_placement(ParticleSystem& particles,
                           std::span<const PrefabPlacementVoxel> voxels,
                           std::uint32_t seed);

/** One frame of the retained rocketpack corpse's exhaust plume. */
void emit_jetpack_death_thruster(ParticleSystem& particles,
                                 std::array<float, 3U> outlet,
                                 std::array<float, 3U> corpse_velocity,
                                 std::uint32_t seed);

/**
 * One retail `Character.update_jetpack` smoke emission (two particles).
 *
 * Callers own the recovered 0.02-second cadence. This emitter is visual only;
 * authoritative thrust and fuel remain in movement/network state.
 */
void emit_jetpack_flight_exhaust(ParticleSystem& particles,
                                 std::array<float, 3U> outlet,
                                 std::array<float, 3U> player_velocity,
                                 std::uint32_t seed);

/**
 * Rocket, grenade or other explosive detonation.
 *
 * Retail Rocket.delete is eight glow blocks plus ten map-coloured particles;
 * ExplodeOnImpactEntity uses four glow blocks plus the same ten particles.
 */
void emit_explosion(ParticleSystem& particles, const TerrainImpactEvent& impact);

/**
 * ExplodeCorpse(36)'s exact Character.explode_corpse composition.
 * Retail creates 40 bright-red, large, fast body particles at the retained
 * body position. The five dark-red particles belong only to ShootResponse
 * blood feedback; the grave is a separate entity and must not be mixed in.
 */
void emit_corpse_explosion(ParticleSystem& particles,
                           const TerrainImpactEvent& impact);

/**
 * Entity 11 deletion's `explode_display` presentation.
 *
 * Retail walks every visible tuple in grave.kv6 (stride one), transforms its
 * authored voxel centre through the display scale, then creates one ordinary
 * particle at that exact position. `grave_model` must already carry the
 * models.py `(0,0,11)` pivot offset. Passing null retains a small fail-safe for
 * an installation whose grave asset could not be loaded; normal gameplay and
 * the parity lab always install the real model during frontend construction.
 */
void emit_grave_explosion(ParticleSystem& particles,
                          const TerrainImpactEvent& impact,
                          const Kv6Model* grave_model = nullptr);

/**
 * Observer-side muzzle feedback for ShootFeedback(8).
 *
 * The server owns the shot edge; this emitter adds only presentation and
 * cannot fabricate damage, recoil, or terrain mutations.
 */
void emit_weapon_muzzle(ParticleSystem& particles,
                        std::array<float, 3U> position,
                        std::array<float, 3U> direction,
                        std::uint32_t seed);

/**
 * A detached structure has reached the end of its size-weighted animation.
 *
 * Every captured block creates one tumbling image with its original colour.
 * It receives an upward/sideways kick, then shrinks to zero over `lifetime`.
 * The complete presentation transform is supplied so debris leaves the tilted
 * structure's visible position rather than its old upright coordinates.
 */
void emit_structure_burst(ParticleSystem& particles,
                          std::span<const FallingVoxel> component,
                          std::array<float, 3U> presented_position,
                          std::array<float, 3U> source_pivot,
                          std::array<float, 3U> rotation_degrees,
                          std::uint32_t seed,
                          float lifetime);

/** One smoke puff at a flying rocket's rendered exhaust point. */
void emit_projectile_trail(ParticleSystem& particles,
                           std::array<float, 3U> position,
                           std::array<float, 3U> velocity, std::uint32_t seed);

/** Retail Block Cannon snowball smoke: compact, fast-decaying and grey. */
void emit_block_cannon_trail(ParticleSystem& particles,
                             std::array<float, 3U> position,
                             std::array<float, 3U> velocity,
                             std::uint32_t seed);

/**
 * Map-coloured chunks pulled from the destroyed voxel toward the shooter's
 * barrel. This replaces ordinary outward block debris for damage type 42.
 */
void emit_block_sucker_debris(ParticleSystem& particles,
                              const TerrainImpactEvent& impact,
                              std::array<float, 3U> barrel_position,
                              std::uint32_t seed);

} // namespace battlespades::world
