#include "battlespades/world/entity_catalog.hpp"
#include "battlespades/world/local_entity.hpp"
#include "battlespades/world/projectile_presentation.hpp"
#include "battlespades/world/vxl_map.hpp"
#include "battlespades/world/weapon_catalog.hpp"
#include "battlespades/world/weapon_state.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace battlespades::world;

void expect(bool value, const std::string& message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] LocalEntity at(double x, double y, double z) {
    LocalEntity entity;
    entity.position = {x, y, z};
    return entity;
}

/**
 * The crate sphere is exclusive at its edge and does not care about facing.
 *
 * Retail deliberately shrank this from 3.0 to 2.5 because the wider sphere let
 * one walk-through eat two adjacent crates, so the exact boundary matters.
 */
void the_touch_sphere_is_pure_proximity() {
    const auto crate = at(10.0, 10.0, 40.0);
    expect(within_touch_radius(crate, Vec3{10.0, 12.4, 40.0}, 2.5F),
           "a player at 2.4 must trigger a 2.5 radius crate");
    expect(!within_touch_radius(crate, Vec3{10.0, 12.6, 40.0}, 2.5F),
           "a player at 2.6 must not trigger a 2.5 radius crate");
    // Approach from every direction: there is no facing or line-of-sight term.
    expect(within_touch_radius(crate, Vec3{8.0, 10.0, 40.0}, 2.5F),
           "approach direction must not matter");
    expect(within_touch_radius(crate, Vec3{10.0, 10.0, 38.0}, 2.5F),
           "vertical approach must trigger too");
    auto consumed = crate;
    consumed.alive = false;
    expect(!within_touch_radius(consumed, Vec3{10.0, 10.0, 40.0}, 2.5F),
           "a consumed crate must not re-trigger while waiting to respawn");
}

/**
 * A mine's trip volume is a CYLINDER, not a sphere.
 *
 * Standing beside a mine at the same height kills; walking over it one storey
 * up does not. A sphere would get the second case wrong in the player's
 * favour, which reads as the mine being broken.
 */
void the_trip_volume_is_a_cylinder() {
    const auto mine = at(20.0, 20.0, 50.0);
    constexpr float radius{2.5F};
    constexpr float layers{3.0F};
    expect(within_trip_volume(mine, Vec3{22.0, 20.0, 50.0}, radius, layers),
           "a player 2.0 away horizontally must trip the mine");
    expect(!within_trip_volume(mine, Vec3{23.0, 20.0, 50.0}, radius, layers),
           "a player 3.0 away horizontally must not trip the mine");
    // Directly overhead but within the layer count still trips: the corner of
    // the cylinder is live where the corner of a sphere would not be.
    expect(within_trip_volume(mine, Vec3{22.0, 20.0, 47.5}, radius, layers),
           "close horizontally and inside the layers must trip");
    expect(!within_trip_volume(mine, Vec3{20.0, 20.0, 46.0}, radius, layers),
           "four layers above must not trip");
    expect(within_trip_volume(mine, Vec3{20.0, 20.0, 53.0}, radius, layers),
           "the volume extends below the mine as well as above");
}

/** Blast damage is linear to the edge and exactly zero beyond it. */
void the_blast_falloff_stops_at_its_radius() {
    expect(std::abs(blast_falloff(0.0, 8.0) - 1.0) < 1e-9, "a direct hit takes full damage");
    expect(std::abs(blast_falloff(4.0, 8.0) - 0.5) < 1e-9, "half way out takes half damage");
    expect(blast_falloff(8.0, 8.0) == 0.0,
           "exactly at the edge takes nothing, so slack cannot leak outward");
    expect(blast_falloff(8.1, 8.0) == 0.0, "past the edge takes nothing");
    expect(blast_falloff(1.0, 0.0) == 0.0, "a zero-radius blast harms nobody");
}

/** Yaw takes the short way around rather than unwinding the long way. */
void turret_yaw_wraps_the_short_way() {
    expect(std::abs(shortest_angle_delta(350.0, 10.0) - 20.0) < 1e-9, "350 to 10 is +20, not -340");
    expect(std::abs(shortest_angle_delta(10.0, 350.0) + 20.0) < 1e-9, "10 to 350 is -20");
    expect(std::abs(shortest_angle_delta(0.0, 180.0) - 180.0) < 1e-9,
           "the half turn resolves positive");

    // A turret sitting at 350 with a target at 10 must reach it in 20 degrees
    // of travel, which at 180 deg/s is 1/9 s -- not 340 degrees of travel.
    double yaw{350.0};
    double elapsed{};
    constexpr double dt{1.0 / 60.0};
    for (int step = 0; step < 600; ++step) {
        const auto aim = step_turret_aim(yaw, 0.0, 10.0, 0.0, 180.0, dt, 0.1);
        yaw = aim.yaw;
        elapsed += dt;
        if (aim.on_target) {
            break;
        }
    }
    expect(elapsed < 0.2,
           "crossing the 0/360 seam must take ~0.11 s, took " + std::to_string(elapsed));
}

/** The aim rate is exactly the recovered 180 deg/s, and it gates firing. */
void turret_barrel_follows_retail_world_update_angles() {
    struct Case {
        double yaw;
        double pitch;
        Vec3 expected;
    };
    const std::array cases{
        Case{0.0, 0.0, {0.0, 1.0, 0.0}},
        Case{90.0, 0.0, {1.0, 0.0, 0.0}},
        Case{180.0, 0.0, {0.0, -1.0, 0.0}},
        Case{-90.0, 0.0, {-1.0, 0.0, 0.0}},
        Case{0.0, 30.0, {0.0, std::sqrt(0.75), -0.5}},
        Case{90.0, -30.0, {std::sqrt(0.75), 0.0, 0.5}},
        Case{-90.0, 90.0, {0.0, 0.0, -1.0}},
    };
    for (const auto& row : cases) {
        const auto rotation = entity_aim_rotation(8U, row.yaw, row.pitch);
        constexpr double radians{3.14159265358979323846 / 180.0};
        const auto pitch = rotation.pitch_degrees * radians;
        const auto yaw = rotation.yaw_degrees * radians;
        // Turret_gun.kv6's barrel runs toward authored +Y (pivot Y=4,
        // muzzle Y=20); Rx(-90) first restores that axis from mesh +Z.
        // Apply the exact axis order consumed by native entity draws.
        Vec3 barrel{0.0, 1.0, 0.0};
        if (rotation.pitch_axis == 0U) {
            barrel = {0.0, std::cos(pitch), std::sin(pitch)};
        }
        const Vec3 pointed{
            barrel.x * std::cos(yaw) - barrel.y * std::sin(yaw),
            barrel.x * std::sin(yaw) + barrel.y * std::cos(yaw), barrel.z};
        expect(std::abs(pointed.x - row.expected.x) < 1e-9 &&
                   std::abs(pointed.y - row.expected.y) < 1e-9 &&
                   std::abs(pointed.z - row.expected.z) < 1e-9,
               "turret barrel must point along its replicated target, including elevation");
    }
    const auto ordinary = entity_aim_rotation(7U, 25.0, 15.0);
    expect(ordinary.pitch_axis == 1U && ordinary.pitch_degrees == -15.0 &&
               ordinary.yaw_degrees == 25.0,
           "turret wire conversion must leave other entity rigs unchanged");
}

void autonomous_rockets_do_not_use_handheld_muzzle_offsets() {
    LauncherMuzzleTracker tracker;
    const auto sent = LauncherMuzzleTracker::Clock::time_point{} + std::chrono::seconds{10};
    LocalEntity rocket;
    rocket.type = 21U;
    rocket.position = {100.0, 100.0, 20.0};
    rocket.velocity = {75.0, 0.0, 0.0};
    expect(!tracker.consume(rocket, sent),
           "owned turret rockets need no first-person offset without a local shot");
    tracker.remember(12U, {40.0, 40.0, 20.0}, rocket.velocity, sent);
    expect(!tracker.consume(rocket, sent + std::chrono::milliseconds{50}),
           "a turret shot must not consume a simultaneous distant handheld shot");
    rocket.position = {40.0, 40.0, 20.0};
    rocket.velocity.y = 1.0;
    expect(!tracker.consume(rocket, sent), "spawn position alone cannot identify the launcher");
    rocket.velocity.y = 0.0;
    rocket.type = 22U;
    expect(!tracker.consume(rocket, sent), "launcher family must match the sent action");
    rocket.type = 21U;
    rocket.position.x += 0.01; // A valid 1/64 wire round-trip error.
    expect(tracker.consume(rocket, sent + std::chrono::milliseconds{100}),
           "explicit local shot must survive wire quantization and weapon switching");
    expect(!tracker.consume(rocket, sent + std::chrono::milliseconds{101}),
           "a local launcher action may match only one created rocket");

    tracker.remember(12U, rocket.position, rocket.velocity, sent);
    expect(!tracker.consume(rocket, sent + std::chrono::seconds{2}),
           "lost replies cannot authorize unrelated later rockets");
    for (unsigned int index{}; index < 9U; ++index) {
        tracker.remember(12U, {static_cast<double>(index), 0.0, 20.0}, rocket.velocity, sent);
    }
    rocket.position = {0.0, 0.0, 20.0};
    expect(!tracker.consume(rocket, sent), "launcher reply history must evict beyond eight requests");
    rocket.position.x = 8.0;
    expect(tracker.consume(rocket, sent), "bounded history must retain the newest launcher request");
    tracker.remember(12U, rocket.position, rocket.velocity, sent);
    tracker.clear();
    expect(!tracker.consume(rocket, sent), "leaving a match must discard pending muzzle ownership");
    for (const auto tool : {13U, 46U}) {
        rocket.type = 22U;
        tracker.remember(static_cast<std::uint8_t>(tool), rocket.position, rocket.velocity, sent);
        expect(tracker.consume(rocket, sent), "RPG2 and UGC RPG2 retain their muzzle presentation");
    }
}

void turret_aim_advances_at_the_recovered_rate() {
    constexpr double dt{1.0 / 60.0};
    const auto aim = step_turret_aim(0.0, 0.0, 90.0, 0.0, 180.0, dt, 0.1);
    expect(std::abs(aim.yaw - 3.0) < 1e-9,
           "180 deg/s at 1/60 s must move exactly 3 degrees, moved " + std::to_string(aim.yaw));
    expect(!aim.on_target, "90 degrees off target must not be a firing solution");
    expect(std::abs(aim.angular_rate - 180.0) < 1e-6,
           "the reported rate drives the aim-loop audio and must be exact");

    const auto settled = step_turret_aim(89.95, 0.0, 90.0, 0.0, 180.0, dt, 0.1);
    expect(settled.on_target, "inside the 0.1 degree tolerance must fire");
    // Pitch must NOT wrap: a turret that took the short way around in pitch
    // would flip upside down to look slightly further down.
    const auto pitched = step_turret_aim(0.0, 170.0, 0.0, -170.0, 180.0, 1.0, 0.1);
    expect(pitched.pitch < 170.0, "pitch must travel toward the target the long way, not wrap");
}

/**
 * The ammo-crate predicate, which is the easiest thing here to get backwards.
 *
 * Retail tests the RESERVE cap: weapons with a reserve top the reserve, and
 * weapons with a magazine but NO reserve top the magazine instead. Inverting
 * it leaves nine catalog rows -- turret, landmine, dynamite, molotov, medpack,
 * chemical bomb, radar, sticky grenade, C4 -- unable to restock at all, with
 * no error anywhere to say why.
 */
void the_ammo_crate_tops_the_right_pool() {
    std::vector<std::uint8_t> magazine_only{16U, 20U, 21U, 33U, 51U, 54U, 56U, 57U, 59U};
    std::vector<std::string> failures;
    for (const auto tool_id : magazine_only) {
        const auto& definition = weapon_catalog()[tool_id];
        if (definition.retail.ammo.reserve_capacity.value_or(0U) != 0U) {
            // The premise of the row has changed; say so rather than silently
            // testing nothing.
            failures.push_back("tool " + std::to_string(tool_id) +
                               " unexpectedly has a reserve capacity");
            continue;
        }
        WeaponReplicationState state;
        state.replace_loadout(std::span{&tool_id, 1U});
        const auto* before = state.ammo(tool_id);
        if (before == nullptr) {
            failures.push_back("tool " + std::to_string(tool_id) + " is not in its own loadout");
            continue;
        }
        const auto magazine_before = before->magazine;
        static_cast<void>(state.restock_from_ammo_crate());
        const auto* after = state.ammo(tool_id);
        if (after == nullptr || after->magazine < magazine_before) {
            failures.push_back("tool " + std::to_string(tool_id) +
                               " lost magazine ammo on a crate restock");
        }
    }
    if (!failures.empty()) {
        std::string report{"ammo crate restock is wrong for:"};
        for (const auto& failure : failures) {
            report += "\n  " + failure;
        }
        throw std::runtime_error{report};
    }
}

/** A crate restock is a PARTIAL top-up; a spawn restock is a full reset. */
void the_crate_and_the_spawn_reset_are_different() {
    // A rifle: has both a magazine and a reserve.
    constexpr std::uint8_t rifle{17U};
    WeaponReplicationState state;
    state.replace_loadout(std::span{&rifle, 1U});
    const auto* fresh = state.ammo(rifle);
    expect(fresh != nullptr, "the rifle must be in its loadout");
    const auto full_reserve = fresh->reserve;
    expect(full_reserve > 0U, "the rifle must ship with reserve ammunition");

    // Drain the reserve, then top up from a crate. The crate must add its
    // restock step, not silently refill to the spawn value.
    for (int shot = 0; shot < 200; ++shot) {
        static_cast<void>(state.begin_reload(rifle));
        static_cast<void>(state.finish_reload(rifle));
    }
    static_cast<void>(state.restock_from_ammo_crate());
    const auto* topped = state.ammo(rifle);
    expect(topped != nullptr, "state must survive the restock");
    expect(topped->reserve <= full_reserve, "a crate must never exceed the reserve capacity");

    state.restock_ammunition();
    const auto* reset = state.ammo(rifle);
    expect(reset != nullptr && reset->reserve == full_reserve,
           "the spawn reset must restore the full initial reserve");
}

/**
 * Every fuse, arm delay and lifetime in the catalog is a sane number.
 *
 * A negative or absurd value here would present as an entity that detonates
 * instantly or never, which is hard to distinguish from a logic bug once it is
 * running in the world.
 */
void every_catalog_timer_is_sane() {
    std::vector<std::string> failures;
    for (const auto& definition : entity_catalog()) {
        const auto complain = [&](const std::string& what, float value) {
            failures.push_back(std::string{definition.symbolic_name} + " " + what + " = " +
                               std::to_string(value));
        };
        if (definition.fuse < 0.0F || definition.fuse > 120.0F) {
            complain("fuse", definition.fuse);
        }
        if (definition.arm_delay < 0.0F || definition.arm_delay > 60.0F) {
            complain("arm_delay", definition.arm_delay);
        }
        if (definition.lifetime < 0.0F || definition.lifetime > 600.0F) {
            complain("lifetime", definition.lifetime);
        }
        if (definition.blast_radius < 0.0F || definition.blast_radius > 32.0F) {
            complain("blast_radius", definition.blast_radius);
        }
        // A blast that deals damage must have somewhere to deal it.
        if (definition.blast_damage > 0.0F && definition.blast_radius <= 0.0F) {
            complain("blast_damage without a radius", definition.blast_damage);
        }
        // Anything with a touch radius must be reachable by a standing player.
        if (definition.touch_radius > 0.0F && definition.touch_radius < 0.5F) {
            complain("touch_radius", definition.touch_radius);
        }
    }
    if (!failures.empty()) {
        std::string report{"implausible catalog timers/ranges:"};
        for (const auto& failure : failures) {
            report += "\n  " + failure;
        }
        throw std::runtime_error{report};
    }
}

/**
 * Face 4 is the ground case, and z growing downward makes its normal point UP.
 *
 * Getting this backwards buries every crate in the floor instead of seating it
 * on top, and it is the easiest sign error in the codebase to make because
 * every other engine would have +Z as up.
 */
void the_default_face_points_up() {
    const auto ground = entity_face_normal(4U);
    expect(ground.z < 0.0, "face 4 is the block's TOP, so its normal is -Z");
    expect(ground.x == 0.0 && ground.y == 0.0, "face 4 is purely vertical");
    expect(entity_face_normal(5U).z > 0.0, "face 5 is the underside");
    // An unset or out-of-range face must land on the ground case rather than
    // producing something that silently sinks the model.
    expect(entity_face_normal(200U).z < 0.0, "an invalid face falls back to ground");

    // Opposing faces must be exact negations, or a wall-stuck charge would sit
    // proud on one side and sunk on the other.
    for (const auto pair : {std::pair{0U, 1U}, std::pair{2U, 3U}, std::pair{4U, 5U}}) {
        const auto a = entity_face_normal(static_cast<std::uint8_t>(pair.first));
        const auto b = entity_face_normal(static_cast<std::uint8_t>(pair.second));
        expect(a.x == -b.x && a.y == -b.y && a.z == -b.z,
               "faces " + std::to_string(pair.first) + " and " + std::to_string(pair.second) +
                   " must be opposites");
    }
}

/** Ground placement needs NO face rotation; the wall cases each need one. */
void the_ground_face_needs_no_rotation() {
    expect(entity_face_rotation(4U).degrees == 0.0F,
           "face 4 is the identity, which is why a ground landmine needs no "
           "face rotation -- its wrong look was a missing basis conversion");
    for (const std::uint8_t face : {std::uint8_t{0U},
                                    std::uint8_t{1U},
                                    std::uint8_t{2U},
                                    std::uint8_t{3U},
                                    std::uint8_t{5U}}) {
        const auto rotation = entity_face_rotation(face);
        expect(rotation.degrees != 0.0F, "face " + std::to_string(face) + " must rotate");
        expect(std::abs(rotation.degrees) == 90.0F || std::abs(rotation.degrees) == 180.0F,
               "retail's face rotations are all quarter or half turns");
        expect(rotation.axis <= 2U, "axis index must be X, Y or Z");
    }
    // The two side pairs turn opposite ways about the same axis.
    expect(entity_face_rotation(0U).axis == entity_face_rotation(1U).axis &&
               entity_face_rotation(0U).degrees == -entity_face_rotation(1U).degrees,
           "faces 0 and 1 are mirrored quarter turns");
    expect(entity_face_rotation(2U).axis == entity_face_rotation(3U).axis &&
               entity_face_rotation(2U).degrees == -entity_face_rotation(3U).degrees,
           "faces 2 and 3 are mirrored quarter turns");
}

/** The landmine's recovered numbers, pinned exactly. */
void the_landmine_matches_the_alias_block() {
    const auto* mine = find_entity_definition(9U);
    expect(mine != nullptr, "LANDMINE must exist");
    expect(std::abs(mine->arm_delay - 4.0F) < 1e-4F, "A1798 arm delay is 4 s");
    expect(std::abs(mine->trip_radius - 2.5F) < 1e-4F, "A1799 trip radius is 2.5");
    expect(std::abs(mine->trip_height - 3.0F) < 1e-4F, "A1800 is 3 vertical layers");
    expect(std::abs(mine->blast_damage - 100.0F) < 1e-4F, "A1802 damage is 100");
    expect(std::abs(mine->blast_radius - 3.0F) < 1e-4F, "A1796 blast radius is 3.0");
    expect(std::abs(mine->health - 1.0F) < 1e-4F, "A1808 health is 1, so any hit detonates it");
}

void entity_display_origins_match_retail_attachment_rules() {
    auto landmine = at(70.0, 71.0, 100.0);
    landmine.type = 9U;
    landmine.face = 4U;
    const auto* landmine_definition = find_entity_definition(landmine.type);
    expect(landmine_definition != nullptr && landmine_definition->parts.size() == 1U,
           "landmine presentation fixture must have one part");
    const auto landmine_origin =
        entity_presentation_position(landmine, landmine_definition->parts[0U]);
    expect(std::abs(landmine_origin.x - 70.5) < 1e-9 &&
               std::abs(landmine_origin.y - 71.5) < 1e-9 &&
               std::abs(landmine_origin.z - 99.5) < 1e-9,
           "a landmine must occupy one voxel centre, not the corner shared by four blocks");

    auto medpack = at(80.25, 81.5, 100.0);
    medpack.type = 30U;
    medpack.face = 4U;
    const auto* medpack_definition = find_entity_definition(medpack.type);
    expect(medpack_definition != nullptr && medpack_definition->parts.size() == 1U,
           "medpack presentation fixture must have one part");
    const auto medpack_origin =
        entity_presentation_position(medpack, medpack_definition->parts[0U]);
    expect(std::abs(medpack_origin.x - 80.25) < 1e-9 && std::abs(medpack_origin.y - 81.5) < 1e-9 &&
               std::abs(medpack_origin.z - 99.5) < 1e-9,
           "ground entities must sit half a block above their support voxel");

    auto grave = at(10.0, 20.0, 30.0);
    grave.type = 11U;
    const auto* grave_definition = find_entity_definition(grave.type);
    expect(grave_definition != nullptr && !grave_definition->parts.empty(),
           "grave presentation fixture must have art");
    const auto grave_origin = entity_presentation_position(grave, grave_definition->parts[0U]);
    expect(std::abs(grave_origin.x - 9.5) < 1e-9 && std::abs(grave_origin.y - 19.5) < 1e-9 &&
               std::abs(grave_origin.z - 29.5) < 1e-9,
           "GraveEntity movement must copy its centered retail display origin");

    auto turret = at(50.0, 60.0, 70.0);
    turret.type = 8U;
    const auto* turret_definition = find_entity_definition(turret.type);
    expect(turret_definition != nullptr && turret_definition->parts.size() == 3U,
           "turret presentation fixture must retain all three parts");
    const auto turret_base = entity_presentation_position(turret, turret_definition->parts[0U]);
    expect(std::abs(turret_base.x - 49.5) < 1e-9 && std::abs(turret_base.y - 59.5) < 1e-9 &&
               std::abs(turret_base.z - 69.32) < 1e-6,
           "turret base must combine XY centring, face seating and part stack");

    EntityModelPart authored;
    authored.offset = {0.1F, 0.2F, 0.3F};
    auto wall = at(5.0, 6.0, 7.0);
    wall.face = 0U;
    const auto wall_origin = entity_presentation_position(wall, authored);
    expect(std::abs(wall_origin.x - 4.8) < 1e-6 && std::abs(wall_origin.y - 6.1) < 1e-6 &&
               std::abs(wall_origin.z - 6.8) < 1e-6,
           "wall faces must rotate authored offsets through Entity.set_position");
}

void only_timed_dynamite_gets_a_countdown_canvas() {
    auto dynamite = at(40.0, 41.0, 90.0);
    dynamite.type = 10U;
    dynamite.fuse = 6.2;
    const auto timed = timed_explosive_presentation(dynamite);
    expect(timed.visible && timed.seconds == 7U &&
               timed.ticking_sound == "dynamite_tick" &&
               std::abs(timed.label_position.x - 40.5) < 1e-9 &&
               std::abs(timed.label_position.y - 41.5) < 1e-9 &&
               std::abs(timed.label_position.z - 89.0) < 1e-9,
           "dynamite must expose its recovered floating countdown and tick loop");

    auto c4 = dynamite;
    c4.type = 38U;
    c4.fuse = 6.2; // malformed input must not invent a timed C4 mechanic.
    expect(!timed_explosive_presentation(c4).visible,
           "retail C4 is remote detonated and must never show a fuse canvas");
}

void objective_and_deployable_labels_follow_entity_update_3dtext() {
    const Vec3 display{10.0, 20.0, 50.0};
    auto intel = at(10.0, 20.0, 50.0);
    intel.type = 16U;
    expect(!entity_world_label(intel, display, std::nullopt, std::nullopt).visible,
           "a carried/home intel has no return timer until set_fuse");
    intel.fuse = 14.2;
    const auto intel_label = entity_world_label(intel, display, std::nullopt, std::nullopt);
    expect(intel_label.visible && intel_label.value == 15U &&
               std::abs(intel_label.position.z - 48.7) < 1e-9 &&
               intel_label.style == EntityWorldLabelStyle::countdown,
           "the dropped intel shows ceil(fuse) at display z - 1.3");

    auto diamond = at(10.0, 20.0, 50.0);
    diamond.type = 15U;
    diamond.fuse = 0.0;
    const auto diamond_label = entity_world_label(diamond, display, std::nullopt, std::nullopt);
    expect(diamond_label.visible && diamond_label.value == 0U,
           "DiamondPickup always carries its lifetime text");

    auto bomb = at(10.0, 20.0, 50.0);
    bomb.type = 14U;
    bomb.fuse = 3.5;
    const auto bomb_label = entity_world_label(bomb, display, std::nullopt, std::nullopt);
    expect(bomb_label.visible && bomb_label.value == 4U &&
               std::abs(bomb_label.position.z - 48.5) < 1e-9,
           "an armed bomb shows its fuse at display z - 1.5");

    auto radar = at(10.0, 20.0, 50.0);
    radar.type = 36U;
    radar.fuse = 44.1;
    const auto radar_label = entity_world_label(radar, display, std::nullopt, std::nullopt);
    expect(radar_label.visible && radar_label.value == 45U &&
               std::abs(radar_label.position.z - 49.0) < 1e-9,
           "the radar station shows its lifetime at z - 1.0");

    auto turret = at(10.0, 20.0, 50.0);
    turret.type = 8U;
    turret.team = 2U;
    turret.ammo = 7U;
    const auto own = entity_world_label(turret, display, std::uint8_t{2U}, Vec3{15.0, 20.0, 50.0});
    expect(own.visible && own.value == 7U && own.style == EntityWorldLabelStyle::turret_ammo,
           "a friendly turret within 20 blocks shows its ammo");
    expect(!entity_world_label(turret, display, std::uint8_t{3U}, Vec3{15.0, 20.0, 50.0}).visible,
           "an enemy turret never reveals its ammo");
    expect(!entity_world_label(turret, display, std::uint8_t{2U}, Vec3{31.0, 20.0, 50.0}).visible,
           "the ammo text disappears beyond A1626 = 20 blocks");
}

void crates_and_intel_spin_and_intel_floats_in_water() {
    auto crate = at(10.0, 20.0, 50.0);
    crate.type = 4U;
    for (int step{}; step < 60; ++step) advance_entity_presentation(crate, 1.0 / 60.0);
    expect(std::abs(crate.spin_degrees - 10.0) < 1e-6, "crates turn 10 degrees per second");

    auto diamond = at(10.0, 20.0, 50.0);
    diamond.type = 15U;
    advance_entity_presentation(diamond, 1.0);
    expect(diamond.spin_degrees == 0.0, "diamond and bomb are not SpinningEntity subclasses");

    const auto* definition = find_entity_definition(4U);
    expect(definition != nullptr && !definition->parts.empty(), "health crate definition");
    auto unspun = at(10.0, 20.0, 50.0);
    unspun.type = 4U;
    const auto still = entity_presentation_transform(unspun, *definition, definition->parts[0U]);
    const auto spun = entity_presentation_transform(crate, *definition, definition->parts[0U]);
    expect(std::abs(still[0U] - spun[0U]) > 1e-4 && std::abs(still[14U] - spun[14U]) < 1e-6,
           "the spin turns the model about the vertical without moving it");

    auto intel = at(10.0, 20.0, 238.0);
    intel.type = 16U;
    for (int step{}; step < 180; ++step) advance_entity_presentation(intel, 1.0 / 60.0);
    expect(std::abs(intel.floating_offset - 0.7) < 1e-9,
           "intel rises out of the water to floating_range 0.7");
    const auto* intel_definition = find_entity_definition(16U);
    expect(intel_definition != nullptr && !intel_definition->parts.empty(), "intel definition");
    auto dry = intel;
    dry.floating_offset = 0.0;
    const auto floating = entity_presentation_transform(intel, *intel_definition,
                                                        intel_definition->parts[0U]);
    const auto resting = entity_presentation_transform(dry, *intel_definition,
                                                       intel_definition->parts[0U]);
    expect(std::abs((resting[14U] - floating[14U]) - 0.7F) < 1e-4F,
           "the float lifts the drawn intel (map z decreases)");
    intel.position.z = 200.0;
    advance_entity_presentation(intel, 1.0 / 60.0);
    expect(intel.floating_offset == 0.0, "leaving the water resets the float");
}

[[nodiscard]] VxlMap empty_world() {
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
    expect(static_cast<bool>(loaded), "synthetic entity-physics world must parse");
    return std::move(*loaded.map);
}

void add_support(VxlMap& map, std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    expect(map.set_voxel(x, y, z, VxlColor{100U, 110U, 120U, 255U}),
           "entity support voxel must be accepted");
}

/** The authored rocket nose must follow velocity over every principal axis. */
void projectile_nose_never_turns_sideways() {
    constexpr std::array<std::array<double, 3U>, 7U> velocities{{
        {-1.0, 0.0, 0.0},
        {1.0, 0.0, 0.0},
        {0.0, -1.0, 0.0},
        {0.0, 1.0, 0.0},
        {0.0, 0.0, -1.0},
        {0.0, 0.0, 1.0},
        {3.0, -4.0, 2.0},
    }};
    for (const auto& velocity : velocities) {
        const auto transform =
            projectile_presentation_transform({10.0, 20.0, 30.0}, velocity, 37.0, 0.02F);
        const auto nose = projectile_presentation_direction(transform, {0.0F, 0.0F, -1.0F});
        const double length = std::sqrt(velocity[0U] * velocity[0U] + velocity[1U] * velocity[1U] +
                                        velocity[2U] * velocity[2U]);
        for (std::size_t axis{}; axis < nose.size(); ++axis) {
            expect(std::abs(static_cast<double>(nose[axis]) - velocity[axis] / length) < 0.0005,
                   "the rocket's authored -Z nose diverged from flight velocity");
        }
        expect(std::abs(transform[12U] - 9.5F) < 0.0001F &&
                   std::abs(transform[13U] - 19.5F) < 0.0001F &&
                   std::abs(transform[14U] - 29.5F) < 0.0001F,
               "projectile placement lost retail's half-block display offset");
        const auto exhaust = projectile_exhaust_position(
            {10.0, 20.0, 30.0}, velocity, 37.0, 0.02F);
        const std::array<double, 3U> centre{transform[12U], transform[13U],
                                            transform[14U]};
        const double exhaust_projection =
            (static_cast<double>(exhaust[0U]) - centre[0U]) * velocity[0U] +
            (static_cast<double>(exhaust[1U]) - centre[1U]) * velocity[1U] +
            (static_cast<double>(exhaust[2U]) - centre[2U]) * velocity[2U];
        const double exhaust_distance = std::sqrt(
            std::pow(static_cast<double>(exhaust[0U]) - centre[0U], 2.0) +
            std::pow(static_cast<double>(exhaust[1U]) - centre[1U], 2.0) +
            std::pow(static_cast<double>(exhaust[2U]) - centre[2U], 2.0));
        expect(exhaust_projection < 0.0 && std::abs(exhaust_distance - 0.38) < 0.0005,
               "rocket smoke anchor must stay on the rendered tail over every flight axis");
    }
}

/** Local RPG presentation begins at the measured barrel, then rejoins wire motion. */
void owning_rocket_exits_the_first_person_launcher() {
    constexpr std::array<double, 3U> spawn{10.0, 20.0, 30.0};
    constexpr std::array<double, 3U> velocity{-75.0, 0.0, 0.0};
    const auto anchor = local_rocket_presentation_anchor(
        12U, spawn, spawn, velocity, 0.0);
    const auto transform = projectile_presentation_transform(
        anchor, velocity, 0.0, 0.02F);
    const std::array<double, 3U> displayed_centre{
        transform[12U], transform[13U], transform[14U]};
    // yaw=0: forward=-X, right=-Y and up=-Z.
    expect(std::abs((spawn[0U] - displayed_centre[0U]) - 1.7775) < 1.0e-5 &&
               std::abs((spawn[1U] - displayed_centre[1U]) - 0.4) < 1.0e-5 &&
               std::abs((displayed_centre[2U] - spawn[2U]) - 0.2875) < 1.0e-5,
           "owning RPG must originate at the measured first-person barrel mouth");

    constexpr std::array<double, 3U> integrated{6.25, 20.0, 30.0};
    const auto settled = local_rocket_presentation_anchor(
        12U, spawn, integrated, velocity, 0.05);
    expect(settled == integrated,
           "muzzle presentation must converge exactly onto authoritative motion");
    expect(local_rocket_presentation_anchor(
               11U, spawn, integrated, velocity, 0.0) == integrated,
           "grenades and remote projectile families must not inherit RPG offsets");
}

/** Pickups, graves and deployables fall continuously, never teleport down. */
void physical_entities_fall_and_anchor_to_the_solid_voxel() {
    constexpr std::array<std::uint8_t, 4U> types{3U, 9U, 11U, 30U};
    for (std::size_t index{}; index < types.size(); ++index) {
        auto map = empty_world();
        const auto x = static_cast<std::uint32_t>(20U + index);
        constexpr std::uint32_t y{25U};
        constexpr std::uint32_t floor_z{100U};
        add_support(map, x, y, floor_z);
        const auto* definition = find_entity_definition(types[index]);
        expect(definition != nullptr && uses_entity_terrain_gravity(*definition),
               "the physical entity row must opt into terrain gravity");

        LocalEntity entity = at(static_cast<double>(x) + 0.25, static_cast<double>(y) + 0.25, 90.0);
        entity.type = types[index];
        const auto first = step_entity_terrain_physics(entity, *definition, map, 1.0 / 60.0);
        expect(first.moved && !first.landed && entity.position.z > 90.0 &&
                   entity.position.z < static_cast<double>(floor_z),
               "an airborne entity must visibly fall on its first tick, not snap");

        for (int tick{}; tick < 300 && !entity.grounded; ++tick) {
            static_cast<void>(step_entity_terrain_physics(entity, *definition, map, 1.0 / 60.0));
        }
        expect(entity.grounded, "the falling entity must eventually land");
        expect(entity.position.z == static_cast<double>(floor_z),
               "landing must store the SOLID support voxel; the renderer's "
               "face standoff handles the visible half-block offset");
        expect(entity.velocity.x == 0.0 && entity.velocity.y == 0.0 && entity.velocity.z == 0.0,
               "a settled entity must stop accumulating hidden velocity");
    }
}

/** Digging away the supporting block wakes a resting entity immediately. */
void removing_support_restarts_gravity() {
    auto map = empty_world();
    add_support(map, 40U, 40U, 100U);
    add_support(map, 40U, 40U, 105U);
    const auto* grave = find_entity_definition(11U);
    expect(grave != nullptr, "GRAVE must exist");
    LocalEntity entity = at(40.25, 40.25, 100.0);
    entity.type = 11U;
    entity.grounded = true;
    expect(map.clear_voxel(40U, 40U, 100U), "the upper grave support must be removable");

    const auto first = step_entity_terrain_physics(entity, *grave, map, 1.0 / 60.0);
    expect(first.support_lost && first.moved && !entity.grounded,
           "a removed floor must wake the grave on the next fixed tick");
    for (int tick{}; tick < 180 && !entity.grounded; ++tick) {
        static_cast<void>(step_entity_terrain_physics(entity, *grave, map, 1.0 / 60.0));
    }
    expect(entity.grounded && entity.position.z == 105.0,
           "the grave must settle on the next surviving VXL support");
}

/** Wall charges stay fixed, then fall once their attachment voxel is mined. */
void attached_charges_fall_when_the_wall_disappears() {
    auto map = empty_world();
    add_support(map, 60U, 60U, 90U);
    add_support(map, 60U, 60U, 100U);
    const auto* c4 = find_entity_definition(38U);
    expect(c4 != nullptr, "C4 must exist");
    LocalEntity entity = at(60.25, 60.25, 90.0);
    entity.type = 38U;
    entity.face = 0U;
    entity.attached = true;

    const auto fixed = step_entity_terrain_physics(entity, *c4, map, 1.0 / 60.0);
    expect(!fixed.moved && entity.attached && entity.position.z == 90.0,
           "an intact wall attachment must suppress gravity");
    expect(map.clear_voxel(60U, 60U, 90U), "the charge attachment voxel must be removable");
    const auto released = step_entity_terrain_physics(entity, *c4, map, 1.0 / 60.0);
    expect(released.support_lost && !entity.attached && entity.face == 4U,
           "destroying the wall must release and reorient the charge");
    for (int tick{}; tick < 240 && !entity.grounded; ++tick) {
        static_cast<void>(step_entity_terrain_physics(entity, *c4, map, 1.0 / 60.0));
    }
    expect(entity.grounded && entity.position.z == 100.0,
           "the released charge must land on the floor below");
}

/** A large server velocity cannot tunnel a physical entity through a floor. */
void swept_entity_falls_cannot_tunnel() {
    auto map = empty_world();
    add_support(map, 80U, 80U, 100U);
    const auto* medpack = find_entity_definition(30U);
    expect(medpack != nullptr, "MEDPACK must exist");
    LocalEntity entity = at(80.25, 80.25, 80.0);
    entity.type = 30U;
    entity.velocity.z = 500.0;
    const auto step = step_entity_terrain_physics(entity, *medpack, map, 0.1);
    expect(step.landed && entity.grounded && entity.position.z == 100.0,
           "swept gravity must catch a one-voxel floor at high speed");
}

/** Finite wire/debug inputs must not overflow the sweep's integer step count. */
void extreme_entity_sweeps_remain_bounded_and_finite() {
    auto map = empty_world();
    const auto* grave = find_entity_definition(11U);
    expect(grave != nullptr, "GRAVE must exist");
    for (const double dt : {1.0, std::numeric_limits<double>::max()}) {
        LocalEntity entity = at(82.25, 82.25, 90.0);
        entity.type = 11U;
        entity.velocity = {std::numeric_limits<double>::max(), 0.0, 0.0};
        static_cast<void>(step_entity_terrain_physics(entity, *grave, map, dt));
        expect(std::isfinite(entity.position.x) && std::isfinite(entity.position.y) &&
                   std::isfinite(entity.position.z) && std::isfinite(entity.velocity.z),
               "overflowing requested displacement must leave a finite, bounded physics state");
    }
}

/** Graves use retail's 50% vertical rebound and eventually settle. */
void grave_bounce_scales_with_fall_distance_and_diminishes() {
    const auto* grave = find_entity_definition(11U);
    expect(grave != nullptr, "GRAVE must exist");
    const auto first_rebound = [&](double start_z) {
        auto map = empty_world();
        add_support(map, 82U, 82U, 100U);
        LocalEntity entity = at(82.25, 82.25, start_z);
        entity.type = 11U;
        EntityPhysicsStep contact;
        for (int tick{}; tick < 600 && !contact.bounced; ++tick) {
            contact = step_entity_terrain_physics(entity, *grave, map, 1.0 / 60.0);
        }
        expect(contact.landed && contact.bounced && contact.impact_speed > 0.0,
               "a falling grave must make a visible first rebound");
        expect(std::abs(-entity.velocity.z - contact.impact_speed * 0.5) < 0.0001,
               "grave rebound must retain exactly half its impact speed");
        const auto rebound = -entity.velocity.z;
        for (int tick{}; tick < 600 && !entity.grounded; ++tick) {
            static_cast<void>(step_entity_terrain_physics(entity, *grave, map, 1.0 / 60.0));
        }
        expect(entity.grounded && entity.position.z == 100.0 && entity.velocity.z == 0.0,
               "diminishing grave rebounds must settle on the support voxel");
        return rebound;
    };

    const auto short_drop = first_rebound(96.0);
    const auto long_drop = first_rebound(80.0);
    expect(long_drop > short_drop, "a longer fall must produce the larger first rebound");
}

/** Projectiles and player/map-attached rows keep their dedicated ownership. */
void anchored_rows_do_not_receive_generic_gravity() {
    auto map = empty_world();
    for (const std::uint8_t type :
         {std::uint8_t{13U}, std::uint8_t{21U}, std::uint8_t{35U}, std::uint8_t{39U}}) {
        const auto* definition = find_entity_definition(type);
        expect(definition != nullptr && !uses_entity_terrain_gravity(*definition),
               "anchored/projectile row must reject ordinary entity gravity");
        LocalEntity entity = at(100.25, 100.25, 90.0);
        entity.type = type;
        const auto before = entity.position;
        const auto step = step_entity_terrain_physics(entity, *definition, map, 1.0 / 60.0);
        expect(!step.moved && entity.position.x == before.x && entity.position.y == before.y &&
                   entity.position.z == before.z,
               "dedicated-ownership rows must remain untouched");
    }
}

/** HealthCrate alone projects its recovered half-cell shadow onto live VXL. */
void only_health_crates_project_a_ground_shadow() {
    auto map = empty_world();
    add_support(map, 40U, 41U, 100U);

    LocalEntity crate = at(40.0, 41.0, 90.0);
    crate.type = 4U;
    const auto airborne = health_crate_spot_shadow_position(crate, map);
    expect(airborne.has_value(), "a falling health crate must retain its ground shadow");
    expect(std::abs(airborne->x - 40.5) < 1e-9 &&
               std::abs(airborne->y - 41.5) < 1e-9 &&
               std::abs(airborne->z - 99.998) < 1e-9,
           "the health-crate shadow must use its exact recovered XY offset and support plane");

    crate.position.z = 100.0;
    expect(health_crate_spot_shadow_position(crate, map).has_value(),
           "a landed health crate must keep the same contact shadow");
    crate.alive = false;
    expect(!health_crate_spot_shadow_position(crate, map).has_value(),
           "a consumed health crate must not leave a shadow behind");
    crate.alive = true;
    crate.type = 3U;
    expect(!health_crate_spot_shadow_position(crate, map).has_value(),
           "ammo and block crates do not opt into retail's spot shadow");
}

void placement_transforms_follow_faces_and_keep_rigs_on_the_support() {
    auto charge = at(10.0, 20.0, 30.0);
    charge.type = 38U;
    const auto* definition = find_entity_definition(charge.type);
    expect(definition != nullptr && !definition->parts.empty(), "C4 has a model part");
    const std::array<Vec3, 6U> normals{{{-1.0, 0.0, 0.0}, {1.0, 0.0, 0.0},
                                       {0.0, -1.0, 0.0}, {0.0, 1.0, 0.0},
                                       {0.0, 0.0, -1.0}, {0.0, 0.0, 1.0}}};
    for (std::uint8_t face{}; face < normals.size(); ++face) {
        charge.face = face;
        const auto& part = definition->parts.front();
        const auto matrix = entity_presentation_transform(charge, *definition, part);
        const auto scale = definition->model_size * part.scale;
        expect(std::abs(matrix[4U] / scale - normals[face].x) < 1e-5 &&
                   std::abs(matrix[5U] / scale - normals[face].y) < 1e-5 &&
                   std::abs(matrix[6U] / scale - normals[face].z) < 1e-5,
               "the KV6 upper axis must face out from each possible attachment surface");
        const auto origin = entity_presentation_position(charge, part);
        expect(std::abs(matrix[12U] - origin.x) < 1e-5 &&
                   std::abs(matrix[13U] - origin.y) < 1e-5 &&
                   std::abs(matrix[14U] - origin.z) < 1e-5,
               "rotating a wall charge must preserve its authored attachment origin");
    }

    for (const auto tool : {16U, 20U, 21U, 51U, 56U, 59U}) {
        auto ghost = at(10.0, 20.0, 30.0);
        ghost.type = entity_placed_by_tool(static_cast<std::uint8_t>(tool));
        ghost.grounded = true;
        ghost.attached = true;
        const auto* rig = find_entity_definition(ghost.type);
        expect(rig != nullptr, "every deployable ghost needs its actual entity catalog row");
        const std::vector<float> bounds(rig->parts.size(), -3.0F);
        const auto contact = entity_rig_vertical_contact_adjustment(ghost, *rig, rig->parts, bounds);
        double lowest = -std::numeric_limits<double>::infinity();
        for (std::size_t part{}; part < rig->parts.size(); ++part) {
            if (rig->parts[part].rotation_mode >= 2U) continue;
            const auto transform = entity_presentation_transform(ghost, *rig, rig->parts[part], contact);
            lowest = std::max(lowest, static_cast<double>(bounds[part] * transform[6U] + transform[14U]));
        }
        expect(std::abs(lowest - ghost.position.z) < 1e-5,
               "each deployed rig and its ghost must rest on the support voxel without hovering");
    }
}

struct TestCase final {
    const char* name;
    void (*run)();
};

} // namespace

int main() {
    const TestCase cases[]{
        {"the_touch_sphere_is_pure_proximity", the_touch_sphere_is_pure_proximity},
        {"the_trip_volume_is_a_cylinder", the_trip_volume_is_a_cylinder},
        {"the_blast_falloff_stops_at_its_radius", the_blast_falloff_stops_at_its_radius},
        {"turret_yaw_wraps_the_short_way", turret_yaw_wraps_the_short_way},
        {"turret_aim_advances_at_the_recovered_rate", turret_aim_advances_at_the_recovered_rate},
        {"turret_barrel_follows_retail_world_update_angles",
         turret_barrel_follows_retail_world_update_angles},
        {"autonomous_rockets_do_not_use_handheld_muzzle_offsets",
         autonomous_rockets_do_not_use_handheld_muzzle_offsets},
        {"the_ammo_crate_tops_the_right_pool", the_ammo_crate_tops_the_right_pool},
        {"the_crate_and_the_spawn_reset_are_different",
         the_crate_and_the_spawn_reset_are_different},
        {"every_catalog_timer_is_sane", every_catalog_timer_is_sane},
        {"the_default_face_points_up", the_default_face_points_up},
        {"the_ground_face_needs_no_rotation", the_ground_face_needs_no_rotation},
        {"entity_display_origins_match_retail_attachment_rules",
         entity_display_origins_match_retail_attachment_rules},
        {"placement_transforms_follow_faces_and_keep_rigs_on_the_support",
         placement_transforms_follow_faces_and_keep_rigs_on_the_support},
        {"only_timed_dynamite_gets_a_countdown_canvas",
         only_timed_dynamite_gets_a_countdown_canvas},
        {"objective_and_deployable_labels_follow_entity_update_3dtext",
         objective_and_deployable_labels_follow_entity_update_3dtext},
        {"crates_and_intel_spin_and_intel_floats_in_water",
         crates_and_intel_spin_and_intel_floats_in_water},
        {"the_landmine_matches_the_alias_block", the_landmine_matches_the_alias_block},
        {"projectile_nose_never_turns_sideways", projectile_nose_never_turns_sideways},
        {"owning_rocket_exits_the_first_person_launcher",
         owning_rocket_exits_the_first_person_launcher},
        {"physical_entities_fall_and_anchor_to_the_solid_voxel",
         physical_entities_fall_and_anchor_to_the_solid_voxel},
        {"removing_support_restarts_gravity", removing_support_restarts_gravity},
        {"attached_charges_fall_when_the_wall_disappears",
         attached_charges_fall_when_the_wall_disappears},
        {"swept_entity_falls_cannot_tunnel", swept_entity_falls_cannot_tunnel},
        {"extreme_entity_sweeps_remain_bounded_and_finite",
         extreme_entity_sweeps_remain_bounded_and_finite},
        {"grave_bounce_scales_with_fall_distance_and_diminishes",
         grave_bounce_scales_with_fall_distance_and_diminishes},
        {"anchored_rows_do_not_receive_generic_gravity",
         anchored_rows_do_not_receive_generic_gravity},
        {"only_health_crates_project_a_ground_shadow",
         only_health_crates_project_a_ground_shadow},
    };
    for (const auto& test : cases) {
        try {
            test.run();
        } catch (const std::exception& error) {
            std::cerr << "FAILED " << test.name << ": " << error.what() << '\n';
            return 1;
        }
        std::cout << "ok " << test.name << '\n';
    }
    return 0;
}
