#include "battlespades/world/flight_profile.hpp"
#include "battlespades/world/player_movement.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

// P3-16: the prediction reads retail JETPACK_PROPERTIES (BS/shared/constants.py).
using battlespades::world::jetpack_properties;
static_assert(jetpack_properties(1U).start_delay == 0.25 &&
              jetpack_properties(1U).activation_cost == 10.0 &&
              jetpack_properties(1U).flying_consumption == 75.0 &&
              jetpack_properties(1U).refill_delay_due_damage == 2.0);
static_assert(jetpack_properties(2U).refill_rate == 9.0 &&
              jetpack_properties(2U).flying_consumption == 17.0 &&
              jetpack_properties(2U).refill_delay_due_damage == 2.0);
static_assert(jetpack_properties(3U).refill_rate == 3.0 &&
              jetpack_properties(3U).refill_delay_due_damage == 0.5);
static_assert(jetpack_properties(4U).start_delay == 0.1 &&
              jetpack_properties(4U).activation_cost == 0.0 &&
              jetpack_properties(4U).refill_delay_due_damage == 0.1);
static_assert(jetpack_properties(9U).max_fuel == 0.0);
// P0-07: the retail-calibrated boundary frames the server pins for BSCF owners.
static_assert(battlespades::world::jetpack_activation_defer_frames == 2U &&
              battlespades::world::jetpack_exhaustion_tail_frames == 3U);

using battlespades::world::apply_crouch_request;
using battlespades::world::constrain_player_to_bounds;
using battlespades::world::grounded;
using battlespades::world::MovementClassConfig;
using battlespades::world::movement_config_for_class;
using battlespades::world::MovementStepResult;
using battlespades::world::player_contact_offset;
using battlespades::world::PlayerInputState;
using battlespades::world::PlayerCollisionBody;
using battlespades::world::PlayerMovementState;
using battlespades::world::PlayerMovementBounds;
using battlespades::world::step_player;
using battlespades::world::VxlColor;
using battlespades::world::VxlMap;

constexpr double fixed_dt{1.0 / 60.0};
constexpr VxlColor stone{120U, 96U, 80U, 255U};

void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error{message};
    }
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
    expect(static_cast<bool>(loaded), "synthetic empty world must parse");
    return std::move(*loaded.map);
}

[[nodiscard]] VxlMap platform_world() {
    auto map = empty_world();
    for (std::uint32_t y{96U}; y <= 110U; ++y) {
        for (std::uint32_t x{96U}; x <= 110U; ++x) {
            expect(map.set_voxel(x, y, 200U, stone), "platform voxel must be accepted");
        }
    }
    return map;
}

[[nodiscard]] double feet_z(const PlayerMovementState& state) {
    return state.position.z + player_contact_offset(state.crouch, state.wade);
}

/** Steps until the player is grounded and vertically settled. */
void settle(PlayerMovementState& state, const VxlMap& map, int maximum_steps) {
    const PlayerInputState idle{};
    for (int step{}; step < maximum_steps; ++step) {
        static_cast<void>(step_player(state, idle, &map, fixed_dt));
        if (!state.airborne && state.velocity.z == 0.0 && grounded(&map, state)) {
            return;
        }
    }
    throw std::runtime_error{"player failed to settle on terrain"};
}

} // namespace

int main() {
    try {
        const auto scout = movement_config_for_class(1U);
        const auto zombie = movement_config_for_class(14U);
        const auto sped_up = movement_config_for_class(1U, 1.25);
        expect(std::fabs(scout.sprint_multiplier - 1.45) < 1e-9 &&
                   std::fabs(zombie.jump_multiplier - 2.5) < 1e-9 &&
                   std::fabs(sped_up.sprint_multiplier - 1.8125) < 1e-9,
               "movement must use the selected class and server speed rule");
        expect(std::fabs(sped_up.accel_multiplier - 0.875) < 1e-9 &&
                   std::fabs(sped_up.crouch_sneak_multiplier - 0.625) < 1e-9 &&
                   std::fabs(sped_up.jump_multiplier - scout.jump_multiplier) < 1e-9 &&
                   std::fabs(sped_up.water_friction - scout.water_friction) < 1e-9,
               "InitialInfo speed scale must affect accel/sprint/crouch but not jump/water");
        const auto classic = movement_config_for_class(5U);
        const auto fast_zombie = movement_config_for_class(14U);
        const auto jump_zombie = movement_config_for_class(15U);
        const auto specialist = movement_config_for_class(16U);
        const auto medic = movement_config_for_class(17U);
        expect(!classic.can_sprint_uphill &&
                   std::fabs(fast_zombie.accel_multiplier - 1.1) < 1e-9 &&
                   std::fabs(fast_zombie.crouch_sneak_multiplier - 0.25) < 1e-9 &&
                   std::fabs(fast_zombie.water_friction - 12.0) < 1e-9 &&
                   std::fabs(jump_zombie.water_friction - 8.0) < 1e-9 &&
                   !jump_zombie.can_sprint_uphill &&
                   std::fabs(specialist.accel_multiplier - 0.85) < 1e-9 &&
                   std::fabs(medic.accel_multiplier - 0.6) < 1e-9,
               "all recovered class profiles must match shared/constants.py");
        // Gravity drop onto the platform: the frame-start freeze plus the
        // measured probe epsilon settle the feet within 0.00875 of the top
        // face, grounded and dry.
        {
            auto map = platform_world();
            PlayerMovementState state;
            state.position = {102.5, 102.5, 190.0};
            state.airborne = true;
            settle(state, map, 600);
            expect(std::fabs(feet_z(state) - 200.0) < 0.01,
                   "feet must settle on the platform top face");
            expect(!state.wade, "a platform landing must be dry");
            expect(state.velocity.z == 0.0, "settled player must have zero fall velocity");
        }

        // Open water: the z=239 clip row samples the empty z=238 bed row, so
        // the player sinks into the water row and lands wading on the bed.
        {
            auto map = empty_world();
            PlayerMovementState state;
            state.position = {50.5, 50.5, 230.0};
            state.airborne = true;
            settle(state, map, 900);
            expect(state.wade, "an open-water landing must set wade");
            expect(feet_z(state) > 239.0 && feet_z(state) <= 240.0,
                   "wading feet must rest inside the water row");
        }

        // Jump: retail assigns the impulse and applies the same-frame gravity
        // step, then flight returns to ground within a bounded arc.
        {
            auto map = platform_world();
            PlayerMovementState state;
            state.position = {102.5, 102.5, 190.0};
            state.airborne = true;
            settle(state, map, 600);
            const double start_feet = feet_z(state);

            PlayerInputState jump;
            jump.jump = true;
            const auto launched = step_player(state, jump, &map, fixed_dt);
            expect(launched.jumped, "grounded jump input must launch");
            expect(state.airborne, "a jump must leave the ground");
            const MovementClassConfig soldier;
            // The box mover stores velocity through retail's float32 pipe, so
            // the stored value is the float rounding of the double math.
            const double expected_vz = static_cast<double>(static_cast<float>(
                static_cast<float>(static_cast<float>(-0.36F * static_cast<float>(soldier.jump_multiplier)) + static_cast<float>(fixed_dt)) / static_cast<double>(static_cast<float>(1.0 + static_cast<float>(fixed_dt)))));
            expect(std::fabs(state.velocity.z - expected_vz) < 1e-12,
                   "jump frame velocity must match the retail impulse+gravity order");

            const PlayerInputState hold_jump{};
            double apex_feet = start_feet;
            bool landed = false;
            MovementStepResult landing_result;
            for (int step{}; step < 240 && !landed; ++step) {
                const auto moved = step_player(state, hold_jump, &map, fixed_dt);
                apex_feet = std::min(apex_feet, feet_z(state));
                landed = moved.landed;
                if (landed) {
                    landing_result = moved;
                }
            }
            expect(landed, "a jump arc must land again");
            expect(landing_result.hard_landing && landing_result.landing_damage < 0,
                   "a damage-free jump landing must retain the hard-impact semantic");
            expect(start_feet - apex_feet > 1.0,
                   "soldier jump apex must clear at least one block");
            // The landed frame freezes z at the frame-start value; the
            // settle epsilon walks the remaining fraction down.
            settle(state, map, 600);
            expect(std::fabs(feet_z(state) - start_feet) < 0.02,
                   "the landing must return to the platform surface");
        }

        // Ability forces are native movement state, not presentation-only
        // WorldUpdate flags. Each compact pack enum has a distinct thrust;
        // ordinary gravity is applied after it in the same frame.
        {
            auto map = empty_world();
            constexpr std::array<double, 4U> thrusts{0.045, 0.0125, 0.020,
                                                     0.025};
            for (std::uint8_t pack{1U}; pack <= 4U; ++pack) {
                PlayerMovementState state;
                state.position = {100.5, 100.5, 100.0};
                state.airborne = true;
                state.jetpack = pack;
                state.jetpack_active = true;
                PlayerInputState input;
                input.jump = true;
                static_cast<void>(step_player(state, input, &map, fixed_dt));
                const double expected = static_cast<double>(static_cast<float>(
                    (-thrusts[pack - 1U] + fixed_dt) / (1.0 + fixed_dt)));
                expect(std::fabs(state.velocity.z - expected) < 1e-7,
                       "each jetpack must use its recovered vertical thrust");
            }

            PlayerMovementState stale_active;
            stale_active.position = {100.5, 100.5, 100.0};
            stale_active.jetpack_active = true;
            PlayerInputState jump;
            jump.jump = true;
            static_cast<void>(
                step_player(stale_active, jump, &map, fixed_dt));
            expect(stale_active.airborne && stale_active.velocity.z > 0.0,
                   "an invalid active pack must follow the original switch default without ordinary jump thrust");
        }

        // Hover is the UGC Builder mode at world.pyd +124. It skips gravity;
        // crouch selects the measured descent force. Wading does not.
        {
            auto map = empty_world();
            PlayerMovementState hover;
            hover.position = {100.5, 100.5, 100.0};
            hover.airborne = true;
            hover.jetpack = 4U;
            hover.velocity.z = 0.25;
            PlayerInputState input;
            input.hover = true;
            static_cast<void>(step_player(hover, input, &map, fixed_dt));
            expect(hover.velocity.z == 0.0,
                   "uncrouched UGC hover must pin vertical velocity");

            hover.velocity = {};
            hover.crouch = true;
            static_cast<void>(step_player(hover, input, &map, fixed_dt));
            const double descent = static_cast<double>(
                static_cast<float>(0.025 / (1.0 + fixed_dt)));
            expect(std::fabs(hover.velocity.z - descent) < 1e-7,
                   "crouched UGC hover must descend at the recovered rate");

            PlayerMovementState water;
            water.position = {100.5, 100.5, 100.0};
            water.airborne = true;
            water.wade = true;
            water.crouch = true;
            static_cast<void>(step_player(water, {}, &map, fixed_dt));
            const double crouch_gravity = static_cast<double>(
                static_cast<float>(fixed_dt / (1.0 + fixed_dt)));
            expect(std::fabs(water.velocity.z - crouch_gravity) < 1e-7,
                   "wading crouch must retain ordinary gravity without invented buoyancy");
        }

        // The shared Z/action bit must not enable UGC zero-gravity physics
        // for ordinary players, flight packs 66/67/68 or the parachute.
        // Compare complete trajectories, including crouched steering, so
        // filtering gravity alone cannot leave another hover branch active.
        for (std::uint8_t pack{}; pack <= 3U; ++pack) {
            for (const bool parachute : {false, true}) {
                if (parachute && pack != 0U) continue;
                for (const bool crouch : {false, true}) {
                    PlayerMovementState normal;
                    normal.position = {100.5, 100.5, 100.0};
                    normal.orientation = {1.0, 0.0, 0.0};
                    normal.airborne = true;
                    normal.crouch = crouch;
                    normal.jetpack = pack;
                    normal.jetpack_active = pack != 0U;
                    normal.parachute = parachute;
                    normal.parachute_active = parachute;
                    auto with_z = normal;
                    PlayerInputState input;
                    input.forward = true;
                    input.jump = pack != 0U;
                    input.crouch = crouch;
                    auto z_input = input;
                    z_input.hover = true;
                    for (int frame{}; frame < 30; ++frame) {
                        static_cast<void>(step_player(normal, input, nullptr, fixed_dt));
                        static_cast<void>(step_player(with_z, z_input, nullptr, fixed_dt));
                        expect(std::fabs(normal.position.x - with_z.position.x) < 1e-12 &&
                                   std::fabs(normal.position.z - with_z.position.z) < 1e-12 &&
                                   std::fabs(normal.velocity.x - with_z.velocity.x) < 1e-12 &&
                                   std::fabs(normal.velocity.z - with_z.velocity.z) < 1e-12 &&
                                   normal.airborne == with_z.airborne,
                               "Z must not change native movement outside UGC Builder hover");
                    }
                    expect(z_input.hover, "physics filtering must preserve the raw action input");
                }
            }
        }

        // A deployed parachute owns the 5% gravity branch and clears fall
        // accumulation; it is unrelated to the burdened/sprint fields.
        {
            auto map = empty_world();
            PlayerMovementState state;
            state.position = {100.5, 100.5, 100.0};
            state.airborne = true;
            state.parachute = true;
            state.parachute_active = true;
            state.fall_distance = 8.0;
            static_cast<void>(step_player(state, {}, &map, fixed_dt));
            const double expected = static_cast<double>(
                static_cast<float>((fixed_dt * 0.05) / (1.0 + fixed_dt)));
            expect(std::fabs(state.velocity.z - expected) < 1e-7 &&
                       state.fall_distance < 0.01,
                   "parachute must apply 5% gravity and reset prior fall distance");
        }

        // Engineer flight speed (2026-10-01): stock world.pyd scales an active
        // Engineer pack's air acceleration by 0.1 (2.8 blocks/s with the
        // InitialInfo 1.25 class scale); the negotiated BSFP v2 profile uses
        // 0.25, the Engineer's own walking speed (7 blocks/s). Server:
        // tests/test_flight_balance.py::test_engineer_flies_at_ground_walking_speed...
        {
            const auto cruise = [](const battlespades::world::MovementClassConfig& config) {
                PlayerMovementState state;
                state.position = {100.5, 100.5, 100.0};
                state.airborne = true;
                state.jetpack = 3U;
                state.jetpack_active = true;
                state.orientation = {1.0, 0.0, 0.0};
                PlayerInputState input;
                input.forward = true;
                for (int frame{}; frame < 600; ++frame) {
                    state.position = {100.5, 100.5, 100.0};
                    state.velocity.z = 0.0;
                    static_cast<void>(step_player(state, input, nullptr, fixed_dt, config));
                }
                return state.velocity.x * 32.0;  // blocks per second
            };
            auto engineer = battlespades::world::movement_config_for_class(12U, 1.25);
            const double retail = cruise(engineer);
            expect(std::fabs(retail - 0.7 * 1.25 * 0.1 * 32.0) < 0.01,
                   "stock Engineer flight must stay at 0.1 x class acceleration");
            battlespades::world::apply_flight_profile(
                engineer, battlespades::world::balanced_flight_profile());
            const double tuned = cruise(engineer);
            expect(std::fabs(tuned - 0.7 * 1.25 * 0.25 * 32.0) < 0.01 &&
                       std::fabs(tuned - 0.7 * 1.25 / 4.0 * 32.0) < 0.01,
                   "v2 Engineer must fly at its ground walking speed");
            // UGC Builder hover flight keeps the stock 0.1 in either profile.
            PlayerMovementState ugc;
            ugc.airborne = true;
            ugc.jetpack = 4U;
            ugc.jetpack_active = true;
            ugc.orientation = {1.0, 0.0, 0.0};
            PlayerInputState forward;
            forward.forward = true;
            auto ugc_stock = ugc;
            static_cast<void>(step_player(ugc, forward, nullptr, fixed_dt, engineer));
            static_cast<void>(step_player(ugc_stock, forward, nullptr, fixed_dt,
                                          battlespades::world::movement_config_for_class(12U, 1.25)));
            expect(ugc.velocity.x == ugc_stock.velocity.x,
                   "the Engineer tuning must not touch the UGC Builder pack");
        }

        // Parachute descent (2026-10-01). Stock canopy: from a slow deploy the
        // fall creeps up to 1.6 blocks/s. v2: a slow body free-falls to the
        // 5 blocks/s terminal within ~10 frames; a fast body brakes with the
        // stock canopy recurrence in both profiles.
        {
            const auto descend = [](const battlespades::world::MovementClassConfig& config,
                                    double initial_vz, int frames) {
                PlayerMovementState state;
                state.airborne = true;
                state.parachute = true;
                state.parachute_active = true;
                state.velocity.z = initial_vz;
                double fallen{};
                for (int frame{}; frame < frames; ++frame) {
                    state.position = {100.5, 100.5, 100.0};
                    static_cast<void>(step_player(state, {}, nullptr, fixed_dt, config));
                    fallen += state.velocity.z * fixed_dt * 32.0;
                }
                return std::pair{state.velocity.z, fallen};
            };
            const auto stock = battlespades::world::movement_config_for_class(0U);
            auto tuned = stock;
            battlespades::world::apply_flight_profile(
                tuned, battlespades::world::balanced_flight_profile());

            const auto [stock_slow_vz, stock_slow_fallen] = descend(stock, 0.0, 60);
            expect(stock_slow_vz < 0.05 && stock_slow_fallen < 1.0,
                   "stock canopy opened at rest stays below 1.6 blocks/s for a second");
            const auto [tuned_slow_vz, tuned_slow_fallen] = descend(tuned, 0.0, 60);
            expect(std::fabs(tuned_slow_vz * 32.0 - 5.0) < 0.01 &&
                       tuned_slow_fallen > 4.0 && tuned_slow_fallen < 5.0,
                   "v2 canopy opened at rest must reach 5 blocks/s almost at once");
            const auto [tuned_ten_vz, ignored] = descend(tuned, 0.0, 12);
            static_cast<void>(ignored);
            expect(std::fabs(tuned_ten_vz - 0.15625) < 1e-6,
                   "the free-fall floor reaches the canopy terminal within twelve frames");

            for (const auto* config : std::array<const MovementClassConfig*, 2U>{&stock, &tuned}) {
                const auto [fast_vz, fast_fallen] = descend(*config, 0.6, 60);
                static_cast<void>(fast_fallen);
                double expected = 0.6;
                for (int frame{}; frame < 60; ++frame)
                    expected = battlespades::world::canopy_vertical_step(expected, fixed_dt, 1.0, *config);
                expect(std::fabs(fast_vz - expected) < 1e-4 && fast_vz < 0.6 &&
                           fast_vz > static_cast<double>(config->parachute_gravity_scale),
                       "a fast deploy must brake with the stock canopy recurrence");
            }
        }

        // The original core normalizes horizontal look before acceleration;
        // aiming up or down must not slow forward movement.
        {
            PlayerInputState forward;
            forward.forward = true;
            PlayerMovementState level;
            level.airborne = true;
            level.orientation = {1.0, 0.0, 0.0};
            PlayerMovementState pitched = level;
            pitched.orientation = {0.5, 0.0, std::sqrt(0.75)};
            PlayerMovementState vertical = level;
            vertical.orientation = {0.0, 0.0, 1.0};
            static_cast<void>(step_player(level, forward, nullptr, fixed_dt));
            static_cast<void>(step_player(pitched, forward, nullptr, fixed_dt));
            static_cast<void>(step_player(vertical, forward, nullptr, fixed_dt));
            expect(std::fabs(pitched.velocity.x - level.velocity.x) < 1e-7 &&
                       std::fabs(vertical.velocity.x) < 1e-12 &&
                       std::fabs(vertical.velocity.y) < 1e-12,
                   "look pitch must preserve horizontal steering after normalization");
        }

        // A carried objective suppresses sprint while keeping the held bit
        // available for replication and immediate restoration after drop.
        {
            auto map = platform_world();
            PlayerMovementState state;
            state.position = {100.5, 102.5, 190.0};
            state.orientation = {1.0, 0.0, 0.0};
            state.airborne = true;
            settle(state, map, 600);
            state.burdened = true;
            PlayerInputState sprint;
            sprint.forward = true;
            sprint.sprint = true;
            double top_speed{};
            for (int frame{}; frame < 180; ++frame) {
                static_cast<void>(step_player(state, sprint, &map, fixed_dt));
                top_speed = std::max(top_speed, state.velocity.x);
            }
            expect(std::fabs(top_speed - 0.7 / 4.0) < 0.005,
                   "burdened sprint must use the base acceleration profile");
        }

        // A tall wall stops horizontal motion without tunneling and without
        // climbing; walk speed converges to the recovered accel/friction
        // equilibrium of 0.7/4 blocks per physics unit.
        {
            auto map = platform_world();
            for (std::uint32_t z{194U}; z <= 199U; ++z) {
                for (std::uint32_t y{100U}; y <= 105U; ++y) {
                    expect(map.set_voxel(106U, y, z, stone), "wall voxel must be accepted");
                }
            }
            PlayerMovementState state;
            state.position = {100.5, 102.5, 190.0};
            state.orientation = {1.0, 0.0, 0.0};
            state.airborne = true;
            settle(state, map, 600);

            PlayerInputState forward;
            forward.forward = true;
            double top_speed{};
            for (int step{}; step < 360; ++step) {
                static_cast<void>(step_player(state, forward, &map, fixed_dt));
                top_speed = std::max(top_speed, state.velocity.x);
                expect(state.position.x + 0.45 <= 106.0 + 1e-3,
                       "the wall must never be tunneled");
            }
            expect(state.position.x + 0.45 > 105.5,
                   "the walk must reach the wall face");
            expect(std::fabs(top_speed - 0.7 / 4.0) < 0.005,
                   "walk speed must converge to the recovered equilibrium");
            expect(feet_z(state) > 199.5, "a six-block wall must not be climbed");
        }

        // A single-block step is climbed while walking.
        {
            auto map = platform_world();
            for (std::uint32_t y{100U}; y <= 105U; ++y) {
                for (std::uint32_t x{104U}; x <= 108U; ++x) {
                    expect(map.set_voxel(x, y, 199U, stone), "step voxel must be accepted");
                }
            }
            PlayerMovementState state;
            state.position = {101.5, 102.5, 190.0};
            state.orientation = {1.0, 0.0, 0.0};
            state.airborne = true;
            settle(state, map, 600);

            PlayerInputState forward;
            forward.forward = true;
            bool climbed = false;
            for (int step{}; step < 360 && state.position.x < 106.0; ++step) {
                climbed = step_player(state, forward, &map, fixed_dt).climbed || climbed;
            }
            expect(climbed, "walking into a one-block step must climb");
            expect(state.position.x >= 106.0, "the climb must continue forward");
            settle(state, map, 600);
            expect(std::fabs(feet_z(state) - 199.0) < 0.01,
                   "the player must stand on the raised step");
        }

        // A jump pressed on the exact frame that the horizontal mover enters
        // a one-block step may legitimately report both transitions.  The
        // network prediction layer needs this distinction: treating it as an
        // ordinary flat-ground launch discards the climb displacement and
        // produces a one-frame backward/downward twitch at voxel edges.
        {
            auto map = platform_world();
            for (std::uint32_t y{100U}; y <= 105U; ++y) {
                for (std::uint32_t x{104U}; x <= 108U; ++x) {
                    expect(map.set_voxel(x, y, 199U, stone),
                           "jump-step voxel must be accepted");
                }
            }
            PlayerMovementState state;
            state.position = {101.5, 102.5, 190.0};
            state.orientation = {1.0, 0.0, 0.0};
            state.airborne = true;
            settle(state, map, 600);

            PlayerInputState forward;
            forward.forward = true;
            bool exercised = false;
            for (int step{}; step < 720 && !exercised; ++step) {
                auto probe = state;
                if (step_player(probe, forward, &map, fixed_dt).climbed) {
                    auto jump_at_edge = forward;
                    jump_at_edge.jump = true;
                    const auto before = state.position;
                    const auto moved =
                        step_player(state, jump_at_edge, &map, fixed_dt);
                    expect(moved.jumped,
                           "grounded edge input must accept the jump");
                    expect(moved.climbed,
                           "the same launch frame must preserve the step transition");
                    expect(state.position.z < before.z - 0.1,
                           "the edge launch must keep the oracle-measured step glide");
                    exercised = true;
                } else {
                    static_cast<void>(step_player(state, forward, &map, fixed_dt));
                }
            }
            expect(exercised,
                   "fixture must reach a simultaneous jump-and-climb frame");
        }

        // The authoritative mover resolves nearby player bodies after
        // friction and before terrain box movement. With equal standing
        // heights, an axis-aligned overlap uses retail's unusual +X fallback
        // (also present in the original binary fixtures), not a signed push.
        {
            auto map = platform_world();
            PlayerMovementState state;
            state.position = {102.5, 102.5, 190.0};
            state.airborne = true;
            settle(state, map, 600);
            const auto start = state.position;
            const std::array<PlayerCollisionBody, 1U> peers{{
                {{start.x + 0.25, start.y, start.z}, 2.7}}};
            static_cast<void>(step_player(
                state, {}, &map, fixed_dt, {}, peers));
            expect(std::fabs((state.position.x - start.x) - 0.65) <
                       1e-5,
                   "axis-aligned peer collision must retain the retail +X fallback");
            expect(state.velocity.x > 1.2,
                   "peer collision must retain the native separation impulse");
        }

        // The original landing curve multiplies in float before truncating.
        // At exactly 31 blocks, Soldier's 70% damage must be 70, not 69.
        {
            auto map = platform_world();
            PlayerMovementState state;
            state.position = {102.5, 102.5, 197.75};
            state.airborne = true;
            state.velocity.z = 0.2;
            state.fall_distance = 31.0;
            const auto landed = step_player(state, {}, &map, fixed_dt);
            expect(landed.landing_damage == 70,
                   "landing damage must round the original float product before integer truncation");
        }

        // StateData gravity is a world scalar, not a visual hint. LunarBase
        // advertises signed fixed 26/64 and the local prediction step must use
        // that exact value or every airborne frame is corrected by the server.
        {
            PlayerMovementState earth;
            earth.airborne = true;
            PlayerMovementState lunar = earth;
            static_cast<void>(
                step_player(earth, {}, nullptr, fixed_dt, {}, {}, 1.0));
            static_cast<void>(
                step_player(lunar, {}, nullptr, fixed_dt, {}, {}, 26.0 / 64.0));
            expect(std::fabs(earth.velocity.z -
                             (fixed_dt / (1.0 + fixed_dt))) < 1.0e-9,
                   "default map gravity must preserve the recovered Earth step");
            expect(std::fabs(lunar.velocity.z -
                             ((fixed_dt * (26.0 / 64.0)) /
                              (1.0 + fixed_dt))) < 1.0e-9 &&
                       lunar.velocity.z < earth.velocity.z,
                   "LunarBase gravity must produce the exact lower airborne acceleration");
        }

        // Crouch shifts the anchor down by 0.9 and stand-up honors headroom.
        {
            auto map = platform_world();
            PlayerMovementState state;
            state.position = {102.5, 102.5, 190.0};
            state.airborne = true;
            settle(state, map, 600);
            const double standing_z = state.position.z;

            apply_crouch_request(state, true, &map);
            expect(state.crouch, "grounded crouch request must crouch");
            expect(std::fabs(state.position.z - static_cast<double>(static_cast<float>(standing_z + 0.9))) < 1e-9,
                   "crouching must shift the anchor down by 0.9");
            settle(state, map, 600);

            // Seal a ceiling two blocks above the feet: crouch height fits,
            // standing does not.
            for (std::uint32_t y{101U}; y <= 104U; ++y) {
                for (std::uint32_t x{101U}; x <= 104U; ++x) {
                    expect(map.set_voxel(x, y, 197U, stone), "ceiling voxel must be accepted");
                }
            }
            apply_crouch_request(state, false, &map);
            expect(state.crouch, "standing up under a low ceiling must stay crouched");

            for (std::uint32_t y{101U}; y <= 104U; ++y) {
                for (std::uint32_t x{101U}; x <= 104U; ++x) {
                    expect(map.clear_voxel(x, y, 197U), "ceiling voxel must clear");
                }
            }
            apply_crouch_request(state, false, &map);
            expect(!state.crouch, "standing up with headroom must succeed");
            expect(std::fabs(state.position.z - standing_z) < 0.01,
                   "standing up must restore the standing anchor");
        }

        // Demolition's build phase uses packet 108 to constrain prediction.
        // The full-world release packet follows the same path with wide bounds.
        {
            PlayerMovementState state;
            state.position = {8.0, 25.0, 12.0};
            state.velocity = {-2.0, 3.0, -4.0};
            expect(constrain_player_to_bounds(
                       state, PlayerMovementBounds{{10.0, 10.0, 10.0},
                                                   {20.0, 20.0, 20.0}}),
                   "valid server movement bounds must be accepted");
            // world.pyx lock_box clamps position only; velocity is untouched.
            expect(state.position.x == 10.0 && state.position.y == 20.0 &&
                       state.position.z == 12.0 && state.velocity.x == -2.0 &&
                       state.velocity.y == 3.0 && state.velocity.z == -4.0,
                   "lock_box must clamp position only and keep wall-ward velocity");
            const auto retained = state;
            expect(!constrain_player_to_bounds(
                       state, PlayerMovementBounds{{20.0, 10.0, 10.0},
                                                   {10.0, 20.0, 20.0}}) &&
                       state.position.x == retained.position.x,
                   "malformed bounds must fail closed without mutating state");
        }

        // lock_box runs inside the native update, after the move: the frame
        // that walks into the wall still moves, then clamps, velocity intact.
        {
            const auto map = platform_world();
            PlayerMovementState free_walk;
            free_walk.position = {103.5, 103.5, 197.75};
            free_walk.orientation = {1.0, 0.0, 0.0};
            for (int frame{}; frame < 30; ++frame) {
                static_cast<void>(step_player(free_walk, {}, &map, fixed_dt));
            }
            auto locked = free_walk;
            PlayerInputState forward;
            forward.forward = true;
            const PlayerMovementBounds box{{0.0, 0.0, 0.0}, {103.5, 512.0, 240.0}};
            static_cast<void>(step_player(free_walk, forward, &map, fixed_dt));
            static_cast<void>(step_player(locked, forward, &map, fixed_dt,
                                          {}, {}, 1.0, &box));
            expect(free_walk.position.x > 103.5 && locked.position.x == 103.5 &&
                       locked.velocity.x == free_walk.velocity.x &&
                       locked.velocity.x > 0.0,
                   "in-step lock_box must clamp the moved position and keep the velocity");
        }

        // Parachute rules mirror BS/server/player.py _update_parachute.
        {
            using battlespades::world::advance_parachute_rules;
            using battlespades::world::parachute_ground_clearance;
            using battlespades::world::settle_parachute_after_move;
            const auto map = platform_world();
            PlayerMovementState body;
            body.position = {103.5, 103.5, 190.0};
            body.airborne = true;
            body.parachute = true;
            const auto clearance = parachute_ground_clearance(&map, body, false);
            expect(clearance.has_value() && std::fabs(*clearance - (200.0 - 192.25)) < 1e-9,
                   "clearance is measured from the standing feet to the platform");
            const auto crouched = parachute_ground_clearance(&map, body, true);
            expect(crouched.has_value() && std::fabs(*crouched - (200.0 - 191.35)) < 1e-9,
                   "a crouch button uses the 1.35 contact offset");
            auto outside = body;
            outside.position = {-4.0, -4.0, 100.0};
            const auto edge = parachute_ground_clearance(&map, outside, false);
            expect(edge.has_value() && std::fabs(*edge - (239.0 - 102.25)) < 1e-9,
                   "out-of-map columns answer z=239 like get_z");
            expect(!parachute_ground_clearance(nullptr, body, false).has_value(),
                   "no map means no clearance veto");

            // Ascending press stays armed and opens only while descending.
            body.velocity.z = -0.2;
            advance_parachute_rules(body, true, true, false, &map, fixed_dt);
            expect(!body.parachute_active && body.parachute_pending,
                   "an ascending press must arm, not open");
            body.velocity.z = 0.01;
            advance_parachute_rules(body, false, true, false, &map, fixed_dt);
            expect(body.parachute_active && body.parachute_used_this_fall &&
                       !body.parachute_pending && body.parachute_open_frames == 0U,
                   "the armed press must open on descent with enough clearance");

            // Lifted canopies spill, and the fall's one deploy is spent.
            body.velocity.z = -0.06;
            advance_parachute_rules(body, false, true, false, &map, fixed_dt);
            expect(!body.parachute_active, "rising faster than 0.05 must close the canopy");
            body.velocity.z = 0.1;
            advance_parachute_rules(body, true, true, false, &map, fixed_dt);
            expect(!body.parachute_active && !body.parachute_pending,
                   "one deploy per fall");

            // Landing re-arms; a low hop (< 6 blocks) never opens.
            body.airborne = false;
            settle_parachute_after_move(body);
            expect(!body.parachute_used_this_fall, "landing re-arms the deploy");
            auto hop = body;
            hop.airborne = true;
            hop.position.z = 195.0;
            hop.velocity.z = 0.05;
            advance_parachute_rules(hop, true, true, false, &map, fixed_dt);
            expect(!hop.parachute_active && hop.parachute_pending,
                   "a press near the ground stays armed but refuses to open");

            // 30 s timeout (1800 frames at 60 Hz).
            auto timed = body;
            timed.airborne = true;
            timed.velocity.z = 0.05;
            advance_parachute_rules(timed, true, true, false, &map, fixed_dt);
            expect(timed.parachute_active, "timeout fixture must open");
            for (int frame{1}; frame < 1800; ++frame) {
                advance_parachute_rules(timed, false, true, false, &map, fixed_dt);
            }
            expect(timed.parachute_active && timed.parachute_open_frames == 1799U,
                   "the canopy stays open for 1799 frames");
            advance_parachute_rules(timed, false, true, false, &map, fixed_dt);
            expect(!timed.parachute_active, "the 1800th open frame collapses the canopy");

            // Any jetpack forbids a canopy (can_hold false).
            auto packed = body;
            packed.airborne = true;
            packed.velocity.z = 0.1;
            advance_parachute_rules(packed, true, false, false, &map, fixed_dt);
            expect(!packed.parachute_active && !packed.parachute_pending,
                   "a jetpack holder cannot hold a canopy");
        }

        // Late-canopy landing damage and RULE_ENABLE_FALL_ON_WATER_DAMAGE.
        {
            using battlespades::world::parachute_landing_damage;
            const auto soldier = movement_config_for_class(0U);
            expect(parachute_landing_damage(0.05, true, fixed_dt, 1.0, soldier, 200.0) == 0,
                   "terminal canopy descent lands softly");
            const int late = parachute_landing_damage(1.0, false, fixed_dt, 1.0, soldier, 200.0);
            expect(late > 0, "a canopy opened just before impact still charges the fall");
            const auto dry_rule = movement_config_for_class(0U, 1.0, false);
            expect(dry_rule.fall_on_water_damage_multiplier == 0.0 &&
                       soldier.fall_on_water_damage_multiplier == 0.5,
                   "the disabled water rule zeroes the mover's water multiplier");
            expect(parachute_landing_damage(1.0, false, fixed_dt, 1.0, dry_rule, 238.0) == 0,
                   "no water landing damage with the rule off");
        }

        std::cout << "player movement: retail step/collision parity checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
