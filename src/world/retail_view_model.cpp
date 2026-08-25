#include "battlespades/world/retail_view_model.hpp"
#include "battlespades/world/weapon_catalog.hpp"

#include <algorithm>

namespace battlespades::world {
namespace {

constexpr std::uint8_t pistol_tool_id{17U};
constexpr std::uint8_t spade_tool_id{2U};
constexpr std::uint8_t block_tool_id{5U};
constexpr std::uint8_t zombie_hand_tool_id{24U};
constexpr std::uint8_t zombie_prefab_tool_id{28U};

[[nodiscard]] constexpr ViewModelVector add(ViewModelVector left,
                                             ViewModelVector right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] RetailModelPose weapon_shoot_animation(double elapsed,
                                                      double length) noexcept {
    RetailModelPose result;
    // AnimWeaponShoot.start() explicitly starts at the identity pose. Its
    // first update uses the remaining timer, hence the strict elapsed > 0.
    if (elapsed <= 0.0 || elapsed >= length) {
        return result;
    }
    const double timer = length - elapsed;
    const double f = -timer / 32.0;
    result.position = {f * 10.0, f * 10.0, f * 10.0};
    result.orientation_degrees.x = f * 280.0;
    return result;
}

[[nodiscard]] RetailModelPose place_block_animation(double elapsed,
                                                     double length) noexcept {
    RetailModelPose result;
    if (elapsed < 0.0 || elapsed >= length) {
        return result;
    }
    const double timer = length - elapsed;
    // AnimPlaceBlock.start() = (-length,-length,0); update adds dt to x/y.
    result.position = {-timer, -timer, 0.0};
    return result;
}

[[nodiscard]] RetailModelPose use_spade_animation(double elapsed,
                                                  double length) noexcept {
    RetailModelPose result;
    if (elapsed < 0.0 || elapsed >= length) {
        return result;
    }
    const double timer = length - elapsed;
    double f{};
    double f2{};
    if (timer >= 0.6) {
        f2 = 0.0;
        f = 1.0 - timer;
    } else if (timer >= 0.3) {
        f2 = 0.6 - timer;
        f = 0.4;
    } else if (timer >= 0.1) {
        f2 = 0.3;
        f = 0.4;
    } else {
        f2 = timer * 3.0;
        f = timer * 4.0;
    }

    if (elapsed == 0.0) {
        // AnimUseSpade.start() uses the opposite signed pose; update() flips
        // it on the first simulation update. This discontinuity is retail.
        result.position = {-(f2 / 16.0 * 3.0), f / 1.3, -f2 / 0.6};
        result.orientation_degrees = {f / -0.32 * 60.0, 0.0, f * 30.0};
    } else {
        result.position = {f2 / 16.0 * 3.0, -(f / 1.3), f2 / 0.6};
        result.orientation_degrees = {-f / -0.32 * 60.0, 0.0, -f * 30.0};
    }
    return result;
}

[[nodiscard]] RetailModelPose use_melee_animation(double elapsed,
                                                  double length,
                                                  bool shield) noexcept {
    RetailModelPose result;
    if (elapsed < 0.0 || elapsed >= length || length <= 0.0) {
        return result;
    }
    const double progress = std::clamp(elapsed / length, 0.0, 1.0);
    result.position.z = 0.4 - progress * 0.4;
    if (!shield) {
        result.position.y = -0.5 + progress * 0.5;
        result.orientation_degrees.x = 120.0 - progress * 120.0;
    }
    return result;
}

[[nodiscard]] RetailModelPose throw_animation(double elapsed,
                                              double length,
                                              bool retain_end) noexcept {
    RetailModelPose result;
    if (elapsed < 0.0 || length <= 0.0 || (!retain_end && elapsed >= length)) {
        return result;
    }
    elapsed = std::min(elapsed, length);
    result.position.x = -elapsed * 1.5;
    result.position.y = elapsed * 0.5;
    result.orientation_degrees.x = elapsed * -40.0;
    return result;
}

[[nodiscard]] RetailModelPose zombie_hand_animation(double elapsed,
                                                    double length) noexcept {
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

[[nodiscard]] double digging_pitch(double elapsed, double length) noexcept {
    if (elapsed < 0.0 || elapsed >= length) {
        return -4.0;
    }
    return 36.0 - 40.0 * std::clamp(elapsed / length, 0.0, 1.0);
}

} // namespace

WeaponViewModelInput compose_weapon_view_model_input(
    std::uint8_t tool_id, double seconds_since_primary,
    std::uint64_t action_serial, ViewModelVector base_sway,
    double pullout_remaining, double mechanism_phase) noexcept {
    const double pullout = std::max(0.0, pullout_remaining);
    WeaponViewModelInput result;
    result.tool_id = tool_id;
    result.seconds_since_primary = seconds_since_primary;
    result.action_serial = action_serial;
    result.sway_x = base_sway.x - pullout * 5.0;
    result.sway_y = base_sway.y - pullout * 5.0;
    result.sway_z = base_sway.z;
    result.mechanism_phase = mechanism_phase;
    return result;
}

RetailViewModelPose
evaluate_retail_view_model(const RetailViewModelInput& input) noexcept {
    std::uint8_t tool_id{pistol_tool_id};
    switch (input.tool) {
    case TutorialTool::pistol:
        tool_id = pistol_tool_id;
        break;
    case TutorialTool::spade:
        tool_id = spade_tool_id;
        break;
    case TutorialTool::block:
        tool_id = block_tool_id;
        break;
    }
    return evaluate_weapon_view_model(
        {tool_id, input.seconds_since_primary, 0U, input.sway_x, input.sway_y,
         input.sway_z});
}

RetailViewModelPose
evaluate_weapon_view_model(const WeaponViewModelInput& input) noexcept {
    RetailViewModelPose result;
    // Recovered from character.pyd Character.draw_fps, lines 2167-2177.
    result.arm_parts = {{
        {{0.29, -0.059, 0.10}, -25.0},
        {{-0.12, -0.061, 0.60}, -50.0},
        {{-0.48, -0.06, 0.48}, 0.0},
    }};

    RetailModelPose animation;
    ViewModelVector arms_offset{};
    const auto* weapon = find_weapon_definition(input.tool_id);
    if (weapon == nullptr) {
        return result;
    }
    result.model_scale = weapon->retail.use.view_model_size.value_or(0.05);
    result.tool_sway = {input.sway_x, input.sway_y, input.sway_z};
    result.draws_player_arms =
        input.tool_id != zombie_hand_tool_id && input.tool_id != zombie_prefab_tool_id;
    // ZombieHandTool.model_scale=0.5 is applied by Character.draw to the
    // third-person weapon hierarchy. The live local character keeps
    // view_weapon.size at the inherited view_model_size (0.05), so applying
    // the tool scale here incorrectly halves and recentres both FPS hands.
    result.tool_part_count =
        std::min<std::size_t>(weapon->first_person_models.size(),
                              result.tool_parts.size());
    // Per-tool __init__ offsets recovered from the retail Python classes.
    // They are not KV6 pivots: the same translation also anchors the hands.
    if (input.tool_id == 12U || input.tool_id == 13U || input.tool_id == 46U ||
        input.tool_id == 29U || input.tool_id == 48U) {
        result.tool.position = {0.0, 0.1, 0.0};
        arms_offset = {0.0, -0.1, 0.0};
    } else if (input.tool_id == 63U) {
        arms_offset = {-0.3, -0.1, 0.4};
    } else if (input.tool_id == 64U) {
        result.tool.position = {-0.1, 0.0, 0.05};
        arms_offset = {0.0, 0.01, 0.0};
    }
    const double length = std::max(weapon->fire_interval, 0.001);
    switch (weapon->mechanism) {
    case WeaponMechanism::firearm_semi:
    case WeaponMechanism::firearm_automatic:
    case WeaponMechanism::firearm_burst:
    case WeaponMechanism::firearm_spinup:
    case WeaponMechanism::shotgun_semi:
    case WeaponMechanism::shotgun_automatic:
    case WeaponMechanism::deployed_machine_gun:
    case WeaponMechanism::oriented_launcher:
        animation = weapon_shoot_animation(input.seconds_since_primary, length);
        break;
    case WeaponMechanism::melee:
        if (input.tool_id == 2U || input.tool_id == 3U || input.tool_id == 4U ||
            input.tool_id == 45U) {
            animation = use_spade_animation(input.seconds_since_primary, length);
        } else if (input.tool_id == zombie_hand_tool_id) {
            animation = zombie_hand_animation(input.seconds_since_primary, length);
        } else {
            animation = use_melee_animation(input.seconds_since_primary, length,
                                             input.tool_id == 52U);
        }
        result.arm_rotation_ratio = 0.25;
        if (input.tool_id == 2U || input.tool_id == 3U || input.tool_id == 4U ||
            input.tool_id == 45U) {
            result.digging_pitch_degrees =
                digging_pitch(input.seconds_since_primary, length);
        }
        break;
    case WeaponMechanism::block_builder:
    case WeaponMechanism::flare_builder:
    case WeaponMechanism::prefab_builder:
    case WeaponMechanism::ugc_prefab_editor:
    case WeaponMechanism::ugc_entity:
    case WeaponMechanism::deployable:
    case WeaponMechanism::c4:
        if (input.tool_id == block_tool_id || input.tool_id == 22U ||
            input.tool_id == 27U) {
            result.tool.position = {-0.04, 0.0, 0.3};
            result.tool.orientation_degrees = {0.0, 45.0, 0.0};
            arms_offset = {0.04, 0.0, -0.3};
        }
        animation = place_block_animation(input.seconds_since_primary, length);
        break;
    case WeaponMechanism::cooked_throwable:
    case WeaponMechanism::charged_throwable:
        animation = throw_animation(input.seconds_since_primary,
                                    weapon->retail.use.fuse.value_or(length),
                                    weapon->mechanism ==
                                        WeaponMechanism::charged_throwable);
        break;
    case WeaponMechanism::inert:
    case WeaponMechanism::objective:
    case WeaponMechanism::paintbrush:
    case WeaponMechanism::block_sucker:
    case WeaponMechanism::disguise:
        break;
    }

    result.tool.position = add(result.tool.position, animation.position);
    result.tool.orientation_degrees =
        add(result.tool.orientation_degrees, animation.orientation_degrees);
    result.arms_position = add(add(result.tool.position, arms_offset),
                               {input.sway_x, input.sway_y, input.sway_z});
    // Tool.get_arms_orientation contains only the tool and animation Euler
    // offsets. DiggingTool.pitch is a separate Character transform.
    result.arms_orientation_degrees = result.tool.orientation_degrees;

    for (std::size_t index{}; index < result.tool_part_count; ++index) {
        result.tool_parts[index] = result.tool;
    }
    if (input.tool_id == 8U && result.tool_part_count >= 2U) {
        // MinigunWeapon.__init__ moves only Minigun_Barrel to
        // (0,-0.3,1.1), then apply_animations adds AnimRoll around its local Z.
        // Without this distinct initial pose the body and barrel overlap even
        // though their authored KV6 pivots are correct.
        result.tool_parts[1U].position =
            add(result.tool_parts[1U].position, {0.0, -0.3, 1.1});
        // RMB can spool without firing; primary continues the same phase into
        // live fire.
        result.tool_parts[1U].orientation_degrees.z +=
            std::clamp(input.mechanism_phase, 0.0, 1.0) * 360.0;
    }
    if (input.tool_id == zombie_prefab_tool_id && result.tool_part_count >= 2U) {
        // ZombiePrefabTool first lets PrefabTool install AnimPlaceBlock, then
        // replaces the two *local* view-model poses for the main character:
        // a flipped right hand and a raised/rolled block.  Its third-person
        // model has a third left hand, but view_model deliberately does not;
        // weapons/list.py even exempts this one tool from the equal-list-size
        // assertion.  Do not manufacture an extra FPS hand here.
        result.tool_parts[0U].orientation_degrees.z += 180.0;
        result.tool_parts[1U].position =
            add(result.tool_parts[1U].position, {0.0, 0.2, 0.8});
        result.tool_parts[1U].orientation_degrees =
            add(result.tool_parts[1U].orientation_degrees, {0.0, 45.0, 30.0});

        // Inherited PrefabTool.arms_position_offset. ZombiePrefabTool hides
        // ordinary arms, but keeping the semantic pose exact prevents a
        // future renderer or debug view from reintroducing the generic hold.
        result.arms_position =
            add(add(result.tool_parts[0U].position, {0.04, 0.0, -0.3}),
                {input.sway_x, input.sway_y, input.sway_z});
        result.arms_orientation_degrees = result.tool_parts[0U].orientation_degrees;
        result.tool = result.tool_parts[0U];
    }
    if (input.tool_id == zombie_hand_tool_id && result.tool_part_count >= 2U) {
        // ZombieHandTool applies AnimZombieHand only to last_used_hand. The
        // inactive hand keeps its initial (0,0,-0.25) view pose.
        for (std::size_t index{}; index < result.tool_part_count; ++index) {
            result.tool_parts[index] = {};
            result.tool_parts[index].position.z = -0.25;
        }
        if (input.action_serial > 0U) {
            const auto active_hand =
                static_cast<std::size_t>((input.action_serial - 1U) % 2U);
            result.tool_parts[active_hand].position =
                add(result.tool_parts[active_hand].position, animation.position);
            result.tool_parts[active_hand].orientation_degrees =
                animation.orientation_degrees;
        }
        result.arms_position.z += 0.25;
    }
    return result;
}

RetailSightPose evaluate_weapon_sight(std::uint8_t tool_id) noexcept {
    RetailSightPose result;
    const auto* weapon = find_weapon_definition(tool_id);
    if (weapon == nullptr) {
        return result;
    }
    const auto& sight = weapon->retail.use.sight_position;
    // character.pyx:2074-2082 resets the matrix, rotates 180 degrees around
    // Y, then draw_scaled(sight, .05, sx+.025, sy-.35, sz+1.85).
    result.position = {sight[0U] + 0.025, sight[1U] - 0.35,
                       sight[2U] + 1.85};
    // character.pyx:2083-2089 draws the pin from the ALREADY-OFFSET X/Y/Z,
    // not from sight_pos: a near rear notch and a far front post 2.1 units
    // apart. Only classicRifleWeapon.py:33 assigns one.
    result.has_pin = !weapon->pin_model.asset.empty();
    result.pin_scale = weapon->retail.use.pin_scale;
    result.pin_position = {result.position.x - 0.015, result.position.y + 0.3,
                           result.position.z + 2.1};
    return result;
}

} // namespace battlespades::world
