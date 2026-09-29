#pragma once

#include "battlespades/world/vxl_map.hpp"

#include <cstdint>
#include <optional>
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

/**
 * Exact Battle Builder class profile (0..17), with server rule scaling.
 * `fall_on_water_damage` is InitialInfo's RULE_ENABLE_FALL_ON_WATER_DAMAGE:
 * retail GameClass passes a zero water multiplier into the native mover when
 * the rule is off (BS/server/player.py _apply_class_profile_to_world).
 */
[[nodiscard]] MovementClassConfig
movement_config_for_class(std::uint8_t class_id,
                          double movement_speed_scale = 1.0,
                          bool fall_on_water_damage = true) noexcept;

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
    /** Server rule: one canopy per fall, re-armed only by ground or water. */
    bool parachute_used_this_fall{};
    /** Frames the current canopy has been open (server closes at 30 s). */
    std::uint32_t parachute_open_frames{};
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
 * Clamp one predicted player position to a server-owned movement volume.
 *
 * This is world.pyd's `lock_box` (BS/aoslib/world.pyx `update`): position
 * only, `min(max(p, lo), hi)` per axis, stored as float32. Velocity is never
 * touched, so wall-ward motion survives exactly as it does in the retail
 * mover. Returns false for malformed/non-finite bounds and leaves the state
 * intact.
 */
[[nodiscard]] bool constrain_player_to_bounds(
    PlayerMovementState& state,
    const PlayerMovementBounds& bounds) noexcept;

/** BattleSpades server parachute policy (BS/server/player.py, docs/PARACHUTE.md). */
inline constexpr double parachute_min_deploy_clearance{6.0};
inline constexpr double parachute_max_open_seconds{30.0};
inline constexpr double parachute_max_rise_velocity{0.05};

/**
 * Blocks from the feet to the nearest solid voxel below the body: the minimum
 * over the centre and the four hull corners (server
 * `Player._parachute_ground_clearance`, including `get_z`'s out-of-map and
 * empty-column answer of z=239). `crouch_input` is the frame's crouch button.
 * Empty without a map, which the server treats as "high enough".
 */
[[nodiscard]] std::optional<double> parachute_ground_clearance(
    const VxlMap* map, const PlayerMovementState& state, bool crouch_input) noexcept;

/**
 * One pre-move frame of the server's canopy rules (`Player._update_parachute`):
 * grounded or unable to hold a canopy closes and disarms it; an open canopy
 * collapses after 30 s or when lifted (`vz < -0.05`); otherwise a press arms
 * one deploy per fall, which opens only while descending (`vz >= 0`) with at
 * least six blocks of clearance. `can_hold` is alive, parachute equipped and
 * no jetpack of any kind.
 */
void advance_parachute_rules(PlayerMovementState& state, bool pressed, bool can_hold,
                             bool crouch_input, const VxlMap* map, double dt) noexcept;

/** Post-move landing/water close and re-arm (`Player._parachute_after_move`). */
void settle_parachute_after_move(PlayerMovementState& state) noexcept;

/**
 * Free-fall-equivalent landing damage for a fall that touched a canopy
 * (`Player._parachute_speed_damage`): the damage of a free fall from rest that
 * reaches this frame's landing speed, with the class curve and the z>237
 * water multiplier (zero when RULE_ENABLE_FALL_ON_WATER_DAMAGE is off).
 */
[[nodiscard]] int parachute_landing_damage(double pre_move_vz, bool canopy_physics,
                                           double dt, double world_gravity,
                                           const MovementClassConfig& movement_class,
                                           double landed_z) noexcept;

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
                                             double world_gravity = 1.0,
                                             const PlayerMovementBounds* lock_box = nullptr);

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
