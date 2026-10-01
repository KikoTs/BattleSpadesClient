#include "battlespades/world/retail_character_pose.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
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
    // Character.draw's ZombiePrefabTool branch (pyx 1910-1940): size 0.04
    // and three hard-coded transforms instead of the generic Tool loop.
    expect_near(prefab.tool_model_scale, 0.04,
                "Zombie prefab third-person draw forces weapon size 0.04");
    expect_near(prefab.tool_parts[0U].position.x, -0.95, "right hand x");
    expect_near(prefab.tool_parts[0U].position.y, -0.2, "right hand y");
    expect_near(prefab.tool_parts[0U].position.z, 0.65, "right hand z");
    expect_near(prefab.tool_parts[0U].orientation_degrees.z, 180.0, "right hand rotation");
    expect_near(prefab.tool_parts[1U].position.x, -0.72, "block x");
    expect_near(prefab.tool_parts[1U].position.z, -0.25, "block z");
    expect_near(prefab.tool_parts[1U].orientation_degrees.x, 45.0, "block pitch");
    expect_near(prefab.tool_parts[1U].orientation_degrees.y, 45.0, "block yaw");
    expect_near(prefab.tool_parts[2U].position.x, 0.65, "left hand x");
    expect_near(prefab.tool_parts[2U].orientation_degrees.x, 90.0, "left hand x rotation");
    expect_near(prefab.tool_parts[2U].orientation_degrees.z, -90.0, "left hand z rotation");

    const auto placing_prefab = evaluate_retail_third_person_pose(28U, 3U, 0.0, 1U);
    expect_near(placing_prefab.tool_parts[0U].position.x, -0.95,
                "the special branch never applies AnimPlaceBlock to observers");
    expect_near(placing_prefab.tool_parts[1U].position.y, 0.5,
                "the special branch never applies AnimPlaceBlock to observers");
}

void minigun_barrel_rolls_about_its_recovered_pivot() {
    const auto rest = evaluate_retail_third_person_pose(8U, 2U);
    const auto spun = evaluate_retail_third_person_pose(8U, 2U, 1.0e9, 0U, 0.0, true, 0.25);
    expect_near(spun.tool_parts[1U].orientation_degrees.z, 90.0,
                "a quarter revolution rolls the barrel 90 degrees");
    expect_near(spun.tool_parts[0U].orientation_degrees.z, 0.0,
                "AnimRoll never turns the minigun body");
    // Row-vector Rz(90) about q = size*(-7,-6.5): q - q*Rz = (qx + qy, qy - qx).
    const double size = rest.tool_model_scale;
    expect_near(spun.tool_parts[1U].position.x - rest.tool_parts[1U].position.x,
                (-7.0 - 6.5) * size, "barrel pivot x compensation");
    expect_near(spun.tool_parts[1U].position.y - rest.tool_parts[1U].position.y,
                (-6.5 + 7.0) * size, "barrel pivot y compensation");
    expect_near(spun.tool_parts[1U].position.z, rest.tool_parts[1U].position.z,
                "the roll axis is z; z never moves");

    using battlespades::world::advance_retail_remote_minigun_spin;
    battlespades::world::RetailRemoteMinigunSpin spin{};
    for (int step{}; step < 120; ++step) {
        spin = advance_retail_remote_minigun_spin(spin, 1.0 / 60.0, true);
    }
    expect_near(spin.ratio, 1.0, "two seconds of trigger reach full spin (0.75/s)");
    for (int step{}; step < 60; ++step) {
        spin = advance_retail_remote_minigun_spin(spin, 1.0 / 60.0, false);
    }
    expect(spin.ratio > 0.6 && spin.ratio < 0.65, "spin winds down at 0.375/s");
    expect(spin.phase >= 0.0 && spin.phase < 1.0, "phase stays a revolution fraction");
}

void hidden_or_unavailable_weapon_keeps_observer_class_arms() {
    const auto visible = evaluate_retail_third_person_pose(17U, 1U, 1.0e9, 0U, 30.0);
    const auto hidden = evaluate_retail_third_person_pose(17U, 1U, 1.0e9, 0U, 30.0, false);
    expect(visible.tool_part_count == 1U && hidden.tool_part_count == 0U,
           "authority may hide the held weapon while the observer keeps the character body");
    expect(hidden.draws_player_arms,
           "retail class arms must remain visible when can_display_weapon clears");
    // Character.draw adds 50 (not shoot_pitch) when the tool is hidden, then
    // clamps to the tool's arm range: 30 + 50 = 80 -> 70 for a pistol.
    expect_near(visible.weapon_pitch_degrees, 30.0, "a shown pistol follows the aim");
    expect_near(hidden.weapon_pitch_degrees, 70.0,
                "a hidden tool drops the arms by 50 degrees, clamped to +70");
    for (std::size_t index{}; index < hidden.arms.size(); ++index) {
        expect_near(hidden.arms[index].pitch_degrees, hidden.weapon_pitch_degrees,
                    "every class arm follows the hidden-tool pitch");
    }
    const auto hidden_up = evaluate_retail_third_person_pose(17U, 1U, 1.0e9, 0U, -30.0, false);
    expect_near(hidden_up.weapon_pitch_degrees, 20.0,
                "sprinting while looking up still drops the arms by 50 degrees");
    const auto hidden_shield = evaluate_retail_third_person_pose(52U, 1U, 1.0e9, 0U, -30.0, false);
    expect_near(hidden_shield.weapon_pitch_degrees, 0.0,
                "a hidden riot shield keeps RiotShieldTool's (-80, 0) range");
    const auto missing = evaluate_retail_third_person_pose(17U, 0U);
    expect(missing.draws_player_arms && missing.tool_part_count == 0U,
           "missing held-tool geometry must not remove valid class arms");
    const auto zombie = evaluate_retail_third_person_pose(24U, 2U, 1.0e9, 0U, 0.0, false);
    expect(!zombie.draws_player_arms && zombie.tool_part_count == 0U,
           "ZombieHandTool keeps its own visibility and never fabricates a class arm rig");
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

void riot_shield_uses_its_recovered_arm_pitch_range_and_bash() {
    // Character.draw: pitch + shoot_pitch (RiotShieldTool's DiggingTool.pitch,
    // resting at pitch_initial -4), clamped to get_arm_pitch_range() =
    // (A1886, A1887) = (-80, 0). Positive pitch looks down.
    const auto level = evaluate_retail_third_person_pose(52U, 1U, 1.0e9, 0U, 0.0);
    expect_near(level.head_pitch_degrees, 0.0, "the head keeps the raw look pitch");
    expect_near(level.weapon_pitch_degrees, -4.0, "resting shield carries pitch_initial");
    expect_near(level.tool_model_scale, 0.073, "RiotShieldTool.model_size is 0.073");
    expect_near(level.tool_parts[0U].position.z, 0.0,
                "remote shields have no main-character initial_position");
    for (const auto& arm : level.arms) {
        expect_near(arm.pitch_degrees, -4.0, "shield arms share the clamped weapon pitch");
    }

    const auto down = evaluate_retail_third_person_pose(52U, 1U, 1.0e9, 0U, 60.0);
    expect_near(down.weapon_pitch_degrees, 0.0,
                "looking down must never swing the shield into the legs (max 0)");
    const auto up = evaluate_retail_third_person_pose(52U, 1U, 1.0e9, 0U, -60.0);
    expect_near(up.weapon_pitch_degrees, -64.0, "looking up tilts the shield with the aim");
    const auto straight_up = evaluate_retail_third_person_pose(52U, 1U, 1.0e9, 0U, -89.0);
    expect_near(straight_up.weapon_pitch_degrees, -80.0, "shield pitch floor is A1886 = -80");

    // use_spade(): offset = max(aim + 10 - 0, 0); pitch runs from
    // (-4 + 10 - offset) to (-4 - offset) across the 1 s shoot interval.
    const auto swing_start = evaluate_retail_third_person_pose(52U, 1U, 0.0, 1U, -30.0);
    expect_near(swing_start.weapon_pitch_degrees, -24.0, "swing starts 10 degrees low");
    const auto swing_mid = evaluate_retail_third_person_pose(52U, 1U, 0.5, 1U, -30.0);
    expect_near(swing_mid.weapon_pitch_degrees, -29.0, "swing interpolates toward rest");
    const auto swing_level = evaluate_retail_third_person_pose(52U, 1U, 0.0, 1U, 0.0);
    expect_near(swing_level.weapon_pitch_degrees, -4.0,
                "the overshoot offset keeps a level swing inside the 0 degree ceiling");

    // AnimUseRiotShield is applied through Tool.apply_transform for observers.
    const auto bash_start = evaluate_retail_third_person_pose(52U, 1U, 0.0, 1U, 0.0);
    expect_near(bash_start.tool_parts[0U].position.z, 0.0, "start() frame is the rest pose");
    const auto bash = evaluate_retail_third_person_pose(52U, 1U, 0.25, 1U, 0.0);
    expect_near(bash.tool_parts[0U].position.z, 0.3, "bash thrusts the shield forward");
    const auto hidden = evaluate_retail_third_person_pose(52U, 1U, 0.25, 1U, 0.0, false);
    expect(hidden.tool_part_count == 0U, "a hidden shield draws no parts");

    // Ordinary tools keep the generic -90..70 range and no digging pitch.
    const auto pistol = evaluate_retail_third_person_pose(17U, 1U, 0.0, 1U, 60.0);
    expect_near(pistol.weapon_pitch_degrees, 60.0, "the shield range must not leak to guns");
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

void running_zombie_legs_stay_inside_the_retail_human_arc() {
    using battlespades::world::retail_walk_max_swing_degrees;
    // Ground terminal speed is accel / friction 4, accel = class multiplier x
    // InitialInfo speed (CLASS_SPRINT_MULTIPLIER): Gangster sprint 1.5 x 1.5.
    constexpr double gangster_sprint{1.5 * 1.5 / 4.0};
    constexpr double soldier_sprint{1.40625 * 1.40625 / 4.0};
    constexpr double zombie_sprint{1.65625 * 1.65625 / 4.0};
    constexpr double fast_zombie_walk{1.1 * 3.0 / 4.0};
    constexpr double fast_zombie_sprint{3.0 * 3.0 / 4.0};
    expect_near(retail_walk_max_swing_degrees, 256.0 * 0.84 * gangster_sprint,
                "the ceiling is retail's own peak at the fastest human gait");

    // Every shipped human gait is untouched: 0 ms is the -255 ramp end.
    const auto soldier =
        evaluate_retail_walk_pose(0U, {soldier_sprint, 0.0, 0.0}, {1.0, 0.0, 0.0}, false);
    expect_near(soldier.left.rotation_x_degrees, -255.0 * 0.84 * soldier_sprint,
                "soldier sprint must remain bit-exact Character.update_animation");
    const auto gangster =
        evaluate_retail_walk_pose(511U, {gangster_sprint, 0.0, 0.0}, {1.0, 0.0, 0.0}, false);
    expect_near(gangster.left.rotation_x_degrees, retail_walk_max_swing_degrees,
                "the fastest human gait reaches, but is not cut by, the ceiling");

    // Zombie speeds: the retail formula alone gives 146, 177 and 482 degrees.
    for (const double speed : {zombie_sprint, fast_zombie_walk, fast_zombie_sprint}) {
        const double retail_peak = 256.0 * 0.84 * speed;
        expect(retail_peak > retail_walk_max_swing_degrees,
               "zombie speeds leave the human arc in raw retail math");
        double largest{};
        for (std::uint64_t timer{}; timer < 1024U; ++timer) {
            const auto pose =
                evaluate_retail_walk_pose(timer, {-speed, 0.0, 0.0}, {-1.0, 0.0, 0.0}, false);
            largest = std::max({largest, std::fabs(pose.left.rotation_x_degrees),
                                std::fabs(pose.right.rotation_x_degrees)});
            expect_near(pose.right.rotation_x_degrees, -pose.left.rotation_x_degrees,
                        "scaled legs must still oppose one another");
        }
        expect(largest <= retail_walk_max_swing_degrees + 1.0e-9,
               "a running zombie's legs must never swing past the human ceiling");
        expect(largest > retail_walk_max_swing_degrees - 1.0,
               "a running zombie still takes the widest human stride");
    }

    // Scaling keeps retail's triangle phase and forward/strafe ratio.
    const auto slow = evaluate_retail_walk_pose(100U, {0.3, 0.1, 0.0}, {1.0, 0.0, 0.0}, false);
    const auto fast = evaluate_retail_walk_pose(100U, {3.0, 1.0, 0.0}, {1.0, 0.0, 0.0}, false);
    expect_near(fast.left.rotation_x_degrees / fast.left.rotation_y_degrees,
                slow.left.rotation_x_degrees / slow.left.rotation_y_degrees,
                "scaled strafing must keep the retail forward/side ratio");
    const auto crossing =
        evaluate_retail_walk_pose(255U, {fast_zombie_sprint, 0.0, 0.0}, {1.0, 0.0, 0.0}, false);
    expect_near(crossing.left.rotation_x_degrees, 0.0,
                "scaled legs still pass each other at retail's 255 ms crossing");
}

void digging_tools_use_retail_pitch_and_range() {
    using battlespades::world::retail_tool_arm_pitch_range;
    using battlespades::world::retail_tool_pitch;
    // DiggingTool: pitch_initial -4; get_arm_pitch_range() upper limit is
    // 70 - pitch_increase at rest and 70 while the swing runs.
    const auto spade_rest = evaluate_retail_third_person_pose(2U, 1U, 1.0e9, 0U, 0.0);
    expect_near(spade_rest.weapon_pitch_degrees, -4.0, "a resting spade carries pitch_initial");
    const auto spade_down = evaluate_retail_third_person_pose(2U, 1U, 1.0e9, 0U, 60.0);
    expect_near(spade_down.weapon_pitch_degrees, 30.0,
                "a resting spade (pitch_increase 40) stops at 70 - 40");
    const auto knife_down = evaluate_retail_third_person_pose(1U, 1U, 1.0e9, 0U, 60.0);
    expect_near(knife_down.weapon_pitch_degrees, 20.0,
                "a resting knife (pitch_increase 50) stops at 70 - 50");
    const auto pickaxe_down = evaluate_retail_third_person_pose(0U, 1U, 1.0e9, 0U, 60.0);
    expect_near(pickaxe_down.weapon_pitch_degrees, 40.0,
                "a resting pickaxe (pitch_increase 30) stops at 70 - 30");

    // use_spade(): offset = max(aim + 40 - 30, 0) = 10 at a level aim, so the
    // 0.8 s swing runs from -4 + 40 - 10 = 26 to -4 - 10 = -14.
    const auto swing_start = evaluate_retail_third_person_pose(2U, 1U, 0.0, 1U, 0.0);
    expect_near(swing_start.weapon_pitch_degrees, 26.0, "spade swing starts raised");
    const auto swing_mid = evaluate_retail_third_person_pose(2U, 1U, 0.4, 1U, 0.0);
    expect_near(swing_mid.weapon_pitch_degrees, 6.0, "spade swing interpolates linearly");
    expect_near(retail_tool_arm_pitch_range(2U, 0.4).maximum, 70.0,
                "the swing lifts the upper limit back to +70");
    expect_near(retail_tool_arm_pitch_range(2U, 0.8).maximum, 30.0,
                "the limit drops again once shoot_interval elapses");
    const auto swing_down = evaluate_retail_third_person_pose(2U, 1U, 0.0, 1U, 60.0);
    expect_near(swing_down.weapon_pitch_degrees, 26.0,
                "looking down, the overshoot offset starts the swing 4 below the rest limit");

    // ZombieHandTool restores pitch around every update and keeps Tool's range.
    expect_near(retail_tool_pitch(24U, 0.1, 0.0), -4.0, "zombie hands never swing pitch");
    const auto zombie_down = evaluate_retail_third_person_pose(24U, 2U, 1.0e9, 0U, 89.0);
    expect_near(zombie_down.weapon_pitch_degrees, 70.0, "zombie hands keep the +70 limit");
    // Grenades rest at GrenadeTool.pitch_initial for observers.
    const auto grenade = evaluate_retail_third_person_pose(11U, 1U, 1.0e9, 0U, 0.0);
    expect_near(grenade.weapon_pitch_degrees, -4.0, "grenades rest at pitch_initial -4");
    // Weapon/BlockTool class `pitch` attributes never reach Character.draw.
    expect_near(retail_tool_pitch(5U, 0.0, 0.0), 0.0, "block tool keeps pitch 0");
    expect_near(retail_tool_pitch(60U, 0.0, 0.0), 0.0, "firearms keep pitch 0");
}

void observer_tools_animate_and_use_remote_initial_positions() {
    // Character.draw's generic branch calls Tool.apply_transform(i) with the
    // default animation position/orientation for remote characters.
    const auto rifle = evaluate_retail_third_person_pose(60U, 1U, 0.1, 1U, 0.0);
    expect(rifle.tool_parts[0U].position.x < 0.0 && rifle.tool_parts[0U].orientation_degrees.x < 0.0,
           "observers see AnimWeaponShoot recoil");
    const auto knife = evaluate_retail_third_person_pose(1U, 1U, 0.1, 1U, 0.0);
    expect(knife.tool_parts[0U].orientation_degrees.x > 0.0 && knife.tool_parts[0U].position.z > 0.0,
           "observers see the AnimUseKnife swing");
    expect_near(knife.tool_parts[0U].position.x, 0.0,
                "KnifeTool's initial_position is main-character only");
    const auto spade = evaluate_retail_third_person_pose(2U, 1U, 0.1, 1U, 0.0);
    expect(spade.tool_parts[0U].orientation_degrees.x > 0.0, "observers see AnimUseSpade");
    const auto block = evaluate_retail_third_person_pose(5U, 1U, 0.0, 1U, 0.0);
    expect_near(block.tool_parts[0U].position.x, 0.0,
                "BlockTool only starts AnimPlaceBlock on the local placing path");
    const auto grenade = evaluate_retail_third_person_pose(11U, 1U, 0.2, 1U, 0.0);
    expect_near(grenade.tool_parts[0U].position.x, 0.0,
                "the throw animation has stopped by the time an observer sees the throw");

    const auto rpg2 = evaluate_retail_third_person_pose(13U, 1U);
    expect_near(rpg2.tool_parts[0U].position.y, 0.3, "RPG2Weapon lifts remote models 0.3");
    const auto ugc_rpg2 = evaluate_retail_third_person_pose(46U, 1U);
    expect_near(ugc_rpg2.tool_parts[0U].position.y, 0.3, "UGC RPG2 inherits the remote lift");
    const auto turret = evaluate_retail_third_person_pose(16U, 3U);
    for (std::size_t part{}; part < 3U; ++part) {
        expect_near(turret.tool_parts[part].position.x, -0.75,
                    "remote turret parts share (-15, 0.18, 10) * 0.05");
        expect_near(turret.tool_parts[part].position.y, 0.009,
                    "remote turret parts are not stacked like the FPS model");
        expect_near(turret.tool_parts[part].position.z, 0.5,
                    "remote turret parts sit 10 authored units forward");
    }
    const auto minigun = evaluate_retail_third_person_pose(8U, 2U, 0.1, 1U, 0.0);
    expect_near(minigun.tool_parts[0U].position.x, 0.0,
                "the MINIGUN branch never applies AnimWeaponShoot to observers");
}

} // namespace

int main() {
    try {
        arm_chain_matches_character_reset_tp_arms();
        display_vectors_match_retail_draw_axis_order();
        zombie_tools_own_their_visible_hands();
        minigun_barrel_rolls_about_its_recovered_pivot();
        hidden_or_unavailable_weapon_keeps_observer_class_arms();
        minigun_barrel_keeps_its_authored_third_person_offset();
        muzzle_attachments_match_retail_weapon_subclasses();
        root_yaw_matches_retail_orientation_basis();
        aim_pitch_uses_retail_joint_pivots();
        riot_shield_uses_its_recovered_arm_pitch_range_and_bash();
        walk_cycle_matches_character_update_animation();
        running_zombie_legs_stay_inside_the_retail_human_arc();
        digging_tools_use_retail_pitch_and_range();
        observer_tools_animate_and_use_remote_initial_positions();
        std::cout << "retail third-person character pose tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
