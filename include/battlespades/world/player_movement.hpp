#pragma once

#include "battlespades/world/vxl_map.hpp"

#include <cstdint>
#include <span>

namespace battlespades::world {

struct Vec3 final {
    double x{};
    double y{};
    double z{};
};

/**
 * Per-class movement tuning. Defaults are the recovered Battle Builder
 * Soldier values from `BattleSpades/shared/constants.py`.
 */
struct MovementClassConfig final {
    double accel_multiplier{0.7};
    double sprint_multiplier{1.4};
    double crouch_sneak_multiplier{0.5};
    double jump_multiplier{1.2};
    double water_friction{8.0};
    bool can_sprint_uphill{true};
    double falling_damage_min_distance{10.0};
    double falling_damage_max_distance{40.0};
    double falling_damage_max_damage{100.0};
    double fall_on_water_damage_multiplier{0.5};
};

/** Exact Battle Builder class profile (0..17), with server rule scaling. */
[[nodiscard]] MovementClassConfig
movement_config_for_class(std::uint8_t class_id,
                          double movement_speed_scale = 1.0) noexcept;

/** Held semantic movement inputs consumed by one fixed simulation step. */
struct PlayerInputState final {
    bool forward{};
    bool backward{};
    bool left{};
    bool right{};
    /** Held flag; a grounded frame launches, airborne frames are no-ops. */
    bool jump{};
    bool crouch{};
    bool sneak{};
    bool sprint{};
    /** Held Z/action state; ability physics is layered onto this seam. */
    bool hover{};
};

/**
 * Replicable player movement state in canonical map coordinates (z down).
 *
 * `position` is the retail anchor: the feet plane sits `contact offset`
 * blocks below it (2.25 standing, 1.35 crouching) and the eye/camera sits at
 * the anchor itself. Movement normalizes the horizontal facing components
 * independently of pitch, using the original float32 normalization stores.
 */
struct PlayerMovementState final {
    Vec3 position{};
    Vec3 velocity{};
    Vec3 orientation{-1.0, 0.0, 0.0};
    bool airborne{};
    bool wade{};
    bool crouch{};
    /** Carrying an objective/pickup disables sprint without clearing its key. */
    bool burdened{};
    /** Internal retail pack enum: 0 none, 1 normal, 2 glider, 3 engineer, 4 UGC. */
    std::uint8_t jetpack{};
    /** Replicated pack firing state; physical key-up still cancels thrust locally. */
    bool jetpack_active{};
    /** Separate stock 0.75-gravity/passive-flight state. */
    bool jetpack_passive{};
    /** A parachute is present in the active normalized loadout. */
    bool parachute{};
    /** Replicated deployed-parachute state (WorldUpdate state bit 0x01). */
    bool parachute_active{};
    /** Local negotiated deploy request waiting for descent; never grants lift. */
    bool parachute_pending{};
    double fall_distance{};
    double climb_timer{};
    double climb_slowdown{1.0};
};

/** Server-authored packet-108 movement volume, in canonical map coordinates. */
struct PlayerMovementBounds final {
    Vec3 minimum{};
    Vec3 maximum{};
};

/**
 * Clamp one predicted player state to a server-owned movement volume.
 *
 * Returns false for malformed/non-finite bounds and leaves the state intact.
 * A velocity component is cancelled only when it points further through the
 * boundary; motion back into the allowed volume remains responsive.
 */
[[nodiscard]] bool constrain_player_to_bounds(
    PlayerMovementState& state,
    const PlayerMovementBounds& bounds) noexcept;

/** One authoritative peer body consumed by the native contact impulse. */
struct PlayerCollisionBody final {
    Vec3 position{};
    /** Full body height: 2.7 standing/wading, 1.8 crouched and dry. */
    double height{2.7};
    /** Protocol identity used only to resolve the current interpolated pose. */
    std::uint8_t player_id{};
};

/** Exact height passed to the retail player-vs-player collision phase. */
[[nodiscard]] double player_body_height(bool crouch, bool wade) noexcept;

struct MovementStepResult final {
    bool jumped{};
    bool climbed{};
    bool landed{};
    /**
     * A landing above the retail soft-contact threshold.  This is distinct
     * from HP damage: a short, fast drop can thud and interrupt horizontal
     * motion while still producing no fall damage.
     */
    bool hard_landing{};
    /**
     * Mirrors the server contract: positive is fall damage, -1 marks a
     * damage-free hard landing, 0 is a quiet frame. The invulnerable Tutorial
     * ignores the damage but keeps the velocity effects.
     */
    int landing_damage{};
};

/**
 * One fixed-timestep step of the recovered Battle Builder movement engine.
 *
 * This is a clean-room port of the BattleSpades server's oracle-calibrated
 * `aoslib/world.pyx` player update (itself validated against the compiled
 * retail `world.pyd`): jump impulse then gravity in the same frame, class
 * multiplier selection without stacking, `1 + k*dt` friction divisors, the
 * single-pass boxclipmove with per-axis glide passes, ledge-lip drift and
 * the severe-landing horizontal slowdown. Collision intermediates run in
 * float32 exactly like retail; changing that changes collision branches at
 * exact block boundaries.
 *
 * Includes authoritative nearby-player contact and the retail ability layer:
 * burdened sprint suppression, all four jetpack modes, UGC hover and the
 * parachute. Ability booleans are supplied by the protocol/session boundary;
 * the movement core never guesses equipment from a class id.
 */
[[nodiscard]] MovementStepResult step_player(PlayerMovementState& state,
                                             const PlayerInputState& input,
                                             const VxlMap* map,
                                             double dt,
                                             const MovementClassConfig& movement_class = {},
                                             std::span<const PlayerCollisionBody>
                                                 collision_bodies = {},
                                             double world_gravity = 1.0);

/**
 * Applies a crouch/stand request with the retail 0.9-block anchor shift and
 * the stand-up headroom check. Airborne crouches do not shift the anchor.
 */
void apply_crouch_request(PlayerMovementState& state, bool crouch, const VxlMap* map,
                         std::span<const PlayerCollisionBody> collision_bodies = {},
                         bool hover = false);

/** Retail clipbox probe (z=239 water row remaps to the z=238 bed). */
[[nodiscard]] bool clip_at(const VxlMap* map, double x, double y, double z) noexcept;

/** Ground-contact probe with the measured 0.00875 epsilon, for diagnostics. */
[[nodiscard]] bool grounded(const VxlMap* map, const PlayerMovementState& state) noexcept;

/** Feet plane offset below the anchor: 2.25 standing, 1.35 crouching dry. */
[[nodiscard]] double player_contact_offset(bool crouch, bool wade) noexcept;

} // namespace battlespades::world
