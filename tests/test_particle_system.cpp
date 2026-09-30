#include "battlespades/world/particle_effects.hpp"
#include "battlespades/world/particle_system.hpp"
#include "battlespades/world/terrain_effects.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace battlespades::world;

void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] VxlMap empty_world() {
    std::vector<std::byte> bytes;
    bytes.reserve(static_cast<std::size_t>(VxlMap::width) * VxlMap::depth * 4U);
    for (std::size_t column{};
         column < static_cast<std::size_t>(VxlMap::width) * VxlMap::depth;
         ++column) {
        bytes.insert(bytes.end(), {std::byte{0U}, std::byte{1U},
                                   std::byte{0U}, std::byte{0U}});
    }
    auto loaded = VxlMap::load(bytes);
    expect(static_cast<bool>(loaded), "synthetic VXL must parse");
    return std::move(*loaded.map);
}

constexpr VxlColor dirt{136U, 96U, 56U, 255U};

[[nodiscard]] ParticleSpawn debris_spawn() {
    ParticleSpawn spawn;
    spawn.position = {64.0F, 64.0F, 100.0F};
    spawn.color = dirt;
    spawn.lifetime = 1.0F;
    spawn.collide = false;
    return spawn;
}

void ring_is_bounded_and_evicts_oldest() {
    ParticleSystem particles;
    for (std::uint32_t index{}; index < 10'000U; ++index) {
        ParticleSpawn spawn = debris_spawn();
        spawn.position[0U] = static_cast<float>(index % 400U);
        particles.emit(spawn);
    }
    expect(particles.live_count() == ParticleSystem::maximum_particles,
           "a flood must saturate the ring, never exceed it");
    particles.build_draw_list({0.0F, 0.0F, 0.0F}, 0.0F);
    expect(particles.instances().size() == ParticleSystem::maximum_particles,
           "every live particle must reach the draw list when unculled");
}

void colours_are_carried_verbatim() {
    // The user requirement: debris from a broken structure must wear the
    // broken blocks' own colours, not an authored effect tint.
    ParticleSystem particles;
    constexpr VxlColor stone{120U, 96U, 80U, 255U};
    ParticleSpawn spawn = debris_spawn();
    spawn.color = stone;
    particles.emit(spawn);
    particles.build_draw_list({0.0F, 0.0F, 0.0F}, 0.0F);
    expect(particles.instances().size() == 1U, "one emit must yield one instance");
    const auto rgba = particles.instances().front().rgba;
    const auto matches = [](float value, std::uint8_t channel) {
        return std::abs(value - static_cast<float>(channel) / 255.0F) < 0.002F;
    };
    expect(matches(rgba[0U], stone.red) && matches(rgba[1U], stone.green) &&
               matches(rgba[2U], stone.blue),
           "particle colour must equal the source voxel colour verbatim");
}

void block_debris_matches_retail_spawn_debris() {
    ParticleSystem particles;
    const TerrainImpactEvent impact{TerrainImpactKind::bullet,
                                    {40U, 40U, 60U},
                                    VxlColor{120U, 96U, 80U, 255U},
                                    {0, 0, -1},
                                    true};
    emit_block_break(particles, impact);
    expect(particles.live_count() == 4U,
           "retail spawn_debris must emit exactly four particles");
    particles.build_draw_list({0.0F, 0.0F, 0.0F}, 0.0F);
    expect(particles.instances().size() == 4U,
           "all four debris particles must reach the renderer");
    for (const auto& instance : particles.instances()) {
        expect(std::abs(instance.size - 0.30F) < 0.0001F,
               "retail authored size 3.0 must render at native size .30");
        expect(instance.rgba[3U] == 1.0F,
               "fresh retail block debris must begin fully opaque");
    }
}

void digging_break_uses_the_same_retail_composition() {
    ParticleSystem particles;
    const TerrainImpactEvent zombie_break{TerrainImpactKind::melee,
                                          {40U, 40U, 60U},
                                          VxlColor{120U, 96U, 80U, 255U},
                                          {0, 0, -1},
                                          true,
                                          1.0F,
                                          24U};
    emit_block_break(particles, zombie_break);
    expect(particles.live_count() == 4U,
           "melee destruction must not stack a fabricated second debris burst");
    particles.tick_unbounded(0.1);
    particles.build_draw_list({}, 0.0F);
    float minimum_x = std::numeric_limits<float>::max();
    float maximum_x = std::numeric_limits<float>::lowest();
    for (const auto& batch : particles.batches()) {
        if (batch.atlas != ParticleAtlas::tumbling_cube) {
            continue;
        }
        for (std::uint32_t offset{}; offset < batch.count; ++offset) {
            const auto x = particles.instances()[batch.first + offset].position[0U];
            minimum_x = std::min(minimum_x, x);
            maximum_x = std::max(maximum_x, x);
        }
    }
    expect(maximum_x - minimum_x > 0.35F,
           "retail explode_velocity .25 must scatter debris before gravity pulls it down");
}

void burst_scatter_matches_the_native_unit_sphere() {
    ParticleSystem particles;
    ParticleSpawn spawn = debris_spawn();
    spawn.explode_velocity = 1.0F;
    spawn.gravity_scale = 0.0F;
    spawn.rotation_degrees = 42.0F;
    particles.emit_burst(spawn, 32U, 0x51A7U);
    particles.tick_unbounded(0.01);
    particles.build_draw_list({}, 0.0F);
    expect(particles.instances().size() == 32U,
           "the native sphere fixture must retain every particle");
    for (const auto& particle : particles.instances()) {
        const float dx = particle.position[0U] - spawn.position[0U];
        const float dy = particle.position[1U] - spawn.position[1U];
        const float dz = particle.position[2U] - spawn.position[2U];
        expect(std::abs(std::sqrt(dx * dx + dy * dy + dz * dz) - 0.32F) < 0.002F,
               "explode_velocity must form a sphere advanced by velocity*32*dt");
        expect(std::abs(particle.rotation_radians -
                        42.0F * 3.14159265358979323846F / 180.0F) < 0.001F,
               "burst creation must not randomize authored rotation");
    }
}

void retail_scatter_does_not_collapse_into_one_hemisphere() {
    ParticleSystem particles;
    ParticleSpawn spawn = debris_spawn();
    spawn.explode_velocity = 1.0F;
    spawn.gravity_scale = 0.0F;
    particles.emit_burst(spawn, 128U, 0x168U);
    particles.tick_unbounded(0.01);
    particles.build_draw_list({}, 0.0F);
    std::array<float, 3U> mean{};
    for (const auto& particle : particles.instances()) {
        for (std::size_t axis{}; axis < mean.size(); ++axis) {
            mean[axis] += (particle.position[axis] - spawn.position[axis]) / 0.32F;
        }
    }
    for (float& axis : mean) axis /= 128.0F;
    expect(std::ranges::all_of(mean, [](float axis) { return std::abs(axis) < 0.15F; }),
           "retail scatter sequence must not collapse a burst into one hemisphere");
}

void burst_lifetimes_are_not_randomized() {
    ParticleSystem particles;
    ParticleSpawn spawn = debris_spawn();
    spawn.lifetime = 1.0F;
    particles.emit_burst(spawn, 32U, 0x168U);
    for (int step{}; step < 9; ++step) {
        particles.tick_unbounded(0.1);
    }
    expect(particles.live_count() == 32U,
           "every member of a native burst must retain the authored lifetime");
    particles.tick_unbounded(0.1);
    expect(particles.live_count() == 0U,
           "the complete native burst must retire at its common lifetime");
}

void atlas_animation_matches_native_direction_and_tick_rules() {
    ParticleSystem particles;
    ParticleSpawn forward = debris_spawn();
    forward.frames_x = 4U;
    forward.frames_y = 1U;
    forward.start_frame = 2U; // native one-based frame 2, renderer frame 1
    forward.framerate = 10U;
    forward.forward_animate = true;
    forward.loop = true;
    particles.emit(forward);
    particles.tick_unbounded(0.1);
    particles.build_draw_list({}, 0.0F);
    expect(particles.instances().front().frame == 2.0F,
           "forward animation must advance one native cell");
    particles.tick_unbounded(0.1);
    particles.build_draw_list({}, 0.0F);
    expect(particles.instances().front().frame == 0.0F,
           "retail forward looping must preserve its terminal-cell wrap rule");

    particles.clear();
    ParticleSpawn reverse = forward;
    reverse.start_frame = 3U;
    reverse.forward_animate = false;
    particles.emit(reverse);
    particles.tick_unbounded(0.1);
    particles.build_draw_list({}, 0.0F);
    expect(particles.instances().front().frame == 1.0F,
           "reverse animation must decrement one native cell");
    particles.tick_unbounded(0.1);
    particles.build_draw_list({}, 0.0F);
    expect(particles.instances().front().frame == 3.0F,
           "retail reverse looping must wrap to the final cell");

    particles.clear();
    forward.start_frame = 1U;
    forward.framerate = 100U;
    particles.emit(forward);
    particles.tick_unbounded(0.1);
    particles.build_draw_list({}, 0.0F);
    expect(particles.instances().front().frame == 1.0F,
           "one long update must advance only one atlas cell, not catch up");
}

void random_start_frame_is_selected_per_burst_particle() {
    ParticleSystem particles;
    ParticleSpawn spawn = debris_spawn();
    spawn.start_frame = 0U;
    particles.emit_burst(spawn, 16U, 0x168U);
    particles.build_draw_list({}, 0.0F);
    const float first = particles.instances().front().frame;
    expect(std::ranges::any_of(particles.instances(), [first](const auto& particle) {
               return particle.frame != first;
           }),
           "colocated native particles must not all reuse one random atlas cell");
}

void rocket_and_grenade_blasts_match_retail_compositions() {
    ParticleSystem particles;
    TerrainImpactEvent impact{
        TerrainImpactKind::explosion, {40U, 40U, 60U},
        VxlColor{120U, 96U, 80U, 255U}, {0, 0, -1}, true, 4.0F, 12U};
    impact.position = {std::array<float, 3U>{40.2F, 40.7F, 60.1F}};
    impact.source_velocity = {75.0F, 0.0F, 0.0F};
    emit_explosion(particles, impact);
    expect(particles.live_count() == 18U,
           "Rocket.delete must emit eight glow blocks plus ten map-colour particles");
    particles.build_draw_list({0.0F, 0.0F, 0.0F}, 0.0F);
    expect(std::ranges::any_of(particles.instances(), [](const auto& particle) {
               return std::abs(particle.position[0U] - 40.2F) < 0.001F &&
                      std::abs(particle.position[1U] - 40.7F) < 0.001F &&
                      std::abs(particle.position[2U] - 60.1F) < 0.001F;
           }),
           "an RPG blast must originate at its exact entity position, not a voxel centre");
    const auto glow_batch = std::ranges::find_if(
        particles.batches(), [](const auto& batch) {
            return batch.atlas == ParticleAtlas::glow_cube;
        });
    expect(glow_batch != particles.batches().end() &&
               glow_batch->blend == ParticleBlend::alpha &&
               glow_batch->color_mode == ParticleColorMode::glow_lut &&
               glow_batch->count == 8U,
           "GlowBlockParticles must use retail's alpha-blended heat LUT path");
    particles.clear();
    // ExplodeOnImpactEntity's only subclass is the GLGrenade (tool 55).
    impact.source_tool = 55U;
    impact.radius = 3.0F;
    emit_explosion(particles, impact);
    expect(particles.live_count() == 14U,
           "ExplodeOnImpactEntity must emit four glow blocks plus ten particles");
    particles.clear();
    // Grenade.update (gameScene 0x100AE790) passes 8, like Rocket.delete.
    impact.source_tool = 11U;
    emit_explosion(particles, impact);
    expect(particles.live_count() == 18U,
           "a hand grenade must emit eight glow blocks plus ten particles");
    particles.build_draw_list({0.0F, 0.0F, 0.0F}, 0.0F);
    // Grenade.update's debris call is (10, 1.3, 5.0): authored size 5 is a
    // 0.5 half-extent, half the Rocket.delete chunk, with the default 2 s life.
    const auto grenade_debris = std::ranges::count_if(
        particles.instances(), [](const auto& particle) {
            return std::abs(particle.size - 0.5F) < 0.001F;
        });
    expect(grenade_debris == 10,
           "hand-grenade debris must use Grenade.update's authored size 5.0");
    particles.clear();
    // LandmineEntity/DynamiteEntity/Drill/C4 on_delete: (10, 1.5, 5.0).
    impact.source_tool = 21U;
    emit_explosion(particles, impact);
    expect(particles.live_count() == 18U,
           "dynamite must emit eight glow blocks plus ten particles");
    particles.clear();
    // BombPickup.explode: create(12) plus (15, 1.3, 10.0).
    impact.source_tool = 25U;
    emit_explosion(particles, impact);
    expect(particles.live_count() == 27U,
           "the bomb must emit twelve glow blocks plus fifteen particles");
}

void sticky_blast_debris_keeps_its_retail_size_speed_and_lifetime() {
    ParticleSystem particles;
    particles.set_gravity(0.0F); // Isolate the emitted speed from gravity.
    const std::array<float, 3U> origin{40.2F, 40.7F, 60.1F};
    TerrainImpactEvent impact{
        TerrainImpactKind::explosion, {40U, 40U, 60U},
        VxlColor{48U, 48U, 48U, 255U}, {0, 0, -1}, true, 4.0F, 57U, origin};
    emit_explosion(particles, impact);
    expect(particles.live_count() == 18U,
           "AttachedStickyGrenadeEntity must emit eight glow blocks plus ten debris");

    const auto check_debris = [&](float expected_size, float expected_distance) {
        particles.build_draw_list({}, 0.0F);
        std::uint32_t count{};
        for (const auto& batch : particles.batches()) {
            if (batch.atlas != ParticleAtlas::tumbling_cube) continue;
            for (std::uint32_t offset{}; offset < batch.count; ++offset) {
                const auto& piece = particles.instances()[batch.first + offset];
                expect(std::abs(piece.size - expected_size) < 0.001F,
                       "sticky debris must use authored size 5 and the default two-second decay");
                const auto dx = piece.position[0U] - origin[0U];
                const auto dy = piece.position[1U] - origin[1U];
                const auto dz = piece.position[2U] - origin[2U];
                expect(std::abs(std::sqrt(dx * dx + dy * dy + dz * dz) -
                                expected_distance) < 0.01F,
                       "sticky debris must scatter at native explode_velocity 1.5");
                ++count;
            }
        }
        expect(count == 10U, "all ten sticky debris pieces must survive beyond one second");
    };

    check_debris(0.5F, 0.0F);
    for (int tick{}; tick < 72; ++tick) particles.tick_unbounded(1.0 / 60.0);
    // Rocket's old fallback had already retired all debris after one second.
    check_debris(0.2F, 1.5F * 32.0F * 1.2F);
    for (int tick{}; tick < 54; ++tick) particles.tick_unbounded(1.0 / 60.0);
    particles.build_draw_list({}, 0.0F);
    expect(std::ranges::none_of(particles.batches(), [](const auto& batch) {
        return batch.atlas == ParticleAtlas::tumbling_cube;
    }), "sticky debris must retire at its two-second lifetime");
}

void glow_trail_children_grow_like_the_spawn_point() {
    // GLOW_SMOKE_TRAIL_SPAWN_POINT: decay_rate -1 (size doubles over the one
    // second life), 60 fps, and sub_10033C00's forward flag is rand() != 0.
    ParticleSystem particles;
    TerrainImpactEvent impact{
        TerrainImpactKind::explosion, {40U, 40U, 60U},
        VxlColor{0U, 0U, 0U, 255U}, {0, 0, -1}, true, 4.0F, 12U};
    impact.position = {std::array<float, 3U>{40.2F, 40.7F, 60.1F}};
    emit_explosion(particles, impact);
    particles.tick_unbounded(1.0 / 60.0);
    particles.build_draw_list({}, 0.0F);
    std::vector<float> initial;
    for (const auto& batch : particles.batches()) {
        if (batch.color_mode != ParticleColorMode::smoke_lut) {
            continue;
        }
        for (std::uint32_t offset{}; offset < batch.count; ++offset) {
            initial.push_back(particles.instances()[batch.first + offset].size);
        }
    }
    expect(initial.size() == 8U, "one child per glow parent after the first update");
    for (int step{}; step < 30; ++step) {
        particles.tick_unbounded(1.0 / 60.0);
    }
    particles.build_draw_list({}, 0.0F);
    float largest_initial{};
    for (const float size : initial) {
        largest_initial = std::max(largest_initial, size);
    }
    int grown{};
    for (const auto& batch : particles.batches()) {
        if (batch.color_mode != ParticleColorMode::smoke_lut) {
            continue;
        }
        for (std::uint32_t offset{}; offset < batch.count; ++offset) {
            const auto& instance = particles.instances()[batch.first + offset];
            if (instance.life01 > 0.45F && instance.size > 0.30F * 1.45F &&
                instance.size <= 0.9003F * 2.0F) {
                ++grown;
            }
        }
    }
    expect(grown >= 8 && largest_initial <= 0.9003F,
           "glow smoke-trail children must grow (decay -1), never shrink");
}

void rocket_glow_emits_the_native_child_smoke_trails() {
    ParticleSystem particles;
    TerrainImpactEvent impact{
        TerrainImpactKind::explosion, {40U, 40U, 60U},
        VxlColor{0U, 0U, 0U, 255U}, {0, 0, -1}, true, 4.0F, 12U};
    impact.position = {std::array<float, 3U>{40.2F, 40.7F, 60.1F}};
    emit_explosion(particles, impact);

    for (int frame{}; frame < 3; ++frame) {
        particles.tick_unbounded(1.0 / 60.0);
    }
    expect(particles.live_count() == 42U,
           "three updates must retain 8 glow parents, 10 debris and 24 spawn-point children");
    particles.build_draw_list({}, 0.0F);
    const auto glow_batch = std::ranges::find_if(
        particles.batches(), [](const auto& batch) {
            return batch.atlas == ParticleAtlas::glow_cube &&
                   batch.color_mode == ParticleColorMode::glow_lut;
        });
    expect(glow_batch != particles.batches().end() && glow_batch->count == 8U,
           "the RPG impact must retain its eight LUT glow-cube parents");
    const auto smoke_batch = std::ranges::find_if(
        particles.batches(), [](const auto& batch) {
            return batch.atlas == ParticleAtlas::smoke_trail &&
                   batch.color_mode == ParticleColorMode::smoke_lut;
        });
    expect(smoke_batch != particles.batches().end() && smoke_batch->count == 24U,
           "each glow parent update must leave one SmokeTrail/particle_lut child");
    const auto child_count = std::ranges::count_if(
        particles.instances(), [](const auto& instance) {
            return instance.life01 == 0.0F && instance.size >= 0.30F &&
                   instance.size <= 0.90F;
        });
    expect(child_count >= 8,
           "the latest trail generation must use the native authored size range 3..9");
}

void rocket_smoke_uses_native_growth_and_fade() {
    ParticleSystem particles;
    emit_projectile_trail(particles, {10.0F, 20.0F, 30.0F},
                          {75.0F, 0.0F, 0.0F}, 0x168U);
    particles.build_draw_list({}, 0.0F);
    expect(particles.instances().size() == 1U &&
               particles.batches().size() == 1U &&
               particles.batches().front().atlas == ParticleAtlas::smoke_trail &&
               particles.batches().front().blend == ParticleBlend::alpha,
           "rocket.py smoke must use the normal alpha-blended 8x8 atlas");
    expect(std::abs(particles.instances().front().position[0U] - 10.0F) < 0.0001F &&
               std::abs(particles.instances().front().position[1U] - 20.0F) < 0.0001F &&
               std::abs(particles.instances().front().position[2U] - 30.0F) < 0.0001F,
           "the smoke emitter must preserve the caller's rendered exhaust anchor");
    const float initial_size = particles.instances().front().size;
    expect(initial_size >= 0.30F && initial_size <= 0.60F,
           "rocket smoke authored size 3..6 must use draw.pyd's exact 0.1 scale");
    particles.tick_unbounded(0.1);
    particles.build_draw_list({}, 0.0F);
    expect(particles.instances().front().position[0U] < 10.0F,
           "draw.pyd must subtract the supplied smoke velocity so the trail moves behind the rocket");
    for (int step{}; step < 11; ++step) {
        particles.tick_unbounded(0.1);
    }
    particles.tick_unbounded(0.05);
    particles.build_draw_list({}, 0.0F);
    expect(particles.instances().size() == 1U &&
               std::abs(particles.instances().front().size - initial_size * 1.5F) < 0.001F &&
               std::abs(particles.instances().front().rgba[3U] - 1.0F) < 0.001F,
           "decay=-1 must grow rocket smoke; draw.pyd never fades a tinted particle's gl_Color");
}

void corpse_and_grave_have_distinct_retail_bursts() {
    ParticleSystem particles;
    TerrainImpactEvent impact{
        TerrainImpactKind::corpse_explosion, {40U, 40U, 60U},
        VxlColor{55U, 125U, 215U, 255U}, {0, 0, -1}, true, 3.0F};
    emit_corpse_explosion(particles, impact);
    expect(particles.live_count() == 40U,
           "Character.explode_corpse must emit its exact 40 body particles");
    particles.build_draw_list({}, 0.0F);
    expect(std::ranges::all_of(particles.instances(), [](const auto& particle) {
               return std::abs(particle.size - 0.50F) < 0.0001F &&
                      particle.rgba[0U] == 1.0F && particle.rgba[1U] == 0.0F &&
                      particle.rgba[2U] == 0.0F;
           }),
           "corpse particles must be bright red and use authored size 5");
    for (int step{}; step < 10; ++step) {
        particles.tick_unbounded(0.1);
    }
    particles.build_draw_list({}, 0.0F);
    expect(std::ranges::all_of(particles.instances(), [](const auto& particle) {
               return particle.rgba[3U] == 1.0F;
           }),
           "corpse chunks must remain opaque; native lifetime decay changes size only");
    particles.clear();
    impact.kind = TerrainImpactKind::grave_explosion;
    std::string error;
    auto grave = Kv6Model::load_file(
        std::filesystem::path{AOS_TEST_ASSET_ROOT} / "kv6" / "grave.kv6",
        &error);
    expect(grave.has_value(), "the retail grave model fixture must load");
    grave->offset_pivots({0.0F, 0.0F, 11.0F});
    emit_grave_explosion(particles, impact, &*grave);
    expect(particles.live_count() == grave->voxels().size(),
           "grave deletion must emit one particle for every displayed KV6 voxel");
    expect(particles.live_count() == 800U,
           "the shipping grave.kv6 fixture must retain its 800 display tuples");
    particles.build_draw_list({0.0F, 0.0F, 0.0F}, 0.0F);
    expect(std::ranges::all_of(particles.instances(), [](const auto& particle) {
               return std::abs(particle.size - 0.30F) < 0.0001F;
           }),
           "explode_display size=3 must render as native size 0.3");
    expect(std::ranges::any_of(particles.instances(), [](const auto& particle) {
               return particle.rgba[0U] == 0.0F && particle.rgba[1U] == 0.0F &&
                      particle.rgba[2U] == 0.0F;
           }),
           "explode_display must preserve the grave model's raw black tuples");
}

void observer_shot_has_a_compact_muzzle_burst() {
    ParticleSystem particles;
    emit_weapon_muzzle(particles, {10.0F, 20.0F, 30.0F},
                       {0.0F, 1.0F, 0.0F}, 0x168U);
    expect(particles.live_count() == 4U,
           "one server-confirmed shot must emit three flashes and one smoke puff");
}

void sticky_fragments_sample_raw_model_tuples_in_the_display_pose() {
    std::string error;
    const auto model = Kv6Model::load_file(
        std::filesystem::path{AOS_TEST_ASSET_ROOT} / "kv6" / "stickygrenade.kv6",
        &error);
    expect(model.has_value() && model->voxels().size() == 138U,
           "the retained retail sticky fixture must contain 138 tuples");
    // A 90-degree turn, 0.06 display size and a nonzero translation. The
    // retail first tuple is (0,2,3), pivot (3.5,3.5,6), RGB (228,200,104).
    const std::array<float, 16U> display{
        0.0F, 0.06F, 0.0F, 0.0F,
        0.0F, 0.0F, -0.06F, 0.0F,
        -0.06F, 0.0F, 0.0F, 0.0F,
        10.0F, 20.0F, 30.0F, 1.0F};
    ParticleSystem particles;
    particles.set_gravity(0.0F);
    emit_sticky_model_explosion(particles, *model, display, 0x57168U);
    expect(particles.live_count() == 28U,
           "sticky deletion must use get_points()[::5], including the last partial stride");
    particles.build_draw_list({}, 0.0F);
    expect(std::ranges::any_of(particles.instances(), [](const auto& particle) {
        return std::abs(particle.position[0U] - 10.56F) < 0.00001F &&
               std::abs(particle.position[1U] - 20.32F) < 0.00001F &&
               std::abs(particle.position[2U] - 30.32F) < 0.00001F &&
               std::abs(particle.rgba[0U] - 228.0F / 255.0F) < 0.00001F &&
               std::abs(particle.rgba[1U] - 200.0F / 255.0F) < 0.00001F &&
               std::abs(particle.rgba[2U] - 104.0F / 255.0F) < 0.00001F;
    }), "fragment positions must truncate half pivots, rotate, and add retail's half-cell offset");
    expect(particles.batches().size() == 1U &&
               particles.batches().front().atlas == ParticleAtlas::tumbling_cube &&
               std::ranges::all_of(particles.instances(), [](const auto& particle) {
        return std::abs(particle.size - 0.3F) < 0.00001F;
    }), "model fragments must use authored size 3, independently of blast debris size 5");
    for (int step{}; step < 12; ++step)
        particles.tick_unbounded(0.1);
    particles.build_draw_list({}, 0.0F);
    expect(particles.live_count() == 28U &&
               std::abs(particles.instances().front().size - 0.12F) < 0.0001F,
           "model fragments must retain their default two-second lifetime and linear size decay");
    for (int step{}; step < 9; ++step)
        particles.tick_unbounded(0.1);
    expect(particles.live_count() == 0U, "sticky model fragments must retire after two seconds");
}

void block_gadgets_have_their_recovered_directional_feedback() {
    ParticleSystem particles;
    emit_block_cannon_trail(particles, {10.0F, 20.0F, 30.0F},
                            {50.0F, 0.0F, 0.0F}, 29U);
    expect(particles.live_count() == 1U,
           "Block Cannon must emit one recovered smoke puff per flight tick");

    const TerrainImpactEvent sucked{TerrainImpactKind::melee,
                                    {15U, 20U, 30U}, dirt,
                                    {1, 0, 0}, true, 1.0F, 63U};
    emit_block_sucker_debris(particles, sucked, {10.0F, 20.0F, 30.0F}, 42U);
    expect(particles.live_count() == 9U,
           "Block Sucker must pull eight coloured chunks toward its barrel");
}

void retail_lut_smoke_and_rings_follow_constants() {
    ParticleSystem particles;
    emit_lut_smoke(particles, {5.0F, 5.0F, 50.0F}, block_fire_smoke, 7U);
    expect(particles.live_count() == 1U, "create_fire_smoke emits exactly one puff");
    particles.build_draw_list({}, 0.0F);
    expect(particles.batches().size() == 1U &&
               particles.batches().front().atlas == ParticleAtlas::smoke_trail &&
               particles.instances().front().size >= 0.4F - 1e-4F &&
               particles.instances().front().size <= 0.8F + 1e-4F,
           "block-fire smoke is a LUT SmokeTrail puff of retail size 4..8");
    const float z_before = particles.instances().front().position[2U];
    const float x_before = particles.instances().front().position[0U];
    const float y_before = particles.instances().front().position[1U];
    particles.tick_unbounded(0.5);
    particles.build_draw_list({}, 0.0F);
    expect(particles.instances().front().position[2U] <= z_before,
           "fire smoke must rise (toward -z) or hover, never sink");
    expect(block_fire_smoke.per_axis_speed &&
               particles.instances().front().position[0U] <= x_before &&
               particles.instances().front().position[1U] <= y_before &&
               (particles.instances().front().position[0U] < x_before ||
                particles.instances().front().position[1U] < y_before),
           "create_fire_smoke draws uniform(0, 0.1) on every axis (stored negated)");

    ParticleSystem rings;
    emit_smoke_ring(rings, {10.0F, 10.0F, 40.0F}, true, 11U);
    expect(rings.live_count() == 6U, "create_snowke_ring emits six puffs");
    {
        ParticleSystem snow;
        emit_snowke_ring(snow, {10.2F, 10.7F, 40.5F}, VxlColor{200U, 10U, 20U, 255U}, 3U);
        snow.build_draw_list({}, 0.0F);
        // a = 0: floor(pos) + (sin 0 + 0.5, cos 0 + 0.5, +1) = (10.5, 11.5, 41).
        const auto head = std::ranges::find_if(snow.instances(), [](const auto& instance) {
            return std::abs(instance.position[0U] - 10.5F) < 0.01F &&
                   std::abs(instance.position[1U] - 11.5F) < 0.01F &&
                   std::abs(instance.position[2U] - 41.0F) < 0.01F;
        });
        expect(snow.live_count() == 6U && head != snow.instances().end() &&
                   head->size >= 0.4F && head->size <= 0.7F &&
                   std::abs(head->rgba[0U] - 200.0F / 255.0F) < 0.01F,
               "the snowke ring uses retail's +0.5/+1 layout, size 4..7 and the caller colour");
    }
    rings.build_draw_list({}, 0.0F);
    expect(std::ranges::all_of(rings.batches(),
                               [](const auto& batch) {
                                   return batch.atlas == ParticleAtlas::snowke_trail;
                               }),
           "the snowke ring must use the SnowkeTrail atlas");

    // create_smoke_ring(position, radius): x += sin(a)*r, y += cos(a)*r,
    // static puffs at rotation 180 in the map colour of each ring point.
    auto ground = empty_world();
    expect(ground.set_voxel(10U, 12U, 40U, dirt), "ring colour fixture");
    ParticleSystem smoke;
    emit_smoke_ring(smoke, {10.5F, 10.5F, 40.5F}, false, 5U, 2.0F, &ground);
    smoke.build_draw_list({}, 0.0F);
    const auto first = std::ranges::find_if(smoke.instances(), [](const auto& instance) {
        return std::abs(instance.position[0U] - 10.5F) < 0.01F &&
               std::abs(instance.position[1U] - 12.5F) < 0.01F;
    });
    expect(smoke.live_count() == 8U && first != smoke.instances().end() &&
               std::abs(first->rotation_radians - 3.14159265F) < 0.001F,
           "the ring honours the caller radius with retail's fixed 180-degree puffs");
    smoke.tick_unbounded(0.5);
    smoke.build_draw_list({}, 0.0F);
    const auto moved = std::ranges::any_of(smoke.instances(), [](const auto& instance) {
        const float dx = instance.position[0U] - 10.5F;
        const float dy = instance.position[1U] - 10.5F;
        return std::abs(std::sqrt(dx * dx + dy * dy) - 2.0F) > 0.01F;
    });
    expect(!moved, "velocity=None: ring puffs do not drift outward");
}

void tilted_structure_bursts_at_its_visible_transform() {
    ParticleSystem particles;
    const FallingComponent component{{{1U, 0U, 0U}, dirt}};
    emit_falling_blocks_breakup(particles, component, {10.0F, 10.0F, 10.0F},
                                {0.5F, 0.5F, 0.5F}, {0.0F, 90.0F, 0.0F}, {},
                                0xC011A95EU);
    particles.build_draw_list({0.0F, 0.0F, 0.0F}, 0.0F);
    expect(std::ranges::any_of(particles.instances(), [](const auto& instance) {
               return std::abs(instance.position[0U] - 10.0F) < 0.01F &&
                      std::abs(instance.position[1U] - 10.0F) < 0.01F &&
                      std::abs(instance.position[2U] - 9.0F) < 0.01F;
           }),
           "collapse debris must originate from the tilted mesh, not its old upright cells");
}

void falling_blocks_breakup_samples_every_mod_th_voxel_into_five() {
    ParticleSystem particles;
    FallingComponent component;
    for (std::uint32_t x{}; x < 64U; ++x) {
        component.push_back({{x, 10U, 40U}, dirt});
    }
    // int(5 + 64/8000*10) = 5: voxels 0, 5, ..., 60 -> 13 x 5 particles.
    emit_falling_blocks_breakup(particles, component, {32.0F, 10.0F, 40.0F},
                                {32.0F, 10.0F, 40.0F}, {}, {0.0F, 0.0F, -0.2F},
                                0xB10C5U);
    expect(particles.live_count() == 65U,
           "FallingBlocks breakup emits five particles for every mod-th voxel");
    particles.build_draw_list({}, 0.0F);
    for (const auto& image : particles.instances()) {
        expect(std::abs(image.size - 0.5F) < 0.001F,
               "authored size 5.0 renders at the native 0.1 scale");
    }
    expect(falling_blocks_particle_mod(0U) == 5U && falling_blocks_particle_mod(799U) == 5U &&
               falling_blocks_particle_mod(800U) == 6U &&
               falling_blocks_particle_mod(8'000U) == 15U &&
               falling_blocks_particle_mod(16'000U) == 25U,
           "mod = int(5 + size / 8000 * 10), unclamped like retail");
}

void shoot_response_blood_matches_retail_composition() {
    ParticleSystem particles;
    emit_player_blood(particles, {12.5F, 33.0F, 48.25F}, 0xB100DU);
    expect(particles.live_count() == 5U,
           "one blood-bearing ShootResponse must emit retail's exact five blood particles");
    particles.build_draw_list({0.0F, 0.0F, 0.0F}, 0.0F);
    expect(particles.instances().size() == 5U,
           "all five blood particles must reach the renderer");
    for (const auto& particle : particles.instances()) {
        expect(std::abs(particle.rgba[0U] - 127.0F / 255.0F) < 0.002F &&
                   particle.rgba[1U] == 0.0F && particle.rgba[2U] == 0.0F,
               "blood particles must use retail's dark-red (127,0,0) color");
    }
}

void server_confirmed_entity_hit_is_visible() {
    ParticleSystem particles;
    emit_entity_hit(particles, {18.5F, 21.25F, 44.0F}, 0x168U);
    expect(particles.live_count() == 5U,
           "one HitEntity packet must emit retail Entity.hit's five particles");
    particles.build_draw_list({0.0F, 0.0F, 0.0F}, 0.0F);
    expect(particles.instances().size() == 5U,
           "every server-confirmed entity-hit particle must reach the renderer");
    for (const auto& particle : particles.instances()) {
        expect(std::abs(particle.rgba[0U] - 127.0F / 255.0F) < 0.002F &&
                   std::abs(particle.rgba[1U] - 127.0F / 255.0F) < 0.002F &&
                   std::abs(particle.rgba[2U] - 127.0F / 255.0F) < 0.002F,
               "entity-hit debris must use retail's grey (127,127,127)");
    }
    expect(std::ranges::all_of(
               particles.batches(),
               [](const auto& batch) { return batch.blend == ParticleBlend::alpha; }),
           "entity hits are solid debris cubes, not additive sparks");
}

void crate_pickup_matches_retail_twinkle_burst() {
    ParticleSystem particles;
    emit_crate_pickup(particles, {12.5F, 18.0F, 42.25F}, 0xC8A7EU);
    expect(particles.live_count() == 25U,
           "a consumed supply crate must emit retail's exact 25 particles");
    particles.build_draw_list({}, 0.0F);
    expect(particles.batches().size() == 1U &&
               particles.batches().front().atlas == ParticleAtlas::pickup_twinkle &&
               particles.batches().front().blend == ParticleBlend::additive &&
               particles.batches().front().count == 25U,
           "crate particles must use the authored additive 4x4 twinkle atlas");
    expect(std::ranges::all_of(particles.instances(), [](const auto& particle) {
               return std::abs(particle.size - 0.40F) < 0.001F;
           }),
           "crate twinkles must start at retail particle size four");
}

void diamond_pickup_matches_retail_twinkle_burst() {
    ParticleSystem particles;
    emit_diamond_pickup(particles, {12.5F, 18.0F, 42.25F}, 0xD1A0U);
    expect(particles.live_count() == 50U,
           "a collected diamond must emit DIAMOND_PICKUP_FX_NOOF (50) particles");
    particles.build_draw_list({}, 0.0F);
    expect(particles.batches().size() == 1U &&
               particles.batches().front().atlas == ParticleAtlas::pickup_twinkle &&
               particles.batches().front().blend == ParticleBlend::additive,
           "diamond particles must use the additive 4x4 twinkle atlas");
    expect(std::ranges::all_of(particles.instances(), [](const auto& particle) {
               return std::abs(particle.size - 0.50F) < 0.001F;
           }),
           "diamond twinkles must start at retail particle size five");
}

void structure_burst_scales_with_component_size() {
    ParticleSystem particles;
    ParticleSpawn spawn = debris_spawn();
    // A 200-voxel component sampled one-per-15 is the retail throttle floor.
    constexpr std::uint32_t expected = 200U / 15U;
    particles.emit_burst(spawn, expected, 0x1234U);
    expect(particles.live_count() >= expected,
           "a structure burst must emit at least one particle per sampled block");
}

void quality_scale_changes_capacity_without_thinning_authored_bursts() {
    ParticleSystem particles;
    particles.set_quality_scale(0.1F);
    particles.emit_burst(debris_spawn(), 24U, 7U);
    expect(particles.live_count() == 24U,
           "effect quality must not thin an authored retail burst");
    particles.emit_burst(debris_spawn(), 500U, 8U);
    expect(particles.live_count() <=
               static_cast<std::size_t>(
                   std::lround(ParticleSystem::maximum_particles * 0.1F)),
           "low effect quality must bound the reusable particle pool instead");
}

void lifetimes_retire_and_dt_is_clamped() {
    auto map = empty_world();
    ParticleSystem particles;
    ParticleSpawn spawn = debris_spawn();
    spawn.lifetime = 0.5F;
    particles.emit(spawn);
    expect(particles.live_count() == 1U, "particle must start alive");
    // A single oversized frame must clamp to 0.1 s, matching the effect sim.
    particles.tick(5.0, map);
    expect(particles.live_count() == 1U,
           "one long frame must not retire a particle early; dt clamps to 0.1");
    for (int step{}; step < 10; ++step) {
        particles.tick(0.1, map);
    }
    expect(particles.live_count() == 0U, "expired particles must retire");
}

void gravity_exempt_particles_do_not_fall() {
    auto map = empty_world();
    ParticleSystem particles;
    ParticleSpawn smoke = debris_spawn();
    smoke.gravity_scale = 0.0F;
    smoke.lifetime = 4.0F;
    particles.emit(smoke);
    for (int step{}; step < 60; ++step) {
        particles.tick(1.0 / 60.0, map);
    }
    particles.build_draw_list({0.0F, 0.0F, 0.0F}, 0.0F);
    expect(particles.instances().size() == 1U, "smoke must survive one second");
    expect(std::abs(particles.instances().front().position[2U] - 100.0F) < 0.001F,
           "gravity-exempt particles must not accumulate downward velocity");
}

void particle_gravity_follows_state_data_gravity() {
    // ParticleEffectManager.set_particles_gravity(world.get_gravity()):
    // LunarBase's 26/64 StateData gravity must slow every falling particle.
    auto map = empty_world();
    const auto drop = [&](float gravity) {
        ParticleSystem particles;
        particles.set_gravity(gravity);
        ParticleSpawn chunk = debris_spawn();
        chunk.gravity_scale = 1.0F;
        chunk.lifetime = 4.0F;
        particles.emit(chunk);
        for (int step{}; step < 30; ++step) {
            particles.tick(1.0 / 60.0, map);
        }
        particles.build_draw_list({0.0F, 0.0F, 0.0F}, 0.0F);
        return particles.instances().front().position[2U] - 100.0F;
    };
    const float full = drop(1.0F);
    const float lunar = drop(26.0F / 64.0F);
    expect(full > 0.1F && std::abs(lunar / full - 26.0F / 64.0F) < 1.0e-3F,
           "particle gravity must scale with the StateData world gravity");
}

void collision_bounces_debris_on_the_crossed_axis() {
    auto map = empty_world();
    ParticleSystem particles;
    ParticleSpawn spawn = debris_spawn();
    // z grows downward; the synthetic world is solid from z=239.
    spawn.position = {64.0F, 64.0F, 238.5F};
    spawn.velocity = {0.0F, 0.0F, 0.5F};
    spawn.gravity_scale = 0.0F;
    spawn.collide = true;
    spawn.lifetime = 5.0F;
    particles.emit(spawn);
    particles.tick(1.0 / 60.0, map);
    particles.tick(1.0 / 60.0, map); // attempts to enter the z=239 floor
    particles.tick(1.0 / 60.0, map); // reversed half-speed velocity moves upward
    particles.build_draw_list({0.0F, 0.0F, 0.0F}, 0.0F);
    expect(particles.instances().size() == 1U, "colliding debris must survive");
    expect(particles.instances().front().position[2U] < 238.76F,
           "native collision must reverse and damp z instead of resting in place");
}

void batches_group_by_atlas_and_blend() {
    ParticleSystem particles;
    ParticleSpawn alpha_cube = debris_spawn();
    alpha_cube.atlas = ParticleAtlas::tumbling_cube;
    alpha_cube.blend = ParticleBlend::alpha;
    particles.emit(alpha_cube);
    particles.emit(alpha_cube);

    ParticleSpawn additive_glow = debris_spawn();
    additive_glow.atlas = ParticleAtlas::glow_cube;
    additive_glow.blend = ParticleBlend::additive;
    particles.emit(additive_glow);

    particles.build_draw_list({0.0F, 0.0F, 0.0F}, 0.0F);
    expect(particles.batches().size() == 2U,
           "two distinct atlas/blend pairs must produce exactly two batches");
    std::size_t total{};
    for (const auto& batch : particles.batches()) {
        total += batch.count;
        expect(batch.first + batch.count <= particles.instances().size(),
               "every batch range must stay inside the instance stream");
    }
    expect(total == 3U, "batches must cover every emitted particle exactly once");

    ParticleSpawn lut_glow = additive_glow;
    lut_glow.color_mode = ParticleColorMode::glow_lut;
    particles.emit(lut_glow);
    particles.build_draw_list({0.0F, 0.0F, 0.0F}, 0.0F);
    expect(particles.batches().size() == 3U,
           "one atlas/blend pair using two fragment colour paths must not merge batches");
}

void blended_particles_sort_back_to_front() {
    ParticleSystem particles;
    for (float distance : {10.0F, 40.0F, 25.0F}) {
        ParticleSpawn spawn = debris_spawn();
        spawn.position = {distance, 0.0F, 0.0F};
        particles.emit(spawn);
    }
    particles.build_draw_list({0.0F, 0.0F, 0.0F}, 0.0F);
    const auto instances = particles.instances();
    expect(instances.size() == 3U, "three particles must survive");
    expect(instances[0U].position[0U] > instances[1U].position[0U] &&
               instances[1U].position[0U] > instances[2U].position[0U],
           "alpha particles must be submitted farthest-first; nothing writes depth");
}

void fog_distance_culls_far_particles() {
    ParticleSystem particles;
    ParticleSpawn near_spawn = debris_spawn();
    near_spawn.position = {10.0F, 0.0F, 0.0F};
    particles.emit(near_spawn);
    ParticleSpawn far_spawn = debris_spawn();
    far_spawn.position = {500.0F, 0.0F, 0.0F};
    particles.emit(far_spawn);
    particles.build_draw_list({0.0F, 0.0F, 0.0F}, 192.0F);
    expect(particles.instances().size() == 1U,
           "particles past the fog distance must be culled from the draw list");
    expect(particles.live_count() == 2U,
           "culling is a draw-list decision and must not kill the simulation");
}

void clear_releases_everything() {
    ParticleSystem particles;
    particles.emit_burst(debris_spawn(), 64U, 3U);
    expect(particles.live_count() > 0U, "burst must populate the pool");
    particles.clear();
    expect(particles.live_count() == 0U, "clear must retire every particle");
    particles.build_draw_list({0.0F, 0.0F, 0.0F}, 0.0F);
    expect(particles.instances().empty(), "clear must empty the draw list");
}

void placement_feedback_is_visible_and_prefab_bounded() {
    ParticleSystem particles;
    constexpr VxlColor team_block{30U, 110U, 210U, 255U};
    emit_block_placement(particles, {20, 30, 40}, team_block, 7U);
    expect(particles.live_count() == 4U,
           "one authoritative block placement must emit one compact four-puff burst");

    std::vector<PrefabPlacementVoxel> construct;
    construct.reserve(256U);
    for (std::int32_t x{}; x < 256; ++x) {
        construct.push_back({{x, 60, 70}, team_block});
    }
    emit_prefab_placement(particles, construct, 11U);
    expect(particles.live_count() == 4U + 24U * 4U,
           "large prefab feedback must be distributed but hard bounded");
    particles.build_draw_list({}, 0.0F);
    expect(!particles.instances().empty(), "placement puffs must reach the renderer");
}

void rocketpack_death_exhaust_contains_flame_and_smoke() {
    ParticleSystem particles;
    emit_jetpack_death_thruster(particles, {10.0F, 20.0F, 30.0F},
                                {0.0F, 0.0F, -8.0F}, 68U);
    expect(particles.live_count() == 5U,
           "one rocketpack death frame must emit three flames and two broad smoke puffs");
    particles.build_draw_list({}, 0.0F);
    expect(std::ranges::any_of(particles.batches(), [](const auto& batch) {
               return batch.atlas == ParticleAtlas::glow_cube &&
                      batch.blend == ParticleBlend::additive;
           }) &&
               std::ranges::any_of(particles.batches(), [](const auto& batch) {
                   return batch.atlas == ParticleAtlas::smoke_trail &&
                          batch.blend == ParticleBlend::premultiplied;
               }),
           "rocketpack exhaust must retain distinct luminous and smoke layers");
}

void active_jetpack_emission_matches_retail_count_and_layer() {
    ParticleSystem particles;
    emit_jetpack_flight_exhaust(
        particles, {10.0F, 20.0F, 30.0F}, {3.0F, 0.0F, -2.0F}, 69U);
    expect(particles.live_count() == 2U,
           "one recovered jetpack cadence edge must emit exactly two particles");
    particles.build_draw_list({}, 0.0F);
    expect(particles.batches().size() == 1U &&
               particles.batches().front().atlas == ParticleAtlas::smoke_trail &&
               particles.batches().front().blend == ParticleBlend::premultiplied,
           "active jetpack exhaust must use the authored smoke-trail layer");
}

} // namespace

int main() {
    try {
        ring_is_bounded_and_evicts_oldest();
        colours_are_carried_verbatim();
        burst_scatter_matches_the_native_unit_sphere();
        retail_scatter_does_not_collapse_into_one_hemisphere();
        burst_lifetimes_are_not_randomized();
        atlas_animation_matches_native_direction_and_tick_rules();
        random_start_frame_is_selected_per_burst_particle();
        block_debris_matches_retail_spawn_debris();
        digging_break_uses_the_same_retail_composition();
        rocket_and_grenade_blasts_match_retail_compositions();
        sticky_blast_debris_keeps_its_retail_size_speed_and_lifetime();
        rocket_glow_emits_the_native_child_smoke_trails();
        glow_trail_children_grow_like_the_spawn_point();
        rocket_smoke_uses_native_growth_and_fade();
        corpse_and_grave_have_distinct_retail_bursts();
        sticky_fragments_sample_raw_model_tuples_in_the_display_pose();
        observer_shot_has_a_compact_muzzle_burst();
        block_gadgets_have_their_recovered_directional_feedback();
        retail_lut_smoke_and_rings_follow_constants();
        tilted_structure_bursts_at_its_visible_transform();
        falling_blocks_breakup_samples_every_mod_th_voxel_into_five();
        shoot_response_blood_matches_retail_composition();
        server_confirmed_entity_hit_is_visible();
        crate_pickup_matches_retail_twinkle_burst();
        diamond_pickup_matches_retail_twinkle_burst();
        structure_burst_scales_with_component_size();
        quality_scale_changes_capacity_without_thinning_authored_bursts();
        lifetimes_retire_and_dt_is_clamped();
        gravity_exempt_particles_do_not_fall();
        particle_gravity_follows_state_data_gravity();
        collision_bounces_debris_on_the_crossed_axis();
        batches_group_by_atlas_and_blend();
        blended_particles_sort_back_to_front();
        fog_distance_culls_far_particles();
        clear_releases_everything();
        placement_feedback_is_visible_and_prefab_bounded();
        rocketpack_death_exhaust_contains_flame_and_smoke();
        active_jetpack_emission_matches_retail_count_and_layer();
        std::cout << "particle system tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
