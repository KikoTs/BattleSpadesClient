#pragma once

#include <cstdint>

namespace battlespades::world {

/** One movement sound event. */
enum class MovementSound : std::uint8_t {
    none,
    footstep,
    wade,
    jump,
    water_jump,
    /** JETPACK_LAND_SOUND (JP_lowthrust_rel): the jetpack's passive flag cleared. */
    jetpack_land,
    land,
    water_land,
    fall_hurt,
};

/**
 * Everything the footstep decision depends on.
 *
 * Deliberately carries NO surface or material field. Retail picks between
 * exactly two ground sets on the wade bit alone, and no per-surface samples ship
 * -- the shipped set is four footstep variants, four wade variants, and the
 * jump/land impacts. A material-keyed system would be an invention rather than
 * parity, and would have nothing to play.
 */
struct FootstepInput final {
    bool walking{};
    bool airborne{};
    bool crouch{};
    bool sneak{};
    bool sprint{};
    bool wade{};
};

/** Cadence state for one character. `armed` models retail's null timer. */
struct FootstepState final {
    double next_footstep{};
    bool armed{};
};

/** Wall-clock cadence, not distance travelled and not an animation event. */
inline constexpr double footstep_interval_walk{0.512};
inline constexpr double footstep_interval_sprint{0.386};
/** One shared lockout so a bunny-hop's land and jump do not both fire. */
inline constexpr double jump_sound_repeat_delay{0.1};

/**
 * Which family a step would use, ignoring the timer.
 *
 * Split from the cadence so the selection table can be tested on its own.
 */
[[nodiscard]] MovementSound footstep_family(const FootstepInput& input) noexcept;

/**
 * Whether a suppressed character leaves its timer STALE rather than re-arming.
 *
 * Retail appears to leave the next-step deadline untouched while crouching,
 * sneaking or airborne, so the first step after standing up fires immediately.
 * This is the one behaviour the recovery could not read cleanly out of the
 * binary, so it is named rather than buried: if the parity rig ever disagrees,
 * flip this and the pinned test fails loudly instead of the cadence quietly
 * drifting.
 */
inline constexpr bool stale_suppressed_timer{true};

/**
 * Advances one character's footstep cadence to absolute time `now`.
 *
 * Returns the sound to emit this tick, or `none`.
 */
[[nodiscard]] MovementSound step_footsteps(FootstepState& state,
                                           const FootstepInput& input,
                                           double now) noexcept;

struct LandJumpInput final {
    bool landed{};
    /** Contact crossed the hard-landing speed threshold, even without HP loss. */
    bool hard_landing{};
    bool jumped{};
    bool wade{};
    bool fall_damage{};
};

struct LandJumpState final {
    double repeat_timer{};
};

struct LandJumpSounds final {
    MovementSound impact{MovementSound::none};
    bool fall_hurt{};
};

/**
 * Land and jump cues, sharing one repeat lockout.
 *
 * A soft landing stays silent. A hard landing owns the ground/water impact;
 * fall damage is deliberately NOT gated by the lockout and layers on top.
 */
[[nodiscard]] LandJumpSounds step_land_jump(LandJumpState& state,
                                            const LandJumpInput& input,
                                            double dt) noexcept;

/** Asset stem for a movement sound, or an empty view for `none`. */
[[nodiscard]] const char* movement_sound_stem(MovementSound sound,
                                              std::uint32_t variant) noexcept;

/** How many interchangeable variants a family ships. 0 when it has none. */
[[nodiscard]] std::uint32_t movement_sound_variants(MovementSound sound) noexcept;

/**
 * Resolve the movement-foley row from retail CLASS_SOUNDS.
 *
 * Zombie classes replace the ordinary movement samples with their own banks.
 * Every other class uses the generic family; jetpack landing is shared except
 * for the deliberately silent UGC Builder row.
 */
[[nodiscard]] const char* class_movement_sound_stem(std::uint8_t class_id,
                                                    MovementSound sound,
                                                    std::uint32_t variant) noexcept;

} // namespace battlespades::world
