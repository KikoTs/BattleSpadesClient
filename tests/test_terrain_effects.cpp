#include "battlespades/world/terrain_effects.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
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

constexpr VxlColor stone{120U, 96U, 80U, 255U};

void falling_mesh_culls_internal_faces() {
    auto map = empty_world();
    TerrainEffectSimulation effects;
    effects.spawn_falling({{{10U, 10U, 100U}, stone},
                           {{11U, 10U, 100U}, stone}});
    effects.tick(1.0 / 60.0, map);
    expect(effects.instances().size() == 1U,
           "connected falling component must remain one visual object");
    expect(effects.instances().front().mesh.face_count() == 10U,
           "two adjacent falling voxels must cull their shared faces");
    expect(effects.instances().front().position[2U] > 100.0F,
           "recovered gravity must advance the falling object downward");
    const auto rotation = effects.instances().front().rotation_degrees;
    const float turned = std::sqrt(rotation[0U] * rotation[0U] + rotation[1U] * rotation[1U] +
                                   rotation[2U] * rotation[2U]);
    expect(std::abs(turned - 50.0F / 60.0F) < 0.01F,
           "FallingBlocks tumbles about a random unit axis at 50 degrees per second");
}

void collapse_timing_sound_banks_and_cap_are_size_aware() {
    expect(falling_sound_tier(14U) == FallingSoundTier::small &&
               falling_sound_tier(15U) == FallingSoundTier::medium &&
               falling_sound_tier(80U) == FallingSoundTier::large,
           "retail collapse sound thresholds must remain exactly 15/80 blocks");
    expect(falling_sound_group(TerrainSoundKind::structure_split, 1U, false) ==
                   "des_split_small_001-003" &&
               falling_sound_group(TerrainSoundKind::structure_split, 20U, false) ==
                   "des_split_med_001-003" &&
               falling_sound_group(TerrainSoundKind::structure_break, 100U, false) ==
                   "des_imp_large_001-004" &&
               falling_sound_group(TerrainSoundKind::structure_break, 20U, true) ==
                   "des_imp_med_water_001-004",
           "collapse events must select their authored split/impact/water banks");
    TerrainEffectSimulation effects;
    FallingComponent oversized;
    constexpr std::size_t large_component{2'068U};
    for (std::size_t index{}; index < large_component; ++index) {
        oversized.push_back(
            {{static_cast<std::uint32_t>(index % VxlMap::width),
              static_cast<std::uint32_t>((index / VxlMap::width) % VxlMap::depth),
              80U},
             stone});
    }
    effects.spawn_falling(std::move(oversized));
    const auto split = effects.take_sound_events();
    expect(split.size() == 1U &&
               split.front().kind == TerrainSoundKind::structure_split &&
               split.front().structure_blocks == large_component &&
               std::abs(split.front().gain - 0.75F) < 0.001F,
           "retail FallingBlocks has no size cap and keeps the retail volume");
    effects.tick(1.0 / 60.0, empty_world());
    expect(effects.instances().size() == 1U &&
               effects.instances().front().mesh.face_count() > 2'048U,
           "every voxel of a large component is drawn");
}

void falling_structure_breaks_on_its_first_map_contact() {
    auto map = empty_world();
    for (std::uint32_t y{8U}; y <= 12U; ++y) {
        for (std::uint32_t x{8U}; x <= 12U; ++x) {
            expect(map.set_voxel(x, y, 101U, stone),
                   "collision fixture platform must be writable");
        }
    }

    TerrainEffectSimulation effects;
    effects.spawn_falling({{{10U, 10U, 100U}, stone}});
    static_cast<void>(effects.take_sound_events());
    // Origin z = 100 (bbox centre). v.z += 0.1 per tick; z: 100.32, 100.96,
    // then 101.92 lands in the platform cell and the body breaks.
    effects.tick(0.1, map);
    effects.tick(0.1, map);
    expect(effects.instances().size() == 1U &&
               effects.instances().front().kind == TerrainEffectKind::falling_structure,
           "the body keeps falling through air");
    effects.tick(0.1, map);
    expect(std::ranges::none_of(effects.instances(), [](const auto& effect) {
               return effect.kind == TerrainEffectKind::falling_structure;
           }),
           "the first contact with a surviving voxel breaks the body");
    const auto sounds = effects.take_sound_events();
    expect(sounds.size() == 1U && sounds.front().kind == TerrainSoundKind::structure_break &&
               std::abs(sounds.front().position[2U] - 100.96F) < 0.01F,
           "the impact bank plays at the restored (pre-contact) origin");
}

void retail_falling_step_probes_bounces_and_uses_the_water_bed() {
    auto map = empty_world();
    std::array<float, 3U> position{10.5F, 10.5F, 238.9F};
    std::array<float, 3U> velocity{0.0F, 0.0F, 1.0F};
    expect(!retail_falling_blocks_step(map, position, velocity, 0.01F, 0.0F) &&
               position[2U] > 239.0F,
           "z = 239 probes z = 238, so the solid bed is water to a falling body");
    int steps{};
    while (!retail_falling_blocks_step(map, position, velocity, 0.01F, 0.0F) && steps < 10) {
        ++steps;
    }
    expect(position[2U] < 240.0F && std::abs(velocity[2U] + 0.5F) < 1.0e-6F,
           "z >= 240 always hits: restore, reflect z, halve");

    expect(map.set_voxel(11U, 10U, 100U, stone), "wall fixture");
    position = {10.9F, 10.5F, 100.5F};
    velocity = {1.0F, 0.0F, 0.0F};
    expect(retail_falling_blocks_step(map, position, velocity, 0.01F, 0.0F) &&
               std::abs(position[0U] - 10.9F) < 1.0e-6F &&
               std::abs(velocity[0U] + 0.5F) < 1.0e-6F,
           "an x-cell change reflects and halves x");
    position = {-5.0F, 10.5F, 100.5F};
    velocity = {0.0F, 0.0F, 1.0F};
    expect(!retail_falling_blocks_step(map, position, velocity, 0.01F, 1.0F),
           "outside the map in x/y never collides");
}

void collapse_does_not_add_a_nonretail_smoke_plume() {
    auto map = empty_world();
    ParticleSystem particles;
    TerrainEffectSimulation effects;
    effects.set_particle_sink(&particles);
    effects.spawn_falling({{{20U, 30U, 80U}, stone},
                           {{21U, 30U, 80U}, stone},
                           {{22U, 30U, 80U}, stone},
                           {{23U, 30U, 80U}, stone}});
    for (int tick{}; tick < 40; ++tick) {
        effects.tick(0.1, map);
    }
    particles.build_draw_list({}, 0.0F);
    const auto smoke = std::ranges::find_if(particles.batches(), [](const auto& batch) {
        return batch.atlas == ParticleAtlas::smoke_trail;
    });
    expect(smoke == particles.batches().end(),
           "retail FallingBlocks must not gain a fabricated ground-smoke layer");
}

void impact_emits_bounded_chips_and_sound() {
    auto map = empty_world();
    TerrainEffectSimulation effects;
    effects.spawn_impact({TerrainImpactKind::bullet,
                          {20U, 21U, 22U}, stone, {0, -1, 0}, false});
    effects.tick(1.0 / 60.0, map);
    expect(effects.instances().size() == 4U,
           "one terrain hit must create the recovered bounded chip burst");
    const auto sounds = effects.take_sound_events();
    expect(sounds.size() == 1U && sounds.front().kind == TerrainSoundKind::bullet_impact,
           "bullet scenery contact must emit one positional sound event");
}

void authoritative_melee_can_spawn_visuals_without_duplicate_audio() {
    auto map = empty_world();
    TerrainEffectSimulation effects;
    effects.spawn_impact({TerrainImpactKind::melee,
                          {20U, 21U, 22U}, stone, {0, -1, 0}, false,
                          1.0F, 50U},
                         TerrainImpactSoundPolicy::silent);
    effects.tick(1.0 / 60.0, map);
    expect(!effects.instances().empty(),
           "server-driven melee must retain chips and crack feedback");
    expect(effects.take_sound_events().empty(),
           "server-driven melee must not duplicate authoritative PlaySound");
}

void drill_bore_has_contact_feedback_without_blast_or_light() {
    auto map = empty_world();
    TerrainEffectSimulation effects;
    effects.spawn_impact({TerrainImpactKind::drill,
                          {24U, 25U, 26U}, stone, {1, 0, 0}, true,
                          2.0F, 14U});
    effects.tick(1.0 / 60.0, map);
    expect(effects.lights().empty(),
           "a Drill bore tick must not create a terminal explosion light");
    expect(std::ranges::none_of(effects.instances(), [](const auto& effect) {
               return effect.kind == TerrainEffectKind::explosion_glow;
           }),
           "a Drill bore tick must not enter the explosion compositor");
    const auto sounds = effects.take_sound_events();
    expect(sounds.size() == 1U &&
               sounds.front().kind == TerrainSoundKind::drill_bore &&
               sounds.front().source_tool == 14U,
           "each Drill contact must retain its one drilling tick cue");
}

void predicted_bullet_break_only_emits_destruction_audio() {
    auto map = empty_world();
    TerrainEffectSimulation effects;
    effects.spawn_impact({TerrainImpactKind::bullet,
                          {20U, 21U, 22U}, stone, {0, -1, 0}, true},
                         TerrainImpactSoundPolicy::destruction_only);
    effects.tick(1.0 / 60.0, map);
    expect(!effects.instances().empty(),
           "authoritative destruction must retain its visual burst");
    const auto sounds = effects.take_sound_events();
    expect(sounds.size() == 1U &&
               sounds.front().kind == TerrainSoundKind::block_break,
           "a predicted contact must suppress only the duplicate impact sound");
}

void falling_structure_breaks_into_colored_debris() {
    auto map = empty_world();
    TerrainEffectSimulation effects;
    FallingComponent component;
    for (std::uint32_t x{}; x < 20U; ++x) {
        component.push_back({{100U + x, 100U, 80U}, stone});
    }
    effects.spawn_falling(std::move(component));

    // No timer: in an empty world the body falls from z = 80 until z >= 240
    // (the z = 239 bed is water to it), 160 blocks at 32 g: about 3.16 s.
    int ticks{};
    const auto still_falling = [&effects] {
        return std::ranges::any_of(effects.instances(), [](const auto& effect) {
            return effect.kind == TerrainEffectKind::falling_structure;
        });
    };
    effects.tick(1.0 / 60.0, map);
    while (still_falling() && ticks < 60 * 5) {
        effects.tick(1.0 / 60.0, map);
        ++ticks;
    }
    expect(!still_falling(), "the structure never reached the map bottom");
    expect(ticks >= 180 && ticks <= 200,
           "the body falls under gravity until its first contact, with no timer");
    expect(!effects.instances().empty(),
           "a landed falling structure must be replaced by voxel debris");
    for (const auto& effect : effects.instances()) {
        expect(effect.kind == TerrainEffectKind::block_debris,
               "post-impact effects must be individual block debris");
    }
    const auto sounds = effects.take_sound_events();
    bool found_break{};
    for (const auto& sound : sounds) {
        found_break = found_break || sound.kind == TerrainSoundKind::structure_break;
    }
    expect(found_break, "structure breakup must emit its retail debris sound");
}

void cosmetic_flood_remains_bounded() {
    auto map = empty_world();
    TerrainEffectSimulation effects;
    for (std::uint32_t index{}; index < 100U; ++index) {
        effects.spawn_impact({TerrainImpactKind::melee,
                              {index, 10U, 10U}, stone, {1, 0, 0}, false});
    }
    effects.tick(1.0 / 60.0, map);
    expect(effects.instances().size() <= TerrainEffectSimulation::maximum_instances,
           "effect floods must never exceed the fixed renderer budget");
}

void explosives_create_retail_glow_and_persistent_aftermath() {
    auto map = empty_world();
    TerrainEffectSimulation effects;
    effects.spawn_impact({TerrainImpactKind::explosion,
                          {30U, 31U, 32U}, stone, {0, 0, -1}, true, 4.0F});
    effects.tick(1.0 / 60.0, map);
    expect(effects.instances().size() == 14U,
           "explosion must create ten debris plus four glow particles");
    expect(std::ranges::count_if(effects.instances(), [](const auto& effect) {
               return effect.kind == TerrainEffectKind::explosion_glow;
           }) == 4,
           "explosion glow particles must be represented explicitly");
    expect(effects.lights().size() == 4U,
           "one explosion must publish its centre plus three particle-trail lights");
    expect(effects.lights().front().radius > 2.0F &&
               effects.lights().front().intensity > 1.0F,
           "the opening explosion light must illuminate more than its source voxel");
    for (int tick{}; tick < 120; ++tick) {
        effects.tick(1.0 / 60.0, map);
    }
    expect(effects.lights().empty(),
           "an explosion light must decay instead of becoming a permanent lamp");

    effects.clear();
    effects.spawn_impact({TerrainImpactKind::fire,
                          {30U, 31U, 32U}, stone, {0, 0, -1}, true, 4.0F});
    effects.tick(1.0 / 60.0, map);
    expect(std::ranges::count_if(effects.instances(), [](const auto& effect) {
               return effect.kind == TerrainEffectKind::fire_flame;
           }) == 5,
           "Molotov impact must retain five BLOCKFIRE-style flame emitters");
    for (int tick{}; tick < 120; ++tick) effects.tick(1.0 / 60.0, map);
    expect(std::ranges::any_of(effects.instances(), [](const auto& effect) {
               return effect.kind == TerrainEffectKind::fire_flame;
           }),
           "Molotov flame aftermath must persist after the impact burst expires");
}

void weapon_flash_is_small_bounded_and_tool_specific() {
    auto map = empty_world();
    TerrainEffectSimulation effects;
    // Round 3: every gun with a propellant flash lights a few blocks around
    // its muzzle (the old list keyed lights to wrong tool ids); tools without
    // a muzzle flash, like the spade, stay dark.
    effects.spawn_weapon_flash({12.0F, 13.0F, 14.0F}, 17U);
    effects.spawn_weapon_flash({12.0F, 13.0F, 14.0F}, 18U);
    effects.spawn_weapon_flash({12.0F, 13.0F, 14.0F}, 2U);
    effects.tick(1.0 / 120.0, map);
    expect(effects.lights().size() == 2U,
           "the pistol and the sniper should flash while the spade stays dark");
    expect(effects.lights().front().radius >= 2.5F && effects.lights().front().radius <= 4.5F &&
               effects.lights().front().intensity < 1.2F,
           "ordinary muzzle light must reach a few blocks yet stay far below an explosion");
    for (int tick{}; tick < 20; ++tick) {
        effects.tick(1.0 / 60.0, map);
    }
    expect(effects.lights().empty(), "muzzle flash must not become a permanent lamp");
}

void corpse_and_grave_have_separate_sound_and_particle_paths() {
    auto map = empty_world();
    TerrainEffectSimulation effects;
    ParticleSystem particles;
    effects.set_particle_sink(&particles);
    TerrainImpactEvent impact{
        TerrainImpactKind::corpse_explosion,
        {30U, 31U, 32U},
        VxlColor{55U, 125U, 215U, 255U},
        {0, 0, -1},
        true,
        3.0F};
    impact.position = {std::array<float, 3U>{30.2F, 31.7F, 32.1F}};
    effects.spawn_impact(impact);
    effects.tick(1.0 / 60.0, map);
    const auto sounds = effects.take_sound_events();
    expect(sounds.size() == 1U &&
               sounds.front().kind == TerrainSoundKind::death_explosion,
           "grave/corpse destruction must request the retail death sound bank");
    expect(particles.live_count() == 40U,
           "corpse destruction must dispatch the exact retail corpse burst");
    expect(std::ranges::none_of(effects.instances(), [](const auto& effect) {
               return effect.kind == TerrainEffectKind::explosion_glow;
           }),
           "death debris must not reuse RPG glow cubes");
    expect(effects.lights().empty(),
           "corpse blood must not fabricate an ordnance light");
    expect(std::abs(sounds.front().position[0U] - 30.2F) < 0.001F,
           "corpse sound must use the exact retained entity position");

    particles.clear();
    impact.kind = TerrainImpactKind::grave_explosion;
    effects.spawn_impact(impact);
    const auto grave_sounds = effects.take_sound_events();
    expect(grave_sounds.size() == 1U &&
               grave_sounds.front().kind == TerrainSoundKind::explosion &&
               particles.live_count() == 28U,
           "grave deletion must explode its display with the generic sound bank");
}

/**
 * Rubble is not a light source.
 *
 * ChunkVertex::abgr's alpha byte is self-illumination, which fs_world adds after
 * every occlusion term. Terrain writes 0 there. Falling structures and debris
 * chips wrote 255, so a collapsing wall carried a full unshaded copy of its own
 * albedo on top of its lit result and glowed instead of matching the world it
 * had just been part of -- worst on the night maps, where the terrain around it
 * is dim and the added term is not.
 *
 * Asserted over BOTH mesh builders, since the falling body and the debris chips
 * are packed by separate call sites into the same shared helper.
 */
void falling_rubble_does_not_emit_light() {
    auto map = empty_world();
    TerrainEffectSimulation effects;
    FallingComponent component;
    for (std::uint32_t x{}; x < 12U; ++x) {
        component.push_back({{100U + x, 100U, 80U}, stone});
    }
    effects.spawn_falling(std::move(component));

    const auto assert_unlit = [](const TerrainEffectSimulation& simulation,
                                 const char* emits, const char* empty) {
        std::size_t checked{};
        for (const auto& effect : simulation.instances()) {
            for (const auto& vertex : effect.mesh.vertices) {
                expect((vertex.abgr >> 24U) == 0U, emits);
                ++checked;
            }
        }
        expect(checked > 0U, empty);
    };

    // One tick publishes the spawned component into instances().
    effects.tick(1.0 / 60.0, map);
    assert_unlit(effects, "a falling structure emits light",
                 "no falling-structure vertices were inspected");
    for (int tick{}; tick < 400 && std::ranges::any_of(effects.instances(), [](const auto& e) {
                         return e.kind == TerrainEffectKind::falling_structure;
                     });
         ++tick) {
        effects.tick(1.0 / 60.0, map);
    }
    assert_unlit(effects, "post-impact debris emits light",
                 "no debris vertices were inspected");
}

} // namespace

int main() {
    try {
        falling_mesh_culls_internal_faces();
        collapse_timing_sound_banks_and_cap_are_size_aware();
        falling_structure_breaks_on_its_first_map_contact();
        retail_falling_step_probes_bounces_and_uses_the_water_bed();
        collapse_does_not_add_a_nonretail_smoke_plume();
        falling_rubble_does_not_emit_light();
        impact_emits_bounded_chips_and_sound();
        authoritative_melee_can_spawn_visuals_without_duplicate_audio();
        drill_bore_has_contact_feedback_without_blast_or_light();
        predicted_bullet_break_only_emits_destruction_audio();
        falling_structure_breaks_into_colored_debris();
        cosmetic_flood_remains_bounded();
        explosives_create_retail_glow_and_persistent_aftermath();
        weapon_flash_is_small_bounded_and_tool_specific();
        corpse_and_grave_have_separate_sound_and_particle_paths();
        std::cout << "terrain effect parity tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
