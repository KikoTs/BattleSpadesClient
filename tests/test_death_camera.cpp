#include "battlespades/frontend/death_camera.hpp"
#include "battlespades/frontend/live_client_policy.hpp"
#include "battlespades/network/protocol168_players.hpp"
#include "battlespades/render/camera_basis.hpp"
#include "battlespades/world/entity_catalog.hpp"
#include "battlespades/world/local_entity.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using battlespades::frontend::chase_camera_eye;
using battlespades::frontend::DeathCameraController;
using battlespades::frontend::DeathCameraMode;
using battlespades::frontend::DeathCameraTarget;
using battlespades::frontend::DeathKillerInfo;
using battlespades::frontend::deathcam_valid_kill_type;
using battlespades::frontend::FlyCameraKey;
using battlespades::frontend::grave_camera_focus;
using battlespades::world::Vec3;
using battlespades::world::VxlColor;
using battlespades::world::VxlMap;

void expect(bool value, const char* message) {
    if (!value) throw std::runtime_error{message};
}

[[nodiscard]] double distance(Vec3 a, Vec3 b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) +
                     (a.z - b.z) * (a.z - b.z));
}

[[nodiscard]] Vec3 camera_position(Vec3 eye) {
    eye.y -= 0.5;
    return eye;
}

/** Where `point` lands in normalised view space: (0, 0) is screen centre. */
struct ViewProjection final {
    double right{};
    double up{};
    double depth{};
};

[[nodiscard]] ViewProjection project(const battlespades::frontend::DeathCameraPose& pose,
                                     Vec3 point) {
    const auto basis = battlespades::render::world_camera_basis(pose.yaw_degrees,
                                                                pose.pitch_degrees);
    const Vec3 v{point.x - pose.eye.x, point.y - pose.eye.y, point.z - pose.eye.z};
    const auto dot = [&v](const std::array<double, 3U>& axis) {
        return v.x * axis[0U] + v.y * axis[1U] + v.z * axis[2U];
    };
    const double depth = dot(basis.forward);
    return {dot(basis.right) / depth, dot(basis.up) / depth, depth};
}

[[nodiscard]] bool centred(const ViewProjection& p, double tolerance = 1.0e-9) {
    return p.depth > 0.0 && std::abs(p.right) < tolerance && std::abs(p.up) < tolerance;
}

[[nodiscard]] std::shared_ptr<VxlMap> empty_world() {
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
    expect(static_cast<bool>(loaded), "synthetic world must parse");
    return std::make_shared<VxlMap>(std::move(*loaded.map));
}

} // namespace

int main() {
    try {
        expect(deathcam_valid_kill_type(0U) && deathcam_valid_kill_type(6U) &&
                   deathcam_valid_kill_type(21U) && deathcam_valid_kill_type(24U) &&
                   !deathcam_valid_kill_type(7U) && !deathcam_valid_kill_type(25U),
               "DEATHCAM_VALID_TYPES is [0..6, 21..24]");

        const Vec3 death_eye{100.0, 100.0, 200.0};
        {
            DeathCameraController camera;
            camera.begin_death(death_eye, std::nullopt, false, false);
            const auto before = camera.pose().eye;
            camera.update_body_position({100, 100, 205});
            expect(std::abs(camera.pose().eye.z - before.z - 5.0) < 1e-8,
                   "death camera follows local corpse gravity");
            camera.bind_grave(42, {100,100,208});
            const auto grave = camera.pose().eye;
            camera.update_body_position({100,100,215});
            expect(std::abs(camera.pose().eye.z - grave.z) < 1e-8, "server grave retains camera ownership");
        }

        // A first kill (streak 1): straight to the chase camera on our own
        // body, locked -- retail never lets a dead player browse the map.
        {
            DeathCameraController camera;
            camera.begin_death(death_eye,
                               DeathKillerInfo{9U, {120.0, 100.0, 200.0}, 0U, 1U}, true);
            expect(camera.mode() == DeathCameraMode::grave,
                   "a killer's first kill must not open the killer view");
            expect(!camera.can_cycle_targets(),
                   "an ordinary death locks the chase camera to our own body");
            const auto pose = camera.pose();
            expect(std::abs(distance(pose.eye, death_eye) - 5.0) < 1.0e-6,
                   "the chase camera sits five blocks from the body with no wall behind");
            expect(centred(project(pose, death_eye)),
                   "our own body is framed dead centre before a grave exists");
            expect(std::abs(pose.yaw_degrees - 90.0) < 1.0e-9 &&
                       std::abs(pose.pitch_degrees - 20.0) < 1.0e-9,
                   "the grave camera keeps the look yaw and starts 20 degrees above the body");
        }

        // One retail Camera: the first-person look is the death camera's
        // starting angle. Looking at the killer when shot keeps them in view
        // past our body even on a first kill (which opens no killer view).
        {
            DeathCameraController camera;
            camera.on_mouse_move(300.0, -200.0); // inactive: ignored
            const Vec3 killer{100.0, 120.0, 200.0}; // +y: yaw -90 in this basis
            camera.set_view_angles(-90.0, 0.0);
            camera.begin_death(death_eye, DeathKillerInfo{9U, killer, 0U, 1U}, true);
            expect(camera.mode() == DeathCameraMode::grave,
                   "streak 1 still goes straight to the chase camera");
            const auto pose = camera.pose();
            expect(std::abs(pose.yaw_degrees + 90.0) < 1.0e-9 &&
                       std::abs(pose.pitch_degrees - 20.0) < 1.0e-9,
                   "the death camera inherits the look yaw and looks down on the body");
            expect(centred(project(pose, death_eye)) && pose.eye.z < death_eye.z,
                   "our body stays centred with the eye above it");
            camera.on_mouse_move(120.0, 40.0);
            camera.end_life();
            camera.set_view_angles(30.0, 95.0);
            camera.begin_death(death_eye, std::nullopt, false);
            expect(std::abs(camera.pose().yaw_degrees - 30.0) < 1.0e-9 &&
                       std::abs(camera.pose().pitch_degrees - 89.9) < 1.0e-9,
                   "each death re-seeds from the current look, not the previous death's orbit");
        }

        // Streak 2 from a look facing away: the killer view turns the camera
        // round to the killer (yaw and pitch) and frames them on screen centre.
        {
            DeathCameraController camera;
            const Vec3 killer{80.0, 85.0, 192.0};
            camera.set_view_angles(180.0, -30.0);
            camera.begin_death(death_eye, DeathKillerInfo{9U, killer, 0U, 2U}, true);
            expect(camera.mode() == DeathCameraMode::killer_view, "streak 2: killer view");
            expect(project(camera.pose(), killer).depth < 0.0,
                   "the fixture starts with the killer behind the camera");
            for (int tick{}; tick < 84; ++tick) camera.tick(1.0 / 60.0);
            const auto pose = camera.pose();
            const auto seen = project(pose, killer);
            expect(seen.depth > 0.0 && std::abs(seen.right) < 1.0e-3 &&
                       std::abs(seen.up) < 1.0e-3,
                   "after 1.4 s the killer view looks straight at the killer");
            // validate_position runs before rotate (DeathController.update), so
            // the orbit trails the turn by one tick's worth of angle.
            expect(centred(project(pose, death_eye), 1.0e-3),
                   "the orbit keeps our body centred in front of the killer");
            expect(pose.pitch_degrees < 0.0,
                   "a killer standing above us is looked up at (negative pitch)");
        }

        // The grave (entity 11) is framed on screen centre: the drawn
        // tombstone's middle, not the packet origin at its corner.
        {
            namespace world = battlespades::world;
            const auto* definition = world::find_entity_definition(11U);
            expect(definition != nullptr && !definition->parts.empty(), "grave catalog row");
            world::LocalEntity grave;
            grave.id = 41U;
            grave.type = 11U;
            grave.position = {200.3, 150.7, 230.2};
            grave.face = 4U;
            grave.grounded = true;
            const auto focus = grave_camera_focus(grave);
            expect(distance(focus, {199.8, 150.2, 230.0 - 1.1}) < 1.0e-6,
                   "grave focus: half-block display centring, 1.1 above the support surface");
            // Cross-check through the renderer's own transform: grave.kv6 is
            // 16x4x22 voxels; after offset_pivots(0,0,11) its centre sits at
            // mesh (0, 11, 0) (Kv6Model stores authored axes as x, -z, y),
            // and its lowest mesh y is 0.
            const auto& part = definition->parts[0U];
            const double adjust =
                world::entity_vertical_contact_adjustment(grave, *definition, part, 0.0F);
            const auto m = world::entity_presentation_transform(grave, *definition, part, adjust);
            const double mesh_y = static_cast<double>(part.pivot_offset[2U]);
            const Vec3 drawn{mesh_y * m[4U] + m[12U], mesh_y * m[5U] + m[13U],
                             mesh_y * m[6U] + m[14U]};
            expect(distance(focus, drawn) < 1.0e-4,
                   "the focus is where entity_presentation_transform draws the stone's middle");

            DeathCameraController camera;
            camera.set_view_angles(37.0, 21.0);
            camera.begin_death({200.3, 150.7, 228.0}, std::nullopt, true);
            camera.bind_grave(grave.id, focus);
            expect(centred(project(camera.pose(), drawn), 1.0e-5),
                   "the bound grave projects onto the middle of the screen");
            camera.on_mouse_move(-450.0, 300.0);
            expect(centred(project(camera.pose(), drawn), 1.0e-5),
                   "it stays centred while the dead player orbits it");
            grave.grounded = false;
            grave.position.z = 226.0;
            camera.update_grave(grave.id, grave_camera_focus(grave));
            expect(centred(project(camera.pose(), grave_camera_focus(grave)), 1.0e-9),
                   "a falling grave is followed and kept centred");

            // The streak-2 killer view orbits the same grave focus.
            grave.grounded = true;
            grave.position.z = 230.2;
            DeathCameraController killer_cam;
            killer_cam.set_view_angles(0.0, 0.0);
            killer_cam.begin_death({200.3, 150.7, 228.0},
                                   DeathKillerInfo{9U, {170.0, 150.0, 228.0}, 0U, 2U}, true);
            killer_cam.bind_grave(grave.id, focus);
            for (int tick{}; tick < 30; ++tick) killer_cam.tick(1.0 / 60.0);
            expect(killer_cam.mode() == DeathCameraMode::killer_view &&
                       centred(project(killer_cam.pose(), focus), 1.0e-3),
                   "the killer view keeps the grave centred while it turns");

            // A wall behind the grave pulls the eye in but keeps the framing.
            auto map = empty_world();
            for (std::uint32_t y{140U}; y <= 160U; ++y) {
                for (std::uint32_t z{215U}; z <= 235U; ++z) {
                    expect(map->set_voxel(202U, y, z, VxlColor{90U, 90U, 90U, 255U}),
                           "wall voxel must be accepted");
                }
            }
            DeathCameraController walled;
            walled.set_terrain(map.get());
            walled.set_view_angles(0.0, 0.0); // looking along -x: the wall is behind
            walled.begin_death({200.3, 150.7, 228.0}, std::nullopt, true);
            walled.bind_grave(grave.id, focus);
            const auto pulled = walled.pose();
            expect(pulled.eye.x < 202.0 && centred(project(pulled, drawn), 1.0e-5),
                   "the pulled-in eye still frames the grave centrally");
        }

        // Second consecutive kill: the killer view faces the killer from the
        // body orbit. Its aim does not follow later killer movements.
        {
            DeathCameraController camera;
            const Vec3 killer{80.0, 100.0, 200.0};
            camera.begin_death(death_eye,
                               DeathKillerInfo{9U, killer, 1U, 2U}, true);
            expect(camera.mode() == DeathCameraMode::killer_view,
                   "streak 2 must open the killer view");
            expect(!camera.chase_available() && !camera.can_cycle_targets(),
                   "chase is not available before 1.5 s");
            for (int tick{}; tick < 60; ++tick) camera.tick(1.0 / 60.0);
            const auto pose = camera.pose();
            expect(std::abs(distance(pose.eye, death_eye) - 5.0) < 1.0e-9,
                   "streak 2 keeps the chase eye five blocks behind the body");
            // The killer lies along -x, which is yaw 0 in this basis.
            expect(std::abs(pose.yaw_degrees) < 1.0,
                   "one second of angle lerp 10 must turn most of the way to the killer");
            camera.set_killer_position(Vec3{100.0, 80.0, 200.0});
            camera.on_mouse_press();
            expect(camera.mode() == DeathCameraMode::killer_view,
                   "a click before 1.5 s must not leave the killer view");
            for (int tick{}; tick < 31; ++tick) camera.tick(1.0 / 60.0);
            expect(std::abs(camera.pose().yaw_degrees) < 0.1,
                   "a non-zooming killer view retains its initial aim");
            expect(camera.chase_available(), "chase becomes available after 1.5 s");
            camera.on_mouse_move(60.0, 0.0);
            expect(camera.mode() == DeathCameraMode::killer_view,
                   "60 counts is below mouse_movement_to_chase_cam");
            camera.on_mouse_move(50.0, 0.0);
            expect(camera.mode() == DeathCameraMode::grave,
                   "more than 100 counts switches to the chase camera on our body");
        }

        // Third consecutive kill with the killer far away: fly toward them
        // after 0.25 s, toward a fixed endpoint five short of the initial killer.
        {
            DeathCameraController camera;
            const Vec3 killer{60.0, 100.0, 200.0};
            camera.begin_death(death_eye,
                               DeathKillerInfo{9U, killer, 0U, 3U}, true);
            for (int tick{}; tick < 14; ++tick) camera.tick(1.0 / 60.0);
            expect(std::abs(distance(camera.pose().eye, death_eye) - 5.0) < 1.0e-9,
                   "before the fly-in the camera uses the chase eye behind the body");
            camera.set_killer_position(Vec3{60.0, 70.0, 200.0});
            for (int tick{}; tick < 250; ++tick) camera.tick(1.0 / 60.0);
            expect(camera.mode() == DeathCameraMode::killer_view,
                   "4.4 s is still before the forced switch");
            const double left = distance(camera_position(camera.pose().eye), killer);
            expect(left > 5.0 && left < 5.001,
                   "the fly-in approaches five short of the original killer despite their movement");
            expect(camera.pose().yaw_degrees > 60.0,
                   "while zooming the aim follows the live killer");
            camera.set_killer_position(std::nullopt);
            camera.tick(1.0 / 60.0);
            expect(distance(camera_position(camera.pose().eye), killer) < 5.001,
                   "a missing killer does not discard the fixed zoom endpoint");
            for (int tick{}; tick < 40; ++tick) camera.tick(1.0 / 60.0);
            expect(camera.mode() == DeathCameraMode::grave,
                   "the chase camera is forced at five seconds");
        }

        // An invalid kill type or disabled deathcam goes straight to chase.
        {
            DeathCameraController camera;
            camera.begin_death(death_eye,
                               DeathKillerInfo{9U, {90.0, 100.0, 200.0}, 7U, 4U}, true);
            expect(camera.mode() == DeathCameraMode::grave,
                   "kill types outside DEATHCAM_VALID_TYPES skip the killer view");
            camera.end_life();
            camera.begin_death(death_eye,
                               DeathKillerInfo{9U, {90.0, 100.0, 200.0}, 0U, 4U}, false);
            expect(camera.mode() == DeathCameraMode::grave,
                   "enable_deathcam off activates CHASE directly");
        }

        // A KillAction that trails the SetHp death still opens the killer view.
        {
            DeathCameraController camera;
            camera.begin_death(death_eye, std::nullopt, true);
            expect(camera.mode() == DeathCameraMode::grave, "no killer: chase");
            camera.tick(0.1);
            camera.set_killer_info(DeathKillerInfo{9U, {90.0, 100.0, 200.0}, 0U, 2U});
            expect(camera.mode() == DeathCameraMode::killer_view &&
                       camera.killer_player_id() == 9U,
                   "late kill info must still activate the DeathController");
        }

        // The dead VIP (never_respawn) may cycle teammates; the followed
        // player's removal falls back onto our own body.
        {
            DeathCameraController camera;
            camera.begin_death(death_eye, std::nullopt, true, true);
            expect(camera.can_cycle_targets(),
                   "never_respawn unlocks LMB/RMB teammate cycling");
            camera.set_chase_target(DeathCameraTarget{4U, {50.0, 60.0, 200.0}, {-1.0, 0.0, 0.0}});
            expect(camera.mode() == DeathCameraMode::chase && camera.chase_player_id() == 4U,
                   "a chosen teammate is followed");
            expect(std::abs(distance(camera_position(camera.pose().eye), {50.0, 60.0, 200.0}) - 5.0) < 1.0e-6,
                   "the teammate chase uses the same five-block orbit");
            camera.set_chase_target(std::nullopt);
            expect(camera.mode() == DeathCameraMode::grave,
                   "a removed teammate falls back to our own body");
            DeathCameraController late;
            late.begin_death(death_eye, std::nullopt, true);
            expect(!late.can_cycle_targets(), "locked before the KillAction");
            late.set_never_respawn(true);
            expect(late.can_cycle_targets(), "a trailing NEVER_RESPAWN_TIME unlocks it");
        }

        // ChaseController.validate_position: a wall behind the focus pulls
        // the camera in to the first solid cell.
        {
            auto map = empty_world();
            // A wall at x = 103 behind a focus at x = 100.5 looking along -x.
            for (std::uint32_t y{90U}; y <= 110U; ++y) {
                for (std::uint32_t z{190U}; z <= 210U; ++z) {
                    expect(map->set_voxel(103U, y, z, VxlColor{90U, 90U, 90U, 255U}),
                           "wall voxel must be accepted");
                }
            }
            const Vec3 focus{100.5, 100.5, 200.5};
            const auto open = chase_camera_eye(nullptr, focus, 0.0, 0.0);
            expect(std::abs(open.x - 105.5) < 1.0e-9, "without terrain the eye is 5 behind");
            const auto walled = chase_camera_eye(map.get(), focus, 0.0, 0.0);
            // The wall face at x = 103 is 2.5 away; the eye keeps 0.35 clear.
            expect(std::abs(walled.x - (103.0 - 0.35)) < 1.0e-5,
                   "the chase eye must stop just in front of the wall face");
            // Sweeping the orbit across wall cells moves the eye continuously.
            double previous = chase_camera_eye(map.get(), focus, 0.0, 0.0).x;
            double largest_step = 0.0;
            for (int tenth{1}; tenth <= 300; ++tenth) {
                const auto eye = chase_camera_eye(map.get(), focus, tenth * 0.1, 0.0);
                largest_step = std::max(largest_step, std::abs(eye.x - previous));
                previous = eye.x;
            }
            expect(largest_step < 0.05, "orbiting along a wall must not zoom in block steps");
            const auto edge = chase_camera_eye(nullptr, {511.0, 100.0, 200.0}, 180.0, 0.0);
            expect(edge.x <= 512.0 + 1.0e-9, "the eye is scaled back inside the map box");

            DeathCameraController camera;
            camera.begin_death(focus, std::nullopt, true);
            camera.on_mouse_move(-900.0, 0.0);
            camera.set_terrain(map.get());
            expect(camera.pose().eye.x < 103.0, "the controller uses the terrain pull-in");
        }

        // Spectator FlyController: WASD along the view at FLYCAMERA_TRAVEL_SPEED
        // (30, ramped by SPEED_NORMALIZE 10), jump/crouch at 0.7 of it, and
        // clamped to the map box.
        {
            DeathCameraController camera;
            camera.enter_spectator({256.0, 256.0, 100.0}, std::nullopt);
            expect(camera.mode() == DeathCameraMode::spectator_free && camera.spectating() &&
                       camera.can_cycle_targets(),
                   "a spectator without a target flies and may browse everyone");
            camera.set_fly_key(FlyCameraKey::forward, true);
            for (int tick{}; tick < 120; ++tick) camera.tick(1.0 / 60.0);
            const auto moved = camera.pose().eye;
            expect(moved.y < 256.5 - 40.0 && moved.y > 256.5 - 60.0 &&
                       std::abs(moved.x - 256.0) < 1.0e-6,
                   "forward flies along the view at about 30 blocks per second");
            camera.set_fly_key(FlyCameraKey::forward, false);
            camera.set_fly_key(FlyCameraKey::jump, true);
            for (int tick{}; tick < 600; ++tick) camera.tick(1.0 / 60.0);
            expect(camera.pose().eye.z >= 0.0 && camera.pose().eye.z < 1.0e-9,
                   "jump rises until the map top clamps it");
            camera.set_fly_key(FlyCameraKey::jump, false);
        }

        // The first fly tick: speed 3, target moves .05, draw moves .005.
        // Strafe carries retail's 0.7 factor and movement exits chase without a jump.
        {
            DeathCameraController forward;
            DeathCameraController strafe;
            forward.enter_spectator({100.0, 100.0, 100.0}, std::nullopt);
            strafe.enter_spectator({100.0, 100.0, 100.0}, std::nullopt);
            forward.set_fly_key(FlyCameraKey::forward, true);
            strafe.set_fly_key(FlyCameraKey::right, true);
            forward.tick(1.0 / 60.0);
            strafe.tick(1.0 / 60.0);
            expect(std::abs(forward.pose().eye.y - 100.495) < 1.0e-9 &&
                       std::abs(strafe.pose().eye.x - 100.0035) < 1.0e-9,
                   "fly translation smooths by 1/10 and strafe travels at 0.7 speed");
            const DeathCameraTarget target{4U, {150.0, 150.0, 200.0}, {-1.0, 0.0, 0.0}};
            forward.set_chase_target(target);
            const auto before = forward.pose();
            forward.set_fly_key(FlyCameraKey::jump, true);
            expect(forward.mode() == DeathCameraMode::spectator_free &&
                       !forward.chase_player_id().has_value() && !forward.wants_chase_target() &&
                       distance(before.eye, forward.pose().eye) < 1.0e-9,
                   "spectator movement selects persistent free flight without shifting the eye");
            forward.set_fly_key(FlyCameraKey::jump, false);
            forward.tick(1.0 / 60.0);
            expect(!forward.wants_chase_target(), "releasing movement does not force chase");
            forward.set_chase_target(target);
            expect(forward.mode() == DeathCameraMode::chase && forward.wants_chase_target(),
                   "mouse target selection returns from free flight to chase");
            forward.end_life();
            forward.begin_death(death_eye, std::nullopt, true, true);
            forward.set_chase_target(target);
            forward.set_fly_key(FlyCameraKey::forward, true);
            expect(forward.mode() == DeathCameraMode::chase,
                   "a dead player with a character cannot leave chase with movement keys");
        }

        // The initial local CreatePlayer precedes both world construction and
        // live roster reveal. Its team, rather than the new session's default
        // health, must select the spectator camera without a fake death.
        namespace net = battlespades::network;
        using battlespades::frontend::local_player_is_spectator;
        using battlespades::frontend::spectator_client_data;
        DeathCameraController camera;
        net::Protocol168Roster roster;
        net::CreatePlayerPacket spectator;
        spectator.player_id = 1U;
        spectator.team = 0U;
        spectator.position = {1.0F, 2.0F, 3.0F};
        spectator.name = "Spectator";
        expect(roster.apply(net::encode_packet(spectator)),
               "the initial spectator CreatePlayer must populate the roster");
        const auto* local = roster.player(1U);
        expect(local != nullptr && !local->dead &&
                   local_player_is_spectator(local->team, true),
               "spectator CreatePlayer dead=false must not imply a playable life");
        expect(!local_player_is_spectator(2U, true) &&
                   !local_player_is_spectator(3U, true) &&
                   !local_player_is_spectator(0U, false),
               "ordinary team lives and disabled spectator must retain their lifecycle");
        camera.enter_spectator(local->position, std::nullopt);
        expect(camera.mode() == DeathCameraMode::spectator_free,
               "spectators without a target need a safe free-camera fallback");
        for (std::int32_t loop = 0; loop < 3; ++loop) {
            // Reveal can take multiple bounded batches. Camera activation
            // must not suppress the neutral packets needed to finish it.
            const auto decoded = net::decode_weapon_packet(net::encode_packet(
                spectator_client_data(loop, spectator.player_id, {1.0F, 0.0F, 0.0F})));
            expect(decoded.packet.has_value(), "spectator readiness must be wire-valid");
            const auto* ready = std::get_if<net::ClientDataPacket>(&*decoded.packet);
            expect(ready != nullptr && ready->loop_count == loop &&
                       ready->player_id == spectator.player_id &&
                       ready->movement_flags == 0U && ready->action_flags == 0U &&
                       !ready->palette_enabled && ready->weapon_deployment_yaw == 0.0F,
                   "spectator readiness must never leak movement, firing, or equipment input");
        }
        net::CreatePlayerPacket revealed;
        revealed.player_id = 9U;
        revealed.team = 2U;
        revealed.position = {20.0F, 30.0F, 40.0F};
        revealed.orientation = {-1.0F, 0.0F, 0.0F};
        revealed.name = "Revealed player";
        expect(roster.apply(net::encode_packet(revealed)),
               "post-readiness roster reveal must be accepted");
        const auto* target = roster.player(revealed.player_id);
        expect(target != nullptr && !target->dead,
               "post-readiness roster must expose the living target");
        camera.set_chase_target(DeathCameraTarget{
            target->player_id, target->position, target->orientation});
        expect(camera.mode() == DeathCameraMode::chase &&
                   camera.chase_player_id() == revealed.player_id &&
                   !camera.grave_entity_id().has_value(),
               "initial spectator must chase the revealed player without a grave phase");
        camera.end_life();
        expect(!camera.active(), "an ordinary alive CreatePlayer must restore first-person view");

        // Player.running_local_player_kills: consecutive kills of us by one
        // player, reset when we kill them, kept across their respawn.
        {
            net::Protocol168Roster kills;
            net::CreatePlayerPacket me;
            me.player_id = 1U;
            me.team = 2U;
            me.name = "Me";
            net::CreatePlayerPacket enemy;
            enemy.player_id = 7U;
            enemy.team = 3U;
            enemy.name = "Enemy";
            expect(kills.apply(net::encode_packet(me)) && kills.apply(net::encode_packet(enemy)),
                   "roster fixture");
            static_cast<void>(kills.apply_kill_relationships(1U, 7U, 1U, false, false, false));
            static_cast<void>(kills.apply_kill_relationships(1U, 7U, 1U, false, false, false));
            expect(kills.player(7U)->running_local_player_kills == 2U,
                   "two kills of us in a row count 2");
            expect(kills.apply(net::encode_packet(enemy)) &&
                       kills.player(7U)->running_local_player_kills == 2U,
                   "the streak lives on the persistent player, not the life");
            static_cast<void>(kills.apply_kill_relationships(7U, 1U, 1U, false, false, false));
            expect(kills.player(7U)->running_local_player_kills == 0U,
                   "killing them resets their streak");
        }

        std::cout << "death camera lifecycle tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
