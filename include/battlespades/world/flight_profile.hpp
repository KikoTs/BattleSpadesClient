#pragma once

#include <array>
#include <cstdint>

namespace battlespades::world {

/**
 * One row of retail `JETPACK_PROPERTIES` (BS/shared/constants.py), indexed by
 * world.pyd's compact pack enum (1 normal/66, 2 glider/67, 3 engineer/68,
 * 4 UGC builder/69). The server's `Player._update_jetpack` reads the same
 * table; the client predicts from it instead of scattering literals.
 */
struct JetpackProperties final {
    double start_delay{};
    double max_fuel{};
    double activation_cost{};
    double refill_rate{};
    double flying_consumption{};
    double burdened_slow_down{};
    double refill_delay_due_damage{};
    double damage_multiplier{};
    double death_acceleration{};
};

inline constexpr std::array<JetpackProperties, 5U> retail_jetpack_properties{{
    {},
    {0.25, 100.0, 10.0, 10.0, 75.0, 0.5, 2.0, 2.0, 3.0},
    {0.25, 100.0, 10.0, 9.0, 17.0, 0.5, 2.0, 2.0, 0.25},
    {0.25, 100.0, 10.0, 3.0, 18.0, 0.5, 0.5, 1.0, 0.25},
    {0.1, 100.0, 0.0, 100.0, 0.0, 0.5, 0.1, 1.0, 0.25},
}};

/** Properties for a compact pack id; pack 0 or unknown yields the empty row. */
[[nodiscard]] constexpr const JetpackProperties& jetpack_properties(std::uint8_t pack) noexcept {
    return pack < retail_jetpack_properties.size() ? retail_jetpack_properties[pack]
                                                   : retail_jetpack_properties[0U];
}

/**
 * Accepted-input frames between the advertised pack boundary and native
 * thrust (BS/server/player.py JETPACK_ACTIVATION_DEFER_FRAMES and
 * JETPACK_EXHAUSTION_TAIL_FRAMES). The tail is the retail-calibrated value
 * from the 60 Hz Rocketeer captures (BS/docs/RETAIL_JUMP_RESTORE.md): the
 * stock owner keeps thrusting 2-3 frames after the exhausted row, and the
 * server keeps 3. Native BSCF owners are pinned to these same constants on
 * the server so a host's `[debug]` override can never split the two sides.
 */
inline constexpr std::uint8_t jetpack_activation_defer_frames{2U};
inline constexpr std::uint8_t jetpack_exhaustion_tail_frames{3U};

/** Stock world.pyd literals: active Engineer air accel (0x10012DC8), canopy gravity (0x10012EFD). */
inline constexpr float retail_engineer_flight_accel{0.1F};
inline constexpr float retail_canopy_gravity_scale{0.05F};

/**
 * Resource policy negotiated with BattleSpades (InitialInfo BSFP trailer).
 * Version 1 carries fuel policy only. Version 2 (BS/server/flight_profile.py
 * BALANCED_FLIGHT_V2) also tunes two native-mover literals, so prediction and
 * authority use the identical numbers: the active Engineer pack's air
 * acceleration and the canopy gravity with its free-fall floor. A stock or v1
 * server leaves them at the world.pyd values.
 */
struct FlightProfile final {
    std::array<double, 5U> drain{0.0, 75.0, 17.0, 18.0, 0.0};
    std::array<double, 5U> refill{0.0, 10.0, 9.0, 3.0, 100.0};
    bool grounded_refill_only{};
    double refill_idle_seconds{};
    bool descending_parachute_only{};
    float engineer_flight_accel{retail_engineer_flight_accel};
    float canopy_gravity_scale{retail_canopy_gravity_scale};
    /** Below canopy terminal speed the body falls with ordinary gravity. */
    bool canopy_free_fall_floor{};
};

/**
 * Requested local balance (BSFP v2), distinct from the recovered original
 * tables: longer fuel, an Engineer that flies at its walking speed (0.25 vs
 * stock 0.1) and a 5 blocks/s canopy (0.15625 vs stock 0.05) that a slow fall
 * reaches at once. Offline and training sessions use it too.
 */
[[nodiscard]] constexpr FlightProfile balanced_flight_profile() noexcept {
    return {{0.0, 30.0, 9.0, 7.5, 0.0}, {0.0, 20.0, 20.0, 20.0, 100.0}, true, 1.0, true,
            0.25F, 0.15625F, true};
}

} // namespace battlespades::world
