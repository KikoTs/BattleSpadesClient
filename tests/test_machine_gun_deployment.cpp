#include "battlespades/world/machine_gun_deployment.hpp"
#include "battlespades/world/tutorial_session.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using namespace battlespades::world;

constexpr double frame{1.0 / 60.0};

void expect(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error{message};
}

[[nodiscard]] std::shared_ptr<VxlMap> platform_world() {
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
    for (std::uint32_t y{60U}; y <= 90U; ++y) {
        for (std::uint32_t x{110U}; x <= 150U; ++x) {
            expect(loaded.map->set_voxel(x, y, 233U, VxlColor{100U, 120U, 90U, 255U}),
                   "platform voxel must be accepted");
        }
    }
    for (std::uint32_t z{234U}; z < VxlMap::height - 1U; ++z) {
        expect(loaded.map->set_voxel(110U, 60U, z, VxlColor{100U, 120U, 90U, 255U}),
               "platform support must be accepted");
    }
    return std::make_shared<VxlMap>(std::move(*loaded.map));
}

[[nodiscard]] MachineGunDeployInput standing(bool held) {
    MachineGunDeployInput input;
    input.trigger_held = held;
    input.space_available = true;
    input.yaw_degrees = 30.0;
    input.position = {130.5, 70.5, 230.75};
    return input;
}

/** Holds the trigger until an event fires; returns the seconds it took. */
[[nodiscard]] double hold_until_event(MachineGunDeployment& gun, MachineGunDeployInput input,
                                      MachineGunDeployEvent expected) {
    double elapsed{};
    for (int step{}; step < 600; ++step) {
        elapsed += frame;
        const auto event = gun.tick(input, frame);
        if (event != MachineGunDeployEvent::none) {
            expect(event == expected, "the hold ended with the wrong event");
            return elapsed;
        }
    }
    throw std::runtime_error{"the hold never completed"};
}

void the_hold_takes_the_retail_times() {
    MachineGunDeployment gun;
    expect(!gun.deployed() && !gun.deploying() && gun.progress() == 1.0,
           "a carried gun is folded and idle");

    const auto deploy =
        hold_until_event(gun, standing(true), MachineGunDeployEvent::deployed);
    expect(std::abs(deploy - machine_gun_deployment_seconds) <= 2.0 * frame,
           "deploying takes MG_DEPLOYMENT_TIME (3.0 s)");
    expect(gun.deployed() && !gun.deploying() && !gun.timer_running(),
           "completion leaves the gun deployed and the timers idle");
    expect(gun.deployment_yaw() == 30.0, "the gun keeps the yaw it was unfolded at");

    // Completion cleared weapon_custom / shoot_secondary: holding on does nothing.
    for (int step{}; step < 120; ++step) {
        expect(gun.tick(standing(true), frame) == MachineGunDeployEvent::none &&
                   !gun.timer_running() && gun.deployed(),
               "the same hold must not start the withdrawal");
    }
    static_cast<void>(gun.tick(standing(false), frame));

    const auto withdraw =
        hold_until_event(gun, standing(true), MachineGunDeployEvent::withdrawn);
    expect(std::abs(withdraw - machine_gun_withdrawal_seconds) <= 2.0 * frame,
           "withdrawing takes MG_WITHDRAWAL_TIME (0.75 s)");
    expect(!gun.deployed() && !gun.locks_movement(), "the gun is folded again");
}

void letting_go_restarts_the_timer() {
    MachineGunDeployment gun;
    for (int step{}; step < 150; ++step) {
        expect(gun.tick(standing(true), frame) == MachineGunDeployEvent::none,
               "2.5 s is not enough");
    }
    expect(gun.deploying() && gun.timer_running() && gun.locks_movement() &&
               gun.blocks_primary() && gun.blocks_swap(),
           "while unfolding the carrier cannot move, shoot or swap");
    expect(gun.progress() < 0.2 && gun.progress() > 0.0,
           "get_deployment_progress falls from 1 towards 0");

    static_cast<void>(gun.tick(standing(false), frame));
    expect(!gun.deploying() && !gun.timer_running() && !gun.locks_movement() &&
               gun.progress() == 1.0,
           "releasing early cancels the deployment and resets the timer");

    const auto again = hold_until_event(gun, standing(true), MachineGunDeployEvent::deployed);
    expect(std::abs(again - machine_gun_deployment_seconds) <= 2.0 * frame,
           "the next attempt needs the full time again");
}

void a_folded_gun_needs_room_and_a_standing_carrier() {
    MachineGunDeployment gun;
    auto input = standing(true);
    input.crouching = true;
    for (int step{}; step < 240; ++step) {
        expect(gun.tick(input, frame) == MachineGunDeployEvent::none && !gun.timer_running(),
               "a crouching carrier cannot deploy");
    }
    input.crouching = false;
    input.space_available = false;
    for (int step{}; step < 240; ++step) {
        expect(gun.tick(input, frame) == MachineGunDeployEvent::none && !gun.timer_running(),
               "without room ahead the gun cannot deploy");
    }

    static_cast<void>(gun.tick(standing(false), frame));
    static_cast<void>(hold_until_event(gun, standing(true), MachineGunDeployEvent::deployed));
    static_cast<void>(gun.tick(standing(false), frame));
    // A deployed gun only needs the trigger to fold.
    input = standing(true);
    input.crouching = true;
    input.space_available = false;
    static_cast<void>(hold_until_event(gun, input, MachineGunDeployEvent::withdrawn));
}

void the_deployed_view_is_limited() {
    MachineGunDeployment gun;
    expect(gun.constrained_yaw(170.0) == 170.0 && gun.constrained_pitch(80.0) == 80.0,
           "a carried gun does not limit the view");

    auto input = standing(true);
    static_cast<void>(gun.tick(input, frame));
    expect(gun.constrained_yaw(75.0) == 30.0,
           "while the timer counts the yaw stays at weapon_deployment_yaw");
    static_cast<void>(hold_until_event(gun, input, MachineGunDeployEvent::deployed));

    expect(gun.constrained_yaw(30.0) == 30.0 && gun.constrained_yaw(60.0) == 60.0,
           "inside the arc the yaw is free");
    expect(gun.constrained_yaw(90.0) == 75.0 && gun.constrained_yaw(-40.0) == -15.0,
           "the yaw stops 45 degrees either side of the deployment yaw");
    expect(gun.constrained_pitch(60.0) == 45.0 && gun.constrained_pitch(-60.0) == -45.0 &&
               gun.constrained_pitch(10.0) == 10.0,
           "the pitch stops at 45 degrees up and down");

    // The arc is measured the short way round the circle.
    MachineGunDeployment wrapped;
    auto west = standing(true);
    west.yaw_degrees = 170.0;
    static_cast<void>(hold_until_event(wrapped, west, MachineGunDeployEvent::deployed));
    expect(std::abs(wrapped.constrained_yaw(-170.0) - 190.0) < 1e-9,
           "20 degrees past the seam is inside the arc");
    expect(std::abs(wrapped.constrained_yaw(-100.0) - 215.0) < 1e-9,
           "90 degrees past the seam stops at +45");
}

void being_moved_folds_the_gun() {
    MachineGunDeployment gun;
    static_cast<void>(hold_until_event(gun, standing(true), MachineGunDeployEvent::deployed));
    auto input = standing(false);
    expect(gun.tick(input, frame) == MachineGunDeployEvent::none, "standing still keeps it up");

    input.position.z += 0.5; // the block below was shot away
    expect(gun.tick(input, frame) == MachineGunDeployEvent::dislodged && !gun.deployed(),
           "a displaced carrier loses the deployment at once");

    MachineGunDeployment networked;
    static_cast<void>(
        hold_until_event(networked, standing(true), MachineGunDeployEvent::deployed));
    input = standing(false);
    input.position_tolerance = 1.0 / 32.0;
    input.position.x += 1.0 / 64.0;
    expect(networked.tick(input, frame) == MachineGunDeployEvent::none,
           "one wire quantum of correction is not a displacement");
    input.position.x += 0.1;
    expect(networked.tick(input, frame) == MachineGunDeployEvent::dislodged,
           "a real push still folds the gun");

    MachineGunDeployment unset;
    static_cast<void>(hold_until_event(unset, standing(true), MachineGunDeployEvent::deployed));
    expect(unset.reset() && !unset.deployed() && !unset.reset(),
           "on_unset folds a deployed gun once");
}

void the_space_check_follows_the_retail_prism() {
    const auto map = platform_world();
    // Standing on the z=233 platform: feet at 230.75 + 2.25.
    const Vec3 standing_position{130.5, 70.5, 230.75};
    expect(machine_gun_deployment_space(*map, standing_position, 0.0),
           "open floor ahead has room (yaw 0 looks down -x)");
    expect(machine_gun_deployment_space(*map, standing_position, 90.0),
           "and to the side");

    auto walled = std::make_shared<VxlMap>(*map);
    expect(walled->set_voxel(128U, 70U, 232U, VxlColor{90U, 90U, 90U, 255U}),
           "wall voxel must be accepted");
    expect(!machine_gun_deployment_space(*walled, standing_position, 0.0),
           "a block within two cells ahead at leg height blocks the gun");
    expect(machine_gun_deployment_space(*walled, standing_position, 180.0),
           "the other direction is still free");

    auto holed = std::make_shared<VxlMap>(*map);
    expect(holed->clear_voxel(129U, 70U, 233U), "floor voxel must be removable");
    expect(!machine_gun_deployment_space(*holed, standing_position, 0.0),
           "a missing floor block under the prism refuses the deployment");

    expect(!machine_gun_deployment_space(*map, {111.5, 70.5, 230.75}, 0.0),
           "the platform edge has no floor two cells ahead");
}

void a_live_session_runs_the_whole_sequence() {
    TutorialSessionConfig live;
    live.network_authoritative = true;
    live.initial_position = {130.5, 70.5, 230.75};
    live.initial_orientation = {-1.0, 0.0, 0.0};
    live.initial_class_id = 0U;
    live.initial_loadout = {15U, 2U, 5U};
    live.initial_tool = static_cast<std::uint8_t>(15U);
    TutorialWorldSession session{platform_world(), live};
    expect(session.selected_tool_id() == 15U, "the fixture holds the mounted gun");
    for (int step{}; step < 60; ++step) session.tick();
    static_cast<void>(session.take_weapon_actions());
    const auto spot = session.player().position;

    session.set_secondary_held(true);
    session.tick();
    expect(session.take_weapon_actions().empty() && !session.machine_gun_deployed(),
           "the RMB press alone places nothing");
    expect(session.machine_gun_deploying() &&
               session.machine_gun_deployment_progress().has_value(),
           "holding RMB starts the deployment and its HUD message");

    session.set_action_held(TutorialAction::forward, true);
    session.set_action_held(TutorialAction::jump, true);
    session.set_primary_held(true);
    for (int step{}; step < 60; ++step) session.tick();
    expect(session.movement_flags() == 0U && (session.action_flags() & 0x01U) == 0U,
           "a deploying carrier sends no movement and no fire");
    expect(session.player().position.x == spot.x && session.player().position.y == spot.y,
           "and does not move");
    session.cycle_tool(1);
    expect(session.selected_tool_id() == 15U, "MGWeapon.can_swap refuses a tool change");

    for (int step{}; step < 150 && !session.machine_gun_deployed(); ++step) session.tick();
    expect(session.machine_gun_deployed() && !session.machine_gun_deploying() &&
               !session.machine_gun_deployment_progress().has_value(),
           "three seconds of RMB deploy the gun");
    expect((session.action_flags() & 0x40U) != 0U,
           "ClientData carries is_weapon_deployed");
    expect(std::abs(std::abs(session.weapon_deployment_yaw()) - 90.0) < 1.0,
           "weapon_deployment_yaw is sent in retail degrees (-x is atan2(-1, 0))");
    const auto deployed_actions = session.take_weapon_actions();
    const auto count = [](const std::vector<WeaponAction>& actions, WeaponActionKind kind) {
        return std::ranges::count_if(actions, [kind](const WeaponAction& action) {
            return action.kind == kind && action.tool_id == 15U;
        });
    };
    expect(count(deployed_actions, WeaponActionKind::deployable_place) == 1 &&
               count(deployed_actions, WeaponActionKind::objective_use) == 1,
           "the server is told once: PlaceMG, then UseCommand to man the gun");

    // Deployed: the view is limited and the old hold does not fold the gun.
    session.set_look_angles(session.yaw() + 120.0, 80.0);
    session.tick();
    expect(std::abs(session.pitch()) <= 45.0 + 1e-9, "the deployed pitch stops at 45 degrees");
    for (int step{}; step < 90; ++step) session.tick();
    expect(session.machine_gun_deployed(), "the spent hold does not start a withdrawal");

    session.set_secondary_held(false);
    session.tick();
    session.set_secondary_held(true);
    for (int step{}; step < 60 && session.machine_gun_deployed(); ++step) session.tick();
    expect(!session.machine_gun_deployed(), "0.75 s of RMB withdraw the gun");
    const auto withdrawn_actions = session.take_weapon_actions();
    expect(count(withdrawn_actions, WeaponActionKind::objective_use) == 1 &&
               count(withdrawn_actions, WeaponActionKind::deployable_place) == 0,
           "leaving the gun sends one UseCommand");
    expect((session.action_flags() & 0x40U) == 0U && session.weapon_deployment_yaw() == 0.0,
           "the deployed flag and yaw are cleared");

    session.set_secondary_held(false);
    session.set_primary_held(false);
    session.tick();
    expect(session.movement_flags() == 0x11U, "a folded gun lets the carrier move again");

    // Death folds a deployed gun (MGWeapon.on_unset).
    session.set_action_held(TutorialAction::forward, false);
    session.set_action_held(TutorialAction::jump, false);
    for (int step{}; step < 60; ++step) session.tick();
    session.set_secondary_held(true);
    for (int step{}; step < 200 && !session.machine_gun_deployed(); ++step) session.tick();
    expect(session.machine_gun_deployed(), "the second deployment completes");
    session.set_server_health(0.0);
    session.tick();
    expect(!session.machine_gun_deployed(), "a dead carrier's gun is folded");
}

} // namespace

int main() {
    try {
        the_hold_takes_the_retail_times();
        letting_go_restarts_the_timer();
        a_folded_gun_needs_room_and_a_standing_carrier();
        the_deployed_view_is_limited();
        being_moved_folds_the_gun();
        the_space_check_follows_the_retail_prism();
        a_live_session_runs_the_whole_sequence();
    } catch (const std::exception& error) {
        std::cerr << "machine gun deployment test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
