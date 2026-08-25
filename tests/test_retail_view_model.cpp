#include "battlespades/world/retail_view_model.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

using battlespades::world::RetailArmPart;
using battlespades::world::RetailViewModelInput;
using battlespades::world::TutorialTool;
using battlespades::world::WeaponViewModelInput;
using battlespades::world::compose_weapon_view_model_input;
using battlespades::world::evaluate_retail_view_model;
using battlespades::world::evaluate_weapon_view_model;
using battlespades::world::evaluate_weapon_sight;

void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

void expect_near(double actual, double expected, const char* message) {
    if (std::fabs(actual - expected) > 1.0e-9) {
        throw std::runtime_error{message};
    }
}

void recovered_arm_rig_is_not_fitted_screen_space() {
    const auto pose = evaluate_retail_view_model({TutorialTool::pistol});
    expect_near(pose.model_scale, 0.05,
                "retail Tool.view_model_size must remain in the oracle");
    expect_near(pose.character_offset.x, -0.4,
                "draw_fps character x offset must match character.pyd");
    expect_near(pose.character_offset.y, -0.55,
                "draw_fps character y offset must match character.pyd");
    expect_near(pose.character_offset.z, 0.9,
                "draw_fps character z offset must match character.pyd");
    expect_near(pose.character_yaw_degrees, 180.0,
                "draw_fps handedness flip must match character.pyd");
    expect_near(pose.arms_anchor.x, 0.401,
                "shared arm anchor x must match character.pyd");
    expect_near(pose.arms_anchor.y, -0.01,
                "shared arm anchor y must match character.pyd");
    expect_near(pose.arms_anchor.z, -0.801,
                "shared arm anchor z must match character.pyd");
    const auto& upper = pose.arm_parts[static_cast<std::size_t>(RetailArmPart::left_upper)];
    const auto& left = pose.arm_parts[static_cast<std::size_t>(RetailArmPart::left_lower)];
    const auto& right = pose.arm_parts[static_cast<std::size_t>(RetailArmPart::right_lower)];
    expect_near(upper.position.x, 0.29, "retail upper-arm x must match character.pyd");
    expect_near(upper.position.y, -0.059, "retail upper-arm y must match character.pyd");
    expect_near(upper.position.z, 0.10, "retail upper-arm z must match character.pyd");
    expect_near(upper.yaw_degrees, -25.0, "retail upper-arm yaw must match character.pyd");
    expect_near(left.position.x, -0.12, "retail left-lower x must match character.pyd");
    expect_near(left.position.z, 0.60, "retail left-lower z must match character.pyd");
    expect_near(left.yaw_degrees, -50.0, "retail left-lower yaw must match character.pyd");
    expect_near(right.position.x, -0.48, "retail right-lower x must match character.pyd");
    expect_near(right.position.z, 0.48, "retail right-lower z must match character.pyd");
    expect_near(right.yaw_degrees, 0.0, "retail right-lower arm has no local yaw");
}

void all_weapons_pullout_moves_xy_without_corrupting_depth() {
    const auto input = compose_weapon_view_model_input(
        17U, 1.0e9, 0U, {0.2, -0.1, 0.3}, 0.25, 0.75);
    expect_near(input.sway_x, -1.05,
                "F4 pullout must move the weapon left on X");
    expect_near(input.sway_y, -1.35,
                "F4 pullout must move the weapon down on Y");
    expect_near(input.sway_z, 0.3,
                "F4 pullout must not push the hands along camera depth");
    expect_near(input.mechanism_phase, 0.75,
                "pullout composition must preserve mechanism animation");
    const auto pose = evaluate_weapon_view_model(input);
    expect_near(pose.tool_sway.x, input.sway_x,
                "all-weapons tool must inherit the same pullout X as its hands");
    expect_near(pose.tool_sway.y, input.sway_y,
                "all-weapons tool must inherit the same pullout Y as its hands");
    expect_near(pose.tool_sway.z, input.sway_z,
                "all-weapons tool and hands must share unmodified depth");
    expect_near(pose.arms_position.x - pose.tool.position.x, pose.tool_sway.x,
                "all-weapons hands and tool must share the pullout X root");
    expect_near(pose.arms_position.y - pose.tool.position.y, pose.tool_sway.y,
                "all-weapons hands and tool must share the pullout Y root");
    expect_near(pose.arms_position.z - pose.tool.position.z, pose.tool_sway.z,
                "all-weapons hands and tool must share the pullout depth root");
}

void block_pose_and_arm_cancellation_match_source() {
    RetailViewModelInput input;
    input.tool = TutorialTool::block;
    input.seconds_since_primary = 1.0;
    input.sway_x = 0.2;
    input.sway_y = -0.1;
    input.sway_z = 0.3;
    const auto pose = evaluate_retail_view_model(input);
    expect_near(pose.tool.position.x, -0.04, "BlockTool initial x must be recovered");
    expect_near(pose.tool.position.z, 0.3, "BlockTool initial z must be recovered");
    expect_near(pose.tool.orientation_degrees.y, 45.0,
                "BlockTool initial yaw must be recovered");
    // arms_position_offset=(+0.04,0,-0.3) cancels the static tool hold.
    expect_near(pose.arms_position.x, 0.2, "block arm x must contain sway only at rest");
    expect_near(pose.arms_position.y, -0.1, "block arm y must contain sway only at rest");
    expect_near(pose.arms_position.z, 0.3, "block arm z must contain sway only at rest");

    input.seconds_since_primary = 0.0;
    const auto placed = evaluate_retail_view_model(input);
    expect_near(placed.tool.position.x, -0.54, "AnimPlaceBlock starts half a unit left");
    expect_near(placed.tool.position.y, -0.5, "AnimPlaceBlock starts half a unit down");
}

void spade_start_and_updated_curves_preserve_retail_discontinuity() {
    RetailViewModelInput input;
    input.tool = TutorialTool::spade;
    input.seconds_since_primary = 0.0;
    const auto started = evaluate_retail_view_model(input);
    expect_near(started.tool.position.x, -0.0375, "AnimUseSpade.start x must match source");
    expect_near(started.tool.position.y, 0.4 / 1.3,
                "AnimUseSpade.start y must match source");
    expect_near(started.tool.position.z, -1.0 / 3.0,
                "AnimUseSpade.start z must match source");
    expect_near(started.tool.orientation_degrees.x, -75.0,
                "AnimUseSpade.start pitch must match source");
    expect_near(started.tool.orientation_degrees.z, 12.0,
                "AnimUseSpade.start roll must match source");
    expect_near(started.digging_pitch_degrees, 36.0,
                "DiggingTool pitch starts at pitch_initial+increase");
    expect_near(started.arm_rotation_ratio, 0.25,
                "spade animation rotates arms by the recovered quarter ratio");

    input.seconds_since_primary = 1.0 / 60.0;
    const auto updated = evaluate_retail_view_model(input);
    expect(updated.tool.position.x > 0.0 && updated.tool.position.y < 0.0 &&
               updated.tool.position.z > 0.0,
           "AnimUseSpade.update must flip the signed start pose");
    expect(updated.tool.orientation_degrees.x > 0.0 &&
               updated.tool.orientation_degrees.z < 0.0,
           "AnimUseSpade.update orientation signs must match source");
}

void pistol_animation_starts_identity_then_recoils() {
    RetailViewModelInput input;
    input.tool = TutorialTool::pistol;
    input.seconds_since_primary = 0.0;
    const auto start = evaluate_retail_view_model(input);
    expect_near(start.tool.position.x, 0.0, "AnimWeaponShoot.start must be identity");
    expect_near(start.tool.orientation_degrees.x, 0.0,
                "AnimWeaponShoot.start orientation must be identity");

    input.seconds_since_primary = 1.0 / 60.0;
    const auto recoil = evaluate_retail_view_model(input);
    expect(recoil.tool.position.x < 0.0, "updated pistol animation must recoil negatively");
    expect(recoil.tool.orientation_degrees.x < 0.0,
           "updated pistol animation must pitch by the recovered curve");
}

void full_catalog_uses_recovered_animation_families() {
    const auto knife = evaluate_weapon_view_model({1U, 0.0});
    expect_near(knife.tool.position.y, -0.5,
                "AnimUseKnife must start half a unit down");
    expect_near(knife.tool.position.z, 0.4,
                "AnimUseKnife must start forward");
    expect_near(knife.tool.orientation_degrees.x, 120.0,
                "AnimUseKnife must start at 120 degree pitch");

    const auto grenade = evaluate_weapon_view_model({11U, 0.2});
    expect_near(grenade.tool.position.x, -0.3,
                "AnimThrowGrenade x curve must match source");
    expect_near(grenade.tool.position.y, 0.1,
                "AnimThrowGrenade y curve must match source");
    expect_near(grenade.tool.orientation_degrees.x, -8.0,
                "AnimThrowGrenade pitch curve must match source");

    const auto c4 = evaluate_weapon_view_model({59U, 0.0});
    expect_near(c4.tool.position.x, -1.0,
                "C4 must reuse its one-second AnimPlaceBlock curve");
    const auto assault = evaluate_weapon_view_model({60U, 0.1});
    expect_near(assault.model_scale, 0.035,
                "assault rifle must use its recovered view_model_size");
    expect(assault.tool.position.x < 0.0,
           "assault rifle must use the common weapon recoil animation");

    for (std::uint8_t tool_id{}; tool_id < 65U; ++tool_id) {
        const auto pose = evaluate_weapon_view_model(
            WeaponViewModelInput{tool_id, 1.0 / 60.0});
        expect(std::isfinite(pose.model_scale) && pose.model_scale > 0.0,
               "every selectable tool must evaluate a finite viewmodel pose");
    }
}

void zombie_hands_are_multipart_and_alternate() {
    const auto resting = evaluate_weapon_view_model({24U, 1.0e9, 0U});
    expect(!resting.draws_player_arms,
           "ZombieHandTool must suppress normal FPS class arms");
    expect_near(resting.model_scale, 0.05,
                "Zombie FPS hands must retain the live view_weapon size");
    expect(resting.tool_part_count == 2U,
           "ZombieHandTool must retain both recovered view models");
    expect_near(resting.tool_parts[0U].position.z, -0.25,
                "right Zombie hand initial z must match source");
    expect_near(resting.tool_parts[1U].position.z, -0.25,
                "left Zombie hand initial z must match source");

    const auto first = evaluate_weapon_view_model({24U, 0.05, 1U});
    expect(first.tool_parts[0U].orientation_degrees.x != 0.0,
           "first Zombie attack must animate the right hand");
    expect_near(first.tool_parts[1U].orientation_degrees.x, 0.0,
                "first Zombie attack must keep left hand static");
    const auto second = evaluate_weapon_view_model({24U, 0.05, 2U});
    expect_near(second.tool_parts[0U].orientation_degrees.x, 0.0,
                "second Zombie attack must keep right hand static");
    expect(second.tool_parts[1U].orientation_degrees.x != 0.0,
           "second Zombie attack must animate the left hand");
}

void zombie_prefab_uses_its_recovered_hand_and_block_pose() {
    const auto resting = evaluate_weapon_view_model({28U, 1.0e9, 0U});
    expect(!resting.draws_player_arms,
           "ZombiePrefabTool must suppress ordinary class arms");
    expect(resting.tool_part_count == 2U,
           "Zombie prefab FPS must intentionally contain only hand and block");
    expect_near(resting.model_scale, 0.05,
                "Zombie prefab FPS parts must retain PrefabTool view size");
    expect_near(resting.tool_parts[0U].position.x, 0.0,
                "Zombie prefab hand must replace PrefabTool's generic x offset");
    expect_near(resting.tool_parts[0U].orientation_degrees.z, 180.0,
                "Zombie prefab hand must face the recovered direction");
    expect_near(resting.tool_parts[1U].position.y, 0.2,
                "Zombie prefab block must use its recovered raised y offset");
    expect_near(resting.tool_parts[1U].position.z, 0.8,
                "Zombie prefab block must use its recovered depth offset");
    expect_near(resting.tool_parts[1U].orientation_degrees.y, 45.0,
                "Zombie prefab block must retain its 45-degree yaw");
    expect_near(resting.tool_parts[1U].orientation_degrees.z, 30.0,
                "Zombie prefab block must retain its 30-degree roll");

    const auto placing = evaluate_weapon_view_model({28U, 0.0, 1U});
    expect_near(placing.tool_parts[0U].position.x, -0.5,
                "Zombie prefab hand must inherit AnimPlaceBlock x motion");
    expect_near(placing.tool_parts[0U].position.y, -0.5,
                "Zombie prefab hand must inherit AnimPlaceBlock y motion");
    expect_near(placing.tool_parts[1U].position.x, -0.5,
                "Zombie prefab block must inherit AnimPlaceBlock x motion");
    expect_near(placing.tool_parts[1U].position.y, -0.3,
                "block animation must add to its authored y offset");
}

void minigun_barrel_uses_its_recovered_local_pose() {
    const auto resting = evaluate_weapon_view_model({8U, 1.0e9, 0U});
    expect(resting.tool_part_count == 2U,
           "minigun must retain body and barrel view models");
    expect_near(resting.tool_parts[0U].position.y, 0.0,
                "minigun body must stay at the base tool origin");
    expect_near(resting.tool_parts[1U].position.y, -0.3,
                "minigun barrel y must match MinigunWeapon.__init__");
    expect_near(resting.tool_parts[1U].position.z, 1.1,
                "minigun barrel z must match MinigunWeapon.__init__");

    const auto spinning = evaluate_weapon_view_model({8U, 1.0e9, 0U, 0.0, 0.0,
                                                       0.0, 0.5});
    expect_near(spinning.tool_parts[1U].orientation_degrees.z, 180.0,
                "minigun AnimRoll must rotate only the offset barrel");
    expect_near(spinning.tool_parts[0U].orientation_degrees.z, 0.0,
                "minigun body must not inherit the barrel roll");
}

void block_gadget_holds_match_retail_tool_classes() {
    const auto cannon = evaluate_weapon_view_model({29U, 1.0e9, 0U});
    expect_near(cannon.tool.position.x, 0.0,
                "Block Cannon initial x must match SnowBlowerWeapon");
    expect_near(cannon.tool.position.y, 0.1,
                "Block Cannon initial y must match SnowBlowerWeapon");
    expect_near(cannon.arms_position.y, 0.0,
                "Block Cannon arm offset must cancel its initial y");

    const auto sucker = evaluate_weapon_view_model({63U, 1.0e9, 0U});
    expect_near(sucker.arms_position.x, -0.3,
                "Block Sucker arms x must match BlockSuckerWeapon");
    expect_near(sucker.arms_position.y, -0.1,
                "Block Sucker arms y must match BlockSuckerWeapon");
    expect_near(sucker.arms_position.z, 0.4,
                "Block Sucker arms z must match BlockSuckerWeapon");

    const auto disguise = evaluate_weapon_view_model({64U, 1.0e9, 0U});
    expect_near(disguise.tool.position.x, -0.1,
                "Disguise initial x must match DisguiseTool");
    expect_near(disguise.tool.position.z, 0.05,
                "Disguise initial z must match DisguiseTool");
    expect_near(disguise.arms_position.x, -0.1,
                "Disguise arms must inherit the tool x anchor");
    expect_near(disguise.arms_position.y, 0.01,
                "Disguise arms y must include its recovered offset");
    expect_near(disguise.arms_position.z, 0.05,
                "Disguise arms must inherit the tool z anchor");
}

void rocket_launchers_use_the_recovered_fps_hold() {
    for (const auto tool_id : {std::uint8_t{12U}, std::uint8_t{13U},
                               std::uint8_t{46U}}) {
        const auto launcher = evaluate_weapon_view_model({tool_id, 1.0e9, 0U});
        expect_near(launcher.tool.position.y, 0.1,
                    "rocket launcher initial y must match its Python tool class");
        expect_near(launcher.arms_position.y, 0.0,
                    "rocket launcher arm offset must cancel its initial y");
    }
}

void sight_is_an_independent_identity_space_model() {
    const auto pistol = evaluate_weapon_sight(17U);
    expect_near(pistol.model_scale, 0.05, "draw_sight uses fixed retail scale");
    expect_near(pistol.position.x, 0.025, "pistol sight x offset must match retail");
    expect_near(pistol.position.y, -0.45, "pistol sight y must include -0.35");
    expect_near(pistol.position.z, 0.85, "pistol sight z must include +1.85");
    expect_near(pistol.yaw_degrees, 180.0, "sight render must retain handedness flip");

    const auto sniper = evaluate_weapon_sight(18U);
    expect_near(sniper.position.x, 0.025, "sniper sight x must match retail");
    expect_near(sniper.position.y, -0.025, "sniper sight y must match retail");
    expect_near(sniper.position.z, 1.85, "sniper sight z must match retail");
}

} // namespace

int main() {
    try {
        recovered_arm_rig_is_not_fitted_screen_space();
        all_weapons_pullout_moves_xy_without_corrupting_depth();
        block_pose_and_arm_cancellation_match_source();
        spade_start_and_updated_curves_preserve_retail_discontinuity();
        pistol_animation_starts_identity_then_recoils();
        full_catalog_uses_recovered_animation_families();
        zombie_hands_are_multipart_and_alternate();
        zombie_prefab_uses_its_recovered_hand_and_block_pose();
        minigun_barrel_uses_its_recovered_local_pose();
        block_gadget_holds_match_retail_tool_classes();
        rocket_launchers_use_the_recovered_fps_hold();
        sight_is_an_independent_identity_space_model();
        std::cout << "retail viewmodel parity tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
