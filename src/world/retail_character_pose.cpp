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

[[nodiscard]] RetailModelPose zombie_hand_animation(double elapsed, double length) noexcept {
    RetailModelPose result;
    if (elapsed < 0.0 || elapsed >= length) {
        return result;
    }
    const double timer = length - elapsed;
    const double factor = timer * 6.0;
    if (elapsed == 0.0) {
        result.position = {0.0, -factor / 3.0, -factor / 6.5};
        result.orientation_degrees.x = -(40.0 - length * 200.0);
    } else {
        result.position = {0.0, factor / 3.0, factor / 6.5};
        result.orientation_degrees.x = 40.0 - timer * 200.0;
    }
    return result;
}

[[nodiscard]] RetailModelPose place_block_animation(double elapsed,
                                                     double length) noexcept {
    RetailModelPose result;
    if (elapsed < 0.0 || elapsed >= length) {
        return result;
    }
    const double timer = length - elapsed;
    result.position = {-timer, -timer, 0.0};
    return result;
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
    const double forward =
        triangle * (forward_x * velocity.x + forward_y * velocity.y) * scale;
    const double lateral =
        triangle * (side_x * velocity.x + side_y * velocity.y) * scale;
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

RetailThirdPersonPose evaluate_retail_third_person_pose(std::uint8_t tool_id,
                                                        std::size_t tool_part_count,
                                                        double seconds_since_primary,
                                                        std::uint64_t action_serial,
                                                        double aim_pitch_degrees,
                                                        bool can_display_weapon) noexcept {
    RetailThirdPersonPose result;
    result.head_pitch_degrees = aim_pitch_degrees;
    // Character.draw clamps the equipped tool's get_pitch()-adjusted value to
    // Tool.get_arm_pitch_range(). Ordinary firearms return zero adjustment;
    // attack-specific digging/grenade pitch remains handled by their action
    // pose and can be added here when that packet state is available.
    result.weapon_pitch_degrees = std::clamp(aim_pitch_degrees, -90.0, 70.0);

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
    if (tool_id == minigun_tool_id && result.tool_part_count >= 2U) {
        // minigunWeapon.py assigns its rotating barrel display a local
        // (0,-0.3,+1.1) position before the common Character weapon anchor.
        // Omitting this in third person leaves the barrel inside the torso.
        result.tool_parts[1U].position.y = -0.3;
        result.tool_parts[1U].position.z = 1.1;
    }
    if (const auto* weapon = find_weapon_definition(tool_id); weapon != nullptr) {
        result.tool_model_scale = weapon->retail.use.model_size.value_or(0.065);
    }

    // ZombieHandTool and ZombiePrefabTool provide the visible arms themselves.
    // Drawing class arms as well creates the doubled/disconnected limb bug.
    result.draws_player_arms = tool_id != zombie_hand_tool_id && tool_id != zombie_prefab_tool_id;

    if (tool_id == zombie_hand_tool_id) {
        // ZombieHandTool.model_scale = 0.5. It applies animation orientation
        // only to the hand used by the latest attack for remote characters;
        // the other hand remains at its authored attachment transform.
        result.tool_model_scale *= 0.5;
        if (action_serial > 0U && result.tool_part_count >= 2U) {
            const auto active_hand = static_cast<std::size_t>((action_serial - 1U) % 2U);
            const auto* weapon = find_weapon_definition(tool_id);
            const double length = weapon != nullptr ? std::max(weapon->fire_interval, 0.001) : 0.4;
            const auto animation = zombie_hand_animation(seconds_since_primary, length);
            // Third-person ZombieHandTool passes
            // apply_animation_position=False but keeps orientation=True.
            result.tool_parts[active_hand].orientation_degrees = animation.orientation_degrees;
        }
    } else if (tool_id == zombie_prefab_tool_id) {
        // ZombiePrefabTool.model contains right hand, block, left hand while
        // its shorter view_model contains only the first two. Tool stores its
        // transform arrays at view_model length, so retail applies
        // AnimPlaceBlock to model indices 0/1 and leaves index 2 at its
        // authored ZombieHandLeft attachment. This asymmetry is intentional.
        const auto* weapon = find_weapon_definition(tool_id);
        const double length =
            weapon != nullptr ? std::max(weapon->fire_interval, 0.001) : 0.5;
        const auto animation = place_block_animation(seconds_since_primary, length);
        const auto animated_parts = std::min<std::size_t>(result.tool_part_count, 2U);
        for (std::size_t part{}; part < animated_parts; ++part) {
            result.tool_parts[part].position = animation.position;
            result.tool_parts[part].orientation_degrees = animation.orientation_degrees;
        }
    }
    return result;
}

} // namespace battlespades::world
