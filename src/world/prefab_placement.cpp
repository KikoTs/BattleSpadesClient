#include "battlespades/world/prefab_placement.hpp"

#include <array>
#include <algorithm>
#include <limits>

namespace battlespades::world {

BlockLinePlacementEvaluation evaluate_block_line_placement(
    const VxlMap& map, std::span<const VoxelCell> line) noexcept {
    BlockLinePlacementEvaluation result;
    // The server accepts at most 64 cells, including already occupied ones.
    result.all_in_bounds = line.size() <= 64U;
    constexpr std::array<std::array<std::int32_t, 3U>, 6U> neighbours{{
        {-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
        {0, 1, 0}, {0, 0, -1}, {0, 0, 1},
    }};
    for (std::size_t index{}; index < line.size(); ++index) {
        const auto& cell = line[index];
        if (cell.x >= VxlMap::width || cell.y >= VxlMap::depth ||
            cell.z == 0U || cell.z > VxlMap::height - 2U) {
            result.all_in_bounds = false;
            continue;
        }
        // BlockToolCommon.get_block_line(False, False) omits existing solids.
        if (map.solid(cell.x, cell.y, cell.z)) continue;
        ++result.required_blocks;
        bool supported{};
        for (const auto& offset : neighbours) {
            const auto x = static_cast<std::int32_t>(cell.x) + offset[0U];
            const auto y = static_cast<std::int32_t>(cell.y) + offset[1U];
            const auto z = static_cast<std::int32_t>(cell.z) + offset[2U];
            if (x < 0 || y < 0 || z < 0 || x >= static_cast<std::int32_t>(VxlMap::width) ||
                y >= static_cast<std::int32_t>(VxlMap::depth) ||
                z >= static_cast<std::int32_t>(VxlMap::height)) continue;
            const VoxelCell neighbour{static_cast<std::uint32_t>(x),
                                      static_cast<std::uint32_t>(y),
                                      static_cast<std::uint32_t>(z)};
            const auto earlier = line.first(index);
            if (map.solid(neighbour.x, neighbour.y, neighbour.z) ||
                std::ranges::find(earlier, neighbour) != earlier.end()) {
                supported = true;
                break;
            }
        }
        result.all_supported = result.all_supported && supported;
    }
    return result;
}

PrefabPlacementCell rotate_prefab_cell(
    PrefabPlacementCell cell, std::uint8_t yaw, std::uint8_t pitch, std::uint8_t roll) noexcept {
    for (std::uint8_t step{}; step < (roll & 3U); ++step) cell = {-cell.z, cell.y, cell.x};
    for (std::uint8_t step{}; step < (pitch & 3U); ++step) cell = {cell.x, cell.z, -cell.y};
    for (std::uint8_t step{}; step < (yaw & 3U); ++step) cell = {cell.y, -cell.x, cell.z};
    return cell;
}

std::array<float, 16U> prefab_preview_transform(
    const std::array<float, 3U>& pivot, PrefabPlacementCell anchor,
    std::uint8_t yaw, std::uint8_t pitch, std::uint8_t roll) noexcept {
    // KV6 writes (x-px, -(z-pz), y-py), with each cube centred on its voxel.
    // Build the signed-permutation matrix directly, avoiding Euler-order and
    // pivot errors between the visible ghost and the server's voxel expansion.
    const auto x = rotate_prefab_cell({1, 0, 0}, yaw, pitch, roll);
    const auto y = rotate_prefab_cell({0, 1, 0}, yaw, pitch, roll);
    const auto z = rotate_prefab_cell({0, 0, 1}, yaw, pitch, roll);
    return {static_cast<float>(x.x), static_cast<float>(x.y), static_cast<float>(x.z), 0.0F,
            static_cast<float>(-z.x), static_cast<float>(-z.y), static_cast<float>(-z.z), 0.0F,
            static_cast<float>(y.x), static_cast<float>(y.y), static_cast<float>(y.z), 0.0F,
            anchor.x + 0.5F + pivot[0U] * x.x + pivot[1U] * y.x + pivot[2U] * z.x,
            anchor.y + 0.5F + pivot[0U] * x.y + pivot[1U] * y.y + pivot[2U] * z.y,
            anchor.z + 0.5F + pivot[0U] * x.z + pivot[1U] * y.z + pivot[2U] * z.z, 1.0F};
}

void PrefabPlacementPreview::reset(std::span<const PrefabPlacementCell> authored) {
    authored_.assign(authored.begin(), authored.end());
    rotated_.clear();
    rotation_.reset();
    evaluated_map_ = nullptr;
    evaluations_ = 0U;
}

const PrefabPlacementBounds& PrefabPlacementPreview::bounds(
    std::uint8_t yaw, std::uint8_t pitch, std::uint8_t roll) {
    const std::array rotation{static_cast<std::uint8_t>(yaw & 3U),
                              static_cast<std::uint8_t>(pitch & 3U),
                              static_cast<std::uint8_t>(roll & 3U)};
    if (rotation_ == rotation) return bounds_;
    rotation_ = rotation;
    evaluated_map_ = nullptr;
    rotated_.clear();
    rotated_.reserve(authored_.size());
    bounds_ = {};
    if (!authored_.empty()) {
        bounds_.minimum.fill(std::numeric_limits<std::int32_t>::max());
        bounds_.maximum.fill(std::numeric_limits<std::int32_t>::lowest());
    }
    for (const auto cell : authored_) {
        const auto rotated = rotate_prefab_cell(cell, yaw, pitch, roll);
        rotated_.push_back(rotated);
        const std::array values{rotated.x, rotated.y, rotated.z};
        for (std::size_t axis{}; axis < 3U; ++axis) {
            bounds_.minimum[axis] = std::min(bounds_.minimum[axis], values[axis]);
            bounds_.maximum[axis] = std::max(bounds_.maximum[axis], values[axis]);
        }
    }
    return bounds_;
}

PrefabPlacementEvaluation PrefabPlacementPreview::evaluate(
    const VxlMap& map, PrefabPlacementCell anchor,
    std::uint8_t yaw, std::uint8_t pitch, std::uint8_t roll) {
    static_cast<void>(bounds(yaw, pitch, roll));
    if (evaluated_map_ != &map || evaluated_revision_ != map.revision() ||
        evaluated_anchor_ != anchor) {
        evaluation_ = evaluate_prefab_placement(map, rotated_, anchor);
        evaluated_map_ = &map;
        evaluated_revision_ = map.revision();
        evaluated_anchor_ = anchor;
        ++evaluations_;
    }
    return evaluation_;
}

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
    const VxlMap& map, std::span<const PrefabPlacementCell> footprint,
    PrefabPlacementCell anchor) noexcept {
    PrefabPlacementEvaluation result;
    static constexpr std::array<std::array<std::int32_t, 3U>, 6U> neighbours{{
        {-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
        {0, 1, 0},  {0, 0, -1}, {0, 0, 1},
    }};

    for (const auto& offset_cell : footprint) {
        const PrefabPlacementCell cell{offset_cell.x + anchor.x,
                                        offset_cell.y + anchor.y,
                                        offset_cell.z + anchor.z};
        // Z_ABOVE_WATERPLANE is 238 (inclusive); 239 is the immutable bed.
        // The local server also reserves z=0 so live builds cannot enter sky.
        if (cell.x < 0 || cell.y < 0 || cell.z <= 0 ||
            cell.x >= static_cast<std::int32_t>(VxlMap::width) ||
            cell.y >= static_cast<std::int32_t>(VxlMap::depth) ||
            cell.z > static_cast<std::int32_t>(VxlMap::height - 2U)) {
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

        // One world contact attaches the whole prefab; the remaining cells
        // still need bounds/overlap checks, but no more six-neighbour scans.
        if (result.touches_world) continue;

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
        if (const auto found = staged_indices_.find(voxel.cell);
            found != staged_indices_.end()) {
            // Slices may overlap at a defensive replay boundary.  The newest
            // authoritative colour wins without charging another visual cell.
            staged_[found->second].color = voxel.color;
        } else {
            staged_indices_.emplace(voxel.cell, staged_.size());
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
    staged_indices_.clear();
}

} // namespace battlespades::world
