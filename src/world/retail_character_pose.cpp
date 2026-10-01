#include "battlespades/world/retail_character_pose.hpp"

#include "battlespades/world/weapon_catalog.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace battlespades::world {
namespace {

constexpr std::uint8_t zombie_hand_tool_id{24U};
constexpr std::uint8_t zombie_prefab_tool_id{28U};
constexpr std::uint8_t minigun_tool_id{8U};
constexpr std::uint8_t riot_shield_tool_id{52U};

/**
 * MINIGUN branch pivot, in model units before `size` (dword_100989B4 = -7,
 * dbl_1008B3F8 = -6.5, dbl_1008B3F0 = 27.5). The roll is about GL z, so
 * only x/y move the axis.
 */
constexpr std::array<double, 3U> minigun_barrel_pivot{-7.0, -6.5, 27.5};

/**
 * Character.draw's ZombiePrefabTool branch (pyx 1910-1940): size 0.04 and
 * three hard-coded part transforms (IDA: Character.draw 0x1004F120 constants
 * in call order), replacing the generic Tool.apply_transform loop.
 */
constexpr double zombie_prefab_model_size{0.04};
constexpr std::array<std::array<ViewModelVector, 2U>, 3U> zombie_prefab_parts{{
    {{{-0.95, -0.2, 0.65}, {0.0, 0.0, 180.0}}},
    {{{-0.72, 0.5, -0.25}, {45.0, 45.0, 0.0}}},
    {{{0.65, 0.5, 0.3}, {90.0, 0.0, -90.0}}},
}};

/** Largest `(timer & 511) - 255` value in Character.update_animation. */
constexpr double retail_walk_triangle_peak{256.0};

/** Character.draw's `pitch += 50` when can_display_weapon is false. */
constexpr double hidden_tool_arm_pitch_degrees{50.0};

[[nodiscard]] constexpr ViewModelVector add(ViewModelVector left,
                                             ViewModelVector right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

/** DiggingTool subclasses' pitch_increase class attribute (0 otherwise). */
[[nodiscard]] constexpr double digging_pitch_increase(std::uint8_t tool_id) noexcept {
    switch (tool_id) {
    case 0U:  // PickAxeTool
    case 34U: // CrowbarTool
    case 44U: // UGCPickAxeTool
        return 30.0;
    case 1U:  // KnifeTool
    case 49U: // RiotStickTool
    case 50U: // MacheteTool
        return 50.0;
    case 2U:  // SpadeTool
    case 3U:  // SuperSpadeTool
    case 4U:  // ClassicSpadeTool
    case 45U: // UGCSuperSpadeTool
        return 40.0;
    case riot_shield_tool_id:
        return 10.0;
    case zombie_hand_tool_id:
        return 200.0;
    default:
        return 0.0;
    }
}

/** DiggingTool.shoot_interval: the swing's animation_timer range. */
[[nodiscard]] double digging_length(std::uint8_t tool_id) noexcept {
    const auto* weapon = find_weapon_definition(tool_id);
    if (weapon == nullptr) {
        return 1.0;
    }
    const double interval = weapon->retail.use.shoot_interval.value_or(weapon->fire_interval);
    return std::max(interval > 0.0 ? interval : weapon->fire_interval, 0.001);
}

} // namespace

ViewModelVector retail_display_vector(ViewModelVector value) noexcept {
    // aoslib.draw.DisplayList.draw, draw.pyx lines 471 and 476:
    // glTranslatef(position.x, -position.z, position.y), followed later by
    // glTranslatef(offset_x, -offset_z, offset_y).
    return {value.x, -value.z, value.y};
}

ViewModelVector retail_third_person_arm_origin(const RetailThirdPersonArmPose& part,
                                               double model_scale) noexcept {
    const auto position = retail_display_vector(part.position);
    const auto offset = retail_display_vector(part.model_offset);
    return {
        position.x + offset.x * model_scale,
        position.y + offset.y * model_scale,
        position.z + offset.z * model_scale,
    };
}

ViewModelVector retail_third_person_tool_origin(const RetailThirdPersonPose& pose,
                                                std::size_t part) noexcept {
    const auto anchor = retail_display_vector(pose.tool_anchor);
    if (part >= pose.tool_part_count || part >= pose.tool_parts.size()) {
        return anchor;
    }
    // Tool.apply_transform uses this vector directly in OpenGL axes. It runs
    // outside DisplayList's KV6 scale, so both the axes and magnitude remain
    // untouched. Minigun's (0,-0.3,1.1) is a world-space barrel attachment,
    // not 0.065-scaled authored voxels.
    const auto& local = pose.tool_parts[part].position;
    return {anchor.x + local.x, anchor.y + local.y, anchor.z + local.z};
}

std::optional<RetailMuzzleAttachment>
retail_third_person_muzzle_attachment(std::uint8_t tool_id) noexcept {
    // Recovered from the retail weapon subclasses' muzzle_flash_offset and
    // muzzle_flash_scale attributes. IDs are the shared.constants tool enum.
    switch (tool_id) {
    case 6U:  // ClassicRifleWeapon uses the flash model's authored pivot.
        return RetailMuzzleAttachment{{0.0, 0.0, 0.0}, 0.5};
    case 7U:  // SMG
    case 38U: // Classic SMG
        return RetailMuzzleAttachment{{-12.0, 59.0, -5.0}, 0.5};
    case 8U: // Minigun
        return RetailMuzzleAttachment{{-9.5, 58.0, 5.0}, 0.75};
    case 9U:  // Shotgun
    case 10U: // Shotgun 2
    case 37U: // Classic shotgun
        return RetailMuzzleAttachment{{-6.0, 36.0, -3.0}, 1.0};
    case 15U: // Deployed machine gun
        return RetailMuzzleAttachment{{-5.0, 69.0, 0.0}, 0.75};
    case 17U: // Pistol
        return RetailMuzzleAttachment{{-13.0, 53.0, -6.0}, 0.5};
    case 18U: // Sniper
    case 19U: // Sniper 2
        return RetailMuzzleAttachment{{-12.0, 78.0, -5.0}, 0.5};
    case 35U: // Tommy gun
        return RetailMuzzleAttachment{{-19.0, 92.0, -14.0}, 0.5};
    case 36U: // Snub pistol
        return RetailMuzzleAttachment{{-19.0, 59.0, -13.0}, 0.5};
    case 53U: // Automatic pistol
        return RetailMuzzleAttachment{{-19.0, 71.0, -13.0}, 0.5};
    case 60U: // Assault rifle
        return RetailMuzzleAttachment{{-21.0, 110.0, -13.0}, 0.5};
    case 61U: // Light machine gun
        return RetailMuzzleAttachment{{-13.0, 91.0, -8.5}, 0.5};
    case 62U: // Automatic shotgun
        return RetailMuzzleAttachment{{-14.0, 72.0, -5.8}, 0.6};
    default:
        return std::nullopt;
    }
}

double retail_character_root_yaw_degrees(ViewModelVector orientation) noexcept {
    if (!std::isfinite(orientation.x) || !std::isfinite(orientation.y) ||
        std::hypot(orientation.x, orientation.y) <= 1.0e-12) {
        return 0.0;
    }
    return std::atan2(-orientation.x, orientation.y) * 180.0 / std::numbers::pi;
}

RetailWalkPose evaluate_retail_walk_pose(std::uint64_t timer_ms,
                                         ViewModelVector velocity,
                                         ViewModelVector orientation,
                                         bool crouching) noexcept {
    RetailWalkPose result;
    // Character.update_animation returns before reading scene.timer when both
    // horizontal components are inside the recovered +/-0.01 dead zone.
    if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y) ||
        (std::fabs(velocity.x) <= 0.01 && std::fabs(velocity.y) <= 0.01)) {
        return result;
    }
    const double horizontal_length = std::hypot(orientation.x, orientation.y);
    if (!std::isfinite(horizontal_length) || horizontal_length <= 1.0e-12) {
        return result;
    }

    const double forward_x = orientation.x / horizontal_length;
    const double forward_y = orientation.y / horizontal_length;
    // WorldObject.s is the right/side vector (-orientation.y, orientation.x).
    const double side_x = -forward_y;
    const double side_y = forward_x;
    const double triangle =
        static_cast<double>(timer_ms & 511U) - 255.0;
    const double scale = (crouching ? 0.016 : 0.028) * 30.0;
    const double forward_speed = forward_x * velocity.x + forward_y * velocity.y;
    const double lateral_speed = side_x * velocity.x + side_y * velocity.y;
    // Retail's arc grows linearly with speed and is never limited. Every
    // shipped human class tops out on the ground at Gangster/VIP sprint
    // (CLASS_SPRINT_MULTIPLIER 1.5 x InitialInfo speed 1.5 / ground friction
    // 4 = 0.5625 blocks/tick, a ~121 degree peak). The zombie classes run
    // well outside that domain: CLASS_ZOMBIE sprint is 1.65 x 1.65 / 4 =
    // 0.68 (146 degrees, the feet kick over the head) and CLASS_FAST_ZOMBIE
    // walks at 1.1 x 3.0 / 4 = 0.825 and sprints at 3.0 x 3.0 / 4 = 2.25
    // (177 to 482 degrees: the legs windmill through full turns). Scale the
    // whole triangle down to that ceiling instead of flat-topping it, so the
    // phase and forward/strafe ratio stay retail and every human gait below
    // the ceiling is bit-identical to Character.update_animation.
    const double peak =
        retail_walk_triangle_peak * scale * std::hypot(forward_speed, lateral_speed);
    const double limit =
        peak > retail_walk_max_swing_degrees ? retail_walk_max_swing_degrees / peak : 1.0;
    const double forward = triangle * forward_speed * scale * limit;
    const double lateral = triangle * lateral_speed * scale * limit;
    // The second 512 ms half negates the fresh ramp, producing the continuous
    // triangle wave seen in retail rather than a speed-dependent sine loop.
    const double side = (timer_ms & 1023U) > 511U ? -1.0 : 1.0;
    // Character.update_animation writes the forward projection to the first
    // DisplayList rotation component.  In the native canonical rig that is
    // the hip X axis: routing it to Y makes the feet swing toward the opposite
    // hip and visibly cross while a player or bot walks straight ahead.
    // Retail's second (Z) component becomes canonical Y after the character
    // basis conversion and carries only the strafe lean.
    result.left = {side * forward, side * lateral};
    result.right = {-result.left.rotation_x_degrees,
                    -result.left.rotation_y_degrees};
    return result;
}

RetailArmPitchRange retail_tool_arm_pitch_range(std::uint8_t tool_id,
                                                double seconds_since_primary) noexcept {
    // Tool.get_arm_pitch_range() = (ARMS_PITCH_MINIMUM, ARMS_PITCH_MAXIMUM).
    RetailArmPitchRange range{-90.0, 70.0};
    if (tool_id == riot_shield_tool_id) {
        // RiotShieldTool overrides the whole range with (A1886, A1887).
        return {-80.0, 0.0};
    }
    if (tool_id == zombie_hand_tool_id) {
        // ZombieHandTool skips DiggingTool: super(DiggingTool, self).
        return range;
    }
    const double increase = digging_pitch_increase(tool_id);
    if (increase > 0.0) {
        // DiggingTool: the upper limit drops by pitch_increase whenever the
        // swing (animation_timer in [0, shoot_interval)) is not running.
        const double length = digging_length(tool_id);
        const bool swinging =
            seconds_since_primary >= 0.0 && seconds_since_primary < length;
        if (!swinging) {
            range.maximum -= increase;
        }
    }
    return range;
}

double retail_tool_pitch(std::uint8_t tool_id, double seconds_since_primary,
                         double aim_pitch_degrees) noexcept {
    constexpr double digging_pitch_initial{-4.0};
    switch (tool_id) {
    case 11U:
    case 31U:
    case 32U:
        // GrenadeTool.pitch_initial; the cook tilt (-90 * fuse fraction) only
        // runs between press and release, which observers never see.
        return digging_pitch_initial;
    case zombie_hand_tool_id:
        // ZombieHandTool.update/use_spade restore the pre-call pitch, so it
        // never leaves pitch_initial.
        return digging_pitch_initial;
    default:
        break;
    }
    const double increase = digging_pitch_increase(tool_id);
    if (!(increase > 0.0)) {
        // Tool.pitch = pitch_initial (0) and pitch_increase is 0 for every
        // non-digging tool, so Weapon/BlockTool/... class `pitch` attributes
        // are overwritten in Tool.__init__ and never reach Character.draw.
        return 0.0;
    }
    const double length = digging_length(tool_id);
    if (!(seconds_since_primary >= 0.0) || seconds_since_primary >= length) {
        return digging_pitch_initial;
    }
    // DiggingTool.use_spade(): the overshoot is measured against the resting
    // range, because animation_timer still equals shoot_interval there.
    const double rest_maximum =
        retail_tool_arm_pitch_range(tool_id, 1.0e9).maximum;
    const double offset =
        std::max(aim_pitch_degrees + increase - rest_maximum, 0.0);
    const double from = digging_pitch_initial + increase - offset;
    const double to = digging_pitch_initial - offset;
    return from + (to - from) * (seconds_since_primary / length);
}

RetailThirdPersonPose evaluate_retail_third_person_pose(std::uint8_t tool_id,
                                                        std::size_t tool_part_count,
                                                        double seconds_since_primary,
                                                        std::uint64_t action_serial,
                                                        double aim_pitch_degrees,
                                                        bool can_display_weapon,
                                                        double mechanism_phase) noexcept {
    RetailThirdPersonPose result;
    result.head_pitch_degrees = aim_pitch_degrees;
    // Character.draw (character.pyx 1833-1840):
    //     if can_display_weapon: pitch += shoot_pitch   (Tool.get_pitch())
    //     else:                  pitch += 50
    //     pitch = clamp(pitch, *weapon_object.get_arm_pitch_range())
    // The +50 (dword_10098244 = PyInt 50) drops the empty hands to the
    // sides whenever the tool is hidden, e.g. while sprinting.
    const double arm_pitch =
        aim_pitch_degrees +
        (can_display_weapon
             ? retail_tool_pitch(tool_id, seconds_since_primary, aim_pitch_degrees)
             : hidden_tool_arm_pitch_degrees);
    const auto range = retail_tool_arm_pitch_range(tool_id, seconds_since_primary);
    result.weapon_pitch_degrees = std::clamp(arm_pitch, range.minimum, range.maximum);

    // Character.reset_tp_arms(), character.pyx lines 894-925. These are four
    // independent ModelDisplay objects even though each side reuses the same
    // class upper/lower KV6 meshes. Character.draw calls set_pitch() on each
    // arm DisplayList after reset_tp_arms(), so the shoulder stays fixed.
    result.arms = {{
        {{-0.49, -0.15, 0.50}, {-0.01, 6.50, 0.01}, 0.0, 0.0, 0.0, 0.0, 0.0},
        {{-0.49, -0.15, 0.50}, {0.00, 13.0, 0.00}, 0.0, 0.0, 0.0, 0.0, 0.0},
        {{0.49, -0.15, 0.50}, {0.75, 8.50, 0.00}, 0.0, -20.0, 0.0, 0.0, 0.0},
        {{-0.03, -0.155, 0.50}, {0.00, 16.5, 0.00}, 0.0, 0.0, 0.0, -65.0, 0.0},
    }};
    for (auto& arm : result.arms) {
        arm.pitch_degrees = result.weapon_pitch_degrees;
    }

    // Character.draw (0x1004F120): player arms draw at pyx 1891-1895;
    // can_display_weapon is checked separately for the held tool at 1908.
    result.tool_part_count = can_display_weapon
                                ? std::min(tool_part_count, RetailThirdPersonPose::maximum_tool_parts)
                                : 0U;
    // Character.weapon is a DisplayList rooted at (0, 0, 0.5). A Tool's model
    // transforms are children of it, but Tool.apply_transform translates them
    // before DisplayList draws/scales the KV6. Both translations are therefore
    // world-sized; only the model vertices receive model_size.
    //
    // Remote characters use the non-main initial_position (zero for almost
    // every tool; RPG2 lifts 0.3, the rocket turret spreads its three parts,
    // the minigun seats its barrel at (0,-0.3,1.1)) plus every playing
    // Animation, because the generic branch calls apply_transform(i) with its
    // default apply_animation_position/orientation=True.
    const auto hold = retail_tool_hold(tool_id, false);
    const auto animation =
        tool_id == minigun_tool_id
            // Character.draw's MINIGUN branch only reads the barrel roll out of
            // apply_animations; AnimWeaponShoot never moves the observer model.
            ? RetailModelPose{}
            : evaluate_retail_tool_animation(tool_id, seconds_since_primary, false);
    for (std::size_t part{}; part < result.tool_parts.size(); ++part) {
        result.tool_parts[part].position =
            add(hold.parts[part].position, animation.position);
        result.tool_parts[part].orientation_degrees =
            add(hold.parts[part].orientation_degrees, animation.orientation_degrees);
    }
    if (const auto* weapon = find_weapon_definition(tool_id); weapon != nullptr) {
        result.tool_model_scale = weapon->retail.use.model_size.value_or(0.065);
    }

    if (tool_id == minigun_tool_id && result.tool_part_count >= 2U) {
        // Character.draw's MINIGUN branch (ch.pyd 0x100567D5-0x100576xx):
        // per part it brackets a glTranslatef(size * (-7, -6.5, 27.5)) with
        // the weapon DisplayList rotations, then set_roll(orientation.z) from
        // apply_animations(i) -- AnimRoll exists only on the barrel. The net
        // effect is the barrel spinning about its axis through that pivot, not
        // about the KV6 origin. `mechanism_phase` is the observer's barrel
        // revolution fraction (0 = at rest).
        const double roll = std::fmod(std::clamp(mechanism_phase, 0.0, 1.0), 1.0) * 360.0;
        if (roll != 0.0) {
            constexpr double to_radians = 3.14159265358979323846 / 180.0;
            const double c = std::cos(roll * to_radians);
            const double s = std::sin(roll * to_radians);
            const double pivot_x = minigun_barrel_pivot[0U] * result.tool_model_scale;
            const double pivot_y = minigun_barrel_pivot[1U] * result.tool_model_scale;
            auto& barrel = result.tool_parts[1U];
            barrel.orientation_degrees.z += roll;
            // Row-vector Rz about the pivot: p' = p*Rz + (q - q*Rz).
            barrel.position.x += pivot_x - (pivot_x * c - pivot_y * s);
            barrel.position.y += pivot_y - (pivot_x * s + pivot_y * c);
        }
    }

    // ZombieHandTool and ZombiePrefabTool provide the visible arms themselves.
    // Drawing class arms as well creates the doubled/disconnected limb bug.
    result.draws_player_arms = tool_id != zombie_hand_tool_id && tool_id != zombie_prefab_tool_id;

    if (tool_id == zombie_hand_tool_id) {
        // ZombieHandTool.model_scale = 0.5. It applies animation orientation
        // only to the hand used by the latest attack for remote characters;
        // the other hand remains at its authored attachment transform.
        result.tool_model_scale *= 0.5;
        for (auto& part : result.tool_parts) {
            part = {};
        }
        if (action_serial > 0U && result.tool_part_count >= 2U) {
            const auto active_hand = static_cast<std::size_t>((action_serial - 1U) % 2U);
            // Third-person ZombieHandTool passes
            // apply_animation_position=False but keeps orientation=True.
            result.tool_parts[active_hand].orientation_degrees = animation.orientation_degrees;
        }
    } else if (tool_id == zombie_prefab_tool_id) {
        // Character.draw does not run the generic apply_transform loop for a
        // Zombie holding ZombiePrefabTool: a special branch draws the right
        // hand, the block and the left hand at fixed transforms with the
        // weapon size forced to 0.04, and no AnimPlaceBlock.
        result.tool_model_scale = zombie_prefab_model_size;
        for (std::size_t part{}; part < zombie_prefab_parts.size(); ++part) {
            result.tool_parts[part].position = zombie_prefab_parts[part][0U];
            result.tool_parts[part].orientation_degrees = zombie_prefab_parts[part][1U];
        }
    }
    return result;
}

RetailRemoteMinigunSpin advance_retail_remote_minigun_spin(RetailRemoteMinigunSpin state,
                                                            double dt,
                                                            bool trigger_held) noexcept {
    // MINIGUN_SHOOT_INTERVAL_{ACTIVE,INACTIVE}_ALTERATION_PER_SECOND over
    // MINIGUN_SHOOT_INTERVAL_RANGE, exactly as WeaponRuntime's local model.
    constexpr double interval_range{0.2};
    constexpr double active_alteration{0.15};
    constexpr double inactive_alteration{0.075};
    constexpr double barrel_spin_speed_max{5.0};
    if (!std::isfinite(dt) || dt <= 0.0) {
        return state;
    }
    const double rate = (trigger_held ? active_alteration : -inactive_alteration) / interval_range;
    state.ratio = std::clamp(state.ratio + rate * dt, 0.0, 1.0);
    state.phase = std::fmod(state.phase + state.ratio * barrel_spin_speed_max * dt, 1.0);
    if (state.phase < 0.0 || !std::isfinite(state.phase)) {
        state.phase = 0.0;
    }
    return state;
}

} // namespace battlespades::world
