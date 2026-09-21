#include "battlespades/world/replicated_shot.hpp"
#include "battlespades/world/retail_random.hpp"

#include "battlespades/world/voxel_raycast.hpp"
#include "battlespades/world/weapon_catalog.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <unordered_set>

namespace battlespades::world {
namespace {

[[nodiscard]] std::array<float, 3U>
normalized(std::array<float, 3U> value) noexcept {
    const float magnitude = std::sqrt(value[0U] * value[0U] + value[1U] * value[1U] +
                                      value[2U] * value[2U]);
    if (!std::isfinite(magnitude) || magnitude <= 1.0e-6F) {
        return {1.0F, 0.0F, 0.0F};
    }
    for (auto& axis : value) {
        axis /= magnitude;
    }
    return value;
}

[[nodiscard]] std::uint64_t cell_key(VoxelCell cell) noexcept {
    return (static_cast<std::uint64_t>(cell.x) << 32U) |
           (static_cast<std::uint64_t>(cell.y) << 16U) |
           static_cast<std::uint64_t>(cell.z);
}

} // namespace

std::vector<TerrainImpactEvent>
replicated_hitscan_impacts(const VxlMap& map,
                           std::array<float, 3U> origin,
                           std::array<float, 3U> orientation,
                           std::uint8_t tool_id,
                           std::uint8_t seed,
                           bool zoomed) {
    const auto* weapon = find_weapon_definition(tool_id);
    if (weapon == nullptr || weapon->melee || weapon->projectile ||
        weapon->maximum_range <= 0.0 || weapon->pellet_count == 0U) {
        return {};
    }

    const auto forward = normalized(orientation);
    const double raw_accuracy =
        zoomed && weapon->retail.aim.accuracy_zoom.has_value()
            ? *weapon->retail.aim.accuracy_zoom
            : weapon->retail.aim.accuracy.value_or(weapon->spread);
    const float accuracy = static_cast<float>(std::max(0.0, raw_accuracy));
    const double spread_scale = zoomed ? 2.0 : 4.0;
    const double spread_center = zoomed ? 1.0 : 2.0;
    RetailRandom random{seed};

    std::vector<TerrainImpactEvent> impacts;
    impacts.reserve(weapon->pellet_count);
    std::unordered_set<std::uint64_t> emitted_cells;
    for (std::uint8_t pellet{}; pellet < weapon->pellet_count; ++pellet) {
        std::array<float, 3U> direction = forward;
        for (auto& axis : direction) {
            axis += static_cast<float>(
                (random.random() * spread_scale - spread_center) * accuracy);
        }
        direction = normalized(direction);
        const auto hit = trace_first_solid(
            map, origin, direction, static_cast<float>(weapon->maximum_range));
        if (!hit.has_value() || !emitted_cells.insert(cell_key(hit->cell)).second) {
            continue;
        }
        const auto color = map.color(hit->cell.x, hit->cell.y, hit->cell.z);
        if (!color.has_value()) {
            continue;
        }
        TerrainImpactEvent impact{TerrainImpactKind::bullet,
                                  hit->cell,
                                  *color,
                                  hit->normal,
                                  false,
                                  1.0F,
                                  tool_id};
        impact.position = hit->position;
        impacts.push_back(impact);
    }
    return impacts;
}

} // namespace battlespades::world
