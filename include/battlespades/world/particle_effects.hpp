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

/** DiamondPickup.on_delete: 50 DIAMOND_PICKUP_FX_* twinkles (size 5, speed .07/.08). */
void emit_diamond_pickup(ParticleSystem& particles,
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

/** AttachedStickyGrenadeEntity.on_delete: explode_display(display, 1.0, 5).
 * `display_transform` is the current model draw transform in map coordinates.
 * Fragments use raw, truncated KV6 tuples, not recoloured mesh vertices.
 */
void emit_sticky_model_explosion(ParticleSystem& particles,
                                  const Kv6Model& model,
                                  const std::array<float, 16U>& display_transform,
                                  std::uint32_t seed);

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
 * gameScene FallingBlocks.update on its first map contact: for every voxel
 * with i % int(5 + size/8000*10) == 0, create_particle_effect(None, rotated
 * voxel centre, body velocity, colour, 5, explode 0.125, size 5.0). draw.pyd
 * stores random_direction * explode - supplied velocity. The complete
 * presentation transform places debris at the tumbled body's visible voxels.
 */
void emit_falling_blocks_breakup(ParticleSystem& particles,
                                 std::span<const FallingVoxel> component,
                                 std::array<float, 3U> presented_position,
                                 std::array<float, 3U> source_pivot,
                                 std::array<float, 3U> rotation_degrees,
                                 std::array<float, 3U> body_velocity,
                                 std::uint32_t seed);

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

/**
 * Retail create_particle_effect_with_lut smoke family (BombTool.create_fuse_fx
 * and every create_fire_smoke override): one LUT-indexed SmokeTrail sprite,
 * velocity `(0,0,1) * speed` (draw.pyd negates the supplied vector, so the
 * puff rises), rotation 160..200, decay -1 (the size doubles over its life),
 * no gravity and no collision. `min_size`/`max_size` are the retail size
 * arguments; the native renderer's 0.1 multiplier is applied when emitting.
 */
struct LutSmokeParameters final {
    float min_size{};
    float max_size{};
    float min_speed{};
    float max_speed{};
    float lifetime{1.0F};
    /**
     * BlockFireEntity.create_fire_smoke (gameScene 0x100EA170) draws
     * uniform(MIN_VELOCITY, MAX_VELOCITY) independently for every axis
     * instead of a vertical-only speed.
     */
    bool per_axis_speed{};
};

/** BLOCKFIRE_SMOKE_GENERATION_*: size 4..8, per-axis speed 0..0.1, lifespan 3. */
inline constexpr LutSmokeParameters block_fire_smoke{4.0F, 8.0F, 0.0F, 0.1F, 3.0F, true};
/** BLOCKFIRE_SMOKE_GENERATION_MIN/MAX_RATE: 1..2 puffs per second. */
inline constexpr float block_fire_smoke_min_rate{1.0F};
inline constexpr float block_fire_smoke_max_rate{2.0F};
/** CHARACTER_MOLOTOV_SMOKE_GENERATION_*: size 2..5, speed 0..0.1, lifespan 1. */
inline constexpr LutSmokeParameters character_fire_smoke{2.0F, 5.0F, 0.0F, 0.1F, 1.0F};
/** Retail CHARACTER_MOLOTOV_SMOKE_GENERATION_RATE: one puff per 0.02 s. */
inline constexpr double character_fire_smoke_period{0.02};
/**
 * MOLOTOV_SMOKE_GENERATION_*: size 3..5, lifespan 2, one puff per
 * ExplodeOnImpactEntity.update. The speed range is not a named constant; the
 * character range (0..0.1) is used. VERIFY in MolotovEntity.create_fire_smoke
 * (the chemical override has the same code layout).
 */
inline constexpr LutSmokeParameters projectile_fire_smoke{3.0F, 5.0F, 0.0F, 0.1F, 2.0F};
/** BOMB_SMOKE_GENERATION_*: size 0.5..1, speed 0.05, lifespan 1, 25 per second. */
inline constexpr LutSmokeParameters bomb_fuse_smoke{0.5F, 1.0F, 0.05F, 0.05F, 1.0F};
inline constexpr double bomb_fuse_smoke_period{1.0 / 25.0};

void emit_lut_smoke(ParticleSystem& particles,
                    std::array<float, 3U> position,
                    const LutSmokeParameters& parameters,
                    std::uint32_t seed);

/** SMOKE_RING_SIZE: the ring radius callers pass by default. */
inline constexpr float smoke_ring_size{1.0F};

/**
 * Retail create_smoke_ring(position, radius) (gameScene 0x10189350) /
 * create_snowke_ring (0x102415D0): SMOKE_RING_NOOF (8) static single puffs
 * on the caller's ring, each coloured by map.get_point there, size
 * uniform(3, 10), rotation 180, decay -1, lifetime 1, framerate 60, no
 * gravity or collision. `snowke` forwards to emit_snowke_ring in white.
 */
void emit_smoke_ring(ParticleSystem& particles,
                     std::array<float, 3U> position,
                     bool snowke,
                     std::uint32_t seed,
                     float radius = smoke_ring_size,
                     const VxlMap* map = nullptr);

/**
 * Retail GameScene.create_snowke_ring (gameScene 0x102415D0), recovered by
 * IDA: SIX puffs at 60-degree steps, floor(position) + (sin(a)*r + 0.5,
 * cos(a)*r + 0.5, +1.0), size uniform(4, 7) (x0.1), in the CALLER's colour,
 * SnowkeTrail atlas, rotation 180, decay -1, lifetime 1, framerate 60.
 */
void emit_snowke_ring(ParticleSystem& particles,
                      std::array<float, 3U> position,
                      VxlColor color,
                      std::uint32_t seed,
                      float radius = smoke_ring_size);

} // namespace battlespades::world
