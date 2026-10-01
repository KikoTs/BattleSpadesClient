#pragma once

#include "battlespades/world/retail_view_model.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace battlespades::world {

enum class RetailThirdPersonArmPart : std::size_t {
    right_upper,
    right_lower,
    left_upper,
    left_lower,
    count,
};

/**
 * One recovered ModelDisplay arm transform from Character.reset_tp_arms().
 *
 * `model_offset` is the ModelDisplay attachment offset measured in authored
 * model axes while `position` is measured in retail character axes.
 * DisplayList converts each vector to `(x,-z,y)` before issuing glTranslatef;
 * the renderer must preserve that convention until the final character-basis
 * conversion.
 */
struct RetailThirdPersonArmPose final {
    ViewModelVector position{};
    ViewModelVector model_offset{};
    double pitch_degrees{};
    double yaw_degrees{};
    double roll_degrees{};
    double extra_yaw_degrees{};
    double extra_roll_degrees{};
};

/**
 * Converts one retail DisplayList vector into the GL model basis.
 *
 * DisplayList.draw emits `(x,-z,y)` for both its position and model offset.
 * Keep this conversion explicit until the completed display hierarchy is
 * rotated into canonical world axes; swapping Y/Z earlier makes arms appear
 * as detached duplicate hands.
 */
[[nodiscard]] ViewModelVector retail_display_vector(
    ViewModelVector value) noexcept;

/**
 * Resolve the two DisplayList translations into the arm's GL-space origin.
 *
 * This is intentionally different from the authored load_model pivot used by
 * body and weapon KV6s. The caller performs the final GL-to-world basis
 * rotation after every local DisplayList transform has been composed.
 */
[[nodiscard]] ViewModelVector retail_third_person_arm_origin(
    const RetailThirdPersonArmPose& part, double model_scale) noexcept;

/** Semantic third-person attachment contract recovered from Character.draw. */
struct RetailThirdPersonPose final {
    static constexpr std::size_t maximum_tool_parts{3U};

    /** Raw Character.head pitch, applied around the neck rather than the root. */
    double head_pitch_degrees{};
    /**
     * Character.weapon DisplayList pitch. It is composed before the fixed
     * hand-height anchor so looking up cannot translate the weapon away.
     */
    double weapon_pitch_degrees{};
    bool draws_player_arms{true};
    double arm_model_scale{0.05};
    std::array<RetailThirdPersonArmPose,
               static_cast<std::size_t>(RetailThirdPersonArmPart::count)>
        arms{};

    double tool_model_scale{0.065};
    /**
     * Character.weapon's shared DisplayList position.
     *
     * This is a parent transform, not a per-model position.  Keeping it
     * separate is essential for multipart tools: child positions are scaled
     * with their KV6 while this hand-height anchor is not.
     */
    ViewModelVector tool_anchor{0.0, 0.0, 0.5};
    std::array<RetailModelPose, maximum_tool_parts> tool_parts{};
    std::size_t tool_part_count{};
};

/**
 * Canonical X/Y rotations applied around one retail hip anchor.
 *
 * Retail `set_rotation(x, 0, z)` maps forward swing to canonical X and the
 * lateral component to canonical Y. Swapping these axes makes forward-moving
 * legs cross through the character centre.
 */
struct RetailLegPose final {
    double rotation_x_degrees{};
    double rotation_y_degrees{};
};

/** One exact Character.update_animation leg result for a render frame. */
struct RetailWalkPose final {
    RetailLegPose left{};
    RetailLegPose right{};
};

/** Recovered Weapon.draw attachment for a third-person muzzle flash. */
struct RetailMuzzleAttachment final {
    /** Authored DisplayList offset in retail model axes. */
    ViewModelVector offset{};
    /** Muzzle DisplayList size relative to the held weapon model. */
    double scale{0.5};
};

/**
 * Return the retail third-person muzzle attachment for a tool.
 *
 * A missing value means the tool has no observer muzzle flash. Values come
 * from the concrete Weapon subclasses; keeping them separate from view-model
 * offsets prevents first-person data from moving another player's flash back
 * into their hands.
 */
[[nodiscard]] std::optional<RetailMuzzleAttachment>
retail_third_person_muzzle_attachment(std::uint8_t tool_id) noexcept;

/**
 * Resolve one tool part's GL-space origin after the retail hierarchy.
 *
 * Character.weapon is a DisplayList and therefore converts its parent vector
 * to `(x,-z,y)`. Tool.apply_transform is different: it calls glTranslatef on
 * the model position directly, outside the model's scale. Converting or
 * scaling that child vector puts MinigunWeapon's barrel back inside its body.
 */
[[nodiscard]] ViewModelVector retail_third_person_tool_origin(
    const RetailThirdPersonPose& pose, std::size_t part) noexcept;

/**
 * Convert a decoded packet orientation into the canonical character-root yaw.
 *
 * The retail KV6 model faces +Y after its (X,-Z,Y) VBO basis is converted to
 * our world basis. Packet orientation (-1,0,0) therefore requires +90 degrees
 * at the root. This is deliberately a render-only conversion; the packet and
 * local movement axes already match the authoritative server.
 */
[[nodiscard]] double retail_character_root_yaw_degrees(
    ViewModelVector orientation) noexcept;

/**
 * Largest leg swing, in degrees, evaluate_retail_walk_pose will produce.
 *
 * It is retail's own peak (256 x 0.028 x 30 x speed) at the fastest shipped
 * human ground gait, Gangster/VIP sprint: CLASS_SPRINT_MULTIPLIER 1.5 times
 * the InitialInfo speed 1.5, over ground friction 4 = 0.5625 blocks/tick.
 * Only faster characters (the zombie classes, jetpack/UGC flight) are scaled.
 */
inline constexpr double retail_walk_max_swing_degrees{256.0 * 0.028 * 30.0 * (1.5 * 1.5 / 4.0)};

/**
 * Evaluate Character.update_animation's shared 1.024-second triangle gait.
 *
 * `timer_ms` is the scene timer used by every character. Velocity and
 * orientation are decoded protocol vectors; invalid/idle input fails closed
 * to planted legs. Crouching keeps the same phase with retail's smaller arc.
 * Speeds whose retail arc would pass retail_walk_max_swing_degrees have the
 * whole triangle scaled down to it (same phase, same forward/strafe ratio).
 */
[[nodiscard]] RetailWalkPose evaluate_retail_walk_pose(
    std::uint64_t timer_ms, ViewModelVector velocity,
    ViewModelVector orientation, bool crouching) noexcept;

/** Tool.get_arm_pitch_range(): the clamp Character.draw applies to arm pitch. */
struct RetailArmPitchRange final {
    double minimum{-90.0};
    double maximum{70.0};
};

/**
 * Recovered get_arm_pitch_range() for a tool `seconds_since_primary` after
 * its last use: ARMS_PITCH (-90, 70) by default, RiotShieldTool (-80, 0),
 * and DiggingTool's upper limit lowered by pitch_increase except while the
 * swing (shoot_interval) is running.
 */
[[nodiscard]] RetailArmPitchRange retail_tool_arm_pitch_range(
    std::uint8_t tool_id, double seconds_since_primary) noexcept;

/**
 * Recovered Tool.get_pitch() (Character.shoot_pitch) as observers see it:
 * 0 for ordinary tools, pitch_initial -4 for grenades and resting digging
 * tools, and DiggingTool.use_spade's from->to interpolation during a swing.
 */
[[nodiscard]] double retail_tool_pitch(std::uint8_t tool_id,
                                       double seconds_since_primary,
                                       double aim_pitch_degrees) noexcept;

/**
 * Evaluate the retail third-person arm and equipped-tool transforms.
 *
 * The function is pure and safe on every thread. `action_serial` is the
 * number of accepted uses of the equipped tool and selects the alternating
 * Zombie hand exactly as ZombieHandTool.last_used_hand does.
 * `can_display_weapon` suppresses only the held tool, never class arms; the
 * arms then take Character.draw's hidden-tool +50 degree pitch.
 * `mechanism_phase` is the minigun barrel's revolution fraction [0, 1): the
 * MINIGUN branch rolls only the barrel, about its recovered pivot.
 */
[[nodiscard]] RetailThirdPersonPose evaluate_retail_third_person_pose(
    std::uint8_t tool_id, std::size_t tool_part_count,
    double seconds_since_primary = 1.0e9, std::uint64_t action_serial = 0U,
    double aim_pitch_degrees = 0.0, bool can_display_weapon = true,
    double mechanism_phase = 0.0) noexcept;

/**
 * Observer-side MinigunWeapon spin state (remote barrels are never
 * replicated; retail runs the same update from the WorldUpdate trigger bits).
 */
struct RetailRemoteMinigunSpin final {
    /** |shoot_interval - initial| / range, 0 at rest and 1 at full spin. */
    double ratio{};
    /** AnimRoll revolution fraction in [0, 1). */
    double phase{};
};

/**
 * One fixed step of MinigunWeapon.update for an observed character: the
 * interval alters at -0.15/s while a trigger is held and +0.075/s otherwise
 * over a -0.2 range (ratio +0.75/s, -0.375/s), and the barrel turns at
 * ratio * MINIGUN_BARREL_SPIN_SPEED_MAX (5) revolutions per second.
 */
[[nodiscard]] RetailRemoteMinigunSpin advance_retail_remote_minigun_spin(
    RetailRemoteMinigunSpin state, double dt, bool trigger_held) noexcept;

} // namespace battlespades::world
