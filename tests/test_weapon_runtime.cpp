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
    runtime.set_secondary(true);
    runtime.tick(0.01);
    auto actions = runtime.take_actions();
    expect(actions.size() == 1U &&
               actions.front().kind == WeaponActionKind::deployable_place,
           "undeployed MG secondary must request placement");

    runtime.set_secondary(false);
    runtime.tick(0.01);
    runtime.set_context(WeaponRuntimeContext{false, true, false});
    runtime.set_secondary(true);
    runtime.tick(0.01);
    actions = runtime.take_actions();
    expect(actions.size() == 1U &&
               actions.front().kind == WeaponActionKind::objective_use,
           "deployed MG secondary must request dismount/use toggle");
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
 * P0-10: a trigger press during a clip_reload shell cycle stops the chain
 * after the shell in progress and fires with the shells loaded so far.
 */
void a_shell_reload_press_fires_after_the_current_shell() {
    WeaponRuntime runtime{9091U};
    runtime.replace_loadout(std::array<std::uint8_t, 1U>{9U}, std::uint8_t{9U});
    const auto& shotgun = weapon_catalog()[9U];
    expect(shotgun.retail.ammo.clip_reload, "the shotgun must be a clip_reload weapon");
    for (int shot{}; shot < 2; ++shot) {
        runtime.set_primary(true);
        runtime.tick(1.0 / 60.0);
        runtime.set_primary(false);
        tick_for(runtime, shotgun.fire_interval + 0.05);
    }
    static_cast<void>(runtime.take_actions());
    const auto magazine_before = runtime.replication().ammo(9U)->magazine;
    expect(runtime.request_reload() == battlespades::world::WeaponStateResult::accepted,
           "the shotgun shell reload must start");
    runtime.tick(1.0 / 60.0);
    runtime.set_primary(true);
    runtime.tick(1.0 / 60.0);
    runtime.set_primary(false);
    auto actions = runtime.take_actions();
    expect(count(actions, WeaponActionKind::hitscan) == 0U,
           "the press must not fire while the current shell is still loading");
    tick_for(runtime, shotgun.retail.use.reload_time.value_or(shotgun.reload_time));
    actions = runtime.take_actions();
    const auto completed = std::ranges::find(actions, WeaponActionKind::reload_completed,
                                             &WeaponAction::kind);
    const auto shot = std::ranges::find(actions, WeaponActionKind::hitscan,
                                        &WeaponAction::kind);
    expect(completed != actions.end() && completed->value == 1.0 &&
               count(actions, WeaponActionKind::reload_started) == 0U,
           "the latched press must end the shell chain after the current shell");
    expect(shot != actions.end() && shot > completed,
           "the latched press must fire once that shell is loaded");
    expect(runtime.replication().ammo(9U)->magazine == magazine_before &&
               runtime.reload_remaining() == 0.0,
           "the interrupted chain fires with the shells loaded so far");
}

/**
 * V1: Character.end_reload stops the chain on `shoot_primary` (the trigger
 * merely HELD at the shell boundary) and, when the shot that emptied the gun
 * had the trigger down (shoot_primary_held), loads ONE shell and fires again.
 */
void a_held_trigger_stops_the_shell_chain_and_resumes_fire() {
    WeaponRuntime runtime{9092U};
    runtime.replace_loadout(std::array<std::uint8_t, 1U>{9U}, std::uint8_t{9U});
    const auto& shotgun = weapon_catalog()[9U];
    const auto reload_time = shotgun.retail.use.reload_time.value_or(shotgun.reload_time);
    // Fire one shell and keep the trigger held (no new press edge), then
    // reload: the held trigger ends the chain after the first shell.
    runtime.set_primary(true);
    runtime.tick(1.0 / 60.0);
    tick_for(runtime, shotgun.fire_interval + 0.05);
    static_cast<void>(runtime.take_actions());
    const auto magazine_before = runtime.replication().ammo(9U)->magazine;
    expect(runtime.request_reload() == battlespades::world::WeaponStateResult::accepted,
           "the manual shell reload must start");
    tick_for(runtime, reload_time + 1.0 / 60.0);
    auto actions = runtime.take_actions();
    const auto completed = std::ranges::find(actions, WeaponActionKind::reload_completed,
                                             &WeaponAction::kind);
    expect(completed != actions.end() && completed->value == 1.0 &&
               runtime.reload_remaining() == 0.0,
           "a held trigger stops the chain after the shell in progress");
    expect(runtime.replication().ammo(9U)->magazine == magazine_before + 1U,
           "exactly one shell was loaded");
    runtime.set_primary(false);
    tick_for(runtime, shotgun.fire_interval + 0.05);

    // Empty the magazine; the last shot has the trigger down.
    while (runtime.replication().ammo(9U)->magazine > 1U) {
        runtime.set_primary(true);
        runtime.tick(1.0 / 60.0);
        runtime.set_primary(false);
        tick_for(runtime, shotgun.fire_interval + 0.05);
    }
    static_cast<void>(runtime.take_actions());
    runtime.set_primary(true);
    runtime.tick(1.0 / 60.0);
    expect(runtime.replication().ammo(9U)->magazine == 0U && runtime.reload_remaining() > 0.0,
           "the emptying shot starts the automatic shell reload");
    static_cast<void>(runtime.take_actions());
    runtime.set_primary(false); // released during the reload: retail still resumes
    std::size_t shots{};
    for (int tick{}; tick < 600 && shots == 0U; ++tick) {
        runtime.tick(1.0 / 60.0);
        shots += count(runtime.take_actions(), WeaponActionKind::hitscan);
    }
    expect(shots == 1U, "empty-while-held loads one shell and fires it");
    expect(runtime.replication().ammo(9U)->magazine == 0U,
           "the resumed shot spent the single loaded shell");
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
        a_shell_reload_press_fires_after_the_current_shell();
        a_held_trigger_stops_the_shell_chain_and_resumes_fire();
        melee_right_click_sends_no_secondary_attack();
        empty_weapons_auto_switch_and_crates_auto_reload();
        hitscan_actions_carry_the_current_bloom();
        std::cout << "All-tool weapon runtime tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
