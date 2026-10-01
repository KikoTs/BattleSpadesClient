#include "battlespades/world/weapon_runtime.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using battlespades::world::WeaponAction;
using battlespades::world::WeaponActionKind;
using battlespades::world::WeaponMechanism;
using battlespades::world::WeaponRuntime;
using battlespades::world::WeaponRuntimeContext;
using battlespades::world::weapon_catalog;

void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

void tick_for(WeaponRuntime& runtime, double seconds) {
    constexpr double dt{1.0 / 60.0};
    for (double elapsed{}; elapsed < seconds; elapsed += dt) {
        runtime.tick(dt);
    }
}

std::size_t count(std::span<const WeaponAction> actions,
                  WeaponActionKind kind) {
    std::size_t result{};
    for (const auto& action : actions) {
        result += action.kind == kind ? 1U : 0U;
    }
    return result;
}

void every_original_tool_has_a_concrete_mechanism() {
    const auto catalog = weapon_catalog();
    expect(catalog.size() == 65U, "all original selectable tools must exist");
    for (const auto& weapon : catalog) {
        const bool intentionally_inert = weapon.tool_id == 39U ||
                                         weapon.tool_id == 40U;
        expect(intentionally_inert ==
                   (weapon.mechanism == WeaponMechanism::inert),
               "only the retail Null and Fake Pistol tools may be inert");
    }
}

/** Character's sprint rule is melee membership OR an explicit Tool flag. */
void sprint_use_matches_retail_character_and_tool_rules() {
    const auto run_once = [](std::uint8_t tool_id, bool secondary) {
        WeaponRuntime runtime{0x5A17U};
        const std::array<std::uint8_t, 1U> loadout{tool_id};
        runtime.replace_loadout(loadout, tool_id);
        runtime.set_context(WeaponRuntimeContext{true, false, false, true});
        if (secondary) {
            runtime.set_secondary(true);
        } else {
            runtime.set_primary(true);
        }
        runtime.tick(1.0 / 60.0);
        return runtime.take_actions();
    };

    const auto zombie = run_once(24U, false);
    expect(count(zombie, WeaponActionKind::melee) == 1U,
           "Zombie hands must claw while sprinting");
    const auto spade_secondary = run_once(45U, true);
    expect(count(spade_secondary, WeaponActionKind::melee) == 1U,
           "melee secondary attacks must remain usable while sprinting");

    expect(run_once(7U, false).empty(),
           "an SMG must not fire while sprinting");
    expect(run_once(28U, false).empty(),
           "the Zombie prefab tool must not place while sprinting");
    expect(run_once(11U, false).empty(),
           "a normal cooked grenade must not prime while sprinting");

    const auto ugc_prefab = run_once(42U, false);
    expect(count(ugc_prefab, WeaponActionKind::prefab_place) == 1U,
           "UGC prefab primary must retain its explicit sprint exception");
    const auto spray = run_once(43U, true);
    expect(count(spray, WeaponActionKind::paint_area) == 1U,
           "paintbrush secondary must retain its explicit sprint exception");
}

void firearms_repeat_burst_spin_and_expand_pellets() {
    WeaponRuntime runtime{123U};
    const std::array<std::uint8_t, 5U> loadout{7U, 8U, 9U, 60U, 62U};
    runtime.replace_loadout(loadout, std::uint8_t{7U});
    runtime.set_primary(true);
    runtime.tick(1.0 / 60.0);
    tick_for(runtime, 0.11);
    auto actions = runtime.take_actions();
    expect(count(actions, WeaponActionKind::hitscan) >= 2U,
           "automatic SMG must repeat while primary is held");
    runtime.set_primary(false);
    runtime.tick(1.0 / 60.0);

    expect(runtime.select(9U) == battlespades::world::WeaponStateResult::accepted,
           "shotgun must be selectable");
    runtime.set_primary(true);
    runtime.tick(1.0 / 60.0);
    actions = runtime.take_actions();
    expect(actions.size() == 1U && actions.front().pellets == 10U,
           "shotgun trigger must emit one seeded ten-pellet action");
    runtime.set_primary(false);
    runtime.tick(1.0 / 60.0);

    expect(runtime.select(60U) == battlespades::world::WeaponStateResult::accepted,
           "assault rifle must be selectable");
    runtime.set_primary(true);
    runtime.tick(1.0 / 60.0);
    runtime.set_primary(false);
    tick_for(runtime, 0.25);
    actions = runtime.take_actions();
    expect(count(actions, WeaponActionKind::hitscan) == 3U,
           "one assault-rifle press must complete its retail three-round burst");

    expect(runtime.select(8U) == battlespades::world::WeaponStateResult::accepted,
           "minigun must be selectable");
    runtime.set_secondary(true);
    tick_for(runtime, 0.25);
    const double rotation_before = runtime.spin_rotation_fraction();
    tick_for(runtime, 0.1);
    expect(runtime.take_actions().empty() && runtime.spin_fraction() > 0.0 &&
               rotation_before != runtime.spin_rotation_fraction(),
           "RMB must pre-spin the minigun without spending ammunition");
    runtime.set_primary(true);
    runtime.tick(1.0 / 60.0);
    actions = runtime.take_actions();
    expect(count(actions, WeaponActionKind::hitscan) == 1U,
           "a pre-spun minigun must fire when LMB is pressed");
    runtime.set_primary(false);
    runtime.set_secondary(false);
    runtime.tick(1.0 / 60.0);
    expect(runtime.select(7U) == battlespades::world::WeaponStateResult::accepted &&
               runtime.select(8U) == battlespades::world::WeaponStateResult::accepted,
           "reselecting minigun must reset the spin motor");
    runtime.set_custom(true);
    tick_for(runtime, 0.25);
    expect(runtime.take_actions().empty() && runtime.spin_fraction() == 0.0,
           "weapon-custom must not impersonate the minigun RMB motor");
    runtime.set_custom(false);
}

void crosshair_tracks_retail_accuracy_and_fixed_tool_rules() {
    WeaponRuntime runtime{0xC2055U};
    runtime.replace_loadout(std::array<std::uint8_t, 3U>{2U, 7U, 18U},
                            std::uint8_t{2U});

    // SpadeTool is a Tool, not a Weapon: no get_accuracy, so the reticle
    // stays at the one-pixel minimum (retail's small square, live A/B).
    expect(runtime.crosshair_radius_pixels(900.0, 75.0, false) == 1.0,
           "melee tools must draw retail's fixed small-square reticle");

    expect(runtime.select(18U) == battlespades::world::WeaponStateResult::accepted,
           "sniper must be selectable for reticle parity");
    const double focal = 900.0 * 0.5 /
                         std::tan(75.0 * 0.5 * std::acos(-1.0) / 180.0);
    const double expected_hip_sniper = focal * 0.025 * 2.0 - 3.5;
    expect(std::fabs(runtime.crosshair_radius_pixels(900.0, 75.0, false) -
                     expected_hip_sniper) < 1.0e-9,
           "hip-fire sniper reticle must project its recovered 0.025 cone");
    expect(runtime.crosshair_radius_pixels(900.0, 18.75, true) == 1.0,
           "scoped sniper accuracy zero must collapse to the fixed one-pixel floor");

    expect(runtime.select(7U) == battlespades::world::WeaponStateResult::accepted,
           "SMG must be selectable for dynamic recoil reticle parity");
    const double settled = runtime.crosshair_radius_pixels(900.0, 75.0, false);
    runtime.set_primary(true);
    runtime.tick(1.0 / 60.0);
    const double kicked = runtime.crosshair_radius_pixels(900.0, 75.0, false);
    expect(kicked > settled,
           "an SMG shot must expand the reticle from the same live spread as bullets");
    runtime.set_primary(false);
    runtime.tick(1.0 / 60.0);
    tick_for(runtime, 1.0);
    const double recovered = runtime.crosshair_radius_pixels(900.0, 75.0, false);
    expect(recovered < kicked && recovered >= settled,
           "the dynamic reticle must recover toward, but never under, spread_min");
}

void delayed_spade_secondary_matches_retail_windup() {
    WeaponRuntime runtime{991U};
    runtime.replace_loadout(std::array<std::uint8_t, 2U>{4U, 45U},
                            std::uint8_t{4U});
    runtime.set_secondary(true);
    tick_for(runtime, 0.5);
    expect(runtime.take_actions().empty(),
           "Classic Spade RMB must not dig before its 0.8 second wind-up");
    tick_for(runtime, 0.32);
    auto actions = runtime.take_actions();
    expect(actions.size() == 1U && actions.front().kind == WeaponActionKind::melee &&
               actions.front().secondary,
           "a completed Classic Spade RMB wind-up must emit secondary melee");
    runtime.set_secondary(false);
    runtime.tick(1.0 / 60.0);

    expect(runtime.select(45U) == battlespades::world::WeaponStateResult::accepted,
           "UGC Super Spade must be selectable");
    runtime.set_secondary(true);
    runtime.tick(1.0 / 60.0);
    actions = runtime.take_actions();
    expect(actions.size() == 1U && actions.front().secondary,
           "UGC Super Spade RMB must remain immediate and carry its area flag");
}

void throwables_preserve_cook_and_charge_release_rules() {
    WeaponRuntime runtime{456U};
    const std::array<std::uint8_t, 3U> loadout{11U, 33U, 57U};
    runtime.replace_loadout(loadout, std::uint8_t{11U});
    runtime.set_primary(true);
    tick_for(runtime, 1.0);
    auto actions = runtime.take_actions();
    expect(actions.size() == 1U &&
               actions.front().kind == WeaponActionKind::throwable_primed,
           "cooked grenade must pull its pin once while remaining in hand");
    runtime.set_primary(false);
    runtime.tick(1.0 / 60.0);
    actions = runtime.take_actions();
    expect(actions.size() == 1U &&
               actions.front().kind == WeaponActionKind::oriented_item &&
               actions.front().value > 1.4 && actions.front().value < 1.6,
           "grenade release must carry its remaining fuse");
    expect(runtime.replication().ammo(11U)->magazine == 1U,
           "grenade must consume exactly one of the retail initial two");

    expect(runtime.select(33U) == battlespades::world::WeaponStateResult::accepted,
           "molotov must be selectable");
    runtime.set_primary(true);
    tick_for(runtime, 1.5);
    expect(runtime.charge_fraction() > 0.49 && runtime.charge_fraction() < 0.52,
           "molotov hold must produce normalized distance charge");
    runtime.set_primary(false);
    runtime.tick(1.0 / 60.0);
    actions = runtime.take_actions();
    expect(actions.size() == 1U &&
               actions.front().kind == WeaponActionKind::oriented_item &&
               actions.front().value > 0.49 && actions.front().value < 0.52,
           "molotov release must preserve its charge in the semantic action");

    constexpr std::array<std::uint8_t, 4U> grenade_variants{31U, 32U, 54U, 57U};
    for (const std::uint8_t tool_id : grenade_variants) {
        WeaponRuntime variant{static_cast<std::uint32_t>(900U + tool_id)};
        variant.replace_loadout(std::array<std::uint8_t, 1U>{tool_id}, tool_id);
        variant.set_primary(true);
        tick_for(variant, 0.2);
        variant.set_primary(false);
        variant.tick(1.0 / 60.0);
        const auto variant_actions = variant.take_actions();
        expect(count(variant_actions, WeaponActionKind::oriented_item) == 1U &&
                   count(variant_actions, WeaponActionKind::throwable_primed) ==
                       (tool_id == 31U || tool_id == 32U ? 1U : 0U),
               "every retail grenade variant must release one oriented projectile");
    }
}

void builders_deployables_and_special_tools_have_distinct_actions() {
    WeaponRuntime runtime{789U};
    const std::array<std::uint8_t, 9U> loadout{
        5U, 16U, 23U, 29U, 43U, 59U, 63U, 64U, 41U};
    runtime.replace_loadout(loadout, std::uint8_t{5U});
    runtime.set_primary(true);
    runtime.tick(1.0 / 60.0);
    runtime.set_primary(false);
    runtime.tick(1.0 / 60.0);
    auto actions = runtime.take_actions();
    expect(count(actions, WeaponActionKind::block_line_begin) == 1U &&
               count(actions, WeaponActionKind::block_line_commit) == 1U,
           "block tool must expose its drag-begin and BlockLine commit edges");

    runtime.set_primary(true);
    runtime.tick(1.0 / 60.0);
    static_cast<void>(runtime.take_actions());
    runtime.set_secondary(true);
    runtime.tick(1.0 / 60.0);
    actions = runtime.take_actions();
    expect(count(actions, WeaponActionKind::block_line_cancel) == 1U,
           "block-tool RMB must cancel the active line locally");
    runtime.set_primary(false);
    runtime.set_secondary(false);
    runtime.tick(1.0 / 60.0);
    expect(count(runtime.take_actions(), WeaponActionKind::block_line_commit) == 0U,
           "a cancelled line must not commit on the later LMB release");
    runtime.set_custom(true);
    runtime.tick(1.0 / 60.0);
    actions = runtime.take_actions();
    expect(count(actions, WeaponActionKind::color_pick) == 1U,
           "weapon-custom must own the block eyedropper edge");
    runtime.set_custom(false);
    runtime.tick(1.0 / 60.0);

    for (const std::uint8_t color_tool :
         std::array<std::uint8_t, 2U>{29U, 43U}) {
        expect(runtime.select(color_tool) ==
                   battlespades::world::WeaponStateResult::accepted,
               "retail color-consuming UGC tool must be selectable");
        runtime.set_custom(true);
        runtime.tick(1.0 / 60.0);
        actions = runtime.take_actions();
        expect(count(actions, WeaponActionKind::color_pick) == 1U,
               "Block Cannon and Paintbrush must share the block eyedropper");
        runtime.set_custom(false);
        runtime.tick(1.0 / 60.0);
    }

    expect(runtime.select(16U) == battlespades::world::WeaponStateResult::accepted,
           "rocket turret must be selectable");
    runtime.set_primary(true);
    runtime.tick(1.0 / 60.0);
    actions = runtime.take_actions();
    expect(count(actions, WeaponActionKind::deployable_place) == 1U,
           "rocket turret must emit a placement action");
    runtime.set_primary(false);
    runtime.tick(1.0 / 60.0);

    expect(runtime.select(59U) == battlespades::world::WeaponStateResult::accepted,
           "C4 must be selectable");
    runtime.set_secondary(true);
    runtime.tick(1.0 / 60.0);
    actions = runtime.take_actions();
    expect(count(actions, WeaponActionKind::c4_detonate) == 1U,
           "C4 secondary must detonate rather than place another charge");
    runtime.set_secondary(false);
    runtime.tick(1.0 / 60.0);

    expect(runtime.select(63U) == battlespades::world::WeaponStateResult::accepted,
           "Block Sucker must be selectable");
    runtime.set_primary(true);
    runtime.tick(1.0 / 60.0);
    expect(runtime.block_sucker_state() == 1U,
           "Block Sucker must enter the recovered warm-up state on press");
    tick_for(runtime, 1.1);
    actions = runtime.take_actions();
    expect(count(actions, WeaponActionKind::block_sucker_state) >= 2U &&
               count(actions, WeaponActionKind::block_suck) > 0U,
           "Block Sucker must warm up, enter full power and pulse actions");
    expect(runtime.block_sucker_state() == 2U,
           "Block Sucker must enter full power after the one-second warm-up");
    const auto powered_shake = runtime.block_sucker_shake();
    expect(std::abs(powered_shake[0U]) > 0.000001 ||
               std::abs(powered_shake[1U]) > 0.000001,
           "full-power Block Sucker must shake the tool and hands");
    runtime.set_primary(false);
    runtime.tick(1.0 / 60.0);
    actions = runtime.take_actions();
    expect(count(actions, WeaponActionKind::block_sucker_state) == 1U &&
               actions.front().value == 0.0,
           "releasing Block Sucker must replicate the inactive state");
    expect(runtime.block_sucker_state() == 0U,
           "released Block Sucker must become inactive immediately");
    tick_for(runtime, 1.1);
    const auto settled_shake = runtime.block_sucker_shake();
    expect(std::abs(settled_shake[0U]) < 0.000001 &&
               std::abs(settled_shake[1U]) < 0.000001,
           "Block Sucker shake must settle to zero one second after release");
}

void mounted_machine_gun_keeps_fire_and_deployment_separate() {
    WeaponRuntime runtime{12U};
    runtime.replace_loadout(std::array<std::uint8_t, 1U>{15U},
                            std::uint8_t{15U});
    // MGWeapon: RMB has to be HELD for MG_DEPLOYMENT_TIME / MG_WITHDRAWAL_TIME
    // (world::MachineGunDeployment); the press itself is no tool action.
    runtime.set_secondary(true);
    runtime.tick(0.01);
    auto actions = runtime.take_actions();
    expect(actions.empty(), "an MG secondary press alone must not place anything");

    runtime.set_secondary(false);
    runtime.tick(0.01);
    runtime.set_context(WeaponRuntimeContext{false, true, false});
    runtime.set_secondary(true);
    runtime.tick(0.01);
    actions = runtime.take_actions();
    expect(actions.empty(), "a deployed MG secondary press alone must not dismount");

    runtime.set_secondary(false);
    runtime.set_primary(true);
    runtime.tick(0.01);
    actions = runtime.take_actions();
    expect(count(actions, WeaponActionKind::hitscan) == 1U,
           "the deployed gun still fires with the primary trigger");
}

void invalid_deployable_targets_do_not_spend_stock_or_emit_packets() {
    WeaponRuntime runtime{121U};
    runtime.replace_loadout(std::array<std::uint8_t, 1U>{20U},
                            std::uint8_t{20U});
    const auto* before = runtime.replication().ammo(20U);
    expect(before != nullptr, "landmine must expose its finite placement stock");
    const auto stock = before->magazine;

    runtime.set_context(WeaponRuntimeContext{false, false, false, false});
    runtime.set_primary(true);
    runtime.tick(1.0 / 60.0);
    {
        const auto refused = runtime.take_actions();
        expect(refused.size() == 1U &&
                   refused.front().kind == WeaponActionKind::placement_rejected &&
                   count(refused, WeaponActionKind::deployable_place) == 0U,
               "a missing retail ghost target must suppress landmine placement and "
               "report one BUILD_ERROR edge");
    }
    runtime.tick(1.0 / 60.0);
    expect(runtime.take_actions().empty(),
           "holding the trigger on an invalid target must not repeat build_error");
    expect(runtime.replication().ammo(20U)->magazine == stock,
           "an invalid landmine target must not consume local stock");

    runtime.set_primary(false);
    runtime.tick(1.0 / 60.0);
    runtime.set_context(WeaponRuntimeContext{false, false, false, true});
    runtime.set_primary(true);
    runtime.tick(1.0 / 60.0);
    const auto actions = runtime.take_actions();
    expect(count(actions, WeaponActionKind::deployable_place) == 1U,
           "a valid landmine target must emit exactly one placement edge");
    expect(runtime.replication().ammo(20U)->magazine + 1U == stock,
           "a valid landmine placement must consume exactly one stock item");

    WeaponRuntime mounted{122U};
    mounted.replace_loadout(std::array<std::uint8_t, 1U>{15U},
                            std::uint8_t{15U});
    mounted.set_context(WeaponRuntimeContext{false, false, false, false});
    mounted.set_secondary(true);
    mounted.tick(1.0 / 60.0);
    expect(mounted.take_actions().empty(),
           "mounted gun deployment must also require a valid floor target");
}

void reloads_match_retail_magazine_and_shell_cycles() {
    WeaponRuntime runtime{711U};
    runtime.replace_loadout(std::array<std::uint8_t, 2U>{17U, 9U},
                            std::uint8_t{17U});

    runtime.set_primary(true);
    runtime.tick(1.0 / 60.0);
    runtime.set_primary(false);
    runtime.tick(1.0 / 60.0);
    static_cast<void>(runtime.take_actions());
    const auto pistol_before = *runtime.replication().ammo(17U);
    expect(runtime.request_reload() ==
               battlespades::world::WeaponStateResult::accepted,
           "pistol magazine reload must start when reserve ammunition exists");
    auto actions = runtime.take_actions();
    expect(count(actions, WeaponActionKind::reload_started) == 1U,
           "reload start must emit the Protocol 168 start edge");
    tick_for(runtime, weapon_catalog()[17U].reload_time + 0.02);
    actions = runtime.take_actions();
    const auto pistol_after = *runtime.replication().ammo(17U);
    expect(count(actions, WeaponActionKind::reload_completed) == 1U &&
               pistol_after.magazine > pistol_before.magazine &&
               !pistol_after.reloading,
           "magazine reload must finish once and atomically transfer rounds");

    expect(runtime.select(9U) ==
               battlespades::world::WeaponStateResult::accepted,
           "shell-loaded shotgun must be selectable");
    runtime.set_primary(true);
    runtime.tick(1.0 / 60.0);
    runtime.set_primary(false);
    runtime.tick(1.0 / 60.0);
    tick_for(runtime, weapon_catalog()[9U].fire_interval + 0.02);
    runtime.set_primary(true);
    runtime.tick(1.0 / 60.0);
    runtime.set_primary(false);
    runtime.tick(1.0 / 60.0);
    static_cast<void>(runtime.take_actions());
    const auto shells_before = runtime.replication().ammo(9U)->magazine;
    expect(runtime.request_reload() ==
               battlespades::world::WeaponStateResult::accepted,
           "shotgun shell reload must start");
    static_cast<void>(runtime.take_actions());
    tick_for(runtime, weapon_catalog()[9U].reload_time + 0.02);
    actions = runtime.take_actions();
    expect(count(actions, WeaponActionKind::reload_completed) == 1U &&
               count(actions, WeaponActionKind::reload_started) == 1U &&
               std::ranges::find(actions, WeaponActionKind::reload_completed,
                                 &WeaponAction::kind)->value == 0.0 &&
               runtime.replication().ammo(9U)->magazine == shells_before + 1U &&
               runtime.reload_remaining() > 0.0,
           "clip_reload must complete one shell then begin the next retail cycle");
}

void block_cannon_repeats_and_empty_firearms_auto_reload() {
    WeaponRuntime cannon{812U};
    cannon.replace_loadout(std::array<std::uint8_t, 1U>{29U},
                           std::uint8_t{29U});
    cannon.set_primary(true);
    tick_for(cannon, 1.0);
    const auto cannon_actions = cannon.take_actions();
    expect(count(cannon_actions, WeaponActionKind::oriented_item) >= 2U,
           "Block Cannon must keep launching while primary remains held");

    WeaponRuntime rifle{813U};
    rifle.replace_loadout(std::array<std::uint8_t, 1U>{7U},
                          std::uint8_t{7U});
    rifle.set_primary(true);
    std::size_t reload_starts{};
    for (int tick{}; tick < 60 * 8 && reload_starts == 0U; ++tick) {
        rifle.tick(1.0 / 60.0);
        reload_starts += count(rifle.take_actions(),
                               WeaponActionKind::reload_started);
    }
    const auto* ammo = rifle.replication().ammo(7U);
    expect(reload_starts == 1U && ammo != nullptr && ammo->reloading &&
               rifle.reload_remaining() > 0.0,
           "a firearm's last round must automatically start one reload");
}

} // namespace

/**
 * The empty magazine clicks once per trigger PRESS, never per tick.
 *
 * Retail plays its `empty` cue inside the fire call and then clears
 * `Character.shoot_primary`, which makes `can_shoot_primary` short-circuit so
 * the fire path is not entered again until the trigger is released and pressed
 * afresh. Held on an empty automatic, our runtime used to re-enter every tick
 * and emit sixty cues a second -- measured against this same runtime before the
 * fix, and audible as a continuous rattle rather than a click.
 *
 * The upper bound is the whole point of the test, so it is asserted as a hard
 * count rather than "not too many".
 */
void the_empty_magazine_clicks_once_per_trigger_pull() {
    WeaponRuntime runtime{4242U};
    const std::array<std::uint8_t, 1U> loadout{7U};
    runtime.replace_loadout(loadout, std::uint8_t{7U});

    // Drain magazine and reserve with the trigger held down.
    runtime.set_primary(true);
    for (int tick{}; tick < 60 * 120; ++tick) {
        runtime.tick(1.0 / 60.0);
        static_cast<void>(runtime.take_actions());
    }

    // Still held, now empty: retail is silent here.
    std::size_t held_cues{};
    for (int tick{}; tick < 60; ++tick) {
        runtime.tick(1.0 / 60.0);
        held_cues += count(runtime.take_actions(), WeaponActionKind::dry_fire);
    }
    expect(held_cues == 0U,
           "holding the trigger on an empty magazine must stay silent after the "
           "first click");

    // Release and press again: exactly one click for the new trigger pull.
    runtime.set_primary(false);
    runtime.tick(1.0 / 60.0);
    static_cast<void>(runtime.take_actions());
    runtime.set_primary(true);
    std::size_t press_cues{};
    for (int tick{}; tick < 30; ++tick) {
        runtime.tick(1.0 / 60.0);
        press_cues += count(runtime.take_actions(), WeaponActionKind::dry_fire);
    }
    expect(press_cues == 1U,
           "a fresh trigger pull on an empty magazine must click exactly once");
}

/**
 * The empty-cue latch must not disturb firing, ammunition or cadence.
 *
 * WeaponRuntime is shared with network prediction against an authoritative
 * server, so a presentation change that shifted a cooldown would surface as
 * desync rather than as a sound bug.
 */
void the_empty_cue_latch_does_not_change_firing() {
    const auto shots_in = [](double seconds) {
        WeaponRuntime runtime{99U};
        const std::array<std::uint8_t, 1U> loadout{7U};
        runtime.replace_loadout(loadout, std::uint8_t{7U});
        runtime.set_primary(true);
        std::size_t shots{};
        constexpr double dt{1.0 / 60.0};
        for (double elapsed{}; elapsed < seconds; elapsed += dt) {
            runtime.tick(dt);
            shots += count(runtime.take_actions(), WeaponActionKind::hitscan);
        }
        return shots;
    };
    // A full magazine's worth of held trigger must still produce shots, and the
    // count must be governed by the fire interval rather than by the latch.
    const auto burst = shots_in(0.5);
    expect(burst > 1U, "the latch must not suppress ordinary automatic fire");
}

/**
 * The minigun's barrel speed ramps, and the audio layer pitches its loop by it.
 *
 * Retail authors NO spin-up and NO spin-down sample. The build-up the player
 * hears is one infinite loop whose pitch is the barrel speed as a raw playback
 * ratio, so this curve IS the sound. If it ever became a step function the
 * weapon would snap to full speed silently-instantly, which is the complaint
 * this fixes.
 *
 * Tool 8 is the minigun. That is not obvious -- an earlier probe of this same
 * runtime assumed 60 and measured a flat zero, which looks identical to a broken
 * ramp.
 */
void the_minigun_spin_ramps_up_and_down() {
    WeaponRuntime runtime{5150U};
    const std::array<std::uint8_t, 1U> loadout{8U};
    runtime.replace_loadout(loadout, std::uint8_t{8U});
    constexpr double dt{1.0 / 60.0};

    expect(runtime.spin_fraction() == 0.0, "a freshly equipped minigun must be still");

    // Secondary alone spins the barrels without firing.
    runtime.set_secondary(true);
    double previous = runtime.spin_fraction();
    bool saw_intermediate = false;
    for (int tick{}; tick < 60 * 3; ++tick) {
        runtime.tick(dt);
        static_cast<void>(runtime.take_actions());
        const auto current = runtime.spin_fraction();
        expect(current >= 0.0 && current <= 1.0, "spin fraction escaped 0..1");
        expect(current >= previous - 1.0e-9, "spin must not fall while held");
        if (current > 0.05 && current < 0.95) {
            saw_intermediate = true;
        }
        previous = current;
    }
    expect(saw_intermediate,
           "the spin went straight to full; there would be no audible build-up");
    expect(previous > 0.99, "holding secondary must reach full barrel speed");

    // Release: it must wind DOWN rather than cut out, since there is no
    // spin-down sample to cover a hard stop.
    runtime.set_secondary(false);
    bool saw_descent = false;
    for (int tick{}; tick < 60 * 5; ++tick) {
        runtime.tick(dt);
        static_cast<void>(runtime.take_actions());
        const auto current = runtime.spin_fraction();
        expect(current <= previous + 1.0e-9, "spin must not rise while released");
        if (current > 0.05 && current < 0.95) {
            saw_descent = true;
        }
        previous = current;
    }
    expect(saw_descent, "the spin cut out instead of winding down");
    expect(previous == 0.0, "the barrels must come to a complete stop");
}

/**
 * Reloading makes the retail minigun motor wind down even while RMB is held.
 *
 * `MinigunWeapon.update` selects its inactive alteration whenever
 * `Character.reloading` is true.  The weapon update itself keeps running, so
 * the interval moves back toward 0.3 at +0.075/s instead of freezing at the
 * pre-reload value.  A full-speed motor therefore reaches a 0.625 spin
 * fraction after one second of the recovered two-second reload.
 */
void the_minigun_winds_down_during_reload() {
    WeaponRuntime runtime{5151U};
    runtime.replace_loadout(std::array<std::uint8_t, 1U>{8U},
                            std::uint8_t{8U});
    constexpr double dt{1.0 / 60.0};

    runtime.set_secondary(true);
    tick_for(runtime, 2.0);
    expect(runtime.spin_fraction() > 0.99,
           "the reload fixture must begin with a fully spun minigun");

    runtime.set_primary(true);
    runtime.tick(dt);
    runtime.set_primary(false);
    runtime.tick(dt);
    static_cast<void>(runtime.take_actions());
    expect(runtime.request_reload() ==
               battlespades::world::WeaponStateResult::accepted,
           "firing one minigun round must make a magazine reload possible");
    static_cast<void>(runtime.take_actions());

    tick_for(runtime, 1.0);
    const auto actions = runtime.take_actions();
    expect(count(actions, WeaponActionKind::hitscan) == 0U,
           "a reloading minigun must not fire while its held motor winds down");
    expect(std::fabs(runtime.spin_fraction() - 0.625) < 0.02,
           "reload must apply retail's inactive +0.075 interval ramp");
}

/** Tool.on_unset is a hard lifecycle edge, unlike ordinary input release. */
void minigun_unset_resets_motor_reload_and_pending_edges() {
    WeaponRuntime runtime{5152U};
    runtime.replace_loadout(std::array<std::uint8_t, 2U>{8U, 17U},
                            std::uint8_t{8U});
    constexpr double dt{1.0 / 60.0};

    runtime.set_secondary(true);
    tick_for(runtime, 2.0);
    expect(runtime.spin_fraction() > 0.99 &&
               runtime.spin_rotation_fraction() > 0.0,
           "unset fixture must begin with an animated full-speed motor");

    runtime.set_primary(true);
    runtime.tick(dt);
    runtime.set_primary(false);
    runtime.tick(dt);
    static_cast<void>(runtime.take_actions());
    expect(runtime.request_reload() ==
               battlespades::world::WeaponStateResult::accepted,
           "unset fixture must own an active reload");
    expect(runtime.reload_remaining() > 0.0,
           "accepted reload must expose its remaining time");

    runtime.on_unset();
    expect(runtime.spin_fraction() == 0.0 &&
               runtime.spin_rotation_fraction() == 0.0 &&
               runtime.reload_remaining() == 0.0 &&
               runtime.cooldown_remaining() == 0.0 &&
               runtime.take_actions().empty(),
           "on_unset must synchronously clear motor, reload, cooldown and stale actions");

    // Held buttons were released by on_unset; time alone cannot silently
    // re-cook the motor in the new life.
    tick_for(runtime, 1.0);
    expect(runtime.spin_fraction() == 0.0 && runtime.take_actions().empty(),
           "an unset minigun must remain idle until a fresh input edge");

    // Starting another reload proves the replication flag was cancelled too,
    // not merely hidden behind a zero presentation timer.
    expect(runtime.request_reload() ==
               battlespades::world::WeaponStateResult::accepted,
           "on_unset must cancel the old Character reload ownership");
    runtime.on_unset();

    // Switching away mid-reload used to leave tool 8 permanently marked as
    // reloading when selected again.
    expect(runtime.request_reload() ==
               battlespades::world::WeaponStateResult::accepted,
           "second reload fixture must start");
    expect(runtime.select(17U) ==
               battlespades::world::WeaponStateResult::accepted &&
               runtime.select(8U) ==
                   battlespades::world::WeaponStateResult::accepted,
           "tool selection must perform the same unset lifecycle");
    expect(runtime.request_reload() ==
               battlespades::world::WeaponStateResult::accepted,
           "returning to a weapon switched away from mid-reload must not deadlock reload");
}

/**
 * A burst fires three rounds but announces only one.
 *
 * Retail's burst sample IS the whole three-round recording -- its envelope has
 * transients at 0, 60 and 130 ms -- and retail plays it once when the burst
 * starts, leaving the follow-up rounds silent. Playing it per round stacks three
 * bursts of audio over one trigger pull.
 *
 * The bullet count is asserted alongside deliberately: the flag must mark rounds
 * for the audio layer WITHOUT suppressing them, since dropping two would also
 * drop two bullets, two ammo decrements and two recoil kicks, and desync from an
 * authoritative server.
 */
void a_burst_announces_once_but_still_fires_three_rounds() {
    WeaponRuntime runtime{321U};
    const std::array<std::uint8_t, 1U> loadout{60U};
    runtime.replace_loadout(loadout, std::uint8_t{60U});

    runtime.set_primary(true);
    runtime.tick(1.0 / 60.0);
    runtime.set_primary(false);
    std::size_t rounds{};
    std::size_t announced{};
    bool first_was_the_opener = false;
    bool seen_any = false;
    constexpr double dt{1.0 / 60.0};
    for (double elapsed{}; elapsed < 0.45; elapsed += dt) {
        runtime.tick(dt);
        for (const auto& action : runtime.take_actions()) {
            if (action.kind != WeaponActionKind::hitscan) {
                continue;
            }
            if (!seen_any) {
                seen_any = true;
                first_was_the_opener = !action.burst_follow_up;
            }
            ++rounds;
            announced += action.burst_follow_up ? 0U : 1U;
        }
    }
    expect(rounds == 3U, "one burst trigger pull must still fire exactly 3 rounds");
    expect(announced == 1U, "a burst must announce exactly once");
    expect(first_was_the_opener, "the announced round must be the first of the burst");
}

/** The presentation flag must never leak onto a non-burst weapon. */
void only_burst_weapons_mark_follow_up_rounds() {
    const auto catalog = weapon_catalog();
    for (const auto& weapon : catalog) {
        if (weapon.mechanism == WeaponMechanism::firearm_burst) {
            continue;
        }
        WeaponRuntime runtime{17U};
        const std::array<std::uint8_t, 1U> loadout{weapon.tool_id};
        runtime.replace_loadout(loadout, weapon.tool_id);
        runtime.set_primary(true);
        constexpr double dt{1.0 / 60.0};
        for (double elapsed{}; elapsed < 0.6; elapsed += dt) {
            runtime.tick(dt);
            for (const auto& action : runtime.take_actions()) {
                expect(!action.burst_follow_up,
                       "a non-burst weapon marked an action as a burst follow-up");
            }
        }
    }
}

/**
 * A TAP during a clip_reload shell cycle is forgotten: set_primary_shoot
 * (character.pyd 0x10028290) only writes the never-read `reload_cancel`
 * while reloading, and the release clears shoot_primary again, so
 * Character.end_reload (0x1001FC00) keeps chaining shells to a full tube and
 * nothing fires on its own.
 */
void a_shell_reload_tap_does_not_interrupt_the_chain() {
    WeaponRuntime runtime{9091U};
    runtime.replace_loadout(std::array<std::uint8_t, 1U>{9U}, std::uint8_t{9U});
    const auto& shotgun = weapon_catalog()[9U];
    expect(shotgun.retail.ammo.clip_reload, "the shotgun must be a clip_reload weapon");
    const auto capacity = shotgun.retail.ammo.magazine_capacity.value_or(shotgun.clip_size);
    for (int shot{}; shot < 2; ++shot) {
        runtime.set_primary(true);
        runtime.tick(1.0 / 60.0);
        runtime.set_primary(false);
        tick_for(runtime, shotgun.fire_interval + 0.05);
    }
    static_cast<void>(runtime.take_actions());
    expect(runtime.request_reload() == battlespades::world::WeaponStateResult::accepted,
           "the shotgun shell reload must start");
    runtime.tick(1.0 / 60.0);
    runtime.set_primary(true);
    runtime.tick(1.0 / 60.0);
    runtime.set_primary(false);
    std::size_t shots{};
    for (int tick{}; tick < 600 && runtime.reload_remaining() > 0.0; ++tick) {
        runtime.tick(1.0 / 60.0);
        shots += count(runtime.take_actions(), WeaponActionKind::hitscan);
    }
    tick_for(runtime, 0.5);
    shots += count(runtime.take_actions(), WeaponActionKind::hitscan);
    expect(shots == 0U, "a tap during a shell reload must never fire");
    expect(runtime.replication().ammo(9U)->magazine == capacity,
           "the tapped shell chain still fills the tube");
}

/**
 * V1: Character.end_reload (character.pyd 0x1001FC00) stops the chain on
 * `shoot_primary` (the trigger HELD at the shell boundary) and on
 * `shoot_primary_held` (the emptying shot had the trigger down and it was
 * never released), then fires. Releasing the trigger runs
 * set_primary_shoot(False) (0x10028290), which clears shoot_primary_held:
 * the chain then runs to a full tube and nothing fires on its own.
 */
void a_held_trigger_stops_the_shell_chain_and_resumes_fire() {
    const auto& shotgun = weapon_catalog()[9U];
    const auto reload_time = shotgun.retail.use.reload_time.value_or(shotgun.reload_time);
    const auto capacity = shotgun.retail.ammo.magazine_capacity.value_or(shotgun.clip_size);
    {
        WeaponRuntime runtime{9092U};
        runtime.replace_loadout(std::array<std::uint8_t, 1U>{9U}, std::uint8_t{9U});
        // Fire one shell, reload, and press during the first shell, holding
        // through its boundary: the held trigger (shoot_primary) ends the
        // chain after that shell and fires it.
        runtime.set_primary(true);
        runtime.tick(1.0 / 60.0);
        runtime.set_primary(false);
        tick_for(runtime, shotgun.fire_interval + 0.05);
        static_cast<void>(runtime.take_actions());
        const auto magazine_before = runtime.replication().ammo(9U)->magazine;
        expect(runtime.request_reload() == battlespades::world::WeaponStateResult::accepted,
               "the manual shell reload must start");
        runtime.tick(1.0 / 60.0);
        runtime.set_primary(true);
        tick_for(runtime, reload_time);
        auto actions = runtime.take_actions();
        const auto completed = std::ranges::find(actions, WeaponActionKind::reload_completed,
                                                 &WeaponAction::kind);
        const auto shot = std::ranges::find(actions, WeaponActionKind::hitscan,
                                            &WeaponAction::kind);
        expect(completed != actions.end() && completed->value == 1.0 &&
                   runtime.reload_remaining() == 0.0 &&
                   count(actions, WeaponActionKind::reload_started) == 1U,
               "a held trigger stops the chain after the shell in progress");
        expect(shot != actions.end() && shot > completed &&
                   count(actions, WeaponActionKind::hitscan) == 1U,
               "the trigger still held at the shell boundary fires once");
        expect(runtime.replication().ammo(9U)->magazine == magazine_before,
               "exactly one shell was loaded and then fired");
    }
    const auto empty_the_tube = [&](WeaponRuntime& runtime) {
        while (runtime.replication().ammo(9U)->magazine > 1U) {
            runtime.set_primary(true);
            runtime.tick(1.0 / 60.0);
            runtime.set_primary(false);
            tick_for(runtime, shotgun.fire_interval + 0.05);
        }
        static_cast<void>(runtime.take_actions());
        runtime.set_primary(true);
        runtime.tick(1.0 / 60.0);
        expect(runtime.replication().ammo(9U)->magazine == 0U &&
                   runtime.reload_remaining() == 0.0,
               "the emptying shot only schedules the reload (weapon_shoot still plays)");
        tick_for(runtime, shotgun.fire_interval + 1.0 / 60.0);
        expect(runtime.reload_remaining() > 0.0,
               "the automatic shell reload starts once weapon_shoot ends");
        static_cast<void>(runtime.take_actions());
    };
    {
        WeaponRuntime runtime{9093U};
        runtime.replace_loadout(std::array<std::uint8_t, 1U>{9U}, std::uint8_t{9U});
        empty_the_tube(runtime);
        runtime.set_primary(false); // released: shoot_primary_held is cleared
        std::size_t shots{};
        for (int tick{}; tick < 1200 && runtime.reload_remaining() > 0.0; ++tick) {
            runtime.tick(1.0 / 60.0);
            shots += count(runtime.take_actions(), WeaponActionKind::hitscan);
        }
        expect(shots == 0U, "a released trigger never fires on its own after the reload");
        expect(runtime.replication().ammo(9U)->magazine == capacity,
               "with the trigger released the shell chain fills the tube");
    }
    {
        WeaponRuntime runtime{9094U};
        runtime.replace_loadout(std::array<std::uint8_t, 1U>{9U}, std::uint8_t{9U});
        empty_the_tube(runtime); // trigger stays down
        std::size_t shots{};
        for (int tick{}; tick < 600 && shots == 0U; ++tick) {
            runtime.tick(1.0 / 60.0);
            shots += count(runtime.take_actions(), WeaponActionKind::hitscan);
        }
        expect(shots == 1U, "empty-while-held loads one shell and fires it");
        expect(runtime.replication().ammo(9U)->magazine == 0U,
               "the resumed shot spent the single loaded shell");
    }
}

/**
 * P0-11: Tool.use_secondary returns None for every melee tool except the two
 * alternate digs, so RMB must never put a secondary melee ShootPacket on the
 * wire. The spade's RMB is only the inert can_swap lock.
 */
void melee_right_click_sends_no_secondary_attack() {
    for (const std::uint8_t tool : std::array<std::uint8_t, 10U>{
             0U, 1U, 2U, 3U, 24U, 34U, 44U, 49U, 50U, 52U}) {
        WeaponRuntime runtime{77U};
        runtime.replace_loadout(std::array<std::uint8_t, 1U>{tool}, tool);
        runtime.set_secondary(true);
        tick_for(runtime, 1.5);
        runtime.set_secondary(false);
        runtime.tick(1.0 / 60.0);
        expect(runtime.take_actions().empty(),
               "a melee RMB without a retail override must emit nothing");
    }

    WeaponRuntime spade{78U};
    spade.replace_loadout(std::array<std::uint8_t, 2U>{2U, 17U}, std::uint8_t{2U});
    spade.set_secondary(true);
    spade.tick(1.0 / 60.0);
    expect(spade.swap_locked(), "spade RMB must block tool swaps while it charges");
    tick_for(spade, 1.05);
    expect(!spade.swap_locked(), "the spade lock must end after its 1.0 s charge");
    spade.set_secondary(false);
    spade.tick(1.0 / 60.0);
    spade.set_secondary(true);
    spade.tick(1.0 / 60.0);
    spade.set_secondary(false);
    spade.tick(1.0 / 60.0);
    expect(!spade.swap_locked(), "releasing RMB must clear the spade lock");
    expect(spade.take_actions().empty(), "the spade RMB lock must emit no action");

    WeaponRuntime ugc{79U};
    ugc.replace_loadout(std::array<std::uint8_t, 1U>{45U}, std::uint8_t{45U});
    ugc.set_secondary(true);
    tick_for(ugc, 0.5);
    const auto dig = ugc.take_actions();
    expect(count(dig, WeaponActionKind::melee) >= 1U &&
               std::ranges::all_of(dig, [](const WeaponAction& action) {
                   return action.secondary;
               }),
           "the UGC super spade RMB is an immediate alternate dig");
}

/**
 * Player report (Beta 0.1): pressing both mouse buttons a few milliseconds
 * apart made a zombie dig twice in one swing. Retail DiggingTool has one
 * action on one shoot_delay (tool.py use_primary / zombieHandTool.py), so
 * however the two buttons are pressed a digging tool swings once per
 * shoot_interval and never sends a secondary ShootPacket.
 */
void both_mouse_buttons_never_double_a_dig() {
    // Every digging tool without a retail secondary (tool ids as on the wire).
    for (const std::uint8_t tool : std::array<std::uint8_t, 10U>{
             0U, 1U, 2U, 3U, 24U, 34U, 44U, 49U, 50U, 52U}) {
        const auto& weapon = weapon_catalog()[tool];
        expect(weapon.fire_interval > 0.0, "a digging tool has a swing interval");
        constexpr double dt{1.0 / 60.0};
        constexpr double duration{3.0};

        WeaponRuntime held{91U};
        held.replace_loadout(std::array<std::uint8_t, 1U>{tool}, tool);
        held.set_primary(true);
        for (double elapsed{}; elapsed < duration; elapsed += dt) held.tick(dt);
        const auto baseline = held.take_actions();

        WeaponRuntime both{91U};
        both.replace_loadout(std::array<std::uint8_t, 1U>{tool}, tool);
        both.set_primary(true);
        both.tick(dt);
        bool secondary{};
        std::size_t frame{};
        for (double elapsed{dt}; elapsed < duration; elapsed += dt, ++frame) {
            // RMB hammered every other frame, starting one frame after LMB.
            secondary = !secondary;
            both.set_secondary(secondary);
            both.tick(dt);
        }
        const auto actions = both.take_actions();
        expect(count(actions, WeaponActionKind::melee) ==
                   count(baseline, WeaponActionKind::melee),
               "RMB must not add swings to a held dig");
        expect(std::ranges::none_of(actions,
                                    [](const WeaponAction& action) { return action.secondary; }),
               "a digging tool must never emit a secondary action");
        const auto limit =
            static_cast<std::size_t>(std::floor(duration / weapon.fire_interval)) + 1U;
        expect(count(actions, WeaponActionKind::melee) <= limit,
               "one swing per shoot_interval, whatever the buttons do");
    }
}

/** P2-19: a dry weapon asks to switch away; a crate restock auto-reloads. */
void empty_weapons_auto_switch_and_crates_auto_reload() {
    WeaponRuntime runtime{4444U};
    runtime.replace_loadout(std::array<std::uint8_t, 2U>{7U, 2U}, std::uint8_t{7U});
    runtime.set_primary(true);
    bool switch_requested{};
    for (int tick{}; tick < 60 * 40 && !switch_requested; ++tick) {
        runtime.tick(1.0 / 60.0);
        static_cast<void>(runtime.take_actions());
        switch_requested = runtime.take_auto_switch_request();
    }
    runtime.set_primary(false);
    const auto* ammo = runtime.replication().ammo(7U);
    expect(switch_requested && ammo != nullptr && ammo->magazine == 0U &&
               ammo->reserve == 0U,
           "a dry pull with no reserve must request Character.auto_switch_tool");
    expect(!runtime.take_auto_switch_request(), "the switch request is one-shot");

    expect(runtime.restock_from_ammo_crate(), "the crate must add reserve ammunition");
    runtime.tick(1.0 / 60.0);
    expect(count(runtime.take_actions(), WeaponActionKind::reload_started) == 1U &&
               runtime.reload_remaining() > 0.0,
           "an empty magazine must reload on the update after a crate restock");

    WeaponRuntime grenade{4445U};
    grenade.replace_loadout(std::array<std::uint8_t, 2U>{11U, 2U}, std::uint8_t{11U});
    bool grenade_switch{};
    for (int attempt{}; attempt < 12 && !grenade_switch; ++attempt) {
        grenade.set_primary(true);
        grenade.tick(1.0 / 60.0);
        grenade.set_primary(false);
        tick_for(grenade, 1.0);
        static_cast<void>(grenade.take_actions());
        grenade_switch = grenade.take_auto_switch_request();
    }
    expect(grenade_switch, "an empty grenade pull must request an auto switch");
}

/** P2-17: hitscan actions carry the bloomed Weapon.accuracy of their shot. */
void hitscan_actions_carry_the_current_bloom() {
    const auto catalog = weapon_catalog();
    const auto found = std::ranges::find_if(catalog, [](const auto& weapon) {
        return weapon.retail.aim.variable_accuracy &&
               weapon.mechanism == WeaponMechanism::firearm_automatic &&
               weapon.retail.aim.spread_increase_per_shot.value_or(0.0) > 0.0;
    });
    expect(found != catalog.end(), "a variable-accuracy automatic must exist");
    WeaponRuntime runtime{5151U};
    runtime.replace_loadout(std::array<std::uint8_t, 1U>{found->tool_id}, found->tool_id);
    runtime.set_primary(true);
    std::vector<double> accuracies;
    for (int tick{}; tick < 60; ++tick) {
        runtime.tick(1.0 / 60.0);
        for (const auto& action : runtime.take_actions()) {
            if (action.kind == WeaponActionKind::hitscan) {
                accuracies.push_back(action.accuracy);
            }
        }
    }
    const double first = found->retail.aim.accuracy_min.value_or(
        found->retail.aim.accuracy.value_or(0.0));
    expect(accuracies.size() >= 3U && std::abs(accuracies.front() - first) < 1e-9 &&
               accuracies.back() > accuracies.front(),
           "the first shot uses base accuracy and sustained fire blooms");
}

/**
 * Single-shot weapons (one-round magazine): RPG, Drillgun, Sniper, Grenade
 * Launcher, Mine Launcher. Retail cycle, per weapon.py / character.pyd:
 *  - Weapon.use_primary fires, starts weapon_shoot(shoot_interval) and sets
 *    reload_next_update (plus shoot_primary_held while LMB is down);
 *  - Character.update_alive calls reload() once weapon_shoot stops playing,
 *    i.e. shoot_interval after the round;
 *  - end_reload refills after reload_time and resumes fire only if the
 *    trigger is still down (set_primary_shoot(False) clears the latch).
 * So a tap never re-fires on its own, and the earliest next round is
 * shoot_interval + reload_time after the previous one.
 */
void single_shot_weapons_follow_the_retail_fire_reload_cycle() {
    constexpr double dt{1.0 / 60.0};
    for (const std::uint8_t tool : std::array<std::uint8_t, 5U>{12U, 14U, 18U, 55U, 58U}) {
        const auto& weapon = weapon_catalog()[tool];
        const auto capacity = weapon.retail.ammo.magazine_capacity.value_or(weapon.clip_size);
        expect(capacity == 1U, "the single-shot roster must hold one round");
        const double interval = weapon.retail.use.shoot_interval.value_or(weapon.fire_interval);
        const double reload = weapon.retail.use.reload_time.value_or(weapon.reload_time);
        const auto fire_kind = weapon.mechanism == WeaponMechanism::oriented_launcher
                                   ? WeaponActionKind::oriented_item
                                   : WeaponActionKind::hitscan;
        const auto ticks_of = [](double seconds) {
            return static_cast<int>(std::ceil(seconds * 60.0 - 1e-6));
        };

        // Tap: one round, a deferred automatic reload, and nothing else.
        {
            WeaponRuntime runtime{1000U + tool};
            runtime.replace_loadout(std::array<std::uint8_t, 1U>{tool}, tool);
            runtime.set_primary(true);
            runtime.tick(dt);
            auto actions = runtime.take_actions();
            expect(count(actions, fire_kind) == 1U, "a tap fires the single round");
            expect(count(actions, WeaponActionKind::reload_started) == 0U &&
                       runtime.reload_remaining() == 0.0,
                   "the reload must wait for the weapon_shoot animation");
            runtime.set_primary(false);
            int reload_tick{-1};
            int completed_tick{-1};
            std::size_t shots{};
            for (int tick{1}; tick < ticks_of(interval + reload) + 240; ++tick) {
                runtime.tick(dt);
                for (const auto& action : runtime.take_actions()) {
                    if (action.kind == WeaponActionKind::reload_started && reload_tick < 0) {
                        reload_tick = tick;
                    }
                    if (action.kind == WeaponActionKind::reload_completed && completed_tick < 0) {
                        completed_tick = tick;
                    }
                    shots += action.kind == fire_kind ? 1U : 0U;
                }
            }
            expect(reload_tick >= ticks_of(interval) - 1 && reload_tick <= ticks_of(interval) + 1,
                   "the automatic reload starts one shoot_interval after the round");
            expect(completed_tick >= ticks_of(interval + reload) - 1 &&
                       completed_tick <= ticks_of(interval + reload) + 2,
                   "the reload completes shoot_interval + reload_time after the round");
            expect(shots == 0U, "a tapped single-shot weapon must not re-fire after reloading");
            expect(runtime.replication().ammo(tool)->magazine == 1U,
                   "the reloaded round stays chambered for the next press");
            // The next press fires at once.
            runtime.set_primary(true);
            runtime.tick(dt);
            expect(count(runtime.take_actions(), fire_kind) == 1U,
                   "a fresh press after the reload fires immediately");
        }

        // A press and release during the reload does not fire afterwards.
        {
            WeaponRuntime runtime{2000U + tool};
            runtime.replace_loadout(std::array<std::uint8_t, 1U>{tool}, tool);
            runtime.set_primary(true);
            runtime.tick(dt);
            runtime.set_primary(false);
            tick_for(runtime, interval + reload * 0.5);
            expect(runtime.reload_remaining() > 0.0, "the reload is running");
            runtime.set_primary(true);
            runtime.tick(dt);
            runtime.set_primary(false);
            static_cast<void>(runtime.take_actions());
            tick_for(runtime, reload + 1.0);
            expect(count(runtime.take_actions(), fire_kind) == 0U,
                   "a press released during the reload must not fire later");
        }

        // Held: retail keeps firing, one round per shoot_interval + reload_time.
        {
            WeaponRuntime runtime{3000U + tool};
            runtime.replace_loadout(std::array<std::uint8_t, 1U>{tool}, tool);
            runtime.set_primary(true);
            std::vector<int> shot_ticks;
            for (int tick{}; tick < ticks_of(2.0 * (interval + reload)) + 30; ++tick) {
                runtime.tick(dt);
                for (const auto& action : runtime.take_actions()) {
                    if (action.kind == fire_kind) {
                        shot_ticks.push_back(tick);
                    }
                }
            }
            expect(shot_ticks.size() >= 2U, "a held trigger fires again after each reload");
            const int gap = shot_ticks[1U] - shot_ticks[0U];
            expect(gap >= ticks_of(interval + reload) - 1 &&
                       gap <= ticks_of(interval + reload) + 2,
                   "held fire cycles at shoot_interval + reload_time, never sooner");
        }
    }
}

/**
 * Character.update_weapon calls use_weapon_primary every update while
 * shoot_primary is set; only Tool.shoot_delay (the shoot_interval) limits it.
 * Retail has no semi-automatic weapons: a held trigger keeps firing at the
 * retail interval on every multi-round weapon, one action per interval.
 */
void every_weapon_fires_at_its_interval_while_held() {
    constexpr double dt{1.0 / 60.0};
    const auto ticks_of = [](double seconds) {
        return static_cast<int>(std::ceil(seconds * 60.0 - 1e-6));
    };
    for (const std::uint8_t tool :
         std::array<std::uint8_t, 8U>{6U, 9U, 10U, 17U, 19U, 36U, 37U, 13U}) {
        const auto& weapon = weapon_catalog()[tool];
        const double interval = weapon.retail.use.shoot_interval.value_or(weapon.fire_interval);
        const auto kind = weapon.mechanism == WeaponMechanism::oriented_launcher
                              ? WeaponActionKind::oriented_item
                              : WeaponActionKind::hitscan;
        const auto capacity = weapon.retail.ammo.magazine_capacity.value_or(weapon.clip_size);
        expect(capacity >= 2U, "the held-fire roster must hold several rounds");
        WeaponRuntime runtime{4000U + tool};
        runtime.replace_loadout(std::array<std::uint8_t, 1U>{tool}, tool);
        runtime.set_primary(true);
        std::vector<int> shot_ticks;
        std::vector<std::uint8_t> pellets;
        for (int tick{}; shot_ticks.size() < capacity && tick < 2000; ++tick) {
            runtime.tick(dt);
            for (const auto& action : runtime.take_actions()) {
                if (action.kind == kind) {
                    shot_ticks.push_back(tick);
                }
            }
        }
        expect(shot_ticks.size() == capacity, "a held trigger empties the magazine");
        for (std::size_t i{1U}; i < shot_ticks.size(); ++i) {
            const int gap = shot_ticks[i] - shot_ticks[i - 1U];
            // Frame-quantised like Tool.shoot_delay: never early, at most
            // one frame late from float accumulation.
            expect(gap >= ticks_of(interval) && gap <= ticks_of(interval) + 1,
                   "held fire repeats at exactly the retail shoot_interval");
        }
        expect(runtime.replication().ammo(tool)->magazine == 0U,
               "one action per round: nothing double-fired");
        // Releasing stops it.
        runtime.set_primary(false);
        tick_for(runtime, 10.0);
        expect(count(runtime.take_actions(), kind) == 0U,
               "a released trigger stops firing");
    }

    // AssaultRifleWeapon: each held use_primary opens a 3-round burst; the
    // last burst round resets shoot_delay, so bursts start every
    // 2 * A1935 + shoot_interval while held.
    {
        const auto& weapon = weapon_catalog()[60U];
        WeaponRuntime runtime{4060U};
        runtime.replace_loadout(std::array<std::uint8_t, 1U>{60U}, std::uint8_t{60U});
        runtime.set_primary(true);
        std::size_t rounds{};
        std::size_t bursts{};
        // Bursts open at 0 and 2 * 0.1 + shoot_interval; stop before a third.
        for (int tick{}; tick < ticks_of(0.2 + weapon.fire_interval + 0.3); ++tick) {
            runtime.tick(dt);
            for (const auto& action : runtime.take_actions()) {
                if (action.kind == WeaponActionKind::hitscan) {
                    ++rounds;
                    bursts += action.burst_follow_up ? 0U : 1U;
                }
            }
        }
        expect(bursts == 2U && rounds == 6U,
               "a held assault rifle keeps firing full 3-round bursts");
    }
}

/** Deployables keep placing while held, but a refused ghost stops the hold. */
void held_deployables_place_until_refused() {
    WeaponRuntime runtime{4020U};
    runtime.replace_loadout(std::array<std::uint8_t, 1U>{20U}, std::uint8_t{20U});
    const auto& mine = weapon_catalog()[20U];
    const auto stock = runtime.replication().ammo(20U)->magazine;
    expect(stock >= 2U, "the landmine needs two placements for this test");
    runtime.set_context(WeaponRuntimeContext{false, false, false, true});
    runtime.set_primary(true);
    runtime.tick(1.0 / 60.0);
    tick_for(runtime, mine.fire_interval + 2.0 / 60.0);
    expect(count(runtime.take_actions(), WeaponActionKind::deployable_place) == 2U,
           "a held valid ghost places again after shoot_interval");
    runtime.set_context(WeaponRuntimeContext{false, false, false, false});
    tick_for(runtime, mine.fire_interval + 2.0 / 60.0);
    auto actions = runtime.take_actions();
    expect(count(actions, WeaponActionKind::placement_rejected) == 1U,
           "a held refused ghost reports BUILD_ERROR once");
    runtime.set_context(WeaponRuntimeContext{false, false, false, true});
    tick_for(runtime, mine.fire_interval * 2.0);
    expect(count(runtime.take_actions(), WeaponActionKind::deployable_place) == 0U,
           "after a refusal the held trigger stays idle (shoot_primary cleared)");
    runtime.set_primary(false);
    runtime.tick(1.0 / 60.0);
    runtime.set_primary(true);
    runtime.tick(1.0 / 60.0);
    expect(count(runtime.take_actions(), WeaponActionKind::deployable_place) == 1U,
           "a fresh press places again");
}

/** UGCRPG2Weapon never spends ammo; UGCDrillgunWeapon reloads to (1, 1). */
void ugc_launchers_never_run_dry() {
    constexpr double dt{1.0 / 60.0};
    {
        const auto& rocket = weapon_catalog()[46U];
        WeaponRuntime runtime{4046U};
        runtime.replace_loadout(std::array<std::uint8_t, 1U>{46U}, std::uint8_t{46U});
        runtime.set_primary(true);
        std::size_t shots{};
        std::size_t reloads{};
        for (int tick{}; tick < static_cast<int>(10.0 / dt); ++tick) {
            runtime.tick(dt);
            for (const auto& action : runtime.take_actions()) {
                shots += action.kind == WeaponActionKind::oriented_item ? 1U : 0U;
                reloads += action.kind == WeaponActionKind::reload_started ? 1U : 0U;
            }
        }
        expect(shots >= static_cast<std::size_t>(10.0 / rocket.fire_interval) - 1U,
               "the UGC rocket fires at its interval forever");
        expect(reloads == 0U && runtime.replication().ammo(46U)->magazine == 1U,
               "the UGC rocket never spends or reloads its round");
    }
    {
        const auto& drill = weapon_catalog()[47U];
        const double cycle = drill.retail.use.shoot_interval.value_or(drill.fire_interval) +
                             drill.retail.use.reload_time.value_or(drill.reload_time);
        WeaponRuntime runtime{4047U};
        runtime.replace_loadout(std::array<std::uint8_t, 1U>{47U}, std::uint8_t{47U});
        runtime.set_primary(true);
        std::size_t shots{};
        for (int tick{}; tick < static_cast<int>((cycle * 6.0 + 1.0) / dt); ++tick) {
            runtime.tick(dt);
            shots += count(runtime.take_actions(), WeaponActionKind::oriented_item);
        }
        expect(shots >= 6U, "the UGC drill keeps reloading past its initial reserve");
        const auto* ammo = runtime.replication().ammo(47U);
        expect(ammo->reserve == 1U, "get_ammo_after_reload leaves one round in reserve");
    }
}

int main() {
    try {
        every_original_tool_has_a_concrete_mechanism();
        sprint_use_matches_retail_character_and_tool_rules();
        the_empty_magazine_clicks_once_per_trigger_pull();
        the_minigun_spin_ramps_up_and_down();
        the_minigun_winds_down_during_reload();
        minigun_unset_resets_motor_reload_and_pending_edges();
        a_burst_announces_once_but_still_fires_three_rounds();
        only_burst_weapons_mark_follow_up_rounds();
        the_empty_cue_latch_does_not_change_firing();
        firearms_repeat_burst_spin_and_expand_pellets();
        crosshair_tracks_retail_accuracy_and_fixed_tool_rules();
        delayed_spade_secondary_matches_retail_windup();
        throwables_preserve_cook_and_charge_release_rules();
        builders_deployables_and_special_tools_have_distinct_actions();
        mounted_machine_gun_keeps_fire_and_deployment_separate();
        invalid_deployable_targets_do_not_spend_stock_or_emit_packets();
        reloads_match_retail_magazine_and_shell_cycles();
        block_cannon_repeats_and_empty_firearms_auto_reload();
        a_shell_reload_tap_does_not_interrupt_the_chain();
        a_held_trigger_stops_the_shell_chain_and_resumes_fire();
        single_shot_weapons_follow_the_retail_fire_reload_cycle();
        every_weapon_fires_at_its_interval_while_held();
        held_deployables_place_until_refused();
        ugc_launchers_never_run_dry();
        melee_right_click_sends_no_secondary_attack();
        both_mouse_buttons_never_double_a_dig();
        empty_weapons_auto_switch_and_crates_auto_reload();
        hitscan_actions_carry_the_current_bloom();
        std::cout << "All-tool weapon runtime tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
