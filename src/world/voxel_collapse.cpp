#include "battlespades/world/voxel_collapse.hpp"

#include <array>
#include <cstdint>
#include <unordered_set>
#include <utility>

namespace battlespades::world {
namespace {

struct SignedCell final {
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t z{};

    [[nodiscard]] friend constexpr bool operator==(const SignedCell&,
                                                    const SignedCell&) = default;
};

struct SignedCellHash final {
    [[nodiscard]] std::size_t operator()(const SignedCell& value) const noexcept {
        const auto x = static_cast<std::uint32_t>(value.x);
        const auto y = static_cast<std::uint32_t>(value.y);
        const auto z = static_cast<std::uint32_t>(value.z);
        return (static_cast<std::size_t>(x) * 73'856'093U) ^
               (static_cast<std::size_t>(y) * 19'349'663U) ^
               (static_cast<std::size_t>(z) * 83'492'791U);
    }
};

[[nodiscard]] constexpr auto collapse_neighbors() noexcept {
    std::array<SignedCell, 18U> result{};
    std::size_t at{};
    for (std::int32_t dx{-1}; dx <= 1; ++dx) {
        for (std::int32_t dy{-1}; dy <= 1; ++dy) {
            for (std::int32_t dz{-1}; dz <= 1; ++dz) {
                const auto distance =
                    static_cast<std::uint32_t>(dx < 0 ? -dx : dx) +
                    static_cast<std::uint32_t>(dy < 0 ? -dy : dy) +
                    static_cast<std::uint32_t>(dz < 0 ? -dz : dz);
                if (distance >= 1U && distance <= 2U) {
                    result[at++] = {dx, dy, dz};
                }
            }
        }
    }
    return result;
}

constexpr auto neighbors = collapse_neighbors();

[[nodiscard]] bool solid(const VxlMap& map, SignedCell cell) noexcept {
    return cell.x >= 0 && cell.y >= 0 && cell.z >= 0 &&
           cell.x < static_cast<std::int32_t>(VxlMap::width) &&
           cell.y < static_cast<std::int32_t>(VxlMap::depth) &&
           cell.z < static_cast<std::int32_t>(VxlMap::height) &&
           map.solid(static_cast<std::uint32_t>(cell.x),
                     static_cast<std::uint32_t>(cell.y),
                     static_cast<std::uint32_t>(cell.z));
}

[[nodiscard]] SignedCell add(SignedCell left, SignedCell right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

} // namespace

std::vector<UnsupportedComponent> find_unsupported_components(
    const VxlMap& map, const std::vector<VoxelCell>& removed_cells,
    std::size_t work_budget) {
    std::vector<UnsupportedComponent> components;
    if (removed_cells.empty() || work_budget == 0U) {
        return components;
    }

    std::unordered_set<SignedCell, SignedCellHash> visited;
    std::unordered_set<SignedCell, SignedCellHash> safe;
    for (const auto& removed : removed_cells) {
        const SignedCell source{static_cast<std::int32_t>(removed.x),
                                static_cast<std::int32_t>(removed.y),
                                static_cast<std::int32_t>(removed.z)};
        for (const auto direction : neighbors) {
            const auto start = add(source, direction);
            if (visited.contains(start) || !solid(map, start)) {
                continue;
            }

            std::vector<SignedCell> stack{start};
            std::unordered_set<SignedCell, SignedCellHash> seen{start};
            UnsupportedComponent component;
            bool grounded{};
            bool exhausted{};
            std::size_t work{};
            while (!stack.empty()) {
                const auto current = stack.back();
                stack.pop_back();
                if (safe.contains(current) || current.z > 238) {
                    grounded = true;
                    break;
                }
                component.push_back({static_cast<std::uint32_t>(current.x),
                                     static_cast<std::uint32_t>(current.y),
                                     static_cast<std::uint32_t>(current.z)});
                for (const auto step : neighbors) {
                    if (++work > work_budget) {
                        exhausted = true;
                        stack.clear();
                        break;
                    }
                    const auto next = add(current, step);
                    if (safe.contains(next)) {
                        grounded = true;
                        stack.clear();
                        break;
                    }
                    if (!seen.contains(next) && solid(map, next)) {
                        seen.insert(next);
                        stack.push_back(next);
                    }
                }
                if (grounded) {
                    break;
                }
            }

            visited.insert(seen.begin(), seen.end());
            if (grounded || exhausted) {
                safe.insert(seen.begin(), seen.end());
            } else if (!component.empty()) {
                components.push_back(std::move(component));
            }
        }
    }
    return components;
}

std::vector<FallingComponent> collapse_unsupported_components(
    VxlMap& map, const std::vector<VoxelCell>& removed_cells,
    std::size_t work_budget) {
    const auto unsupported =
        find_unsupported_components(map, removed_cells, work_budget);
    std::vector<FallingComponent> falling;
    falling.reserve(unsupported.size());
    for (const auto& component : unsupported) {
        FallingComponent captured;
        captured.reserve(component.size());
        for (const auto& cell : component) {
            const auto color = map.color(cell.x, cell.y, cell.z);
            if (color.has_value()) {
                captured.push_back({cell, *color});
            }
        }
        for (const auto& voxel : captured) {
            static_cast<void>(
                map.clear_voxel(voxel.cell.x, voxel.cell.y, voxel.cell.z));
        }
        if (!captured.empty()) {
            falling.push_back(std::move(captured));
        }
    }
    return falling;
}

} // namespace battlespades::world
