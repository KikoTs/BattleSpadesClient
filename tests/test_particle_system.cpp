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
    const auto& rgba = particles.instances().front().rgba;
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
    impact.source_tool = 17U;
    impact.radius = 3.0F;
    emit_explosion(particles, impact);
    expect(particles.live_count() == 14U,
           "ExplodeOnImpactEntity must emit four glow blocks plus ten particles");
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
               std::abs(particles.instances().front().rgba[3U] - 0.5F) < 0.001F,
           "decay=-1 must grow rocket smoke while native remaining-life alpha fades it");
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

void tilted_structure_bursts_at_its_visible_transform() {
    ParticleSystem particles;
    const FallingComponent component{{{1U, 0U, 0U}, dirt}};
    emit_structure_burst(particles, component, {10.0F, 10.0F, 10.0F},
                         {0.5F, 0.5F, 0.5F}, {0.0F, 90.0F, 0.0F},
                         0xC011A95EU, 0.9F);
    particles.build_draw_list({0.0F, 0.0F, 0.0F}, 0.0F);
    expect(std::ranges::any_of(particles.instances(), [](const auto& instance) {
               return std::abs(instance.position[0U] - 10.0F) < 0.01F &&
                      std::abs(instance.position[1U] - 10.0F) < 0.01F &&
                      std::abs(instance.position[2U] - 9.0F) < 0.01F;
           }),
           "collapse debris must originate from the tilted mesh, not its old upright cells");
}

void structure_burst_emits_one_shrinking_image_per_block() {
    auto map = empty_world();
    ParticleSystem particles;
    FallingComponent component;
    for (std::uint32_t x{}; x < 64U; ++x) {
        component.push_back({{x, 10U, 40U}, dirt});
    }
    emit_structure_burst(particles, component, {32.0F, 10.0F, 40.0F},
                         {32.0F, 10.0F, 40.0F}, {},
                         0xB10C5U, 0.9F);
    particles.build_draw_list({}, 0.0F);
    const auto initial_batch = std::ranges::find_if(
        particles.batches(), [](const auto& batch) {
            return batch.atlas == ParticleAtlas::tumbling_cube &&
                   batch.blend == ParticleBlend::alpha;
        });
    expect(initial_batch != particles.batches().end() &&
               initial_batch->count ==
                   static_cast<std::uint32_t>(component.size()),
           "every collapsed voxel must create one tumbling block image");

    for (int step{}; step < 5; ++step) {
        particles.tick(0.09, map);
    }
    particles.build_draw_list({}, 0.0F);
    const auto halfway_batch = std::ranges::find_if(
        particles.batches(), [](const auto& batch) {
            return batch.atlas == ParticleAtlas::tumbling_cube &&
                   batch.blend == ParticleBlend::alpha;
        });
    expect(halfway_batch != particles.batches().end(),
           "block images must survive halfway through their animation");
    for (std::uint32_t offset{}; offset < halfway_batch->count; ++offset) {
        const auto& image = particles.instances()[halfway_batch->first + offset];
        expect(image.size < 0.6F && image.size > 0.4F,
               "block images must smoothly shrink toward zero");
    }
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
    expect(particles.live_count() == 9U,
           "one HitEntity packet must emit six sparks and three solid chips");
    particles.build_draw_list({0.0F, 0.0F, 0.0F}, 0.0F);
    expect(particles.instances().size() == 9U,
           "every server-confirmed entity-hit particle must reach the renderer");
    expect(std::ranges::any_of(
               particles.batches(), [](const auto& batch) {
                   return batch.blend == ParticleBlend::additive;
               }) &&
               std::ranges::any_of(
                   particles.batches(), [](const auto& batch) {
                       return batch.blend == ParticleBlend::alpha;
                   }),
           "entity hits must contain both luminous sparks and physical chips");
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
        rocket_glow_emits_the_native_child_smoke_trails();
        rocket_smoke_uses_native_growth_and_fade();
        corpse_and_grave_have_distinct_retail_bursts();
        observer_shot_has_a_compact_muzzle_burst();
        block_gadgets_have_their_recovered_directional_feedback();
        tilted_structure_bursts_at_its_visible_transform();
        structure_burst_emits_one_shrinking_image_per_block();
        shoot_response_blood_matches_retail_composition();
        server_confirmed_entity_hit_is_visible();
        crate_pickup_matches_retail_twinkle_burst();
        structure_burst_scales_with_component_size();
        quality_scale_changes_capacity_without_thinning_authored_bursts();
        lifetimes_retire_and_dt_is_clamped();
        gravity_exempt_particles_do_not_fall();
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
