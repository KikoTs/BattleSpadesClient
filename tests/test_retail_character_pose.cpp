#include "battlespades/world/retail_character_pose.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

using battlespades::world::evaluate_retail_third_person_pose;
using battlespades::world::evaluate_retail_walk_pose;
using battlespades::world::retail_character_root_yaw_degrees;
using battlespades::world::retail_display_vector;
using battlespades::world::retail_third_person_arm_origin;
using battlespades::world::retail_third_person_muzzle_attachment;
using battlespades::world::retail_third_person_tool_origin;
using battlespades::world::RetailThirdPersonArmPart;

void expect(bool value, const char* message) {
    if (!value)
        throw std::runtime_error{message};
}

void expect_near(double actual, double expected, const char* message) {
    if (std::fabs(actual - expected) > 1.0e-9) {
        throw std::runtime_error{message};
    }
}

void arm_chain_matches_character_reset_tp_arms() {
    const auto pose = evaluate_retail_third_person_pose(17U, 1U);
    const auto& right_upper =
        pose.arms[static_cast<std::size_t>(RetailThirdPersonArmPart::right_upper)];
    const auto& right_lower =
        pose.arms[static_cast<std::size_t>(RetailThirdPersonArmPart::right_lower)];
    const auto& left_upper =
        pose.arms[static_cast<std::size_t>(RetailThirdPersonArmPart::left_upper)];
    const auto& left_lower =
        pose.arms[static_cast<std::size_t>(RetailThirdPersonArmPart::left_lower)];

    expect_near(right_upper.position.x, -0.49, "right upper shoulder x must match character.pyx");
    expect_near(right_upper.model_offset.y, 6.5, "right upper pivot y must match character.pyx");
    expect_near(right_lower.model_offset.y, 13.0, "right lower pivot y must match character.pyx");
    expect_near(left_upper.position.x, 0.49, "left upper shoulder x must match character.pyx");
    expect_near(left_upper.yaw_degrees, -20.0, "left upper yaw must match character.pyx");
    expect_near(left_lower.position.x, -0.03, "left lower x must match character.pyx");
    expect_near(
        left_lower.extra_yaw_degrees, -65.0, "left lower extra yaw must match character.pyx");
    expect_near(left_lower.extra_roll_degrees,
                0.0,
                "left lower extra roll must remain independent from extra yaw");

    const auto right_upper_origin =
        retail_third_person_arm_origin(right_upper, pose.arm_model_scale);
    expect_near(right_upper_origin.x, -0.4905, "right upper display offset x must retain its sign");
    expect_near(
        right_upper_origin.y, -0.5005, "right upper GL y must come from reflected retail z");
    expect_near(
        right_upper_origin.z, 0.175, "right upper GL z must combine retail y and scaled offset y");

    const auto right_lower_origin =
        retail_third_person_arm_origin(right_lower, pose.arm_model_scale);
    expect_near(right_lower_origin.y, -0.5, "right lower GL y must keep the shared shoulder depth");
    expect_near(
        right_lower_origin.z, 0.5, "right lower GL z must include the long lower-arm offset");
    expect_near(pose.tool_anchor.z,
                0.5,
                "held tool must share the retail character hand-height anchor");
    expect_near(pose.tool_parts[0U].position.z,
                0.0,
                "ordinary tool child must not duplicate the character anchor");
}

void display_vectors_match_retail_draw_axis_order() {
    const auto translated = retail_display_vector({1.25, -2.5, 0.75});
    expect_near(translated.x, 1.25, "DisplayList translation must preserve x");
    expect_near(translated.y, -0.75, "DisplayList translation must negate source z");
    expect_near(translated.z, -2.5, "DisplayList translation must move source y to GL z");

    const auto held_anchor = retail_display_vector({0.0, 0.0, 0.5});
    expect_near(
        held_anchor.y, -0.5, "held weapon anchor must move toward the retail character hands");
    expect_near(
        held_anchor.z, 0.0, "held weapon anchor must not become a vertical duplicate-hand offset");
}

void zombie_tools_own_their_visible_hands() {
    const auto pistol = evaluate_retail_third_person_pose(17U, 1U);
    expect(pistol.draws_player_arms, "ordinary weapons must draw class arms");

    const auto hands = evaluate_retail_third_person_pose(24U, 2U, 0.05, 1U);
    expect(!hands.draws_player_arms, "ZombieHandTool must suppress the normal class arm rig");
    expect_near(hands.tool_model_scale, 0.0325, "ZombieHandTool.model_scale must halve model size");
    expect(hands.tool_parts[0U].orientation_degrees.x != 0.0,
           "first accepted attack must animate the right hand");
    expect_near(
        hands.tool_parts[1U].orientation_degrees.x, 0.0, "inactive Zombie hand must remain static");

    const auto alternate = evaluate_retail_third_person_pose(24U, 2U, 0.05, 2U);
    expect_near(alternate.tool_parts[0U].orientation_degrees.x,
                0.0,
                "second attack must leave right hand static");
    expect(alternate.tool_parts[1U].orientation_degrees.x != 0.0,
           "second attack must animate the left hand");

    const auto prefab = evaluate_retail_third_person_pose(28U, 3U);
    expect(!prefab.draws_player_arms, "ZombiePrefabTool also supplies its own visible hands");
    expect(prefab.tool_part_count == 3U,
           "ZombiePrefabTool third person must retain right hand, block, and left hand");
    expect_near(prefab.tool_model_scale, 0.065,
                "Zombie prefab third-person model must keep inherited Tool size");

    const auto placing_prefab = evaluate_retail_third_person_pose(28U, 3U, 0.0, 1U);
    expect_near(placing_prefab.tool_parts[0U].position.x, -0.5,
                "remote Zombie right hand must inherit AnimPlaceBlock");
    expect_near(placing_prefab.tool_parts[1U].position.y, -0.5,
                "remote Zombie block must inherit AnimPlaceBlock");
    expect_near(placing_prefab.tool_parts[2U].position.x, 0.0,
                "third Zombie prefab part exceeds view_model transform arrays and stays static");
    expect_near(placing_prefab.tool_parts[2U].position.y, 0.0,
                "remote Zombie left hand must keep its authored attachment");
}

void minigun_barrel_keeps_its_authored_third_person_offset() {
    const auto minigun = evaluate_retail_third_person_pose(8U, 2U);
    expect_near(minigun.tool_anchor.z, 0.5, "minigun must retain the shared tool anchor");
    expect_near(minigun.tool_parts[0U].position.z,
                0.0,
                "minigun body must remain at its local child origin");
    expect_near(minigun.tool_parts[1U].position.y,
                -0.3,
                "minigun barrel must use its retail local y offset");
    expect_near(minigun.tool_parts[1U].position.z,
                1.1,
                "minigun barrel must retain its authored local z offset");
    const auto body_origin = retail_third_person_tool_origin(minigun, 0U);
    const auto barrel_origin = retail_third_person_tool_origin(minigun, 1U);
    expect_near(body_origin.y, -0.5, "minigun body must sit at the unscaled hand anchor");
    expect_near(barrel_origin.y,
                -0.8,
                "Tool.apply_transform y must remain direct GL space");
    expect_near(barrel_origin.z,
                1.1,
                "barrel forward offset must remain unscaled outside DisplayList");
}

void muzzle_attachments_match_retail_weapon_subclasses() {
    const auto sniper = retail_third_person_muzzle_attachment(18U);
    expect(sniper.has_value(), "sniper must expose its observer muzzle attachment");
    expect_near(sniper->offset.x, -12.0, "sniper muzzle x must match SniperWeapon");
    expect_near(sniper->offset.y, 78.0, "sniper muzzle length must match SniperWeapon");
    expect_near(sniper->scale, 0.5, "sniper muzzle display must retain default half scale");

    const auto shotgun = retail_third_person_muzzle_attachment(9U);
    expect(shotgun.has_value(), "shotgun must expose its observer muzzle attachment");
    expect_near(shotgun->offset.y, 36.0, "shotgun muzzle length must match ShotgunWeapon");
    expect_near(shotgun->scale, 1.0, "shotgun muzzle display must retain full scale");

    expect(!retail_third_person_muzzle_attachment(2U).has_value(),
           "a spade must never emit a firearm muzzle attachment");
}

void root_yaw_matches_retail_orientation_basis() {
    expect_near(retail_character_root_yaw_degrees({-1.0, 0.0, 0.0}),
                90.0,
                "retail forward -X must rotate the canonical body by +90");
    expect_near(retail_character_root_yaw_degrees({0.0, 1.0, 0.0}),
                0.0,
                "canonical body +Y must require no root rotation");
    expect_near(retail_character_root_yaw_degrees({1.0, 0.0, 0.0}),
                -90.0,
                "retail backward +X must rotate the canonical body by -90");
    expect_near(std::fabs(retail_character_root_yaw_degrees({0.0, -1.0, 0.0})),
                180.0,
                "orientation -Y must face the canonical body backward");
    expect_near(retail_character_root_yaw_degrees({0.0, 0.0, 1.0}),
                0.0,
                "vertical-only corrupt input must fail to a stable yaw");
}

void aim_pitch_uses_retail_joint_pivots() {
    const auto aimed = evaluate_retail_third_person_pose(17U, 1U, 1.0e9, 0U, 30.0);
    expect_near(aimed.head_pitch_degrees,
                30.0,
                "head must retain the decoded look pitch");
    expect_near(aimed.weapon_pitch_degrees,
                30.0,
                "weapon must receive the retail arm pitch");
    for (const auto& arm : aimed.arms) {
        expect_near(
            arm.pitch_degrees, 30.0, "each arm DisplayList must pitch around its own joint");
    }
    const auto clamped = evaluate_retail_third_person_pose(17U, 1U, 1.0e9, 0U, 89.0);
    expect_near(clamped.head_pitch_degrees, 89.0, "head pitch must retain the raw look angle");
    expect_near(clamped.weapon_pitch_degrees,
                70.0,
                "ordinary tool arms must use the recovered +70 degree clamp");
}

void walk_cycle_matches_character_update_animation() {
    const auto idle = evaluate_retail_walk_pose(0U, {0.01, -0.01, 0.0}, {1.0, 0.0, 0.0}, false);
    expect_near(idle.left.rotation_x_degrees, 0.0, "retail dead zone must plant left leg");
    expect_near(idle.right.rotation_y_degrees, 0.0, "retail dead zone must plant right leg");

    const auto start =
        evaluate_retail_walk_pose(0U, {0.1, 0.0, 0.0}, {1.0, 0.0, 0.0}, false);
    expect_near(start.left.rotation_x_degrees,
                -21.42,
                "first half-cycle must use the recovered triangle projection");
    expect_near(start.right.rotation_x_degrees,
                21.42,
                "legs must oppose one another exactly");
    expect_near(start.left.rotation_y_degrees,
                0.0,
                "forward walking must not swing a foot toward the opposite hip");

    const auto zero =
        evaluate_retail_walk_pose(255U, {0.1, 0.0, 0.0}, {1.0, 0.0, 0.0}, false);
    expect_near(zero.left.rotation_x_degrees, 0.0, "triangle wave must cross zero at 255 ms");
    const auto before_wrap =
        evaluate_retail_walk_pose(511U, {0.1, 0.0, 0.0}, {1.0, 0.0, 0.0}, false);
    const auto after_wrap =
        evaluate_retail_walk_pose(512U, {0.1, 0.0, 0.0}, {1.0, 0.0, 0.0}, false);
    expect(before_wrap.left.rotation_x_degrees > 0.0 &&
               after_wrap.left.rotation_x_degrees > 0.0,
           "second half-cycle must remain continuous across the 512 ms mask");

    const auto strafe =
        evaluate_retail_walk_pose(0U, {0.0, 0.1, 0.0}, {1.0, 0.0, 0.0}, false);
    expect_near(strafe.left.rotation_y_degrees,
                -21.42,
                "side velocity must drive the recovered lateral leg rotation");
    expect_near(strafe.left.rotation_x_degrees,
                0.0,
                "pure strafing must not invent forward swing");

    const auto crouched =
        evaluate_retail_walk_pose(0U, {0.1, 0.0, 0.0}, {1.0, 0.0, 0.0}, true);
    expect_near(crouched.left.rotation_x_degrees,
                start.left.rotation_x_degrees * (16.0 / 28.0),
                "crouch gait must use retail's 0.016/0.028 arc ratio");
}

} // namespace

int main() {
    try {
        arm_chain_matches_character_reset_tp_arms();
        display_vectors_match_retail_draw_axis_order();
        zombie_tools_own_their_visible_hands();
        minigun_barrel_keeps_its_authored_third_person_offset();
        muzzle_attachments_match_retail_weapon_subclasses();
        root_yaw_matches_retail_orientation_basis();
        aim_pitch_uses_retail_joint_pivots();
        walk_cycle_matches_character_update_animation();
        std::cout << "retail third-person character pose tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
