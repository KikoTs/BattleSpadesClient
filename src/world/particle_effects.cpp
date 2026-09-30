#include "battlespades/world/particle_effects.hpp"

#include "battlespades/world/terrain_effects.hpp"

#include <algorithm>
#include <cmath>

namespace battlespades::world {
namespace {

[[nodiscard]] std::uint32_t mix(std::uint32_t value) noexcept {
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    return value ^ (value >> 16U);
}

[[nodiscard]] float random_unit(std::uint32_t seed) noexcept {
    return static_cast<float>(mix(seed) & 0xFFFFU) / 65535.0F;
}

[[nodiscard]] std::uint32_t cell_seed(const VoxelCell& cell) noexcept {
    return mix(cell.x ^ (cell.y << 9U) ^ (cell.z << 18U));
}

[[nodiscard]] std::array<float, 3U> face_offset_position(
    const TerrainImpactEvent& impact) noexcept {
    // Retail offsets the burst 0.6 along the struck face from the block
    // centre, which places it just outside the surface instead of inside it
    // (GameScene.spawn_debris' six-entry face table collapses to this).
    return {
        static_cast<float>(impact.cell.x) + 0.5F +
            static_cast<float>(impact.normal[0U]) * 0.6F,
        static_cast<float>(impact.cell.y) + 0.5F +
            static_cast<float>(impact.normal[1U]) * 0.6F,
        static_cast<float>(impact.cell.z) + 0.5F +
            static_cast<float>(impact.normal[2U]) * 0.6F,
    };
}

[[nodiscard]] constexpr float degrees_to_radians(float degrees) noexcept {
    return degrees * 0.01745329251994329577F;
}

[[nodiscard]] std::array<float, 3U> transformed_voxel_center(
    const FallingVoxel& voxel, std::array<float, 3U> presented_position,
    std::array<float, 3U> source_pivot,
    std::array<float, 3U> rotation_degrees) noexcept {
    std::array<float, 3U> point{
        static_cast<float>(voxel.cell.x) + 0.5F - source_pivot[0U],
        static_cast<float>(voxel.cell.y) + 0.5F - source_pivot[1U],
        static_cast<float>(voxel.cell.z) + 0.5F - source_pivot[2U]};
    {
        const float angle = degrees_to_radians(rotation_degrees[0U]);
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        const float y = point[1U] * cosine - point[2U] * sine;
        const float z = point[1U] * sine + point[2U] * cosine;
        point[1U] = y;
        point[2U] = z;
    }
    {
        const float angle = degrees_to_radians(rotation_degrees[1U]);
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        const float x = point[0U] * cosine + point[2U] * sine;
        const float z = -point[0U] * sine + point[2U] * cosine;
        point[0U] = x;
        point[2U] = z;
    }
    {
        const float angle = degrees_to_radians(rotation_degrees[2U]);
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        const float x = point[0U] * cosine - point[1U] * sine;
        const float y = point[0U] * sine + point[1U] * cosine;
        point[0U] = x;
        point[1U] = y;
    }
    for (std::size_t axis{}; axis < point.size(); ++axis) {
        point[axis] += presented_position[axis];
    }
    return point;
}

} // namespace

void emit_block_break(ParticleSystem& particles, const TerrainImpactEvent& impact) {
    const auto origin = face_offset_position(impact);
    const auto seed = cell_seed(impact.cell);

    // Retail GameScene.spawn_debris calls create_particle_effect once with
    // count=4, explode_velocity=.25 and authored size=3.0. The native renderer
    // scales particle size by .1, so the matching client-space size is .30.
    // Chipping and destruction deliberately share this one composition.
    ParticleSpawn chip;
    chip.position = origin;
    chip.color = impact.color;
    chip.explode_velocity = 0.25F;
    chip.velocity = {};
    chip.size_begin = 0.30F;
    chip.size_end = 0.0F;
    chip.alpha_begin = 1.0F;
    chip.alpha_end = 0.0F;
    chip.rotation_degrees = 180.0F;
    chip.rotation_speed = 0.0F;
    chip.lifetime = 2.0F;
    chip.gravity_scale = 1.0F;
    chip.atlas = ParticleAtlas::tumbling_cube;
    chip.blend = ParticleBlend::alpha;
    chip.collide = true;
    particles.emit_burst(chip, 4U, seed);
}

void emit_player_blood(ParticleSystem& particles,
                       std::array<float, 3U> position,
                       std::uint32_t seed) {
    // make_blood_particles in retail gameScene is:
    // (127, 0, 0), count=5, explode_velocity=0.25, size=2.0. The native
    // particle renderer multiplies velocity by 32*dt and authored size by .1.
    ParticleSpawn blood;
    blood.position = position;
    blood.color = VxlColor{127U, 0U, 0U, 255U};
    blood.explode_velocity = 0.25F;
    blood.size_begin = 0.20F;
    blood.size_end = 0.0F;
    blood.alpha_begin = 1.0F;
    // draw.pyd passes remaining_lifetime / lifetime as vertex alpha for every
    // native particle, independent of the decay value used for its size.
    blood.alpha_end = 0.0F;
    blood.rotation_degrees = 180.0F;
    blood.rotation_speed = 0.0F;
    blood.lifetime = 2.0F;
    blood.gravity_scale = 1.0F;
    blood.atlas = ParticleAtlas::tumbling_cube;
    blood.blend = ParticleBlend::alpha;
    blood.collide = true;
    particles.emit_burst(blood, 5U, seed);
}

void emit_entity_hit(ParticleSystem& particles,
                     std::array<float, 3U> position,
                     std::uint32_t seed) {
    // Retail Entity.hit (scenes/main/entity.py:76-79):
    // create_particle_effect(None, pos, None, (127,127,127), 5, 0.25,
    // size=2.0) -- the blood composition in neutral grey. Native particle
    // size is the authored size times 0.1.
    ParticleSpawn chip;
    chip.position = position;
    chip.color = VxlColor{127U, 127U, 127U, 255U};
    chip.explode_velocity = 0.25F;
    chip.size_begin = 0.20F;
    chip.size_end = 0.0F;
    chip.alpha_begin = 1.0F;
    chip.alpha_end = 0.0F;
    chip.rotation_degrees = 180.0F;
    chip.rotation_speed = 0.0F;
    chip.lifetime = 2.0F;
    chip.gravity_scale = 1.0F;
    chip.atlas = ParticleAtlas::tumbling_cube;
    chip.blend = ParticleBlend::alpha;
    chip.collide = true;
    particles.emit_burst(chip, 5U, seed);
}

void emit_crate_pickup(ParticleSystem& particles,
                       std::array<float, 3U> position,
                       std::uint32_t seed) {
    // shared.constants CRATE_PICKUP_FX_* and Crate.delete in entity.pyd:
    // 25, vertical/explosion speed .05, size 4, rotation speed 180,
    // lifetime 2, 4x4 at 30 fps, non-looping, additive, no gravity/collision.
    ParticleSpawn twinkle;
    twinkle.position = position;
    twinkle.velocity = {0.0F, 0.0F, -0.05F};
    twinkle.explode_velocity = 0.05F;
    // The atlas contains the authored blue/white colour ramp; white modulation
    // preserves it instead of double-tinting the texture.
    twinkle.color = VxlColor{255U, 255U, 255U, 255U};
    // draw.pyd applies the native authored-size multiplier of 0.1.
    twinkle.size_begin = 0.40F;
    twinkle.size_end = 0.0F;
    twinkle.alpha_begin = 1.0F;
    twinkle.alpha_end = 0.0F;
    twinkle.rotation_degrees = 0.0F;
    twinkle.rotation_speed = 180.0F;
    twinkle.lifetime = 2.0F;
    twinkle.gravity_scale = 0.0F;
    twinkle.atlas = ParticleAtlas::pickup_twinkle;
    twinkle.blend = ParticleBlend::additive;
    twinkle.frames_x = 4U;
    twinkle.frames_y = 4U;
    twinkle.start_frame = 0U;
    twinkle.framerate = 30U;
    twinkle.loop = false;
    twinkle.collide = false;
    particles.emit_burst(twinkle, 25U, seed);
}

void emit_diamond_pickup(ParticleSystem& particles,
                         std::array<float, 3U> position,
                         std::uint32_t seed) {
    // DiamondPickup.on_delete (gameScene diamond.py:30-40) with the
    // DIAMOND_PICKUP_FX_* constants: 50, vertical .08, explosion .07, size 5,
    // rotation speed 180, lifetime 2, 4x4 at 30 fps, additive, no
    // gravity/collision. Same particle_pickup_twinkle atlas as Crate.delete.
    ParticleSpawn twinkle;
    twinkle.position = position;
    twinkle.velocity = {0.0F, 0.0F, -0.08F};
    twinkle.explode_velocity = 0.07F;
    twinkle.color = VxlColor{255U, 255U, 255U, 255U};
    twinkle.size_begin = 0.50F;
    twinkle.size_end = 0.0F;
    twinkle.alpha_begin = 1.0F;
    twinkle.alpha_end = 0.0F;
    twinkle.rotation_degrees = 0.0F;
    twinkle.rotation_speed = 180.0F;
    twinkle.lifetime = 2.0F;
    twinkle.gravity_scale = 0.0F;
    twinkle.atlas = ParticleAtlas::pickup_twinkle;
    twinkle.blend = ParticleBlend::additive;
    twinkle.frames_x = 4U;
    twinkle.frames_y = 4U;
    twinkle.start_frame = 0U;
    twinkle.framerate = 30U;
    twinkle.loop = false;
    twinkle.collide = false;
    particles.emit_burst(twinkle, 50U, seed);
}

void emit_block_placement(ParticleSystem& particles,
                          PrefabPlacementCell cell,
                          VxlColor color,
                          std::uint32_t seed) {
    ParticleSpawn puff;
    puff.position = {static_cast<float>(cell.x) + 0.5F,
                     static_cast<float>(cell.y) + 0.5F,
                     static_cast<float>(cell.z) + 0.5F};
    puff.velocity = {0.0F, 0.0F, -0.018F};
    puff.color = VxlColor{
        static_cast<std::uint8_t>((static_cast<std::uint16_t>(color.red) + 176U) / 2U),
        static_cast<std::uint8_t>((static_cast<std::uint16_t>(color.green) + 176U) / 2U),
        static_cast<std::uint8_t>((static_cast<std::uint16_t>(color.blue) + 176U) / 2U),
        180U};
    puff.explode_velocity = 0.035F;
    puff.size_begin = 0.11F;
    puff.size_end = 0.24F;
    puff.alpha_begin = 0.50F;
    puff.alpha_end = 0.0F;
    puff.lifetime = 0.42F;
    puff.gravity_scale = 0.0F;
    puff.atlas = ParticleAtlas::soft_round;
    puff.blend = ParticleBlend::premultiplied;
    puff.collide = false;
    puff.loop = false;
    particles.emit_burst(puff, 4U, seed);
}

void emit_prefab_placement(ParticleSystem& particles,
                           std::span<const PrefabPlacementVoxel> voxels,
                           std::uint32_t seed) {
    if (voxels.empty()) {
        return;
    }
    // A construct is committed in one frame, and its smoke must read the same
    // way. Even sampling gives a long wall feedback along its complete extent
    // instead of emitting everything at the first packet slice or at its pivot.
    constexpr std::size_t maximum_puffs{24U};
    const auto samples = std::min(voxels.size(), maximum_puffs);
    for (std::size_t sample{}; sample < samples; ++sample) {
        const auto index = samples == 1U
                               ? 0U
                               : sample * (voxels.size() - 1U) / (samples - 1U);
        emit_block_placement(particles, voxels[index].cell, voxels[index].color,
                             seed ^ static_cast<std::uint32_t>(sample * 0x9E3779B9U));
    }
}

void emit_jetpack_death_thruster(ParticleSystem& particles,
                                 std::array<float, 3U> outlet,
                                 std::array<float, 3U> corpse_velocity,
                                 std::uint32_t seed) {
    // The server owns the rising corpse transform. Exhaust is presentation
    // only and follows that authoritative point; it never supplies lift.
    ParticleSpawn flame;
    flame.position = outlet;
    flame.velocity = {-corpse_velocity[0U] * 0.015F,
                      -corpse_velocity[1U] * 0.015F,
                      0.12F - corpse_velocity[2U] * 0.015F};
    flame.color = VxlColor{255U, 185U, 62U, 255U};
    flame.explode_velocity = 0.035F;
    flame.size_begin = 0.14F;
    flame.size_end = 0.04F;
    flame.alpha_begin = 1.0F;
    flame.alpha_end = 0.0F;
    flame.lifetime = 0.24F;
    flame.gravity_scale = 0.0F;
    flame.atlas = ParticleAtlas::glow_cube;
    flame.blend = ParticleBlend::additive;
    flame.collide = false;
    flame.loop = false;
    particles.emit_burst(flame, 3U, seed);

    ParticleSpawn smoke = flame;
    smoke.velocity = {-corpse_velocity[0U] * 0.02F,
                      -corpse_velocity[1U] * 0.02F,
                      0.08F - corpse_velocity[2U] * 0.02F};
    smoke.color = VxlColor{155U, 150U, 140U, 190U};
    smoke.explode_velocity = 0.02F;
    smoke.size_begin = 0.11F;
    smoke.size_end = 0.425F;
    smoke.alpha_begin = 0.65F;
    smoke.lifetime = 0.75F;
    smoke.atlas = ParticleAtlas::smoke_trail;
    smoke.blend = ParticleBlend::premultiplied;
    smoke.frames_x = 8U;
    smoke.frames_y = 8U;
    smoke.framerate = 28U;
    particles.emit_burst(smoke, 2U, seed ^ 0xA17B53U);
}

void emit_jetpack_flight_exhaust(ParticleSystem& particles,
                                 std::array<float, 3U> outlet,
                                 std::array<float, 3U> player_velocity,
                                 std::uint32_t seed) {
    // constants.py: generation rate .02, two particles, spread .01,
    // lifespan 5, decay -1 and authored size 2. draw.pyd applies the common
    // 0.1 size multiplier. Map Z points down, so positive Z trails beneath the
    // pack while inherited velocity prevents the plume from looking detached.
    ParticleSpawn smoke;
    smoke.position = outlet;
    smoke.velocity = {player_velocity[0U] / 60.0F,
                      player_velocity[1U] / 60.0F,
                      player_velocity[2U] / 60.0F + 0.035F};
    smoke.color = VxlColor{190U, 186U, 178U, 210U};
    smoke.explode_velocity = 0.01F;
    smoke.size_begin = 0.20F;
    smoke.size_end = 0.40F;
    smoke.alpha_begin = 0.72F;
    smoke.alpha_end = 0.0F;
    smoke.rotation_degrees = 160.0F;
    smoke.rotation_speed = 180.0F;
    smoke.lifetime = 5.0F;
    smoke.gravity_scale = 0.0F;
    smoke.atlas = ParticleAtlas::smoke_trail;
    smoke.blend = ParticleBlend::premultiplied;
    smoke.frames_x = 8U;
    smoke.frames_y = 8U;
    smoke.start_frame = 1U;
    smoke.framerate = 30U;
    smoke.loop = false;
    smoke.collide = false;
    particles.emit_burst(smoke, 2U, seed);
}

namespace {

/**
 * One retail explosion recipe: the GlowBlockParticles.create count plus the
 * create_particle_effect(None, pos, None, colour, count, explode_velocity,
 * size, ...) debris call made by the same delete/update handler.
 */
struct ExplosionRecipe final {
    std::uint32_t glow_count;
    std::uint32_t debris_count;
    float debris_explode_velocity;
    /** Authored size; draw.pyd renders it *0.1 as a quad half-extent. */
    float debris_authored_size;
    float debris_lifetime;
};

[[nodiscard]] constexpr ExplosionRecipe explosion_recipe(std::uint8_t tool) noexcept {
    switch (tool) {
    // Grenade.update (gameScene 0x100AE790): glow_block_particles.create(8,
    // pos) then create_particle_effect(None, pos, None, colour, 10, 1.3,
    // 5.0) -- every later argument is the default, so lifetime is 2.0.
    // GRENADE_TOOL, CLASSIC_GRENADE_TOOL, ANTIPERSONNEL_GRENADE_TOOL.
    case 11U:
    case 31U:
    case 32U:
        return {8U, 10U, 1.3F, 5.0F, 2.0F};
    // LandmineEntity.on_delete 0x100A8E00, DynamiteEntity.on_delete
    // 0x100AC230, Drill.delete 0x100C5C30, C4Entity.on_delete 0x100EBA90,
    // AttachedStickyGrenadeEntity.on_delete 0x100FB730:
    // create(8) + (10, 1.5, 5.0), default lifetime 2.0.
    case 14U:
    case 20U:
    case 21U:
    case 47U:
    case 57U:
    case 59U:
        return {8U, 10U, 1.5F, 5.0F, 2.0F};
    // BombPickup.explode 0x100D4980: create(12) + (15, 1.3, 10.0).
    case 25U:
        return {12U, 15U, 1.3F, 10.0F, 2.0F};
    // ExplodeOnImpactEntity.on_delete (explodeOnImpactEntity.py:69-72), whose
    // one subclass is the GLGrenade: create(4) + the Rocket debris call.
    case 55U:
        return {4U, 10U, 1.0F, 10.0F, 1.0F};
    // Rocket.delete 0x100B5F60 / Rocket2.delete 0x100BC150 (rocket.py:83-86):
    // create(8) + (10, 1.0, 10.0, 180, 0, True, 1.0, 1.0, 0, 8, 8, random,
    // 1, 30, True). Also the fallback for unrecovered sources.
    default:
        return {8U, 10U, 1.0F, 10.0F, 1.0F};
    }
}

} // namespace

void emit_explosion(ParticleSystem& particles, const TerrainImpactEvent& impact) {
    const auto origin = terrain_impact_position(impact);
    const auto seed = cell_seed(impact.cell);
    const auto recipe = explosion_recipe(impact.source_tool);
    // The flight trail already exists on the projectile and must not be
    // fabricated again as impact smoke fingers.
    ParticleSpawn glow;
    glow.position = origin;
    glow.color = VxlColor{255U, 255U, 255U, 255U};
    // GlowBlockParticles.create (gameScene 0x10067230) builds a 22-tuple for
    // create_particle_effect_with_lut: particle_glow_block, particle_lut_image,
    // pos, None, (255,255,255), n, explode_velocity 1.0, size 10.0, rotation
    // 180, rotation speed 0, collision True, decay 1.0, lifetime 1.0, start
    // frame 0 (random), 8x8, randint(0,1), loop 1, 30 fps, gravity True,
    // ALPHA_BLEND_MODE_BLEND, GLOW_SMOKE_TRAIL_SPAWN_POINT. It is the same for
    // every caller; only n changes.
    glow.explode_velocity = 1.0F;
    glow.size_begin = 1.0F;
    glow.size_end = 0.0F;
    glow.alpha_begin = 1.0F;
    // The native LUT shader ignores gl_Color alpha. Its opacity comes from the
    // animated cube atlas and LUT.
    glow.alpha_end = 1.0F;
    glow.rotation_degrees = 180.0F;
    glow.lifetime = 1.0F;
    // gravity True: the parents arc under the StateData world gravity (the
    // native particle gravity global is set from world.get_gravity()).
    glow.gravity_scale = 1.0F;
    glow.atlas = ParticleAtlas::glow_cube;
    glow.color_mode = ParticleColorMode::glow_lut;
    glow.blend = ParticleBlend::alpha;
    // GlowBlockParticles chooses one direction for the complete emission.
    glow.forward_animate = (mix(seed + 19U) & 1U) != 0U;
    glow.collide = true;
    // draw.pyd invokes the spawn-point child emitter once after every parent
    // update; these are the fluffy fire plumes along each arc.
    glow.child_emitter = ParticleChildEmitter::glow_smoke_trail;
    particles.emit_burst(glow, recipe.glow_count, seed + 19U);

    ParticleSpawn debris;
    debris.position = origin;
    debris.color = impact.color;
    debris.explode_velocity = recipe.debris_explode_velocity;
    debris.size_begin = recipe.debris_authored_size * 0.1F;
    debris.size_end = 0.0F;
    debris.alpha_begin = 1.0F;
    // particle_frag multiplies the atlas by a constant gl_Color. Remaining
    // lifetime shrinks the quad; it does not fade these chunks.
    debris.alpha_end = 1.0F;
    debris.rotation_degrees = 180.0F;
    debris.rotation_speed = 0.0F;
    debris.lifetime = recipe.debris_lifetime;
    debris.gravity_scale = 1.0F;
    debris.atlas = ParticleAtlas::tumbling_cube;
    debris.blend = ParticleBlend::alpha;
    debris.forward_animate = (mix(seed + 89U) & 1U) != 0U;
    debris.collide = true;
    particles.emit_burst(debris, recipe.debris_count, seed + 89U);
}

void emit_corpse_explosion(ParticleSystem& particles,
                           const TerrainImpactEvent& impact) {
    const auto origin = terrain_impact_position(impact);
    const auto seed = cell_seed(impact.cell) ^ 0xD34D168U;

    // Character.explode_corpse in character.pyd calls:
    // create_particle_effect(None, self.position, None, (255, 0, 0),
    //                        40, 0.9, 5.0)
    // This is deliberately much larger and more violent than the five
    // (127,0,0) hit particles created by make_blood_particles.
    ParticleSpawn body;
    body.position = origin;
    body.color = VxlColor{255U, 0U, 0U, 255U};
    body.explode_velocity = 0.9F;
    // draw.pyd applies the native authored-size multiplier of 0.1.
    body.size_begin = 0.50F;
    body.size_end = 0.0F;
    body.alpha_begin = 1.0F;
    // Character.explode_corpse keeps saturated red opacity for its complete
    // two-second lifetime; native decay affects size only.
    body.alpha_end = 1.0F;
    body.rotation_degrees = 180.0F;
    body.rotation_speed = 0.0F;
    body.lifetime = 2.0F;
    body.gravity_scale = 1.0F;
    body.atlas = ParticleAtlas::tumbling_cube;
    body.blend = ParticleBlend::alpha;
    body.collide = true;
    particles.emit_burst(body, 40U, seed);
}

void emit_grave_explosion(ParticleSystem& particles,
                          const TerrainImpactEvent& impact,
                          const Kv6Model* grave_model) {
    const auto origin = terrain_impact_position(impact);
    const auto seed = cell_seed(impact.cell) ^ 0x6A7A3E16U;

    if (grave_model != nullptr && !grave_model->voxels().empty()) {
        // GameScene.explode_display(display, 1.0, 1) iterates every six-field
        // model tuple, transforms its xyz by display.size, and calls:
        // create_particle_effect(None, position, None, color,
        //                        1, 1.0, 3.0)
        // The native particle path then applies its own authored-size * 0.1,
        // so size 3.0 is a 0.3-world-unit tumbling cube. GraveEntity.size is
        // 0.10; its DisplayList position is physics_origin - 0.5 and
        // explode_display adds 0.5 back, leaving the packet/entity origin here.
        constexpr float display_scale{0.10F};
        const auto& pivot = grave_model->pivot();
        std::uint32_t index{};
        for (const auto& voxel : grave_model->voxels()) {
            ParticleSpawn chunk;
            chunk.position = {
                origin[0U] +
                    (static_cast<float>(voxel.x) - pivot[0U]) * display_scale,
                origin[1U] +
                    (static_cast<float>(voxel.y) - pivot[1U]) * display_scale,
                origin[2U] +
                    (static_cast<float>(voxel.z) - pivot[2U]) * display_scale,
            };
            // explode_display reads the six raw tuples returned by the KV6
            // model. Entity.draw's temporary default team colour is reset
            // before packet processing, so the disappearing fragments keep
            // their authored grey/black tuple colours rather than inheriting
            // the live display's team substitution.
            chunk.color = voxel.color;
            chunk.explode_velocity = 1.0F;
            chunk.size_begin = 0.30F;
            chunk.size_end = 0.0F;
            chunk.alpha_begin = 1.0F;
            chunk.alpha_end = 0.0F;
            chunk.rotation_degrees = 180.0F;
            chunk.rotation_speed = 0.0F;
            chunk.lifetime = 2.0F;
            chunk.gravity_scale = 1.0F;
            chunk.atlas = ParticleAtlas::tumbling_cube;
            chunk.blend = ParticleBlend::alpha;
            chunk.collide = true;
            particles.emit_burst(chunk, 1U,
                                 seed ^ mix(++index * 0x9E3779B9U));
        }
        return;
    }

    // Asset-load failure only. Keep a visible, bounded indication instead of
    // silently swallowing an authoritative deletion; this is not the normal
    // retail path and is deliberately much cheaper than fabricating 800 grey
    // particles with no model-space positions.
    ParticleSpawn chunks;
    chunks.position = origin;
    chunks.color = impact.color;
    chunks.explode_velocity = 0.085F;
    chunks.velocity = {0.0F, 0.0F, -0.055F};
    chunks.size_begin = 0.17F;
    chunks.size_end = 0.0F;
    chunks.alpha_begin = 1.0F;
    chunks.alpha_end = 0.0F;
    chunks.rotation_speed = 360.0F;
    chunks.lifetime = 1.15F;
    chunks.atlas = ParticleAtlas::tumbling_cube;
    chunks.blend = ParticleBlend::alpha;
    chunks.collide = true;
    particles.emit_burst(chunks, 18U, seed + 17U);

    ParticleSpawn dark_chunks = chunks;
    dark_chunks.color = VxlColor{48U, 43U, 40U, 255U};
    dark_chunks.explode_velocity = 0.105F;
    dark_chunks.size_begin = 0.13F;
    dark_chunks.lifetime = 0.95F;
    particles.emit_burst(dark_chunks, 10U, seed + 41U);
}

void emit_sticky_model_explosion(ParticleSystem& particles,
                                  const Kv6Model& model,
                                  const std::array<float, 16U>& display_transform,
                                  std::uint32_t seed) {
    // gameScene 0x100FB730 -> explode_display 0x101687B0. The slice is [::5],
    // speed 1.0, one size-3 particle per tuple, default lifetime 2 seconds.
    // kv6.get_points 0x10015000 stores (coordinate - pivot) as signed shorts;
    // it neither applies VBO centre correction nor the model-quality scale.
    const auto& pivot = model.pivot();
    const auto& voxels = model.voxels();
    for (std::size_t index{}; index < voxels.size(); index += 5U) {
        const auto& voxel = voxels[index];
        const std::array<float, 3U> point{
            std::trunc(static_cast<float>(voxel.x) - pivot[0U]),
            -std::trunc(static_cast<float>(voxel.z) - pivot[2U]),
            std::trunc(static_cast<float>(voxel.y) - pivot[1U])};
        ParticleSpawn chunk;
        for (std::size_t axis{}; axis < 3U; ++axis) {
            chunk.position[axis] = display_transform[12U + axis] + 0.5F;
            for (std::size_t component{}; component < 3U; ++component)
                chunk.position[axis] += point[component] *
                    display_transform[component * 4U + axis];
        }
        chunk.color = voxel.color;
        chunk.explode_velocity = 1.0F;
        chunk.size_begin = 0.30F;
        chunk.size_end = 0.0F;
        chunk.alpha_begin = 1.0F;
        chunk.alpha_end = 0.0F;
        chunk.rotation_degrees = 180.0F;
        chunk.lifetime = 2.0F;
        chunk.gravity_scale = 1.0F;
        chunk.atlas = ParticleAtlas::tumbling_cube;
        chunk.blend = ParticleBlend::alpha;
        chunk.collide = true;
        particles.emit_burst(chunk, 1U,
                             seed ^ mix(static_cast<std::uint32_t>(index) + 1U));
    }
}

void emit_weapon_muzzle(ParticleSystem& particles,
                        std::array<float, 3U> position,
                        std::array<float, 3U> direction,
                        std::uint32_t seed) {
    const float length =
        std::sqrt(direction[0U] * direction[0U] +
                  direction[1U] * direction[1U] +
                  direction[2U] * direction[2U]);
    if (length > 0.0001F) {
        for (auto& axis : direction) axis /= length;
    } else {
        direction = {0.0F, 1.0F, 0.0F};
    }
    for (std::size_t axis{}; axis < position.size(); ++axis) {
        position[axis] += direction[axis] * 0.32F;
    }

    ParticleSpawn flash;
    flash.position = position;
    flash.velocity = {direction[0U] * 0.018F, direction[1U] * 0.018F,
                      direction[2U] * 0.018F};
    flash.color = VxlColor{255U, 228U, 150U, 255U};
    flash.explode_velocity = 0.012F;
    flash.size_begin = 0.17F;
    flash.size_end = 0.015F;
    flash.alpha_begin = 1.0F;
    flash.alpha_end = 0.0F;
    flash.lifetime = 0.075F;
    flash.gravity_scale = 0.0F;
    flash.atlas = ParticleAtlas::soft_round;
    flash.blend = ParticleBlend::additive;
    flash.frames_x = 1U;
    flash.frames_y = 1U;
    flash.start_frame = 1U;
    flash.framerate = 0U;
    flash.loop = false;
    flash.collide = false;
    particles.emit_burst(flash, 3U, seed);

    ParticleSpawn smoke = flash;
    smoke.color = VxlColor{92U, 88U, 82U, 255U};
    smoke.explode_velocity = 0.006F;
    smoke.size_begin = 0.09F;
    smoke.size_end = 0.31F;
    smoke.alpha_begin = 0.34F;
    smoke.lifetime = 0.28F;
    smoke.atlas = ParticleAtlas::smoke_trail;
    smoke.blend = ParticleBlend::premultiplied;
    smoke.frames_x = 8U;
    smoke.frames_y = 8U;
    smoke.start_frame = 1U;
    smoke.framerate = 30U;
    particles.emit(smoke);
}

void emit_falling_blocks_breakup(ParticleSystem& particles,
                                 std::span<const FallingVoxel> component,
                                 std::array<float, 3U> presented_position,
                                 std::array<float, 3U> source_pivot,
                                 std::array<float, 3U> rotation_degrees,
                                 std::array<float, 3U> body_velocity,
                                 std::uint32_t seed) {
    if (component.empty()) {
        return;
    }
    const auto mod = std::max<std::size_t>(1U, falling_blocks_particle_mod(component.size()));
    for (std::size_t index{}; index < component.size(); index += mod) {
        const auto& voxel = component[index];
        // The spawn_debris composition with the FallingBlocks arguments:
        // count 5, explode_velocity 0.125, authored size 5.0 (native * 0.1),
        // default lifetime 2, rotation 180, gravity, collision.
        ParticleSpawn chip;
        chip.position = transformed_voxel_center(
            voxel, presented_position, source_pivot, rotation_degrees);
        chip.color = voxel.color;
        chip.velocity = {-body_velocity[0U], -body_velocity[1U], -body_velocity[2U]};
        chip.explode_velocity = 0.125F;
        chip.size_begin = 0.5F;
        chip.size_end = 0.0F;
        chip.alpha_begin = 1.0F;
        chip.alpha_end = 0.0F;
        chip.rotation_degrees = 180.0F;
        chip.rotation_speed = 0.0F;
        chip.lifetime = 2.0F;
        chip.gravity_scale = 1.0F;
        chip.atlas = ParticleAtlas::tumbling_cube;
        chip.blend = ParticleBlend::alpha;
        chip.collide = true;
        particles.emit_burst(chip, 5U,
                             mix(seed ^ static_cast<std::uint32_t>(index * 0x9E3779B9U)));
    }
}

void emit_projectile_trail(ParticleSystem& particles,
                           std::array<float, 3U> position,
                           std::array<float, 3U> velocity, std::uint32_t seed) {
    // The caller supplies Rocket.update's anchor: the rocket position plus
    // 0.5 z (not the rendered exhaust). It grows as it ages and never
    // collides.
    const float length = std::sqrt(velocity[0U] * velocity[0U] +
                                   velocity[1U] * velocity[1U] +
                                   velocity[2U] * velocity[2U]);
    const float inverse_length = length > 0.0001F ? 1.0F / length : 0.0F;
    // rocket.py passes norm(velocity) * (dt * 75) * .005, then gives each
    // axis an independent 0.70..0.75 multiplier. draw.pyd sub_1000BDD0 stores
    // random_direction*explode_velocity - supplied_velocity, so with zero
    // explode velocity the authored positive vector deliberately becomes a
    // trail moving backwards from the rocket. Keeping the sign here is
    // essential: the old port pushed smoke through the nose of the projectile.
    constexpr float per_tick_speed = (75.0F / 60.0F) * 0.005F;
    ParticleSpawn puff;
    puff.position = position;
    puff.velocity = {
        -velocity[0U] * inverse_length * per_tick_speed *
            (0.70F + random_unit(seed + 1U) * 0.05F),
        -velocity[1U] * inverse_length * per_tick_speed *
            (0.70F + random_unit(seed + 2U) * 0.05F),
        -velocity[2U] * inverse_length * per_tick_speed *
            (0.70F + random_unit(seed + 3U) * 0.05F)};
    // rocket.py passes pure white and a random retail size of 3..6. The
    // native renderer's authored-size multiplier is exactly 0.1.
    puff.color = VxlColor{255U, 255U, 255U, 255U};
    puff.size_begin = 0.30F + random_unit(seed ^ 0x51A0U) * 0.30F;
    // decay_rate=-1 gives size(t)=base*.1*(1+age/lifetime).
    puff.size_end = puff.size_begin * 2.0F;
    puff.alpha_begin = 1.0F;
    // draw.pyd never fades a tinted particle: sub_1000BDD0 stores the RGBA
    // once (this[6..9]) and sub_10033D70 only reads it, so particle_frag's
    // gl_Color alpha stays 255 for the whole 2.5 s. The puff thins out only
    // through the SmokeTrail atlas (60 fps from frame 1, non-looping). The
    // old remaining-life fade made the RPG back-blast a small lumpy puff
    // instead of retail's one big soft cloud.
    puff.alpha_end = 1.0F;
    puff.rotation_degrees = 160.0F + random_unit(seed) * 40.0F;
    puff.rotation_speed = 0.0F;
    puff.lifetime = 2.5F;
    puff.gravity_scale = 0.0F;
    puff.atlas = ParticleAtlas::smoke_trail;
    puff.blend = ParticleBlend::alpha;
    puff.framerate = 60U;
    puff.loop = false;
    puff.start_frame = 1U;
    puff.collide = false;
    particles.emit(puff);
}

void emit_block_cannon_trail(ParticleSystem& particles,
                             std::array<float, 3U> position,
                             std::array<float, 3U> velocity,
                             std::uint32_t seed) {
    // constants.py: velocity multiplier 0.7, size 0.2..0.5,
    // rotation 160..200, lifetime 0.5 and decay 1. ParticleSystem stores
    // per-60Hz displacement, hence the explicit /60 conversion.
    ParticleSpawn puff;
    puff.position = position;
    puff.velocity = {velocity[0U] * (0.7F / 60.0F),
                     velocity[1U] * (0.7F / 60.0F),
                     velocity[2U] * (0.7F / 60.0F)};
    puff.color = VxlColor{215U, 212U, 208U, 255U};
    puff.size_begin = 0.2F;
    puff.size_end = 0.5F;
    puff.alpha_begin = 0.62F;
    puff.alpha_end = 0.0F;
    puff.rotation_degrees = 160.0F + random_unit(seed) * 40.0F;
    puff.rotation_speed = 27.0F;
    puff.lifetime = 0.5F;
    puff.gravity_scale = 0.0F;
    // Snow uses retail's particle_snowke_trail sheet, not the smoke sheet.
    puff.atlas = ParticleAtlas::snowke_trail;
    puff.blend = ParticleBlend::premultiplied;
    puff.framerate = 60U;
    puff.loop = false;
    puff.start_frame = 1U;
    puff.collide = false;
    particles.emit(puff);
}

void emit_block_sucker_debris(ParticleSystem& particles,
                              const TerrainImpactEvent& impact,
                              std::array<float, 3U> barrel_position,
                              std::uint32_t seed) {
    const std::array<float, 3U> source{
        static_cast<float>(impact.cell.x) + 0.5F,
        static_cast<float>(impact.cell.y) + 0.5F,
        static_cast<float>(impact.cell.z) + 0.5F};
    constexpr float travel_ticks{18.0F};
    ParticleSpawn chunk;
    chunk.position = source;
    chunk.velocity = {(barrel_position[0U] - source[0U]) / travel_ticks,
                      (barrel_position[1U] - source[1U]) / travel_ticks,
                      (barrel_position[2U] - source[2U]) / travel_ticks};
    chunk.color = impact.color;
    chunk.explode_velocity = 0.012F;
    chunk.size_begin = 0.09F;
    chunk.size_end = 0.04F;
    chunk.alpha_begin = 1.0F;
    chunk.alpha_end = 0.18F;
    chunk.rotation_speed = 660.0F;
    chunk.lifetime = 0.36F;
    chunk.gravity_scale = 0.0F;
    chunk.drag = 0.01F;
    chunk.atlas = ParticleAtlas::tumbling_cube;
    chunk.blend = ParticleBlend::alpha;
    chunk.loop = true;
    chunk.collide = false;
    particles.emit_burst(chunk, 8U, seed);
}

void emit_lut_smoke(ParticleSystem& particles,
                    std::array<float, 3U> position,
                    const LutSmokeParameters& parameters,
                    std::uint32_t seed) {
    const float size =
        (parameters.min_size + random_unit(seed) * (parameters.max_size - parameters.min_size)) *
        0.1F;
    const float speed =
        parameters.min_speed +
        random_unit(seed ^ 0x5EEDU) * (parameters.max_speed - parameters.min_speed);
    ParticleSpawn puff;
    puff.position = position;
    // draw.pyd stores random_direction*explode_velocity - supplied_velocity;
    // retail supplies (0,0,+speed), so the stored puff drifts toward -z (up).
    puff.velocity = {0.0F, 0.0F, -speed};
    if (parameters.per_axis_speed) {
        const auto axis_speed = [&](std::uint32_t salt) {
            return parameters.min_speed +
                   random_unit(seed ^ salt) * (parameters.max_speed - parameters.min_speed);
        };
        puff.velocity = {-axis_speed(0x0A15U), -axis_speed(0x0B15U), -speed};
    }
    puff.color = VxlColor{255U, 255U, 255U, 255U};
    puff.size_begin = size;
    // decay_rate -1: size(t) = base * (1 + age/lifetime).
    puff.size_end = size * 2.0F;
    puff.alpha_begin = 1.0F;
    puff.alpha_end = 0.0F;
    puff.rotation_degrees = 160.0F + random_unit(seed ^ 0xA11CEU) * 40.0F;
    puff.rotation_speed = 0.0F;
    puff.lifetime = parameters.lifetime;
    puff.gravity_scale = 0.0F;
    puff.atlas = ParticleAtlas::smoke_trail;
    puff.blend = ParticleBlend::alpha;
    puff.color_mode = ParticleColorMode::smoke_lut;
    puff.frames_x = 8U;
    puff.frames_y = 8U;
    puff.start_frame = 1U;
    puff.framerate = 30U;
    puff.forward_animate = true;
    puff.loop = false;
    puff.collide = false;
    particles.emit(puff);
}

void emit_smoke_ring(ParticleSystem& particles,
                     std::array<float, 3U> position,
                     bool snowke,
                     std::uint32_t seed,
                     float radius,
                     const VxlMap* map) {
    if (snowke) {
        emit_snowke_ring(particles, position, VxlColor{255U, 255U, 255U, 255U}, seed, radius);
        return;
    }
    constexpr std::uint32_t ring_count{8U};  // SMOKE_RING_NOOF
    constexpr float ring_lifetime{1.0F};     // SMOKE_RING_LIFETIME
    constexpr float particle_min{3.0F};      // SMOKE_RING_PARTICLE_SIZE_MIN
    constexpr float particle_max{10.0F};     // SMOKE_RING_PARTICLE_SIZE_MAX
    for (std::uint32_t index{}; index < ring_count; ++index) {
        const auto particle_seed = mix(seed ^ (index * 0x9E3779B9U));
        // create_smoke_ring (gameScene 0x10189350): a = i * 2pi / NOOF,
        // x += sin(a) * radius, y += cos(a) * radius.
        const float angle = (static_cast<float>(index) / static_cast<float>(ring_count)) *
                            6.28318530717958647692F;
        const float size =
            (particle_min + random_unit(particle_seed) * (particle_max - particle_min)) * 0.1F;
        ParticleSpawn puff;
        puff.position = {position[0U] + std::sin(angle) * radius,
                         position[1U] + std::cos(angle) * radius, position[2U]};
        // colour = map.get_point(x, y, z): the voxel under that ring point.
        puff.color = VxlColor{255U, 255U, 255U, 255U};
        if (map != nullptr) {
            const auto cell_x = std::floor(puff.position[0U]);
            const auto cell_y = std::floor(puff.position[1U]);
            const auto cell_z = std::floor(puff.position[2U]);
            if (cell_x >= 0.0F && cell_y >= 0.0F && cell_z >= 0.0F) {
                if (const auto color = map->color(static_cast<std::uint32_t>(cell_x),
                                                  static_cast<std::uint32_t>(cell_y),
                                                  static_cast<std::uint32_t>(cell_z));
                    color.has_value()) {
                    puff.color = VxlColor{color->red, color->green, color->blue, 255U};
                }
            }
        }
        // numparticles 1, velocity None, initial_rotation 180,
        // rotation_speed 0, decay_rate -1 (size doubles over the life),
        // start_frame 1, 8x8 frames, forward, no loop, framerate 60, no
        // collision and no gravity.
        puff.velocity = {};
        puff.size_begin = size;
        puff.size_end = size * 2.0F;
        puff.alpha_begin = 1.0F;
        puff.alpha_end = 0.0F;
        puff.rotation_degrees = 180.0F;
        puff.rotation_speed = 0.0F;
        puff.lifetime = ring_lifetime;
        puff.gravity_scale = 0.0F;
        puff.atlas = snowke ? ParticleAtlas::snowke_trail : ParticleAtlas::smoke_trail;
        puff.blend = ParticleBlend::alpha;
        puff.color_mode = snowke ? ParticleColorMode::tinted : ParticleColorMode::smoke_lut;
        puff.frames_x = 8U;
        puff.frames_y = 8U;
        puff.start_frame = 1U;
        puff.framerate = 60U;
        puff.forward_animate = true;
        puff.loop = false;
        puff.collide = false;
        particles.emit(puff);
    }
}

void emit_snowke_ring(ParticleSystem& particles,
                      std::array<float, 3U> position,
                      VxlColor color,
                      std::uint32_t seed,
                      float radius) {
    // GameScene.create_snowke_ring (gameScene 0x102415D0): six points at 60
    // degree steps around the caller's cell, pos + (sin(a)*r + 0.5,
    // cos(a)*r + 0.5, +1.0), size uniform(4, 7), in the caller's colour.
    constexpr std::uint32_t ring_count{6U};
    constexpr float particle_min{4.0F};
    constexpr float particle_max{7.0F};
    const std::array<float, 3U> cell{std::floor(position[0U]), std::floor(position[1U]),
                                     std::floor(position[2U])};
    for (std::uint32_t index{}; index < ring_count; ++index) {
        const auto particle_seed = mix(seed ^ (index * 0x9E3779B9U));
        const float angle = (static_cast<float>(index) / static_cast<float>(ring_count)) *
                            6.28318530717958647692F;
        const float size =
            (particle_min + random_unit(particle_seed) * (particle_max - particle_min)) * 0.1F;
        ParticleSpawn puff;
        puff.position = {cell[0U] + std::sin(angle) * radius + 0.5F,
                         cell[1U] + std::cos(angle) * radius + 0.5F, cell[2U] + 1.0F};
        puff.color = VxlColor{color.red, color.green, color.blue, 255U};
        puff.velocity = {};
        puff.size_begin = size;
        puff.size_end = size * 2.0F;
        puff.alpha_begin = 1.0F;
        puff.alpha_end = 1.0F;
        puff.rotation_degrees = 180.0F;
        puff.rotation_speed = 0.0F;
        puff.lifetime = 1.0F;
        puff.gravity_scale = 0.0F;
        puff.atlas = ParticleAtlas::snowke_trail;
        puff.blend = ParticleBlend::alpha;
        puff.color_mode = ParticleColorMode::tinted;
        puff.frames_x = 8U;
        puff.frames_y = 8U;
        puff.start_frame = 1U;
        puff.framerate = 60U;
        puff.forward_animate = true;
        puff.loop = false;
        puff.collide = false;
        particles.emit(puff);
    }
}

} // namespace battlespades::world
