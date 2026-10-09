#pragma once

#include "battlespades/world/vxl_map.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace battlespades::world {

struct VoxelCell final {
    std::uint32_t x{};
    std::uint32_t y{};
    std::uint32_t z{};

    [[nodiscard]] friend constexpr bool operator==(const VoxelCell&,
                                                    const VoxelCell&) = default;
};

struct FallingVoxel final {
    VoxelCell cell{};
    VxlColor color{};
};

using UnsupportedComponent = std::vector<VoxelCell>;
using FallingComponent = std::vector<FallingVoxel>;
enum class CollapseRules { retail, classic };

/**
 * Finds every face/edge-connected component exposed by removed cells which
 * does not reach the indestructible z=239 base plane.
 *
 * The 18-neighbor graph and work-budget semantics match the recovered retail
 * chunk check and the authoritative BattleSpades server. Three-axis corner
 * contact alone is not structural support. Exhaustion fails safe: a partial
 * component is never reported as falling.
 */
[[nodiscard]] std::vector<UnsupportedComponent> find_unsupported_components(
    const VxlMap& map, const std::vector<VoxelCell>& removed_cells,
    std::size_t work_budget = 10'000'000U, CollapseRules rules = CollapseRules::retail);

/**
 * Captures colors then removes all unsupported components from `map`. The
 * returned voxels are suitable for a falling-structure presentation; world
 * collision changes atomically before the visual animation starts.
 */
[[nodiscard]] std::vector<FallingComponent> collapse_unsupported_components(
    VxlMap& map, const std::vector<VoxelCell>& removed_cells,
    std::size_t work_budget = 10'000'000U, CollapseRules rules = CollapseRules::retail);

} // namespace battlespades::world
