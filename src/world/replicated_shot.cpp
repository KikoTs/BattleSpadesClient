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

std::vector<ReplicatedPellet>
replicated_hitscan_pellets(const VxlMap& map,
                           std::array<float, 3U> origin,
                           std::array<float, 3U> orientation,
                           std::uint8_t tool_id,
                           std::uint8_t seed,
                           bool zoomed,
                           std::optional<double> hip_accuracy) {
    const auto* weapon = find_weapon_definition(tool_id);
    if (weapon == nullptr || weapon->melee || weapon->projectile ||
        weapon->maximum_range <= 0.0 || weapon->pellet_count == 0U) {
        return {};
    }
    // Same pellet stream as replicated_hitscan_impacts (Character.shoot's
    // `for i in xrange(weapon.pellets)` loop), without the per-cell dedup.
    const auto forward = normalized(orientation);
    const double raw_accuracy =
        zoomed && weapon->retail.aim.accuracy_zoom.has_value()
            ? *weapon->retail.aim.accuracy_zoom
            : hip_accuracy.value_or(weapon->retail.aim.accuracy.value_or(weapon->spread));
    const float accuracy = static_cast<float>(std::max(0.0, raw_accuracy));
    const double spread_scale = zoomed ? 2.0 : 4.0;
    const double spread_center = zoomed ? 1.0 : 2.0;
    RetailRandom random{seed};
    std::vector<ReplicatedPellet> pellets;
    pellets.reserve(weapon->pellet_count);
    for (std::uint8_t pellet{}; pellet < weapon->pellet_count; ++pellet) {
        std::array<float, 3U> direction = forward;
        for (auto& axis : direction) {
            axis += static_cast<float>(
                (random.random() * spread_scale - spread_center) * accuracy);
        }
        direction = normalized(direction);
        ReplicatedPellet result{direction, std::nullopt};
        if (const auto hit = trace_first_solid(map, origin, direction,
                                               static_cast<float>(weapon->maximum_range));
            hit.has_value()) {
            result.contact = hit->position;
        }
        pellets.push_back(result);
    }
    return pellets;
}

std::vector<TerrainImpactEvent>
replicated_hitscan_impacts(const VxlMap& map,
                           std::array<float, 3U> origin,
                           std::array<float, 3U> orientation,
                           std::uint8_t tool_id,
                           std::uint8_t seed,
                           bool zoomed,
                           std::optional<double> hip_accuracy) {
    const auto* weapon = find_weapon_definition(tool_id);
    if (weapon == nullptr || weapon->melee || weapon->projectile ||
        weapon->maximum_range <= 0.0 || weapon->pellet_count == 0U) {
        return {};
    }

    const auto forward = normalized(orientation);
    const double raw_accuracy =
        zoomed && weapon->retail.aim.accuracy_zoom.has_value()
            ? *weapon->retail.aim.accuracy_zoom
            : hip_accuracy.value_or(weapon->retail.aim.accuracy.value_or(weapon->spread));
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

double observe_hitscan_bloom(ObservedShotBloom& state,
                             std::uint8_t tool_id,
                             double now_seconds) noexcept {
    const auto* weapon = find_weapon_definition(tool_id);
    if (weapon == nullptr) {
        return 0.0;
    }
    const auto& aim = weapon->retail.aim;
    if (!aim.variable_accuracy) {
        return aim.accuracy.value_or(weapon->spread);
    }
    const double spread_min = aim.spread_min.value_or(1.0);
    const double spread_max = std::max(spread_min, aim.spread_max.value_or(spread_min));
    if (state.tool_id != tool_id || !std::isfinite(state.spread)) {
        // Weapon.on_unset/on_set restore accuracy_spread_min.
        state = ObservedShotBloom{tool_id, spread_min, now_seconds};
    } else {
        const double elapsed = std::max(0.0, now_seconds - state.time_seconds);
        state.spread = std::max(spread_min,
                                state.spread -
                                    elapsed * aim.spread_reduction_per_second.value_or(0.0));
    }
    state.time_seconds = now_seconds;
    const double accuracy_min = aim.accuracy_min.value_or(aim.accuracy.value_or(0.0));
    const double accuracy_max = aim.accuracy_max.value_or(accuracy_min);
    const double ratio = spread_max > spread_min
                             ? std::clamp((state.spread - spread_min) / (spread_max - spread_min),
                                          0.0, 1.0)
                             : 0.0;
    const double accuracy = accuracy_min + ratio * (accuracy_max - accuracy_min);
    state.spread = std::min(spread_max,
                            state.spread + aim.spread_increase_per_shot.value_or(0.0));
    return accuracy;
}

} // namespace battlespades::world
