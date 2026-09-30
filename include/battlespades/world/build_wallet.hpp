#pragma once

#include "battlespades/world/block_placement.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace battlespades::world {

/**
 * The block wallet tests of the retail building tools, from the stock client
 * scripts (aoslib/weapons):
 *
 *   tool.py:225            Tool.can_draw_ghosting
 *   blockTool.py:78        BlockTool.get_has_enough_ammo
 *   flareBlockTool.py:66   FlareBlockTool.get_has_enough_ammo
 *   prefabTool.py:145      PrefabTool.get_has_enough_ammo
 *   blockToolCommon.py:118 get_block_line: `(i + 1) * block_cost <= ammo`
 *
 * A tool that fails its test plays build_error and sends nothing, so a player
 * with an empty wallet can never ask the server for a block.
 */

/** FLAREBLOCK_COST (A2258): the glowing block costs ten blocks. */
inline constexpr std::int32_t retail_flare_block_cost{10};

/**
 * Tool.can_draw_ghosting: `block_count > 0 or team.infinite_blocks`.
 * Character.draw_fps (character.pyd 0x1005d36c) calls draw_ghosting only when
 * this holds, so an empty wallet shows no ghost and posts no placement hint.
 */
[[nodiscard]] constexpr bool can_draw_build_ghost(std::int32_t blocks,
                                                  bool infinite_blocks) noexcept {
    return blocks > 0 || infinite_blocks;
}

/**
 * BlockTool.get_has_enough_ammo:
 * `block_count > 0 and (block_count >= len(line) or team.infinite_blocks)`.
 * The empty-wallet refusal holds even on an infinite-blocks team.
 */
[[nodiscard]] constexpr bool block_line_affordable(std::int32_t blocks, std::size_t line_cells,
                                                   bool infinite_blocks) noexcept {
    return blocks > 0 &&
           (infinite_blocks || static_cast<std::int64_t>(blocks) >=
                                   static_cast<std::int64_t>(line_cells));
}

/** FlareBlockTool.get_has_enough_ammo: `block_count >= FLAREBLOCK_COST or infinite`. */
[[nodiscard]] constexpr bool flare_block_affordable(std::int32_t blocks,
                                                    bool infinite_blocks) noexcept {
    return infinite_blocks || blocks >= retail_flare_block_cost;
}

/** PrefabTool.get_has_enough_ammo: `block_count >= prefab_cost or infinite`. */
[[nodiscard]] constexpr bool prefab_affordable(std::int32_t blocks, std::int32_t prefab_cost,
                                               bool infinite_blocks) noexcept {
    return infinite_blocks || blocks >= prefab_cost;
}

/**
 * FlareBlockTool inherits BlockToolCommon.draw_ghosting with block_cost = 10
 * and never sets old_hit_cube, so its ghost is the single hit cube and the
 * wallet test is `1 * 10 <= block_count`.
 */
[[nodiscard]] inline BlockLineGhost evaluate_flare_block_ghost(
    const VxlMap& map, const BlockTarget& target, std::int32_t blocks, bool infinite_blocks,
    bool space_to_add_blocks, const BlockOccupiedPredicate& occupied,
    std::int32_t max_modifiable_z = retail_max_modifiable_z) {
    std::vector<VoxelCell> cells;
    if (target.cell.has_value() && (*target.cell)[0U] >= 0 && (*target.cell)[1U] >= 0 &&
        (*target.cell)[2U] >= 0) {
        cells.push_back({static_cast<std::uint32_t>((*target.cell)[0U]),
                         static_cast<std::uint32_t>((*target.cell)[1U]),
                         static_cast<std::uint32_t>((*target.cell)[2U])});
    }
    return evaluate_block_line_ghost(map, target, cells,
                                     blocks > 0 ? blocks / retail_flare_block_cost : 0,
                                     infinite_blocks, space_to_add_blocks, occupied,
                                     max_modifiable_z);
}

/**
 * FlareBlockTool.use_primary: the placement is sent only when
 * `valid_placement and hit_cube and get_has_enough_ammo()`; anything else
 * plays build_error. Unlike BlockTool it does not test old_hit_cube_adjacent.
 */
[[nodiscard]] inline bool flare_block_placement_allowed(const BlockLineGhost& ghost,
                                                        std::int32_t blocks,
                                                        bool infinite_blocks) noexcept {
    return ghost.valid && !ghost.cells.empty() &&
           flare_block_affordable(blocks, infinite_blocks);
}

/**
 * BlockTool.on_stop_primary: the line is sent only when the ghost is valid,
 * anchored and affordable.
 */
[[nodiscard]] inline bool block_line_placement_allowed(const BlockLineGhost& ghost,
                                                       std::int32_t blocks,
                                                       bool infinite_blocks) noexcept {
    return ghost.valid && ghost.first_adjacent && !ghost.cells.empty() &&
           block_line_affordable(blocks, ghost.cells.size(), infinite_blocks);
}

} // namespace battlespades::world
