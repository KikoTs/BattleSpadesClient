// Round 2 camera/feel/building parity (docs/RETAIL_PARITY_GAPS_2026-09-27.md):
// recoil, empty-magazine un-zoom, crosshair visibility, block bridging and
// refusal hints, the block wallet multiplier and the predicted blast push.
#include "battlespades/audio/retail_event_cues.hpp"
#include "battlespades/audio/server_audio_catalog.hpp"
#include "battlespades/frontend/retail_hud_rules.hpp"
#include "battlespades/world/block_placement.hpp"
#include "battlespades/world/build_wallet.hpp"
#include "battlespades/world/class_catalog.hpp"
#include "battlespades/world/player_inventory.hpp"
#include "battlespades/world/retail_blast.hpp"
#include "battlespades/world/retail_recoil.hpp"
#include "battlespades/world/terrain_effects.hpp"
#include "battlespades/world/tutorial_session.hpp"
#include "battlespades/world/weapon_catalog.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

namespace {

using namespace battlespades::world;

void expect(bool value, const char* message) {
    if (!value) throw std::runtime_error{message};
}

[[nodiscard]] bool near(double a, double b, double tolerance = 1.0e-9) {
    return std::abs(a - b) <= tolerance;
}

[[nodiscard]] std::shared_ptr<VxlMap> platform_world() {
    std::vector<std::byte> bytes;
    bytes.reserve(static_cast<std::size_t>(VxlMap::width) * VxlMap::depth * 4U);
    for (std::size_t column{}; column < static_cast<std::size_t>(VxlMap::width) * VxlMap::depth;
         ++column) {
        bytes.push_back(std::byte{0U});
        bytes.push_back(std::byte{1U});
        bytes.push_back(std::byte{0U});
        bytes.push_back(std::byte{0U});
    }
    auto loaded = VxlMap::load(bytes);
    expect(static_cast<bool>(loaded), "synthetic world must parse");
    for (std::uint32_t y{60U}; y <= 90U; ++y) {
        for (std::uint32_t x{110U}; x <= 150U; ++x) {
            expect(loaded.map->set_voxel(x, y, 233U, VxlColor{100U, 120U, 90U, 255U}),
                   "platform voxel");
        }
    }
    for (std::uint32_t z{234U}; z < VxlMap::height - 1U; ++z) {
        expect(loaded.map->set_voxel(110U, 60U, z, VxlColor{100U, 120U, 90U, 255U}),
               "platform support");
    }
    return std::make_shared<VxlMap>(std::move(*loaded.map));
}

void recoil_tests() {
    // Character.shoot: ((timer & 511) - 255.5) * recoil_side, sign flipping
    // every 512 ms, stance multipliers, then x60.
    auto kick = retail_recoil_kick(-0.05, 0.001, 0U, false, false, false);
    expect(near(kick.pitch_degrees, -3.0) && near(kick.yaw_degrees, 255.5 * 0.001 * 60.0),
           "timer 0 is the first half-cycle: side = (0 - 255.5) * -recoil_side");
    kick = retail_recoil_kick(-0.05, 0.001, 600U, false, false, false);
    expect(near(kick.yaw_degrees, ((600 & 511) - 255.5) * 0.001 * 60.0),
           "past 511 ms the sawtooth takes +recoil_side");
    kick = retail_recoil_kick(-0.05, 0.001, 255U, false, false, false);
    expect(std::abs(kick.yaw_degrees) < 0.05, "mid-ramp the side kick crosses zero");
    const auto base = retail_recoil_kick(-0.05, 0.001, 0U, false, false, false);
    const auto walking = retail_recoil_kick(-0.05, 0.001, 0U, true, false, false);
    const auto airborne_walk = retail_recoil_kick(-0.05, 0.001, 0U, true, false, true);
    const auto crouched = retail_recoil_kick(-0.05, 0.001, 0U, true, true, false);
    const auto crouch_jump = retail_recoil_kick(-0.05, 0.001, 0U, true, true, true);
    expect(near(walking.pitch_degrees, base.pitch_degrees * 2.0) &&
               near(airborne_walk.pitch_degrees, base.pitch_degrees * 4.0) &&
               near(crouched.pitch_degrees, base.pitch_degrees * 0.5) &&
               near(crouched.yaw_degrees, base.yaw_degrees * 0.5),
           "walking x2, airborne x2 again, crouching /2 (and no walking bonus)");
    expect(near(crouch_jump.pitch_degrees, base.pitch_degrees * 2.0) &&
               near(crouch_jump.yaw_degrees, base.yaw_degrees * 2.0),
           "airborne recoil wins over crouching: crouch-jump never halves the kick");
    expect(retail_scene_timer_ms(60U) == 1000U, "60 ticks are one second of scene timer");
}

void session_tests() {
    // A live RPG shot: exact retail kick while standing still, and the round
    // that empties the one-round magazine drops the sight.
    TutorialSessionConfig live;
    live.network_authoritative = true;
    live.initial_position = {130.0, 70.0, 230.75};
    live.initial_orientation = {-1.0, 0.0, 0.0};
    live.initial_class_id = 0U;
    live.initial_loadout = {12U};
    live.initial_tool = static_cast<std::uint8_t>(12U);
    TutorialWorldSession session{platform_world(), live};
    const auto* weapon = find_weapon_definition(12U);
    expect(weapon != nullptr, "tool 12 is catalogued");
    session.set_secondary_held(true);
    expect(session.zoomed(), "the RPG aims through its sight");
    const double before = session.pitch();
    const auto ticks = session.ticks_simulated();
    session.set_primary_held(true);
    session.tick();
    session.set_primary_held(false);
    const auto actions = session.take_weapon_actions();
    expect(!actions.empty(), "the RPG fires");
    const auto expected = retail_recoil_kick(weapon->retail.aim.recoil_up.value_or(0.0),
                                             weapon->retail.aim.recoil_side.value_or(0.0),
                                             retail_scene_timer_ms(ticks), false, false, false);
    expect(near(session.pitch() - before, expected.pitch_degrees, 1.0e-6),
           "a standing shot kicks pitch by recoil_up * 60");
    const auto* ammo = session.selected_ammo();
    expect(ammo != nullptr && ammo->magazine == 0U, "the shot empties the magazine");
    static_cast<void>(session.take_attack_events());
    session.tick();
    expect(!session.zoomed(), "set_zoom(0) after the shot that empties the magazine");
    expect(session.take_attack_events().zoom_dropped, "the un-zoom plays the zoom_out cue");

    // FlareBlockTool (22) held in a live session: one LMB press is one
    // flare_place action (the frontend turns it into PlaceFlareBlock(104)),
    // and the wallet it is judged against can pay FLAREBLOCK_COST.
    {
        TutorialSessionConfig flare_live;
        flare_live.network_authoritative = true;
        flare_live.initial_position = {130.5, 75.5, 230.75};
        flare_live.initial_orientation = {0.0, 0.0, 1.0};
        flare_live.initial_class_id = 0U;
        flare_live.initial_loadout = {2U, 5U, 22U};
        flare_live.initial_tool = static_cast<std::uint8_t>(22U);
        TutorialWorldSession flare_session{platform_world(), flare_live};
        expect(flare_session.selected_tool_id() == std::optional<std::uint8_t>{static_cast<std::uint8_t>(22U)},
               "the live session holds the Flare Block");
        expect(flare_block_affordable(flare_session.blocks_remaining(),
                                      flare_session.infinite_blocks()),
               "a fresh live wallet pays FLAREBLOCK_COST");
        flare_session.set_primary_held(true);
        flare_session.tick();
        flare_session.set_primary_held(false);
        const auto flare_actions = flare_session.take_weapon_actions();
        expect(std::ranges::count_if(flare_actions, [](const WeaponAction& action) {
                   return action.kind == WeaponActionKind::flare_place && action.tool_id == 22U;
               }) == 1,
               "one LMB press with tool 22 emits exactly one flare_place");
    }

    // Offline weapon lab (F4): FlareBlockTool places at the cube its ghost
    // shows -- BlockToolCommon's target, out to MAX_BLOCK_DISTANCE -- not at
    // the end of a four-block melee ray, and a refused cube is a build_error.
    {
        TutorialSessionConfig lab;
        lab.initial_position = {130.5, 75.5, 230.75};
        lab.initial_orientation = {6.0 / std::sqrt(36.0 + 2.25 * 2.25), 0.0,
                                   2.25 / std::sqrt(36.0 + 2.25 * 2.25)};
        const auto lab_map = platform_world();
        TutorialWorldSession offline{lab_map, lab};
        offline.debug_grant_full_loadout();
        const auto slots = offline.inventory().slots();
        const auto flare_slot = std::ranges::find(slots, std::uint8_t{22U}, &InventorySlot::tool_id);
        expect(flare_slot != slots.end() &&
                   offline.equip_inventory_slot(
                       static_cast<std::size_t>(flare_slot - slots.begin())),
               "the weapon lab can hold the Flare Block");
        for (int settle = 0; settle < 60; ++settle) offline.tick();
        const BlockOccupiedPredicate body = [&offline](BlockTargetCell cell) {
            return block_cell_overlaps_body(cell, offline.player().position);
        };
        const auto aimed = resolve_block_target(*lab_map, offline.player().position,
                                                offline.player().orientation,
                                                retail_max_block_distance, body);
        expect(aimed.valid && aimed.cell.has_value(), "the lab flare has a ghost cube");
        const auto cell = *aimed.cell;
        const double dx = cell[0U] + 0.5 - offline.player().position.x;
        const double dy = cell[1U] + 0.5 - offline.player().position.y;
        const double dz = cell[2U] + 0.5 - offline.player().position.z;
        expect(std::sqrt(dx * dx + dy * dy + dz * dz) > 4.5,
               "the ghost cube is past the old four-block melee reach");
        const int wallet = offline.blocks_remaining();
        static_cast<void>(offline.take_weapon_actions());
        offline.set_primary_held(true);
        offline.tick();
        offline.set_primary_held(false);
        expect(lab_map->solid(static_cast<std::uint32_t>(cell[0U]),
                              static_cast<std::uint32_t>(cell[1U]),
                              static_cast<std::uint32_t>(cell[2U])),
               "LMB places the Flare Block on its ghost cube");
        expect(offline.blocks_remaining() == wallet - retail_flare_block_cost,
               "the Flare Block costs FLAREBLOCK_COST");
        expect(offline.static_lights().lights().size() == 1U,
               "the placed Flare Block registers its light");

        // Aimed at the sky: no cube, nothing placed, one build_error edge.
        TutorialSessionConfig sky = lab;
        sky.initial_orientation = {0.0, 0.0, -1.0};
        TutorialWorldSession skyward{platform_world(), sky};
        skyward.debug_grant_full_loadout();
        const auto sky_slots = skyward.inventory().slots();
        const auto sky_flare =
            std::ranges::find(sky_slots, std::uint8_t{22U}, &InventorySlot::tool_id);
        expect(sky_flare != sky_slots.end() &&
                   skyward.equip_inventory_slot(
                       static_cast<std::size_t>(sky_flare - sky_slots.begin())),
               "the sky probe holds the Flare Block");
        for (int settle = 0; settle < 60; ++settle) skyward.tick();
        static_cast<void>(skyward.take_weapon_actions());
        const int sky_wallet = skyward.blocks_remaining();
        skyward.set_primary_held(true);
        skyward.tick();
        skyward.set_primary_held(false);
        const auto sky_actions = skyward.take_weapon_actions();
        expect(skyward.blocks_remaining() == sky_wallet && skyward.static_lights().empty(),
               "a Flare Block with no cube places nothing");
        expect(std::ranges::count_if(sky_actions, [](const WeaponAction& action) {
                   return action.kind == WeaponActionKind::placement_rejected;
               }) == 1,
               "a refused Flare Block plays build_error");
    }

    // RPG2: stock (3, 3, 3, 3, 3) with clip_reload -- 6 per life, one rocket
    // per 1.0 s reload cycle.
    const auto* rpg2 = find_weapon_definition(13U);
    expect(rpg2 != nullptr && rpg2->retail.ammo.magazine_capacity == 3U &&
               rpg2->retail.ammo.initial_magazine == 3U &&
               rpg2->retail.ammo.initial_reserve == 3U && rpg2->retail.ammo.clip_reload &&
               rpg2->retail.use.reload_time == 1.0,
           "RPG2 is 3 + 3 per life and loads one rocket per second");
}

void crosshair_tests() {
    using battlespades::frontend::retail_crosshair_visible;
    expect(retail_crosshair_visible(3, true, false) && retail_crosshair_visible(3, false, false),
           "ALWAYS draws regardless of aim and ammo");
    expect(retail_crosshair_visible(1, true, false) && !retail_crosshair_visible(1, false, true),
           "ZOOMED draws only while aiming");
    expect(!retail_crosshair_visible(2, true, true) && retail_crosshair_visible(2, false, true),
           "UNZOOMED hides in the sights");
    expect(retail_crosshair_visible(4, true, true) && !retail_crosshair_visible(4, false, false),
           "HAS_AMMO follows get_has_enough_ammo");
    expect(!retail_crosshair_visible(0, false, true) && !retail_crosshair_visible(0, true, true),
           "NEVER (bomb, intel, diamond, fake pistol, null tool) never draws");
    expect(!retail_crosshair_visible(std::nullopt, false, false) &&
               retail_crosshair_visible(std::nullopt, false, true),
           "an unrecovered ordinal inherits Tool.show_crosshair = HAS_AMMO");
}

void block_placement_tests() {
    const auto map = platform_world();
    const BlockOccupiedPredicate nobody = [](BlockTargetCell) { return false; };

    // Aiming down past the platform edge into open air: the hitscan misses
    // the platform, and the bridge scan finds the free cell beside its edge.
    const Vec3 eye{112.5, 75.5, 230.75};
    const Vec3 down_past_edge{-0.8, 0.0, 0.6};
    auto target = resolve_block_target(*map, eye, down_past_edge, 10.0, nobody);
    expect(target.valid && target.cell == BlockTargetCell{109, 75, 233} &&
               target.hint == BlockPlaceHint::none,
           "scan_bridge_placement must extend the platform past its edge");
    const std::vector<VoxelCell> single{{109U, 75U, 233U}};
    auto ghost = evaluate_block_line_ghost(*map, target, single, 10, false, true, nobody);
    expect(ghost.valid && ghost.first_adjacent && !ghost.draw_red(),
           "an attached bridge block draws in the player's colour");

    // Looking level into the sky: nothing to attach to.
    target = resolve_block_target(*map, eye, {-1.0, 0.0, 0.0}, 10.0, nobody);
    expect(!target.valid && target.cell.has_value() &&
               target.hint == BlockPlaceHint::not_attached,
           "a floating bridge is BLOCK_PLACE_FAIL_NOT_ATTACHED");
    ghost = evaluate_block_line_ghost(*map, target,
                                      std::vector<VoxelCell>{{static_cast<std::uint32_t>(
                                                                  (*target.cell)[0U]),
                                                              75U, 230U}},
                                      10, false, true, nobody);
    expect(ghost.draw_red(), "an invalid ghost is drawn (255, 0, 0)");

    // Skimming the platform top: the farthest in-reach cell above it.
    const Vec3 low_eye{112.5, 75.5, 232.5};
    target = resolve_block_target(*map, low_eye, {1.0, 0.0, 0.0}, 10.0, nobody);
    expect(target.valid && target.cell == BlockTargetCell{121, 75, 232},
           "the bridge scan takes the farthest touching cell strictly inside reach");

    // A player standing in every candidate turns it into TOO_FAR.
    const BlockOccupiedPredicate everyone = [](BlockTargetCell) { return true; };
    target = resolve_block_target(*map, low_eye, {1.0, 0.0, 0.0}, 10.0, everyone);
    expect(!target.valid && target.hint == BlockPlaceHint::too_far,
           "an attached but unplaceable bridge is BLOCK_PLACE_FAIL_TOO_FAR");

    // A hitscan hit whose next cube holds a player.
    target = resolve_block_target(*map, {130.5, 75.5, 230.75}, {0.0, 0.0, 1.0}, 10.0, everyone);
    expect(!target.valid && target.hint == BlockPlaceHint::something_in_the_way,
           "a block on a player is BLOCK_PLACE_FAIL_SOMETHING_IN_THE_WAY");
    target = resolve_block_target(*map, {130.5, 75.5, 230.75}, {0.0, 0.0, 1.0}, 10.0, nobody);
    expect(target.valid && target.cell == BlockTargetCell{130, 75, 232},
           "an in-reach hit places on the struck face");

    // The dragged line: wallet, water and capacity refusals.
    const std::vector<VoxelCell> line{{130U, 75U, 232U}, {131U, 75U, 232U}, {132U, 75U, 232U}};
    ghost = evaluate_block_line_ghost(*map, target, line, 2, false, true, nobody);
    expect(!ghost.valid && ghost.hint == BlockPlaceHint::not_enough_blocks,
           "a line longer than the wallet is BLOCK_PLACE_FAIL_NOT_ENOUGH_BLOCKS");
    ghost = evaluate_block_line_ghost(*map, target, line, 2, true, true, nobody);
    expect(ghost.valid && ghost.cells.size() == 3U, "TeamInfiniteBlocks lifts the wallet test");
    ghost = evaluate_block_line_ghost(*map, target, line, 10, false, false, nobody);
    expect(!ghost.valid && ghost.hint == BlockPlaceHint::ugc_capacity,
           "a full UGC map is BLOCK_PLACE_UGC_CAPACITY");
    // The z=239 bed is always solid (so skipped); anything deeper than
    // max_modifiable_z that is not solid refuses as water.
    BlockTarget water{BlockTargetCell{130, 75, 240}, true, BlockPlaceHint::none};
    ghost = evaluate_block_line_ghost(*map, water, std::vector<VoxelCell>{{130U, 75U, 240U}}, 10,
                                      false, true, nobody);
    // beach_z_modifiable = 0 lowers max_modifiable_z to 237: z = 238 is water.
    expect(retail_max_modifiable_z_for(true) == 238 && retail_max_modifiable_z_for(false) == 237,
           "on_connect: max_modifiable_z = 238 if beach_z_modifiable else 237");
    std::uint32_t air_x{};
    while (air_x < 511U && map->solid(air_x, 75U, 238U)) ++air_x;
    BlockTarget beach{BlockTargetCell{static_cast<std::int16_t>(air_x), 75, 238}, true,
                      BlockPlaceHint::none};
    const auto no_beach = evaluate_block_line_ghost(
        *map, beach, std::vector<VoxelCell>{{air_x, 75U, 238U}}, 10, false, true, nobody,
        retail_max_modifiable_z_for(false));
    expect(!no_beach.valid && no_beach.hint == BlockPlaceHint::water,
           "without beach_z_modifiable a z = 238 block is refused as water");
    expect(!ghost.valid && ghost.hint == BlockPlaceHint::water,
           "below max_modifiable_z is BLOCK_PLACE_FAIL_WATER");
    expect(block_place_hint_key(BlockPlaceHint::water) == "BLOCK_PLACE_FAIL_WATER" &&
               block_place_hint_key(BlockPlaceHint::ugc_capacity) ==
                   "BLOCK_PLACE_UGC_CAPACITY" &&
               block_place_hint_key(BlockPlaceHint::none).empty(),
           "hint keys are the retail string ids");

    // can_place_block_on_player's body column.
    expect(block_cell_overlaps_body({130, 75, 231}, {130.5, 75.5, 230.75}) &&
               block_cell_overlaps_body({130, 75, 232}, {130.5, 75.5, 230.75}) &&
               !block_cell_overlaps_body({130, 75, 233}, {130.5, 75.5, 230.75}) &&
               !block_cell_overlaps_body({132, 75, 231}, {130.5, 75.5, 230.75}),
           "a body occupies its +-0.45 column from the eye to two blocks below");

    const auto wire = placement_preview_wire_cube(VxlColor{255U, 0U, 0U, 255U});
    expect(wire.vertices.size() == 12U * 24U && wire.minimum[0U] < 0.0F &&
               wire.maximum[0U] > 1.0F,
           "the classic ghost is twelve edge bars around the cell");
}

void wallet_tests() {
    const auto* soldier = find_class_definition(0U);
    expect(soldier != nullptr, "class 0 is catalogued");
    PlayerInventory scaled;
    scaled.set_block_wallet_multiplier(0.5);
    expect(scaled.spawn_as(0U, PlayerLoadoutScope::retail_default), "spawn with a multiplier");
    const auto expected_max = static_cast<std::uint16_t>(
        std::nearbyint(static_cast<double>(soldier->maximum_blocks) * 0.5));
    const auto expected_start = std::min(
        expected_max, static_cast<std::uint16_t>(
                          std::nearbyint(static_cast<double>(soldier->initial_blocks) * 0.5)));
    expect(scaled.maximum_blocks() == expected_max && scaled.blocks() == expected_start,
           "InitialInfo.block_wallet_multiplier scales the real wallet like the server");
    PlayerInventory plain;
    expect(plain.spawn_as(0U, PlayerLoadoutScope::retail_default) &&
               plain.maximum_blocks() == soldier->maximum_blocks &&
               plain.blocks() == soldier->initial_blocks,
           "the default multiplier keeps the class wallet");
}

/**
 * Player report (Beta 0.1): "with zero blocks you can still place them".
 * The retail tools refuse before anything is sent (blockTool.py:78,
 * flareBlockTool.py:66, prefabTool.py:145, tool.py:210).
 */
void build_wallet_tests() {
    // Tool.can_draw_ghosting: no ghost, no hint on an empty wallet.
    expect(!can_draw_build_ghost(0, false) && can_draw_build_ghost(1, false) &&
               can_draw_build_ghost(0, true),
           "block_count > 0 or team.infinite_blocks");

    // BlockTool.get_has_enough_ammo.
    expect(!block_line_affordable(0, 1U, false), "an empty wallet places nothing");
    expect(!block_line_affordable(0, 1U, true),
           "block_count > 0 is required even on an infinite-blocks team");
    expect(block_line_affordable(1, 1U, false) && !block_line_affordable(1, 2U, false) &&
               block_line_affordable(3, 3U, false) && block_line_affordable(1, 40U, true),
           "block_count >= len(line) unless the team has infinite blocks");

    // FlareBlockTool / PrefabTool.
    expect(retail_flare_block_cost == 10, "FLAREBLOCK_COST");
    expect(!flare_block_affordable(9, false) && flare_block_affordable(10, false) &&
               flare_block_affordable(0, true),
           "the Flare Block needs ten blocks");
    expect(!prefab_affordable(35, 36, false) && prefab_affordable(36, 36, false) &&
               prefab_affordable(0, 36, true),
           "a construct needs its whole voxel count");

    const auto map = platform_world();
    const BlockOccupiedPredicate nobody = [](BlockTargetCell) { return false; };
    const auto target =
        resolve_block_target(*map, {130.5, 75.5, 230.75}, {0.0, 0.0, 1.0}, 10.0, nobody);
    expect(target.valid && target.cell == BlockTargetCell{130, 75, 232}, "fixture hit cube");
    const std::vector<VoxelCell> single{{130U, 75U, 232U}};

    // A single block with an empty wallet: the ghost refuses and nothing is sent.
    auto ghost = evaluate_block_line_ghost(*map, target, single, 0, false, true, nobody);
    expect(!ghost.valid && ghost.hint == BlockPlaceHint::not_enough_blocks &&
               !block_line_placement_allowed(ghost, 0, false),
           "zero blocks: BLOCK_PLACE_FAIL_NOT_ENOUGH_BLOCKS and no BlockLine");
    ghost = evaluate_block_line_ghost(*map, target, single, 0, true, true, nobody);
    expect(ghost.valid && !block_line_placement_allowed(ghost, 0, true),
           "zero blocks on an infinite team still sends nothing");
    ghost = evaluate_block_line_ghost(*map, target, single, 1, false, true, nobody);
    expect(block_line_placement_allowed(ghost, 1, false), "one block places one block");
    const std::vector<VoxelCell> line{{130U, 75U, 232U}, {131U, 75U, 232U}, {132U, 75U, 232U}};
    ghost = evaluate_block_line_ghost(*map, target, line, 2, false, true, nobody);
    expect(!block_line_placement_allowed(ghost, 2, false),
           "a line longer than the wallet is not sent");

    // The Flare Block ghost is the single hit cube at ten blocks a piece.
    auto flare = evaluate_flare_block_ghost(*map, target, 9, false, true, nobody);
    expect(flare.cells.size() == 1U && !flare.valid &&
               flare.hint == BlockPlaceHint::not_enough_blocks &&
               !flare_block_placement_allowed(flare, 9, false),
           "nine blocks cannot pay for a Flare Block");
    flare = evaluate_flare_block_ghost(*map, target, 10, false, true, nobody);
    expect(flare.valid && !flare.draw_red() && flare_block_placement_allowed(flare, 10, false),
           "ten blocks place one Flare Block");
    flare = evaluate_flare_block_ghost(*map, target, 0, true, true, nobody);
    expect(flare_block_placement_allowed(flare, 0, true),
           "an infinite-blocks team places Flare Blocks for free");
    // A refused cube (a player is standing in it) is refused for the flare too.
    const BlockOccupiedPredicate everyone = [](BlockTargetCell) { return true; };
    const auto blocked =
        resolve_block_target(*map, {130.5, 75.5, 230.75}, {0.0, 0.0, 1.0}, 10.0, everyone);
    flare = evaluate_flare_block_ghost(*map, blocked, 50, false, true, everyone);
    expect(!flare_block_placement_allowed(flare, 50, false),
           "a Flare Block cannot be placed inside a player");

    // Placement cues: the server's PlaySound ids and the local cues.
    expect(battlespades::audio::server_sound_group(46U) == "build" &&
               battlespades::audio::server_sound_group(32U) == "prefabbuild" &&
               battlespades::audio::server_sound_group(33U) == "hitground" &&
               battlespades::audio::server_sound_group(38U) == "hitground_zombie",
           "SOUND_MAP: BUILD 46, PREFABBUILD 32, dig hits 33..38");
    expect(battlespades::audio::build_error_cue.group == std::string_view{"build_error"} &&
               battlespades::audio::build_light_cue.group == std::string_view{"build_light"},
           "local cues: build_error on a refusal, build_light for the Flare Block");
}

void blast_tests() {
    const auto grenade = retail_blast_for_damage_type(7U);
    expect(grenade.has_value() && near(grenade->radius, 4.0) &&
               near(grenade->knockback_min, 0.5) && near(grenade->knockback_max, 1.0),
           "GRENADE_DAMAGE pushes like the stock grenade handler");
    expect(!retail_blast_for_damage_type(14U).has_value() &&
               !retail_blast_for_damage_type(25U).has_value() &&
               retail_blast_for_damage_type(9U)->knockback_max == 0.25,
           "only damage_functions types push; RPG2 uses the stock 0/0.25");
    // Explosion 2 blocks level in front of a standing eye, no occlusion.
    const Vec3 eye{100.0, 100.0, 200.0};
    const auto impulse = retail_blast_impulse(nullptr, {98.0, 100.0, 200.0}, eye, false, *grenade);
    expect(impulse.has_value(), "inside the radius the character is pushed");
    const double falloff = (16.0 - (4.0 + 0.75 * 0.75)) / 16.0;
    expect(near(impulse->x, 0.5 + falloff * 0.5, 1.0e-9) && near(impulse->y, 0.0) &&
               near(impulse->z, 0.0),
           "magnitude kmin + falloff * (kmax - kmin) along explosion -> eye");
    expect(!retail_blast_impulse(nullptr, {90.0, 100.0, 200.0}, eye, false, *grenade),
           "outside the radius nothing happens");
    // A wall between blast and body blocks all three sight rays.
    auto map = platform_world();
    for (std::uint32_t y{95U}; y <= 105U; ++y) {
        for (std::uint32_t z{195U}; z <= 205U; ++z) {
            expect(map->set_voxel(99U, y, z, VxlColor{90U, 90U, 90U, 255U}), "wall");
        }
    }
    expect(!retail_blast_impulse(map.get(), {98.0, 100.0, 200.0}, eye, false, *grenade),
           "a fully occluded blast does not push");
}

} // namespace

int main() {
    try {
        recoil_tests();
        session_tests();
        crosshair_tests();
        block_placement_tests();
        wallet_tests();
        build_wallet_tests();
        blast_tests();
        std::cout << "retail feel tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
