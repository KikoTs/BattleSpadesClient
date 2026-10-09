#include "battlespades/world/retail_effects.hpp"
#include "battlespades/world/blood_marks.hpp"
#include "battlespades/world/retail_view_model.hpp"
#include "battlespades/world/terrain_effects.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <cmath>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using namespace battlespades::world;

void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] bool near(float a, float b) {
    return std::abs(a - b) < 1.0e-4F;
}

[[nodiscard]] std::unique_ptr<VxlMap> empty_map() {
    std::vector<std::byte> bytes;
    bytes.reserve(static_cast<std::size_t>(VxlMap::width) * VxlMap::depth * 4U);
    for (std::size_t column{}; column < static_cast<std::size_t>(VxlMap::width) * VxlMap::depth;
         ++column) {
        bytes.push_back(std::byte{0U});
        bytes.push_back(std::byte{1U});
        bytes.push_back(std::byte{0U});
        bytes.push_back(std::byte{0U});
    }
    auto loaded = VxlMap::load(bytes);
    expect(static_cast<bool>(loaded), "synthetic map must parse");
    return std::make_unique<VxlMap>(std::move(*loaded.map));
}

void patch_colour_ramps_follow_retail_constants() {
    const auto hot = block_fire_colour(4.0);
    const auto mid = block_fire_colour(2.0);
    const auto cold = block_fire_colour(0.0);
    expect(near(hot[0U], 1.0F) && near(hot[1U], 1.0F) && near(hot[2U], 1.0F),
           "a fresh fire is BLOCKFIRE_HOT_COLOUR white");
    expect(near(mid[0U], 1.0F) && near(mid[1U], 1.0F) && near(mid[2U], 0.0F),
           "half-way fire is BLOCKFIRE_MID_COLOUR yellow");
    expect(near(cold[0U], 1.0F) && near(cold[1U], 0.0F) && near(cold[2U], 0.0F),
           "burnt-out fire is BLOCKFIRE_COLD_COLOUR red");
    const auto goo_mid = block_goo_colour(2.0);
    const auto goo_cold = block_goo_colour(-1.0);
    expect(near(goo_mid[0U], 20.0F / 255.0F) && near(goo_mid[1U], 1.0F) &&
               near(goo_mid[2U], 50.0F / 255.0F),
           "goo passes A2433 (20,255,50)");
    expect(near(goo_cold[0U], 0.0F) && near(goo_cold[1U], 1.0F) && near(goo_cold[2U], 0.0F),
           "expired goo is A2434 green");
}

void surface_anchor_resolves_its_voxel() {
    auto map = empty_map();
    expect(map->set_voxel(100U, 100U, 200U, VxlColor{90U, 90U, 90U, 255U}), "fixture voxel");
    const auto top = surface_patch_voxel(*map, {100.5, 100.5, 199.99});
    expect(top.has_value() && top->x == 100U && top->y == 100U && top->z == 200U,
           "a top-face anchor resolves to the voxel below it");
    const auto side = surface_patch_voxel(*map, {101.01, 100.5, 200.5});
    expect(side.has_value() && side->x == 100U && side->z == 200U,
           "a +x side anchor resolves to the voxel behind it");
    expect(!surface_patch_voxel(*map, {50.5, 50.5, 50.0}).has_value(),
           "an anchor next to air has no voxel");
}

void set_hp_rows_map_to_retail_feedback() {
    expect(set_hp_feedback(1U, 100, 80) == SetHpFeedback::hit, "type 1 is a hit");
    expect(set_hp_feedback(3U, 100, 97) == SetHpFeedback::burn, "type 3 is burn");
    expect(set_hp_feedback(4U, 100, 90) == SetHpFeedback::sudden_death, "type 4 sudden death");
    expect(set_hp_feedback(2U, 40, 100) == SetHpFeedback::heal, "type 2 raising HP heals");
    expect(set_hp_feedback(0U, 100, 90) == SetHpFeedback::none, "fall damage has no feedback");
    const std::array<std::uint8_t, 3U> blue{44U, 117U, 255U};
    const auto burn = status_tints(1.2, 0.0, blue);
    expect(burn.burn.has_value() && burn.burn->alpha == 255U &&
               burn.burn->color == std::array<std::uint8_t, 3U>{255U, 0U, 0U} &&
               !burn.sudden_death.has_value(),
           "a fresh burn is (255, 0, 0, 255) over inside_zone_texture");
    const auto none = status_tints(0.0, 0.0, blue);
    expect(!none.burn && !none.sudden_death, "expired indicators draw nothing");
    const auto both = status_tints(0.6, 1.2, blue);
    expect(both.burn.has_value() && both.burn->alpha == 127U &&
               both.sudden_death.has_value() && both.sudden_death->color == blue &&
               both.sudden_death->alpha == 255U,
           "burn fades over 1.2 s and sudden death uses the team colour; both draw");
}

void status_edges_detect_fire_and_goo() {
    const auto ignite = character_status_edges(false, true, false, false);
    expect(ignite.ignited && !ignite.extinguished, "0x20 rising edge ignites");
    const auto out = character_status_edges(true, false, true, false);
    expect(out.extinguished && out.goo_ended, "falling edges end both effects");
    expect(character_in_water({1.0, 1.0, 237.5}) && !character_in_water({1.0, 1.0, 230.0}),
           "water test uses the z=237 plane");
}

void tracers_fly_at_retail_speed() {
    auto tracer = make_tracer({0.0F, 0.0F, 0.0F}, {100.0F, 0.0F, 0.0F}, 3U);
    expect(tracer.has_value(), "a real shot makes a tracer");
    expect(advance_tracer(*tracer, 0.25F) && near(tracer->position[0U], 50.0F),
           "WEAPON_TRACER_SPEED is 200 blocks per second");
    expect(!advance_tracer(*tracer, 1.0F) && near(tracer->position[0U], 100.0F),
           "a tracer stops exactly at its hit point");
    expect(!make_tracer({1.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 1.0F}, 3U).has_value(),
           "a zero-length shot draws no tracer");
    expect(muzzle_flash_roll_degrees(7U) < 360.0F, "muzzle roll is an angle");
    expect(near(retail_tracer_ttl(60.0), 0.3F) && near(retail_tracer_ttl(150.0), 0.5F) &&
               near(retail_tracer_ttl(1'000.0), 0.5F),
           "Tracer ttl = min(range / 200, 0.5): at most 100 blocks of flight");
}

void crate_drop_matches_the_live_trace() {
    // Live trace: a crate created at z=100 over support 213 opens its chute
    // near z=205, falls at 2 blocks/s under canopy, releases near 211 and
    // lands on the support voxel after about 5.1 s for a 214-block drop.
    CrateDropState state;
    state.falling = true;
    double z = -1.0;
    double velocity = 0.0;
    const double support = 213.0;
    double seconds{};
    bool opened{};
    bool released{};
    double open_z{};
    for (int frame{}; frame < 2000; ++frame) {
        const auto step = step_crate_drop(z, velocity, support, state);
        seconds += crate_drop_step;
        if (step.chute_opened) {
            opened = true;
            open_z = z;
        }
        released = released || step.chute_released;
        if (step.landed) {
            break;
        }
    }
    expect(opened && released, "the chute opens and releases");
    expect(open_z > 203.0 && open_z < 206.0, "the chute opens under ten blocks");
    expect(z == support, "the crate rests on its support voxel");
    expect(seconds > 4.6 && seconds < 5.6, "a 214-block drop takes about 5.1 s");
}

} // namespace

/**
 * Retail Character.shoot: Tracer(eye, dir * speed, prestep=16) -> born 16
 * blocks down the aim line, dropped when the hit is nearer. Enhanced tiers
 * fly the tracer out of the muzzle to the contact instead.
 */
void tracers_launch_from_the_muzzle_on_enhanced_tiers() {
    const std::array<float, 3U> eye{100.0F, 100.0F, 50.0F};
    const std::array<float, 3U> forward{1.0F, 0.0F, 0.0F};
    const std::array<float, 3U> muzzle{101.5F, 100.3F, 50.4F};
    const std::array<float, 3U> far_hit{160.0F, 100.0F, 50.0F};

    const auto retail = plan_tracer_launch(true, eye, forward, muzzle, far_hit);
    expect(retail.has_value() && near(retail->start[0U], 116.0F) &&
               near(retail->start[1U], 100.0F) && near(retail->end[0U], 160.0F),
           "retail tracers start 16 blocks down the eye line, not at the muzzle");
    expect(!plan_tracer_launch(true, eye, forward, muzzle, {110.0F, 100.0F, 50.0F}).has_value(),
           "retail drops a tracer whose hit lies inside the 16-block pre-step");

    const auto enhanced = plan_tracer_launch(false, eye, forward, muzzle, far_hit);
    expect(enhanced.has_value() && near(enhanced->start[0U], muzzle[0U]) &&
               near(enhanced->start[1U], muzzle[1U]) && near(enhanced->start[2U], muzzle[2U]) &&
               near(enhanced->end[0U], far_hit[0U]),
           "enhanced tracers start at the muzzle and fly to the crosshair contact");
    expect(plan_tracer_launch(false, eye, forward, muzzle, {104.0F, 100.0F, 50.0F}).has_value(),
           "enhanced tracers still show a near hit beyond the muzzle");
    expect(!plan_tracer_launch(false, eye, forward, muzzle, {101.0F, 100.0F, 50.0F}).has_value(),
           "no tracer when the wall is already behind the muzzle");
    const auto fallback = plan_tracer_launch(false, eye, forward, std::nullopt, far_hit);
    expect(fallback.has_value() && near(fallback->start[0U], 116.0F),
           "without a muzzle the enhanced plan falls back to the retail pre-step");
}

/** The enhanced-tier shot light: every gun, at the muzzle, never in Retail. */
void muzzle_light_spawns_at_the_muzzle_on_enhanced_tiers() {
    constexpr std::array<std::uint8_t, 15U> guns{7U, 8U, 9U, 10U, 17U, 18U, 35U, 36U,
                                                  37U, 38U, 53U, 60U, 61U, 62U, 6U};
    for (const auto gun : guns) {
        expect(muzzle_light_enabled(false, gun), "every gun must cast a muzzle light");
        expect(!muzzle_light_enabled(true, gun), "the Retail tier casts no muzzle light");
    }
    expect(!muzzle_light_enabled(false, 2U) && !muzzle_light_enabled(false, 5U),
           "the spade and block cast no muzzle light");

    const auto map = empty_map();
    TerrainEffectSimulation effects;
    const std::array<float, 3U> muzzle{101.5F, 100.3F, 50.4F};
    effects.spawn_weapon_flash(muzzle, 8U);
    effects.tick(1.0 / 120.0, *map);
    expect(effects.lights().size() == 1U, "a minigun shot must light the scene");
    const auto& light = effects.lights().front();
    expect(near(light.position[0U], muzzle[0U]) && near(light.position[1U], muzzle[1U]) &&
               near(light.position[2U], muzzle[2U]),
           "the shot light sits at the muzzle");
    expect(light.radius > 0.5F && light.radius <= 4.5F && light.color[0U] >= light.color[2U],
           "the shot light is warm and reaches a few blocks");
    for (int frame{}; frame < 10; ++frame) {
        effects.tick(1.0 / 120.0, *map);
    }
    expect(effects.lights().empty(), "the shot light is gone within ~80 ms");
}

/** The enhanced flash anchors to the drawn barrel mouth. */
void view_muzzle_tip_is_the_barrel_mouth() {
    ChunkMesh mesh;
    // A 1x1 barrel reaching z=10.5 and a body block well behind it.
    for (const auto& vertex : std::initializer_list<std::array<float, 3U>>{
             {-0.5F, -0.5F, 10.5F}, {0.5F, 0.5F, 10.5F}, {-0.5F, 0.5F, 9.5F},
             {0.5F, -0.5F, 9.5F}, {-3.0F, -4.0F, 0.0F}, {3.0F, 2.0F, -5.0F}}) {
        ChunkVertex v;
        v.x = vertex[0U];
        v.y = vertex[1U];
        v.z = vertex[2U];
        mesh.vertices.push_back(v);
    }
    const auto tip = view_model_muzzle_tip(mesh);
    expect(tip.has_value() && near((*tip)[0U], 0.0F) && near((*tip)[1U], 0.0F) &&
               near((*tip)[2U], 10.5F),
           "the tip is the centre of the front-most voxel slab");
    expect(!view_model_muzzle_tip(ChunkMesh{}).has_value(), "an empty part has no muzzle");
    const auto centre = anchored_view_muzzle_flash_centre(*tip, 0.75);
    expect(near(centre[2U], static_cast<float>(10.5 + view_muzzle_flash_lead_voxels * 0.75)),
           "the flash centre leads the mouth by lead * flash scale voxels");
    // The flash (6 voxels * scale either side) must overlap the mouth.
    expect(centre[2U] - 6.0F * 0.75F < (*tip)[2U], "the flash rear overlaps the barrel mouth");
}

void blood_marks_stick_expire_and_follow_terrain() {
    auto map=empty_map();
    for(unsigned x=18;x<33;++x)for(unsigned y=18;y<33;++y)
        static_cast<void>(map->set_voxel(x,y,50,{80,90,100}));
    const auto revision=map->revision();
    BloodMarks blood;
    blood.emit({25,25,48},42);
    expect(blood.drop_count()==8,"hits emit visible block droplets");
    for(int tick=0;tick<180;++tick)blood.tick(1./60,*map);
    expect(blood.drop_count()==0 && blood.mark_count()>8,"droplets leave irregular clusters on terrain");
    const auto first=blood.mesh();
    expect(!first.empty(),"lingering blood has surface geometry");
    for(const auto& vertex:first.vertices)
        expect(vertex.z<50 && (vertex.abgr>>24)==0,"blood sits above its supporting face without emissive glow");
    const auto count=blood.mark_count();
    for(int tick=0;tick<180;++tick)blood.tick(1./60,*map);
    const auto later=blood.mesh();
    expect(blood.mark_count()==count && first.minimum==later.minimum && first.maximum==later.maximum,
           "landed stains stay fixed instead of bouncing or following the camera");
    expect(map->revision()==revision,"blood never recolors or changes server terrain");
    for(unsigned x=18;x<33;++x)for(unsigned y=18;y<33;++y)
        static_cast<void>(map->clear_voxel(x,y,50));
    blood.tick(1./60,*map);
    expect(blood.mark_count()==0 && blood.mesh().empty(),"destroying support removes its blood immediately");
    for(unsigned y=18;y<33;++y)for(unsigned z=40;z<55;++z)
        static_cast<void>(map->set_voxel(26,y,z,{80,90,100}));
    blood.emit({25.8F,25,45},123);
    for(int tick=0;tick<100;++tick)blood.tick(1./60,*map);
    expect(blood.mark_count()>0,"blood also sticks to vertical block faces");
    for(int tick=0;tick<2100;++tick)blood.tick(1./60,*map);
    expect(blood.mark_count()==0 && blood.drop_count()==0,"blood expires after its bounded lifetime");
    for(int hit=0;hit<1000;++hit)blood.emit({25.8F,25,45},static_cast<std::uint32_t>(hit));
    expect(blood.drop_count()<=BloodMarks::maximum_drops,"automatic fire cannot grow the drop pool without bound");
    for(int tick=0;tick<150;++tick)blood.tick(1./60,*map);
    expect(blood.mark_count()<=BloodMarks::maximum_marks,"surface mark memory is bounded");
    blood.clear();
    expect(blood.mesh().empty(),"disabling or changing maps clears all blood geometry");
}

int main() {
    try {
        blood_marks_stick_expire_and_follow_terrain();
        patch_colour_ramps_follow_retail_constants();
        surface_anchor_resolves_its_voxel();
        set_hp_rows_map_to_retail_feedback();
        status_edges_detect_fire_and_goo();
        tracers_fly_at_retail_speed();
        tracers_launch_from_the_muzzle_on_enhanced_tiers();
        muzzle_light_spawns_at_the_muzzle_on_enhanced_tiers();
        view_muzzle_tip_is_the_barrel_mouth();
        crate_drop_matches_the_live_trace();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "retail effects tests passed\n";
    return 0;
}
