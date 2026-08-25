#include "battlespades/frontend/death_camera.hpp"
#include "battlespades/render/camera_basis.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

using battlespades::frontend::DeathCameraController;
using battlespades::frontend::DeathCameraMode;
using battlespades::frontend::DeathCameraTarget;
using battlespades::world::Vec3;

void expect(bool value, const char* message) {
    if (!value) throw std::runtime_error{message};
}

} // namespace

int main() {
    try {
        DeathCameraController camera;
        const DeathCameraTarget killer{
            9U, {20.0, 30.0, 40.0}, {-1.0, 0.0, 0.0}};
        camera.begin_death({10.0, 11.0, 12.0}, 25.0, killer, true);
        expect(camera.active() && camera.mode() == DeathCameraMode::grave &&
                   !camera.chase_available(),
               "death must begin on the grave camera");
        const auto initial_pose = camera.pose();
        const Vec3 killer_side{killer.position.x - 10.0,
                              killer.position.y - 11.0, 0.0};
        const Vec3 camera_side{initial_pose.eye.x - 10.0,
                              initial_pose.eye.y - 11.0, 0.0};
        expect(killer_side.x * camera_side.x +
                       killer_side.y * camera_side.y >
                   0.0,
               "the initial grave view must begin on the killer's side");
        const auto initial_basis = battlespades::render::world_camera_basis(
            initial_pose.yaw_degrees, initial_pose.pitch_degrees);
        const Vec3 to_grave{10.0 - initial_pose.eye.x,
                            11.0 - initial_pose.eye.y,
                            12.0 - 0.9 - initial_pose.eye.z};
        const auto grave_distance = std::sqrt(
            to_grave.x * to_grave.x + to_grave.y * to_grave.y +
            to_grave.z * to_grave.z);
        expect(std::abs(grave_distance - 5.0) < 1.0e-6 &&
                   (initial_basis.forward[0U] * to_grave.x +
                    initial_basis.forward[1U] * to_grave.y +
                    initial_basis.forward[2U] * to_grave.z) /
                           grave_distance >
                       0.999,
               "the opening death camera must sit five blocks away and aim at the tombstone");

        camera.bind_grave(44U, {12.0, 13.0, 14.0});
        const auto grave_pose = camera.pose();
        expect(camera.grave_entity_id() == 44U &&
                   std::hypot(grave_pose.eye.x - 12.0,
                              grave_pose.eye.y - 13.0) > 3.0,
               "the authoritative grave must replace the fallback anchor");

        camera.tick(1.49);
        camera.on_mouse_press();
        expect(camera.mode() == DeathCameraMode::grave,
               "chase must remain locked for retail's first 1.5 seconds");
        camera.tick(0.02);
        expect(camera.chase_available(),
               "chase must unlock at 1.5 seconds");
        camera.on_mouse_move(4.0, 3.0);
        expect(camera.mode() == DeathCameraMode::grave,
               "fewer than eight mouse counts must only orbit the grave");
        camera.on_mouse_move(1.0, 0.0);
        expect(camera.mode() == DeathCameraMode::chase &&
                   camera.chase_player_id() == 9U,
               "eight accumulated mouse counts must select the killer");

        camera.set_chase_target(std::nullopt);
        expect(camera.mode() == DeathCameraMode::grave,
               "a dead/disconnected chase target must fall back to the grave");
        camera.set_chase_target(killer);
        camera.tick(5.0);
        expect(camera.mode() == DeathCameraMode::chase,
               "a legal target must be forced after five seconds");

        camera.end_life();
        expect(!camera.active() &&
                   camera.mode() == DeathCameraMode::inactive,
               "CreatePlayer must end the previous death generation");

        camera.begin_death({10.0, 11.0, 12.0}, 25.0, killer, false);
        expect(camera.mode() == DeathCameraMode::grave &&
                   !camera.chase_available(),
               "disabled free deathcam must still show the tombstone first");
        camera.tick(1.49);
        expect(camera.mode() == DeathCameraMode::grave,
               "automatic chase must respect the complete tombstone dwell");
        camera.tick(0.02);
        expect(camera.mode() == DeathCameraMode::chase,
               "disabled free deathcam must advance after the tombstone dwell");
        camera.end_life();

        camera.begin_death({10.0, 11.0, 12.0}, 5.0, std::nullopt, true);
        const auto world_death_pose = camera.pose();
        expect(std::abs(world_death_pose.eye.x - 10.0) < 1.0e-6 &&
                   std::abs(world_death_pose.eye.y - 11.0) > 4.0,
               "a world/self death must frame the broad face of grave.kv6");
        camera.end_life();

        camera.enter_spectator({1.0, 2.0, 3.0}, 0.0, std::nullopt);
        expect(camera.mode() == DeathCameraMode::spectator_free,
               "spectators without a target need a safe free-camera fallback");
        camera.set_chase_target(killer);
        expect(camera.mode() == DeathCameraMode::chase,
               "a newly available living player must become the spectator target");

        std::cout << "death camera lifecycle tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
