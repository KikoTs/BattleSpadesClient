#pragma once

#include "battlespades/world/player_movement.hpp"
#include "battlespades/world/voxel_collapse.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>

namespace battlespades::world {

/**
 * Retail block-tool targeting, ported from the stock client:
 * aoslib/weapons/blockToolCommon.py draw_ghosting (lines 19-95) and
 * character.pyd Character.scan_bridge_placement (0x100635b0).
 *
 * The tool resolves one hit cube per frame. A hitscan that misses, or hits a
 * surface out of reach, falls back to the bridge scan: march back from the
 * reach limit toward the eye one block at a time and take the farthest free
 * cell that touches a block, is clear of every player and is in reach. That
 * is what lets a player aim into open air past a ledge and extend a bridge.
 */
using BlockTargetCell = std::array<std::int16_t, 3U>;

/** MAX_BLOCK_DISTANCE (10) and CLASSIC_MAX_BLOCK_DISTANCE (5). */
inline constexpr double retail_max_block_distance{10.0};
inline constexpr double retail_classic_max_block_distance{5.0};
/** block_manager.max_modifiable_z (A2215): below it is the water plane. */
inline constexpr std::int32_t retail_max_modifiable_z{238};
/**
 * GameScene.on_connect (gameScene 0x10127f40): max_modifiable_z = 238 when
 * InitialInfo.beach_z_modifiable, else 237.
 */
[[nodiscard]] constexpr std::int32_t retail_max_modifiable_z_for(bool beach_z_modifiable) noexcept {
    return beach_z_modifiable ? retail_max_modifiable_z : retail_max_modifiable_z - 1;
}

/** The six one-frame background big-text refusals draw_ghosting raises. */
enum class BlockPlaceHint : std::uint8_t {
    none,
    something_in_the_way,
    not_attached,
    too_far,
    water,
    not_enough_blocks,
    ugc_capacity,
};

/** strings.BLOCK_PLACE_FAIL_* / BLOCK_PLACE_UGC_CAPACITY key; empty for none. */
[[nodiscard]] std::string_view block_place_hint_key(BlockPlaceHint hint) noexcept;

/**
 * True when `cell` is inside a character standing at `body_position` (eye):
 * the same +-0.45 x/y column and floor(z)..floor(z + 2) span the server
 * refuses (construction._overlaps_living_player).
 */
[[nodiscard]] bool block_cell_overlaps_body(BlockTargetCell cell, Vec3 body_position) noexcept;

/** GameScene.can_place_block_on_player inverted: true when a player is in the way. */
using BlockOccupiedPredicate = std::function<bool(BlockTargetCell)>;

/** WorldObject.check_cube_placement: in the map, above the water, in reach. */
struct CubePlacementCheck final {
    bool ok{};
    /** get_cube_sq_distance(); safe_radius squared when the cube is out of bounds. */
    double sq_distance{};
};
[[nodiscard]] CubePlacementCheck check_cube_placement(
    Vec3 eye, BlockTargetCell cube, double safe_radius,
    std::int32_t max_modifiable_z = retail_max_modifiable_z) noexcept;

/** Character.is_block_touching_blocks: a face neighbour is solid. */
[[nodiscard]] bool is_block_touching_blocks(const VxlMap& map, BlockTargetCell cell) noexcept;

struct BridgeScanResult final {
    std::optional<BlockTargetCell> position;
    bool valid{};
    /** No block to attach to: BLOCK_PLACE_FAIL_NOT_ATTACHED rather than TOO_FAR. */
    bool floating{};
};

[[nodiscard]] BridgeScanResult scan_bridge_placement(const VxlMap& map, Vec3 eye,
                                                     Vec3 orientation, double max_distance,
                                                     const BlockOccupiedPredicate& occupied,
                                                     std::int32_t max_modifiable_z =
                                                         retail_max_modifiable_z);

/** The first half of draw_ghosting: which cube the tool is aimed at. */
struct BlockTarget final {
    std::optional<BlockTargetCell> cell;
    bool valid{};
    BlockPlaceHint hint{BlockPlaceHint::none};
};

[[nodiscard]] BlockTarget resolve_block_target(const VxlMap& map, Vec3 eye, Vec3 orientation,
                                               double max_block_distance,
                                               const BlockOccupiedPredicate& occupied,
                                               std::int32_t max_modifiable_z =
                                                   retail_max_modifiable_z);

/** The second half: the dragged line from old_hit_cube to the hit cube. */
struct BlockLineGhost final {
    bool valid{};
    /** map.has_neighbors(first_block): the line is anchored to the world. */
    bool first_adjacent{};
    BlockPlaceHint hint{BlockPlaceHint::none};
    /** The line's air cells, the ones draw_cube renders and the wallet pays for. */
    std::vector<VoxelCell> cells;

    /** block_color only when valid AND anchored; otherwise (255, 0, 0). */
    [[nodiscard]] bool draw_red() const noexcept { return !valid || !first_adjacent; }
};

/** Alpha of every retail block ghost: `color + (80,)`. */
inline constexpr float retail_block_ghost_alpha{80.0F / 255.0F};

[[nodiscard]] BlockLineGhost evaluate_block_line_ghost(const VxlMap& map,
                                                       const BlockTarget& target,
                                                       std::span<const VoxelCell> line,
                                                       std::int32_t blocks, bool infinite_blocks,
                                                       bool space_to_add_blocks,
                                                       const BlockOccupiedPredicate& occupied,
                                                       std::int32_t max_modifiable_z =
                                                           retail_max_modifiable_z);

} // namespace battlespades::world
