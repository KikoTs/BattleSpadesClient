#include "battlespades/world/block_placement.hpp"

#include "battlespades/world/voxel_raycast.hpp"

#include <algorithm>
#include <cmath>

namespace battlespades::world {
namespace {

// Stock hitscan has no practical range; the reach test is check_cube_placement.
constexpr float target_hitscan_range{128.0F};
// A2214: the map floor the bridge scan refuses to march into.
constexpr std::int32_t bridge_floor_z{240};

[[nodiscard]] bool in_map_columns(std::int32_t x, std::int32_t y) noexcept {
    return x >= 0 && y >= 0 && x < static_cast<std::int32_t>(VxlMap::width) &&
           y < static_cast<std::int32_t>(VxlMap::depth);
}

[[nodiscard]] bool solid_at(const VxlMap& map, std::int32_t x, std::int32_t y,
                            std::int32_t z) noexcept {
    if (!in_map_columns(x, y) || z < 0 || z >= static_cast<std::int32_t>(VxlMap::height)) {
        return false;
    }
    return map.solid(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                     static_cast<std::uint32_t>(z));
}

[[nodiscard]] BlockTargetCell to_cell(std::int32_t x, std::int32_t y, std::int32_t z) noexcept {
    return {static_cast<std::int16_t>(std::clamp(x, -32768, 32767)),
            static_cast<std::int16_t>(std::clamp(y, -32768, 32767)),
            static_cast<std::int16_t>(std::clamp(z, -32768, 32767))};
}

[[nodiscard]] bool is_occupied(const BlockOccupiedPredicate& occupied,
                               BlockTargetCell cell) noexcept {
    return occupied && occupied(cell);
}

} // namespace

std::string_view block_place_hint_key(BlockPlaceHint hint) noexcept {
    switch (hint) {
    case BlockPlaceHint::something_in_the_way:
        return "BLOCK_PLACE_FAIL_SOMETHING_IN_THE_WAY";
    case BlockPlaceHint::not_attached:
        return "BLOCK_PLACE_FAIL_NOT_ATTACHED";
    case BlockPlaceHint::too_far:
        return "BLOCK_PLACE_FAIL_TOO_FAR";
    case BlockPlaceHint::water:
        return "BLOCK_PLACE_FAIL_WATER";
    case BlockPlaceHint::not_enough_blocks:
        return "BLOCK_PLACE_FAIL_NOT_ENOUGH_BLOCKS";
    case BlockPlaceHint::ugc_capacity:
        return "BLOCK_PLACE_UGC_CAPACITY";
    case BlockPlaceHint::none:
        break;
    }
    return {};
}

bool block_cell_overlaps_body(BlockTargetCell cell, Vec3 body_position) noexcept {
    const auto x0 = static_cast<std::int32_t>(std::floor(body_position.x - 0.45));
    const auto x1 = static_cast<std::int32_t>(std::floor(body_position.x + 0.45));
    const auto y0 = static_cast<std::int32_t>(std::floor(body_position.y - 0.45));
    const auto y1 = static_cast<std::int32_t>(std::floor(body_position.y + 0.45));
    const auto z0 = static_cast<std::int32_t>(std::floor(body_position.z));
    const auto z1 = static_cast<std::int32_t>(std::floor(body_position.z + 2.0));
    return cell[0U] >= x0 && cell[0U] <= x1 && cell[1U] >= y0 && cell[1U] <= y1 &&
           cell[2U] >= z0 && cell[2U] <= z1;
}

CubePlacementCheck check_cube_placement(Vec3 eye, BlockTargetCell cube,
                                        double safe_radius,
                                        std::int32_t max_modifiable_z) noexcept {
    if (in_map_columns(cube[0U], cube[1U]) && cube[2U] <= max_modifiable_z) {
        const double dx = eye.x - (static_cast<double>(cube[0U]) + 0.5);
        const double dy = eye.y - (static_cast<double>(cube[1U]) + 0.5);
        const double dz = eye.z - (static_cast<double>(cube[2U]) + 0.5);
        const double sq = dx * dx + dy * dy + dz * dz;
        return {safe_radius * safe_radius > sq, sq};
    }
    return {false, safe_radius * safe_radius};
}

bool is_block_touching_blocks(const VxlMap& map, BlockTargetCell cell) noexcept {
    constexpr std::array<std::array<std::int32_t, 3U>, 6U> neighbours{{
        {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1},
    }};
    return std::ranges::any_of(neighbours, [&](const auto& offset) {
        return solid_at(map, cell[0U] + offset[0U], cell[1U] + offset[1U],
                        cell[2U] + offset[2U]);
    });
}

BridgeScanResult scan_bridge_placement(const VxlMap& map, Vec3 eye, Vec3 orientation,
                                       double max_distance,
                                       const BlockOccupiedPredicate& occupied,
                                       std::int32_t max_modifiable_z) {
    BridgeScanResult result;
    if (!std::isfinite(max_distance) || max_distance <= 0.0) {
        return result;
    }
    std::optional<BlockTargetCell> first;
    std::optional<BlockTargetCell> last;
    // `dist = max_dist; while True: ...; dist -= 1; if dist <= 0: break`.
    for (double distance = max_distance; distance > 0.0; distance -= 1.0) {
        const auto cell = to_cell(
            static_cast<std::int32_t>(std::floor(eye.x + orientation.x * distance)),
            static_cast<std::int32_t>(std::floor(eye.y + orientation.y * distance)),
            static_cast<std::int32_t>(std::floor(eye.z + orientation.z * distance)));
        if (!first.has_value()) first = cell;
        if (last == cell) continue;
        if (solid_at(map, cell[0U], cell[1U], cell[2U]) || cell[2U] >= bridge_floor_z) {
            result.position = last;
            return result;
        }
        if (is_block_touching_blocks(map, cell) && !is_occupied(occupied, cell) &&
            check_cube_placement(eye, cell, max_distance, max_modifiable_z).ok) {
            result.position = cell;
            result.valid = true;
            return result;
        }
        last = cell;
    }
    result.position = first;
    result.floating = !first.has_value() || !is_block_touching_blocks(map, *first);
    return result;
}

BlockTarget resolve_block_target(const VxlMap& map, Vec3 eye, Vec3 orientation,
                                 double max_block_distance,
                                 const BlockOccupiedPredicate& occupied,
                                 std::int32_t max_modifiable_z) {
    BlockTarget target;
    target.valid = true;
    bool check_bridge = false;
    const auto hit = trace_first_solid(
        map, {static_cast<float>(eye.x), static_cast<float>(eye.y), static_cast<float>(eye.z)},
        {static_cast<float>(orientation.x), static_cast<float>(orientation.y),
         static_cast<float>(orientation.z)},
        target_hitscan_range);
    if (!hit.has_value()) {
        check_bridge = true;
        target.valid = false;
    } else {
        // get_next_cube(*ret): the air cell on the struck face.
        const auto next = to_cell(static_cast<std::int32_t>(hit->cell.x) + hit->normal[0U],
                                  static_cast<std::int32_t>(hit->cell.y) + hit->normal[1U],
                                  static_cast<std::int32_t>(hit->cell.z) + hit->normal[2U]);
        target.cell = next;
        if (is_occupied(occupied, next)) {
            target.hint = BlockPlaceHint::something_in_the_way;
            target.valid = false;
        } else if (const auto reach =
                       check_cube_placement(eye, next, max_block_distance, max_modifiable_z);
                   !reach.ok) {
            check_bridge = true;
            if (reach.sq_distance < max_block_distance * max_block_distance) {
                max_block_distance = std::sqrt(reach.sq_distance);
            }
        }
    }
    if (check_bridge) {
        const auto bridge =
            scan_bridge_placement(map, eye, orientation, max_block_distance, occupied,
                                  max_modifiable_z);
        if (!bridge.valid && bridge.position.has_value() &&
            target.hint == BlockPlaceHint::none) {
            target.hint = bridge.floating ? BlockPlaceHint::not_attached : BlockPlaceHint::too_far;
        }
        target.cell = bridge.position;
        target.valid = bridge.position.has_value() && bridge.valid;
    }
    return target;
}

BlockLineGhost evaluate_block_line_ghost(const VxlMap& map, const BlockTarget& target,
                                         std::span<const VoxelCell> line, std::int32_t blocks,
                                         bool infinite_blocks, bool space_to_add_blocks,
                                         const BlockOccupiedPredicate& occupied,
                                         std::int32_t max_modifiable_z) {
    BlockLineGhost ghost;
    ghost.valid = target.valid && target.cell.has_value();
    ghost.hint = target.hint;
    if (!target.cell.has_value()) {
        ghost.valid = false;
        return ghost;
    }
    // The server accepts at most 64 cells, including already occupied ones.
    if (line.size() > 64U) ghost.valid = false;
    // get_block_line(include_solids=False).
    for (const auto& cell : line) {
        if (cell.x < VxlMap::width && cell.y < VxlMap::depth && cell.z < VxlMap::height &&
            map.solid(cell.x, cell.y, cell.z)) {
            continue;
        }
        ghost.cells.push_back(cell);
    }
    if (ghost.cells.empty()) {
        ghost.valid = false;
        return ghost;
    }
    const auto& first = ghost.cells.front();
    ghost.first_adjacent = is_block_touching_blocks(
        map, to_cell(static_cast<std::int32_t>(first.x), static_cast<std::int32_t>(first.y),
                     static_cast<std::int32_t>(first.z)));
    const auto post = [&ghost](BlockPlaceHint hint) {
        if (ghost.hint == BlockPlaceHint::none) ghost.hint = hint;
        ghost.valid = false;
    };
    if (!space_to_add_blocks) {
        post(BlockPlaceHint::ugc_capacity);
        return ghost;
    }
    for (std::size_t index{}; index < ghost.cells.size(); ++index) {
        const auto& cell = ghost.cells[index];
        const auto signed_cell = to_cell(static_cast<std::int32_t>(cell.x),
                                         static_cast<std::int32_t>(cell.y),
                                         static_cast<std::int32_t>(cell.z));
        const bool unobstructed = !is_occupied(occupied, signed_cell);
        // block_manager.valid_to_add: inside the map and above the water.
        const bool valid_to_add = cell.x < VxlMap::width && cell.y < VxlMap::depth &&
                                  cell.z >= 1U &&
                                  static_cast<std::int32_t>(cell.z) <= max_modifiable_z;
        if (!unobstructed || !valid_to_add) {
            post(static_cast<std::int32_t>(cell.z) > max_modifiable_z
                     ? BlockPlaceHint::water
                     : BlockPlaceHint::something_in_the_way);
            break;
        }
        const bool sufficient = infinite_blocks ||
                                static_cast<std::int64_t>(index + 1U) <=
                                    static_cast<std::int64_t>(blocks);
        if (!sufficient) {
            post(BlockPlaceHint::not_enough_blocks);
            break;
        }
    }
    return ghost;
}

} // namespace battlespades::world
