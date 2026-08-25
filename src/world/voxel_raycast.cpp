#include "battlespades/world/voxel_raycast.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace battlespades::world {
namespace {

[[nodiscard]] std::array<float, 3U>
subtract(std::array<float, 3U> left, std::array<float, 3U> right) noexcept {
    return {left[0U] - right[0U], left[1U] - right[1U], left[2U] - right[2U]};
}

[[nodiscard]] float length(std::array<float, 3U> value) noexcept {
    return std::sqrt(value[0U] * value[0U] + value[1U] * value[1U] +
                     value[2U] * value[2U]);
}

[[nodiscard]] bool segment_blocked(const VxlMap& map,
                                   std::array<float, 3U> listener,
                                   std::array<float, 3U> source) noexcept {
    auto delta = subtract(source, listener);
    const float distance = length(delta);
    if (!std::isfinite(distance) || distance <= 0.001F) {
        return false;
    }
    for (auto& axis : delta) {
        axis /= distance;
    }
    // PlaySound and terrain effects commonly use the impacted cell centre.
    // Trace almost to the source so a thin wall immediately in front remains
    // meaningful, then explicitly ignore only the cell that owns the source.
    constexpr float source_clearance{0.02F};
    const float trace_distance = std::max(0.0F, distance - source_clearance);
    if (trace_distance <= 0.001F) {
        return false;
    }
    const auto hit = trace_first_solid(map, listener, delta, trace_distance);
    if (!hit.has_value()) {
        return false;
    }
    const std::array<std::int64_t, 3U> source_cell{
        static_cast<std::int64_t>(std::floor(source[0U])),
        static_cast<std::int64_t>(std::floor(source[1U])),
        static_cast<std::int64_t>(std::floor(source[2U]))};
    return source_cell[0U] < 0 || source_cell[1U] < 0 || source_cell[2U] < 0 ||
           source_cell[0U] >= static_cast<std::int64_t>(VxlMap::width) ||
           source_cell[1U] >= static_cast<std::int64_t>(VxlMap::depth) ||
           source_cell[2U] >= static_cast<std::int64_t>(VxlMap::height) ||
           hit->cell.x != static_cast<std::uint32_t>(source_cell[0U]) ||
           hit->cell.y != static_cast<std::uint32_t>(source_cell[1U]) ||
           hit->cell.z != static_cast<std::uint32_t>(source_cell[2U]);
}

} // namespace

std::optional<VoxelRayHit>
trace_first_solid(const VxlMap& map,
                  std::array<float, 3U> origin,
                  std::array<float, 3U> direction,
                  float maximum_distance) noexcept {
    constexpr float epsilon{1.0e-7F};
    if (!std::isfinite(maximum_distance) || maximum_distance <= 0.0F ||
        !std::ranges::all_of(origin, [](float value) { return std::isfinite(value); }) ||
        !std::ranges::all_of(direction, [](float value) { return std::isfinite(value); })) {
        return std::nullopt;
    }
    const float direction_length = length(direction);
    if (direction_length <= epsilon) {
        return std::nullopt;
    }
    for (auto& axis : direction) {
        axis /= direction_length;
    }

    std::array<std::int64_t, 3U> cell{
        static_cast<std::int64_t>(std::floor(origin[0U])),
        static_cast<std::int64_t>(std::floor(origin[1U])),
        static_cast<std::int64_t>(std::floor(origin[2U]))};
    std::array<std::int64_t, 3U> step{};
    std::array<float, 3U> next{};
    std::array<float, 3U> delta{};
    for (std::size_t axis{}; axis < 3U; ++axis) {
        step[axis] = direction[axis] > epsilon ? 1 : direction[axis] < -epsilon ? -1 : 0;
        if (step[axis] == 0) {
            next[axis] = std::numeric_limits<float>::infinity();
            delta[axis] = std::numeric_limits<float>::infinity();
            continue;
        }
        const float boundary = static_cast<float>(cell[axis]) + (step[axis] > 0 ? 1.0F : 0.0F);
        next[axis] = (boundary - origin[axis]) / direction[axis];
        delta[axis] = 1.0F / std::abs(direction[axis]);
    }

    float travelled{};
    while (travelled <= maximum_distance) {
        std::size_t axis = next[1U] < next[0U] ? 1U : 0U;
        if (next[2U] < next[axis]) {
            axis = 2U;
        }
        travelled = next[axis];
        if (!std::isfinite(travelled) || travelled > maximum_distance) {
            return std::nullopt;
        }
        next[axis] += delta[axis];
        cell[axis] += step[axis];

        if (cell[0U] < 0 || cell[0U] >= static_cast<std::int64_t>(VxlMap::width) ||
            cell[1U] < 0 || cell[1U] >= static_cast<std::int64_t>(VxlMap::depth) ||
            cell[2U] >= static_cast<std::int64_t>(VxlMap::height)) {
            return std::nullopt;
        }
        if (cell[2U] < 0) {
            if (step[2U] <= 0) {
                return std::nullopt;
            }
            continue;
        }

        const auto x = static_cast<std::uint32_t>(cell[0U]);
        const auto y = static_cast<std::uint32_t>(cell[1U]);
        const auto z = static_cast<std::uint32_t>(cell[2U]);
        if (!map.solid(x, y, z)) {
            continue;
        }
        VoxelRayHit hit;
        hit.cell = {x, y, z};
        hit.normal[axis] = static_cast<std::int32_t>(-step[axis]);
        hit.position = {origin[0U] + direction[0U] * travelled,
                        origin[1U] + direction[1U] * travelled,
                        origin[2U] + direction[2U] * travelled};
        hit.distance = travelled;
        return hit;
    }
    return std::nullopt;
}

AcousticRaycast raycast_acoustic_path(const VxlMap& map,
                                      std::array<float, 3U> listener,
                                      std::array<float, 3U> source) noexcept {
    constexpr float aperture{0.32F};
    constexpr std::array<std::array<float, 3U>, AcousticRaycast::sample_count> offsets{{
        {0.0F, 0.0F, 0.0F},
        {aperture, 0.0F, 0.0F},
        {-aperture, 0.0F, 0.0F},
        {0.0F, aperture, -aperture},
        {0.0F, -aperture, -aperture},
    }};

    AcousticRaycast result;
    for (const auto& offset : offsets) {
        const std::array<float, 3U> ray_listener{
            listener[0U] + offset[0U], listener[1U] + offset[1U],
            listener[2U] + offset[2U]};
        const std::array<float, 3U> ray_source{
            source[0U] + offset[0U], source[1U] + offset[1U],
            source[2U] + offset[2U]};
        if (segment_blocked(map, ray_listener, ray_source)) {
            ++result.blocked_samples;
        }
    }
    if (result.blocked_samples == 0U) {
        result.transmission = 1.0F;
        return result;
    }
    const float open_fraction =
        static_cast<float>(AcousticRaycast::sample_count - result.blocked_samples) /
        static_cast<float>(AcousticRaycast::sample_count);
    // Terrain cover is a low-frequency leak, not a mute button. Retail did
    // not attenuate through VXL at all; this conservative floor retains that
    // presence while still making a fully closed wall audibly distinct.
    result.transmission = 0.35F + 0.65F * open_fraction;
    return result;
}

} // namespace battlespades::world
