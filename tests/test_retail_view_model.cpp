#include "battlespades/world/retail_view_model.hpp"
#include "battlespades/world/retail_effects.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <iostream>
#include <stdexcept>

namespace {

using battlespades::world::RetailArmPart;
using battlespades::world::RetailViewModelInput;
using battlespades::world::TutorialTool;
using battlespades::world::ViewModelVector;
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
    // Stock SPADE_SHOOT_INTERVAL is 0.8 s (the 0.4 s value was the nonsteam
    // mod), so start() takes its `length >= 0.6` branch: f = 1 - 0.8, f2 = 0.
    expect_near(started.tool.position.x, 0.0, "AnimUseSpade.start x must match source");
    expect_near(started.tool.position.y, 0.2 / 1.3,
                "AnimUseSpade.start y must match source");
    expect_near(started.tool.position.z, 0.0,
                "AnimUseSpade.start z must match source");
    expect_near(started.tool.orientation_degrees.x, -37.5,
                "AnimUseSpade.start pitch must match source");
    expect_near(started.tool.orientation_degrees.z, 6.0,
                "AnimUseSpade.start roll must match source");
    expect_near(started.arm_rotation_ratio, 0.25,
                "spade animation rotates arms by the recovered quarter ratio");

    input.seconds_since_primary = 1.0 / 60.0;
    const auto updated = evaluate_retail_view_model(input);
    expect(updated.tool.position.y < 0.0,
           "AnimUseSpade.update must flip the signed start pose");
    input.seconds_since_primary = 0.3;
    const auto swinging = evaluate_retail_view_model(input);
    expect(swinging.tool.position.x > 0.0 && swinging.tool.position.y < 0.0 &&
               swinging.tool.position.z > 0.0,
           "AnimUseSpade.update mid-swing pose signs must match source");
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
    // AnimUseKnife.start() zeroes its pose and Tool.update runs before
    // use_primary, so the start frame is KnifeTool's rest hold; the first
    // update() jumps to the 0.4 / -0.5 / 120 degree wind-up.
    const auto knife_start = evaluate_weapon_view_model({1U, 0.0});
    expect_near(knife_start.tool.position.y, 0.03,
                "AnimUseKnife start frame must be the KnifeTool rest hold");
    expect_near(knife_start.tool.orientation_degrees.x, 0.0,
                "AnimUseKnife start frame must not be rotated");
    const auto knife = evaluate_weapon_view_model({1U, 1.0e-9});
    expect_near(knife.tool.position.y, 0.03 - 0.5 + 1.0e-9,
                "AnimUseKnife must wind up half a unit down");
    expect(std::fabs(knife.tool.position.z - (-0.05 + 0.4)) < 1.0e-6,
           "AnimUseKnife must wind up forward");
    expect(std::fabs(knife.tool.orientation_degrees.x - 120.0) < 1.0e-6,
           "AnimUseKnife must wind up at 120 degree pitch");

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
    expect_near(c4.tool.position.y, 0.18 - 1.0,
                "C4 place animation must add to its 0.18 initial y");
    const auto c4_mid = evaluate_weapon_view_model({59U, 0.5});
    expect_near(c4_mid.tool.position.x, -0.5 - 0.5 / 32.0 * 10.0,
                "C4 sums Weapon.use_primary's AnimWeaponShoot with AnimPlaceBlock");
    expect_near(c4_mid.tool.orientation_degrees.x, -0.5 / 32.0 * 280.0,
                "C4 recoil pitch comes from AnimWeaponShoot");
    const auto molotov = evaluate_weapon_view_model({33U, 2.0});
    expect_near(molotov.tool.position.x, -3.0,
                "Molotov charge uses AnimThrowGrenade(A1645 = 3 s)");
    const auto molotov_held = evaluate_weapon_view_model({33U, 10.0});
    expect_near(molotov_held.tool.position.x, -4.5,
                "stop_on_end=False keeps the fully drawn-back pose");
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

void flare_block_is_held_like_the_block_with_both_hands() {
    // flareBlockTool.py and blockTool.py share BLOCK_VIEW_MODEL, the
    // (-0.04, 0, 0.3) / 45-degree hold, arms_position_offset (0.04, 0, -0.3)
    // and AnimPlaceBlock: the Flare Block's first-person frame is the block
    // tool's, hands included, at rest and mid-placement.
    for (const double since : {1.0e9, 0.0, 0.2}) {
        const auto block = evaluate_weapon_view_model({5U, since, 0U});
        const auto flare = evaluate_weapon_view_model({22U, since, 0U});
        expect(flare.draws_player_arms, "the Flare Block is held in the class hands");
        expect(flare.tool_part_count == 1U && block.tool_part_count == 1U,
               "the Flare Block draws the one block model");
        expect_near(flare.model_scale, block.model_scale, "flare view size is the block's");
        expect_near(flare.arm_model_scale, block.arm_model_scale, "flare arm size");
        expect_near(flare.arm_rotation_ratio, block.arm_rotation_ratio, "flare arm ratio");
        for (const auto axis : {0, 1, 2}) {
            const auto pick = [axis](const ViewModelVector& v) {
                return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
            };
            expect_near(pick(flare.tool.position), pick(block.tool.position),
                        "flare hold position is the block's");
            expect_near(pick(flare.tool.orientation_degrees),
                        pick(block.tool.orientation_degrees),
                        "flare hold orientation is the block's");
            expect_near(pick(flare.arms_position), pick(block.arms_position),
                        "flare hands sit where the block's do");
            expect_near(pick(flare.arms_orientation_degrees),
                        pick(block.arms_orientation_degrees),
                        "flare hands turn with the block's");
        }
    }
    const auto rest = evaluate_weapon_view_model({22U, 1.0e9, 0U});
    expect_near(rest.tool.orientation_degrees.y, 45.0, "FlareBlockTool initial yaw 45");
    expect_near(rest.arms_position.x, 0.0, "the flare arm offset cancels its hold x");
    expect_near(rest.arms_position.z, 0.0, "the flare arm offset cancels its hold z");
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

void riot_shield_uses_its_recovered_fps_hold_and_bash() {
    // riotShieldTool.py: view_model_size 0.18, main-character initial_position
    // (0.45,-0.6,-0.2), arms_position_offset (0.05,-0.03,0.05), and
    // AnimUseRiotShield(shoot_interval = A1881 = 1 s).
    const auto resting = evaluate_weapon_view_model({52U, 1.0e9, 0U});
    expect_near(resting.model_scale, 0.18, "riot shield must use its 0.18 view size");
    expect_near(resting.arm_model_scale, 0.05,
                "fps arms keep BODY_PARTS_SIZE; the shield's 0.18 must not grow them");
    expect_near(resting.tool.position.x, 0.45, "shield initial x must match RiotShieldTool");
    expect_near(resting.tool.position.y, -0.6, "shield initial y must match RiotShieldTool");
    expect_near(resting.tool.position.z, -0.2, "shield initial z must match RiotShieldTool");
    expect_near(resting.arms_position.x, 0.5, "shield arms x = initial + offset");
    expect_near(resting.arms_position.y, -0.63, "shield arms y = initial + offset");
    expect_near(resting.arms_position.z, -0.15, "shield arms z = initial + offset");
    expect_near(resting.tool.orientation_degrees.x, 0.0, "shield rests unrotated");

    const auto started = evaluate_weapon_view_model({52U, 0.0, 1U});
    expect_near(started.tool.position.z, -0.2,
                "AnimUseRiotShield.start() draws the rest pose before its first update");
    const auto bashing = evaluate_weapon_view_model({52U, 0.25, 1U});
    expect_near(bashing.tool.position.z, -0.2 + 0.3, "bash eases z from +0.4 to 0 over 1 s");
    expect_near(bashing.tool.position.y, -0.6, "the shield bash is a pure forward thrust");
    expect_near(bashing.tool.orientation_degrees.x, 0.0, "the shield bash never tilts");
    expect_near(bashing.arms_position.z, -0.15 + 0.3, "hands follow the shield thrust");
    const auto finished = evaluate_weapon_view_model({52U, 1.0, 1U});
    expect_near(finished.tool.position.z, -0.2, "bash ends back at the rest pose");

    // Every tool shares the fixed arm size; only the tool mesh changes scale.
    for (const auto tool_id : {std::uint8_t{1U}, std::uint8_t{51U}, std::uint8_t{60U}}) {
        const auto pose = evaluate_weapon_view_model({tool_id, 1.0e9, 0U});
        expect_near(pose.arm_model_scale, 0.05, "fps arm size is independent of the tool");
    }
}

struct ExpectedHold final {
    std::uint8_t tool_id;
    ViewModelVector position;
    ViewModelVector orientation;
    ViewModelVector arms_offset;
};

void every_tool_uses_its_retail_first_person_hold() {
    // Main-character Tool.__init__ initial_position[0] / initial_orientation[0]
    // and arms_position_offset for all 65 tools, extracted from the retail
    // aoslib/weapons/*.py classes (resolved through inheritance).
    constexpr std::array<ExpectedHold, 65U> expected{{
        {0U, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}}, // PickAxeTool
        {1U, {-0.05, 0.03, -0.05}, {0.0, 0.0, 0.0}, {0.05, -0.03, 0.05}}, // KnifeTool
        {2U, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}}, // SpadeTool
        {3U, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}}, // SuperSpadeTool
        {4U, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}}, // ClassicSpadeTool
        {5U, {-0.04, 0.0, 0.3}, {0.0, 45.0, 0.0}, {0.04, 0.0, -0.3}}, // BlockTool
        {6U, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}}, // ClassicRifleWeapon
        {7U, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}}, // SMGWeapon
        {8U, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}}, // MinigunWeapon
        {9U, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}}, // ShotgunWeapon
        {10U, {0.0, 0.1, 0.0}, {0.0, 0.0, 0.0}, {0.0, -0.1, 0.0}}, // Shotgun2Weapon
        {11U, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}}, // GrenadeTool
        {12U, {0.0, 0.1, 0.0}, {0.0, 0.0, 0.0}, {0.0, -0.1, 0.0}}, // RPGWeapon
        {13U, {0.0, 0.1, 0.0}, {0.0, 0.0, 0.0}, {0.0, -0.1, 0.0}}, // RPG2Weapon: arms = initial * -1
        {14U, {0.0, 0.1, 0.0}, {0.0, 0.0, 0.0}, {0.0, -0.1, 0.0}}, // DrillgunWeapon
        {15U, {0.0, 0.18, 0.0}, {0.0, 0.0, 0.0}, {0.0, -0.18, 0.0}}, // MGWeapon
        {16U, {-0.75, 0.009, 0.5}, {0.0, 0.0, 0.0}, {0.0, -0.18, 0.0}}, // RocketTurretWeapon part 0: ((-15,0,10)+(0,0.18,0))*0.05
        {17U, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}}, // PistolWeapon
        {18U, {0.0, 0.1, 0.0}, {0.0, 0.0, 0.0}, {0.0, -0.1, 0.0}}, // SniperWeapon
        {19U, {0.0, 0.2, 0.1}, {0.0, 0.0, 0.0}, {0.0, -0.2, -0.1}}, // Sniper2Weapon
        {20U, {0.0, 0.18, 0.0}, {0.0, 70.0, 0.0}, {0.0, -0.18, 0.0}}, // LandmineWeapon
        {21U, {0.0, 0.18, 0.0}, {-20.0, 200.0, 0.0}, {0.0, -0.18, 0.0}}, // DynamiteWeapon
        {22U, {-0.04, 0.0, 0.3}, {0.0, 45.0, 0.0}, {0.04, 0.0, -0.3}}, // FlareBlockTool
        {23U, {-0.04, 0.0, 0.3}, {0.0, 45.0, 0.0}, {0.04, 0.0, -0.3}}, // PrefabTool
        {24U, {0.0, 0.0, -0.25}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.25}}, // ZombieHandTool
        {25U, {0.0, 0.18, 0.0}, {0.0, 0.0, 0.0}, {0.0, -0.18, 0.0}}, // BombTool
        {26U, {0.0, 0.18, 0.0}, {0.0, 0.0, 0.0}, {0.0, -0.18, 0.0}}, // DiamondTool
        {27U, {-0.04, 0.0, 0.3}, {0.0, 45.0, 0.0}, {0.04, 0.0, -0.3}}, // BlockTool
        {28U, {0.0, 0.0, 0.0}, {0.0, 0.0, 180.0}, {0.04, 0.0, -0.3}}, // ZombiePrefabTool part 0 override
        {29U, {0.0, 0.1, 0.0}, {0.0, 0.0, 0.0}, {0.0, -0.1, 0.0}}, // SnowBlowerWeapon
        {30U, {0.0, 0.18, 0.0}, {0.0, 0.0, 0.0}, {0.0, -0.18, 0.0}}, // IntelTool
        {31U, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}}, // ClassicGrenadeTool
        {32U, {0.0, 0.2, 0.0}, {0.0, 0.0, 0.0}, {0.0, -0.2, 0.0}}, // AntipersonnelGrenadeTool
        {33U, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}}, // MolotovWeapon
        {34U, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}}, // CrowbarTool
        {35U, {0.0, 0.12, 0.0}, {0.0, 0.0, 0.0}, {0.0, -0.12, 0.0}}, // TommyGunWeapon
        {36U, {0.0, 0.12, 0.0}, {0.0, 0.0, 0.0}, {0.0, -0.12, 0.0}}, // SnubPistolWeapon
        {37U, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}}, // ClassicShotgunWeapon
        {38U, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}}, // ClassicSmgWeapon
        {39U, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}}, // NullTool
        {40U, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}}, // FakePistolTool
        {41U, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}}, // UGCTool
        {42U, {-0.04, 0.0, 0.3}, {0.0, 0.0, 0.0}, {0.04, 0.0, -0.3}}, // UGCPrefabTool
        {43U, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}}, // PaintbrushTool: initial = -arms = 0
        {44U, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}}, // UGCPickAxeTool
        {45U, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}}, // UGCSuperSpadeTool
        {46U, {0.0, 0.1, 0.0}, {0.0, 0.0, 0.0}, {0.0, -0.1, 0.0}}, // UGCRPG2Weapon: arms = initial * -1
        {47U, {0.0, 0.1, 0.0}, {0.0, 0.0, 0.0}, {0.0, -0.1, 0.0}}, // UGCDrillgunWeapon
        {48U, {0.0, 0.1, 0.0}, {0.0, 0.0, 0.0}, {0.0, -0.1, 0.0}}, // UGCSnowBlowerWeapon
        {49U, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.03, -0.05}}, // RiotStickTool
        {50U, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.03, -0.05}}, // MacheteTool
        {51U, {-0.1, -0.18, 0.45}, {0.0, 0.0, 0.0}, {0.0, 0.1, -0.2}}, // MedPackWeapon
        {52U, {0.45, -0.6, -0.2}, {0.0, 0.0, 0.0}, {0.05, -0.03, 0.05}}, // RiotShieldTool
        {53U, {-0.15, -0.05, 0.2}, {0.0, 0.0, 0.0}, {0.0, 0.05, -0.05}}, // AutoPistolWeapon
        {54U, {-0.02, 0.2, 0.0}, {0.0, 0.0, 0.0}, {0.02, -0.2, 0.0}}, // ChemicalBombWeapon
        {55U, {-0.15, 0.12, 0.1}, {0.0, 0.0, 0.0}, {0.1, -0.18, 0.0}}, // GrenadeLauncherWeapon
        {56U, {-0.1, 0.6, 0.2}, {0.0, 0.0, 0.0}, {0.0, -0.66, -0.1}}, // RadarStationWeapon (initial_position[0])
        {57U, {0.0, 0.1, 0.0}, {0.0, 0.0, 0.0}, {0.0, -0.1, 0.0}}, // StickyGrenadeWeapon
        {58U, {-0.15, 0.15, 0.0}, {0.0, 0.0, 0.0}, {0.0, -0.1, 0.0}}, // MineLauncherWeapon
        {59U, {0.0, 0.18, 0.0}, {0.0, 0.0, 0.0}, {-0.1, -0.1, 0.2}}, // C4Weapon
        {60U, {0.0, 0.2, 0.0}, {0.0, 0.0, 0.0}, {-0.1, -0.08, -0.1}}, // AssaultRifleWeapon
        {61U, {0.0, 0.0, 0.25}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}}, // LightMachineGunWeapon
        {62U, {0.0, 0.125, 0.13}, {0.0, 0.0, 0.0}, {0.0, -0.1, -0.2}}, // AutoShotgunWeapon
        {63U, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {-0.3, -0.1, 0.4}}, // BlockSuckerWeapon
        {64U, {-0.1, 0.0, 0.05}, {0.0, 0.0, 0.0}, {0.0, 0.01, 0.0}}, // DisguiseTool
    }};
    for (const auto& row : expected) {
        const auto pose = evaluate_weapon_view_model({row.tool_id, 1.0e9, 0U});
        const std::string tool = "tool " + std::to_string(row.tool_id) + ": ";
        const auto near = [&](double actual, double wanted, const char* what) {
            if (std::fabs(actual - wanted) > 1.0e-9) {
                throw std::runtime_error{tool + what};
            }
        };
        if (pose.tool_part_count == 0U) {
            // MGWeapon/NullTool: get_arms_position() returns zero without models.
            near(pose.arms_position.x, 0.0, "model-less tool arms x must be sway only");
            near(pose.arms_position.y, 0.0, "model-less tool arms y must be sway only");
            near(pose.arms_position.z, 0.0, "model-less tool arms z must be sway only");
            continue;
        }
        near(pose.tool.position.x, row.position.x, "initial_position x");
        near(pose.tool.position.y, row.position.y, "initial_position y");
        near(pose.tool.position.z, row.position.z, "initial_position z");
        near(pose.tool.orientation_degrees.x, row.orientation.x, "initial_orientation x");
        near(pose.tool.orientation_degrees.y, row.orientation.y, "initial_orientation y");
        near(pose.tool.orientation_degrees.z, row.orientation.z, "initial_orientation z");
        near(pose.arms_position.x, row.position.x + row.arms_offset.x, "arms x");
        near(pose.arms_position.y, row.position.y + row.arms_offset.y, "arms y");
        near(pose.arms_position.z, row.position.z + row.arms_offset.z, "arms z");
        near(pose.arms_orientation_degrees.x, row.orientation.x,
             "arms orientation follows model 0");
        // DiggingTool.rotate_arm_ratio = 0.25; Tool default 1.0.
        const bool digging = row.tool_id <= 4U || row.tool_id == 24U || row.tool_id == 34U ||
                             row.tool_id == 44U || row.tool_id == 45U ||
                             (row.tool_id >= 49U && row.tool_id <= 50U) || row.tool_id == 52U;
        near(pose.arm_rotation_ratio, digging ? 0.25 : 1.0, "rotate_arm_ratio");
    }

    const auto turret = evaluate_weapon_view_model({16U, 1.0e9, 0U});
    expect(turret.tool_part_count == 3U, "rocket turret keeps base, ball and gun");
    expect_near(turret.tool_parts[1U].position.y, (14.0 + 0.18) * 0.05,
                "turret ball sits 14 authored units above the base");
    expect_near(turret.tool_parts[2U].position.y, (8.0 + 0.18) * 0.05,
                "turret gun sits 8 authored units above the base");

    const auto dynamite = evaluate_weapon_view_model({21U, 1.0e9, 0U});
    expect_near(dynamite.arms_orientation_degrees.x, -20.0,
                "Dynamite's -20 degree initial pitch also tilts the hands");
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

void expect_vector(const ViewModelVector& actual, const ViewModelVector& expected,
                   const std::string& message) {
    if (std::fabs(actual.x - expected.x) > 1.0e-9 || std::fabs(actual.y - expected.y) > 1.0e-9 ||
        std::fabs(actual.z - expected.z) > 1.0e-9) {
        throw std::runtime_error{message};
    }
}

void every_weapon_flash_uses_its_retail_view_offsets() {
    using battlespades::world::evaluate_retail_view_muzzle_flash;
    using battlespades::world::retail_view_muzzle_flash;
    struct Row final {
        std::uint8_t tool;
        const char* name;
        ViewModelVector view;
        ViewModelVector zoomed;
        double scale;
        double duration;
        double view_model_size;
    };
    // aoslib/weapons/<class>.py muzzle_flash_* attributes and view_model_size.
    constexpr ViewModelVector z3{0.0, -0.1, 3.0};
    const std::array<Row, 16U> rows{{
        {7U, "SMGWeapon", {0.0, 0.12, 0.5}, z3, 0.5, 0.05, 0.05},
        {8U, "MinigunWeapon", {0.0, 0.12 - 0.30000000000000004, 2.0}, {}, 0.75, 0.05, 0.05},
        {9U, "ShotgunWeapon", {-0.05, 0.12, 0.8}, z3, 1.0, 0.05, 0.05},
        {10U, "Shotgun2Weapon", {-0.05, 0.12, 0.8}, z3, 1.0, 0.05, 0.05},
        {15U, "MGWeapon", {0.0, 0.18, 2.0}, z3, 0.75, 0.01, 0.05},
        {17U, "PistolWeapon", {-0.05, 0.12, 0.35}, {0.0, -0.1, 1.5}, 0.5, 0.05, 0.05},
        {18U, "SniperWeapon", {0.0, 0.12, 0.9}, {}, 0.5, 0.05, 0.05},
        {19U, "Sniper2Weapon", {0.0, 0.12, 0.9}, {}, 0.5, 0.05, 0.05},
        {35U, "TommyGunWeapon", {0.0, 0.35, 1.2}, z3, 0.5, 0.01, 0.05},
        {36U, "SnubPistolWeapon", {0.0, 0.34, 0.5}, {0.0, -0.2, 2.5}, 0.5, 0.05, 0.05},
        {37U, "ClassicShotgunWeapon", {-0.05, 0.12, 0.8}, z3, 1.0, 0.05, 0.05},
        {38U, "ClassicSmgWeapon", {0.0, 0.12, 0.5}, z3, 0.5, 0.01, 0.05},
        {53U, "AutoPistolWeapon", {0.0, 0.12, 0.5}, z3, 0.5, 0.05, 0.035},
        {60U, "AssaultRifleWeapon", {0.0, 0.12, 0.5}, z3, 0.5, 0.05, 0.035},
        {61U, "LightMachineGunWeapon", {0.0, 0.12, 0.5}, z3, 0.5, 0.05, 0.055},
        {62U, "AutoShotgunWeapon", {-0.05, 0.12, 0.8}, z3, 0.6, 0.05, 0.05},
    }};
    const ViewModelVector sway{0.01, -0.02, 0.03};
    for (const auto& row : rows) {
        const std::string name{row.name};
        const auto flash = retail_view_muzzle_flash(row.tool);
        expect(flash.has_value(), (name + " must flash in first person").c_str());
        expect_vector(flash->view_offset, row.view, name + " muzzle_flash_view_offset");
        expect_vector(flash->zoomed_view_offset, row.zoomed,
                      name + " muzzle_flash_zoomed_view_offset");
        expect_near(flash->scale, row.scale, (name + " muzzle_flash_scale").c_str());
        expect_near(flash->duration, row.duration, (name + " muzzle_flash_duration").c_str());
        expect_near(battlespades::world::muzzle_flash_duration_for(row.tool), row.duration,
                    (name + " effect duration table must agree").c_str());

        // Hip: draw_fps root T(sway + (-0.4,-0.55,0.9)) then the view offset.
        const auto hip = evaluate_retail_view_muzzle_flash(row.tool, false, sway, 123.0);
        expect(hip.has_value(), (name + " hip flash pose").c_str());
        expect_vector(hip->position,
                      {-0.4 + sway.x + row.view.x, -0.55 + sway.y + row.view.y,
                       0.9 + sway.z + row.view.z},
                      name + " hip flash must sit under the draw_fps root");
        expect_near(hip->model_scale, row.view_model_size * row.scale,
                    (name + " flash size = view_model_size * muzzle_flash_scale").c_str());
        expect_near(hip->roll_degrees, 123.0, (name + " random roll is kept").c_str());
        expect_near(hip->yaw_degrees, 180.0, (name + " handedness flip").c_str());
        // The regression: the local flash used to sit on the optical axis
        // 0.72 ahead of the eye. The retail hip flash is always off-axis
        // (draw_fps x -0.4) and at least 1.2 units out.
        expect(std::fabs(hip->position.x) >= 0.35 && hip->position.z >= 1.2,
               (name + " hip flash must not sit in front of the eye").c_str());

        // Aimed: draw_sight is identity + R_y(180); no sway, no root.
        const auto aimed = evaluate_retail_view_muzzle_flash(row.tool, true, sway, 5.0);
        expect(aimed.has_value(), (name + " aimed flash pose").c_str());
        expect_vector(aimed->position, row.zoomed, name + " aimed flash ignores sway and root");
        expect_near(aimed->model_scale, hip->model_scale, (name + " aimed flash size").c_str());
    }
    // No view display: third-person-only classic rifle, launchers, throwables,
    // melee, block tools and the fake pistol.
    constexpr std::array<std::uint8_t, 15U> no_flash{0U,  2U,  5U,  6U,  11U, 12U, 13U, 14U,
                                                     16U, 29U, 33U, 40U, 55U, 58U, 63U};
    for (const std::uint8_t tool : no_flash) {
        expect(!retail_view_muzzle_flash(tool).has_value(),
               ("tool " + std::to_string(tool) + " must not flash in first person").c_str());
        expect(!evaluate_retail_view_muzzle_flash(tool, false, {}, 0.0).has_value(),
               ("tool " + std::to_string(tool) + " must have no flash pose").c_str());
    }
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
        flare_block_is_held_like_the_block_with_both_hands();
        rocket_launchers_use_the_recovered_fps_hold();
        riot_shield_uses_its_recovered_fps_hold_and_bash();
        every_tool_uses_its_retail_first_person_hold();
        sight_is_an_independent_identity_space_model();
        every_weapon_flash_uses_its_retail_view_offsets();
        std::cout << "retail viewmodel parity tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
