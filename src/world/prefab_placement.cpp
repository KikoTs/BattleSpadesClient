#include "battlespades/world/prefab_placement.hpp"

#include <array>
#include <algorithm>

namespace battlespades::world {

std::string_view placement_sound_stem(PlacementFeedbackKind kind) noexcept {
    switch (kind) {
    case PlacementFeedbackKind::block:
        return "build";
    case PlacementFeedbackKind::prefab:
        return "prefabbuild";
    case PlacementFeedbackKind::ugc_prefab:
        return "ugc_place";
    }
    return {};
}

PrefabPlacementEvaluation evaluate_prefab_placement(
    const VxlMap& map, std::span<const PrefabPlacementCell> footprint) noexcept {
    PrefabPlacementEvaluation result;
    static constexpr std::array<std::array<std::int32_t, 3U>, 6U> neighbours{{
        {-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
        {0, 1, 0},  {0, 0, -1}, {0, 0, 1},
    }};

    for (const auto& cell : footprint) {
        // z=239 is the immutable support bed and z=238 is reserved from
        // competitive building, matching the ordinary block-tool boundary.
        if (cell.x < 0 || cell.y < 0 || cell.z < 0 ||
            cell.x >= static_cast<std::int32_t>(VxlMap::width) ||
            cell.y >= static_cast<std::int32_t>(VxlMap::depth) ||
            cell.z >= static_cast<std::int32_t>(VxlMap::height - 2U)) {
            result.all_in_bounds = false;
            continue;
        }

        const auto x = static_cast<std::uint32_t>(cell.x);
        const auto y = static_cast<std::uint32_t>(cell.y);
        const auto z = static_cast<std::uint32_t>(cell.z);
        if (map.solid(x, y, z)) {
            // Overlap is intentional retail behaviour: that authored voxel is
            // already satisfied and also attaches the remaining new cells.
            result.touches_world = true;
            continue;
        }
        ++result.required_blocks;

        for (const auto& offset : neighbours) {
            const auto nx = cell.x + offset[0U];
            const auto ny = cell.y + offset[1U];
            const auto nz = cell.z + offset[2U];
            if (nx >= 0 && ny >= 0 && nz >= 0 &&
                nx < static_cast<std::int32_t>(VxlMap::width) &&
                ny < static_cast<std::int32_t>(VxlMap::depth) &&
                nz < static_cast<std::int32_t>(VxlMap::height) &&
                map.solid(static_cast<std::uint32_t>(nx),
                          static_cast<std::uint32_t>(ny),
                          static_cast<std::uint32_t>(nz))) {
                result.touches_world = true;
                break;
            }
        }
    }
    return result;
}

bool PrefabPlacementTransaction::stage(
    std::span<const PrefabPlacementVoxel> voxels) {
    if (voxels.empty() || voxels.size() > maximum_voxels - staged_.size()) {
        return false;
    }
    for (const auto& voxel : voxels) {
        const auto same_cell = [&voxel](const PrefabPlacementVoxel& current) {
            return current.cell.x == voxel.cell.x && current.cell.y == voxel.cell.y &&
                   current.cell.z == voxel.cell.z;
        };
        if (auto found = std::ranges::find_if(staged_, same_cell);
            found != staged_.end()) {
            // Slices may overlap at a defensive replay boundary.  The newest
            // authoritative colour wins without charging another visual cell.
            found->color = voxel.color;
        } else {
            staged_.push_back(voxel);
        }
    }
    return true;
}

PrefabCommitResult PrefabPlacementTransaction::commit(VxlMap& map) noexcept {
    PrefabCommitResult result;
    if (staged_.empty()) {
        return result;
    }

    for (const auto& voxel : staged_) {
        if (voxel.cell.x < 0 || voxel.cell.y < 0 || voxel.cell.z < 0 ||
            voxel.cell.x >= static_cast<std::int32_t>(VxlMap::width) ||
            voxel.cell.y >= static_cast<std::int32_t>(VxlMap::depth) ||
            voxel.cell.z >= static_cast<std::int32_t>(VxlMap::height - 1U)) {
            clear();
            return result;
        }
    }

    for (const auto& voxel : staged_) {
        const auto x = static_cast<std::uint32_t>(voxel.cell.x);
        const auto y = static_cast<std::uint32_t>(voxel.cell.y);
        const auto z = static_cast<std::uint32_t>(voxel.cell.z);
        if (map.solid(x, y, z)) {
            // Existing world geometry satisfies the prefab footprint and is
            // not recoloured. Retail charges and builds only missing voxels.
            ++result.overlapped;
            continue;
        }
        if (map.set_voxel(x, y, z, voxel.color)) {
            ++result.placed;
        }
    }
    result.committed = true;
    clear();
    return result;
}

void PrefabPlacementTransaction::clear() noexcept {
    staged_.clear();
}

} // namespace battlespades::world
