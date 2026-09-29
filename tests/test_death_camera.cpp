#include "battlespades/frontend/death_camera.hpp"
#include "battlespades/frontend/live_client_policy.hpp"
#include "battlespades/network/protocol168_players.hpp"
#include "battlespades/render/camera_basis.hpp"
#include "battlespades/world/vxl_map.hpp"

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

        // A first kill (streak 1): straight to the chase camera on our own
        // body, locked -- retail never lets a dead player browse the map.
        {
            DeathCameraController camera;
            camera.begin_death(death_eye, 30.0, 10.0,
                               DeathKillerInfo{9U, {120.0, 100.0, 200.0}, 0U, 1U}, true);
            expect(camera.mode() == DeathCameraMode::grave,
                   "a killer's first kill must not open the killer view");
            expect(!camera.can_cycle_targets(),
                   "an ordinary death locks the chase camera to our own body");
            const auto pose = camera.pose();
            expect(std::abs(distance(pose.eye, death_eye) - 5.0) < 1.0e-6,
                   "the chase camera sits five blocks from the body with no wall behind");
            const auto basis =
                battlespades::render::world_camera_basis(pose.yaw_degrees, pose.pitch_degrees);
            const Vec3 look{death_eye.x - pose.eye.x, death_eye.y - pose.eye.y,
                            death_eye.z - pose.eye.z};
            expect((basis.forward[0U] * look.x + basis.forward[1U] * look.y +
                    basis.forward[2U] * look.z) / 5.0 > 0.999,
                   "the chase camera aims at the retail focus, not 0.9 above it");
            expect(std::abs(pose.yaw_degrees - 30.0) < 1.0e-9 &&
                       std::abs(pose.pitch_degrees - 10.0) < 1.0e-9,
                   "the chase camera keeps the view the player died with");
        }

        // Second consecutive kill: the killer view faces the killer from the
        // death eye, and never moves.
        {
            DeathCameraController camera;
            const Vec3 killer{80.0, 100.0, 200.0};
            camera.begin_death(death_eye, 180.0, 0.0,
                               DeathKillerInfo{9U, killer, 1U, 2U}, true);
            expect(camera.mode() == DeathCameraMode::killer_view,
                   "streak 2 must open the killer view");
            expect(!camera.chase_available() && !camera.can_cycle_targets(),
                   "chase is not available before 1.5 s");
            for (int tick{}; tick < 60; ++tick) camera.tick(1.0 / 60.0);
            const auto pose = camera.pose();
            expect(distance(pose.eye, death_eye) < 1.0e-9,
                   "streak 2 only turns to face the killer");
            // The killer lies along -x, which is yaw 0 in this basis.
            expect(std::abs(pose.yaw_degrees) < 1.0,
                   "one second of angle lerp 10 must turn most of the way to the killer");
            camera.on_mouse_press();
            expect(camera.mode() == DeathCameraMode::killer_view,
                   "a click before 1.5 s must not leave the killer view");
            for (int tick{}; tick < 31; ++tick) camera.tick(1.0 / 60.0);
            expect(camera.chase_available(), "chase becomes available after 1.5 s");
            camera.on_mouse_move(60.0, 0.0);
            expect(camera.mode() == DeathCameraMode::killer_view,
                   "60 counts is below mouse_movement_to_chase_cam");
            camera.on_mouse_move(50.0, 0.0);
            expect(camera.mode() == DeathCameraMode::grave,
                   "more than 100 counts switches to the chase camera on our body");
        }

        // Third consecutive kill with the killer far away: fly toward them
        // after 0.25 s. set_killer_view keeps aiming 5 short while the killer
        // is more than 7 away, then freezes (zoom_possible = False).
        {
            DeathCameraController camera;
            const Vec3 killer{60.0, 100.0, 200.0};
            camera.begin_death(death_eye, 0.0, 0.0,
                               DeathKillerInfo{9U, killer, 0U, 3U}, true);
            for (int tick{}; tick < 14; ++tick) camera.tick(1.0 / 60.0);
            expect(distance(camera.pose().eye, death_eye) < 1.0e-9,
                   "the fly-in waits for DEATHCAM_TIME_TILL_POSITION_CHANGE");
            for (int tick{}; tick < 250; ++tick) camera.tick(1.0 / 60.0);
            expect(camera.mode() == DeathCameraMode::killer_view,
                   "4.4 s is still before the forced switch");
            const double left = distance(camera.pose().eye, killer);
            expect(left > 6.5 && left <= 7.0,
                   "the fly-in stops once the killer is within RANGE + 2");
            for (int tick{}; tick < 40; ++tick) camera.tick(1.0 / 60.0);
            expect(camera.mode() == DeathCameraMode::grave,
                   "the chase camera is forced at five seconds");
        }

        // An invalid kill type or disabled deathcam goes straight to chase.
        {
            DeathCameraController camera;
            camera.begin_death(death_eye, 0.0, 0.0,
                               DeathKillerInfo{9U, {90.0, 100.0, 200.0}, 7U, 4U}, true);
            expect(camera.mode() == DeathCameraMode::grave,
                   "kill types outside DEATHCAM_VALID_TYPES skip the killer view");
            camera.end_life();
            camera.begin_death(death_eye, 0.0, 0.0,
                               DeathKillerInfo{9U, {90.0, 100.0, 200.0}, 0U, 4U}, false);
            expect(camera.mode() == DeathCameraMode::grave,
                   "enable_deathcam off activates CHASE directly");
        }

        // A KillAction that trails the SetHp death still opens the killer view.
        {
            DeathCameraController camera;
            camera.begin_death(death_eye, 0.0, 0.0, std::nullopt, true);
            expect(camera.mode() == DeathCameraMode::grave, "no killer: chase");
            camera.tick(0.1);
            camera.set_killer_info(DeathKillerInfo{9U, {90.0, 100.0, 200.0}, 0U, 2U});
            expect(camera.mode() == DeathCameraMode::killer_view &&
                       camera.killer_player_id() == 9U,
                   "late kill info must still activate the DeathController");
        }

        // The dead VIP (never_respawn) may cycle teammates; the followed
        // player dying drops the camera back onto our own body.
        {
            DeathCameraController camera;
            camera.begin_death(death_eye, 0.0, 0.0, std::nullopt, true, true);
            expect(camera.can_cycle_targets(),
                   "never_respawn unlocks LMB/RMB teammate cycling");
            camera.set_chase_target(DeathCameraTarget{4U, {50.0, 60.0, 200.0}, {-1.0, 0.0, 0.0}});
            expect(camera.mode() == DeathCameraMode::chase && camera.chase_player_id() == 4U,
                   "a chosen teammate is followed");
            expect(std::abs(distance(camera.pose().eye, {50.0, 60.0, 200.0}) - 5.0) < 1.0e-6,
                   "the teammate chase uses the same five-block orbit");
            camera.set_chase_target(std::nullopt);
            expect(camera.mode() == DeathCameraMode::grave,
                   "a dead/disconnected teammate falls back to our own body");
            DeathCameraController late;
            late.begin_death(death_eye, 0.0, 0.0, std::nullopt, true);
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
            // Cell centre 103.5 is 3 away: 3 - sqrt(3)/2 - 0.5 = 1.634.
            expect(std::abs(walled.x - (100.5 + 3.0 - std::sqrt(3.0) * 0.5 - 0.5)) < 1.0e-6 &&
                       walled.x < 103.0,
                   "the chase eye must stop in front of the wall");
            const auto edge = chase_camera_eye(nullptr, {511.0, 100.0, 200.0}, 180.0, 0.0);
            expect(edge.x <= 512.0 + 1.0e-9, "the eye is scaled back inside the map box");

            DeathCameraController camera;
            camera.begin_death(focus, 0.0, 0.0, std::nullopt, true);
            camera.set_terrain(map.get());
            expect(camera.pose().eye.x < 103.0, "the controller uses the terrain pull-in");
        }

        // Spectator FlyController: WASD along the view at FLYCAMERA_TRAVEL_SPEED
        // (30, ramped by SPEED_NORMALIZE 10), jump/crouch at 0.7 of it, and
        // clamped to the map box.
        {
            DeathCameraController camera;
            camera.enter_spectator({256.0, 256.0, 100.0}, 0.0, std::nullopt);
            expect(camera.mode() == DeathCameraMode::spectator_free && camera.spectating() &&
                       camera.can_cycle_targets(),
                   "a spectator without a target flies and may browse everyone");
            camera.set_fly_key(FlyCameraKey::forward, true);
            for (int tick{}; tick < 120; ++tick) camera.tick(1.0 / 60.0);
            const auto moved = camera.pose().eye;
            expect(moved.x < 256.0 - 40.0 && moved.x > 256.0 - 60.0 &&
                       std::abs(moved.y - 256.0) < 1.0e-6,
                   "forward flies along the view at about 30 blocks per second");
            camera.set_fly_key(FlyCameraKey::forward, false);
            camera.set_fly_key(FlyCameraKey::jump, true);
            for (int tick{}; tick < 600; ++tick) camera.tick(1.0 / 60.0);
            expect(camera.pose().eye.z >= 0.0 && camera.pose().eye.z < 1.0e-9,
                   "jump rises until the map top clamps it");
            camera.set_fly_key(FlyCameraKey::jump, false);
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
        camera.enter_spectator(local->position, 0.0, std::nullopt);
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
