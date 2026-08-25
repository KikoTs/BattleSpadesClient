#include "battlespades/world/emissive_volume.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace battlespades::world {
namespace {

struct Source final {
    std::array<std::uint32_t, 3U> cell{};
    /** Actual fixture centroid, not the centre of its coarse 4-block bin. */
    std::array<float, 3U> origin{};
    std::array<float, 3U> color{};
};

[[nodiscard]] constexpr std::size_t cell_index(std::uint32_t x, std::uint32_t y,
                                               std::uint32_t z) noexcept {
    return ((static_cast<std::size_t>(z) * EmissiveVolume::depth) + y) *
               EmissiveVolume::width +
           x;
}

[[nodiscard]] constexpr float channel(std::uint8_t value) noexcept {
    return static_cast<float>(value) / 255.0F;
}

/**
 * Whether light can travel between two block positions.
 *
 * Stepped at voxel resolution deliberately: the cells are four blocks wide, so a
 * cell-resolution test would let light pour straight through a one-voxel facade.
 * The endpoints are skipped because the emitter itself and the receiving surface
 * are both solid by definition.
 */
[[nodiscard]] bool light_reaches(const VxlMap& map, std::array<float, 3U> from,
                                 std::array<float, 3U> to) noexcept {
    const std::array<float, 3U> delta{to[0U] - from[0U], to[1U] - from[1U],
                                      to[2U] - from[2U]};
    const float distance =
        std::sqrt(delta[0U] * delta[0U] + delta[1U] * delta[1U] + delta[2U] * delta[2U]);
    if (distance < 1.0F) {
        return true;
    }
    const std::array<std::int64_t, 3U> source_voxel{
        static_cast<std::int64_t>(std::floor(from[0U])),
        static_cast<std::int64_t>(std::floor(from[1U])),
        static_cast<std::int64_t>(std::floor(from[2U]))};
    const auto steps = static_cast<int>(distance);
    for (int step{1}; step < steps; ++step) {
        const float t = static_cast<float>(step) / static_cast<float>(steps);
        const auto probe_x = static_cast<std::int64_t>(from[0U] + delta[0U] * t);
        const auto probe_y = static_cast<std::int64_t>(from[1U] + delta[1U] * t);
        const auto probe_z = static_cast<std::int64_t>(from[2U] + delta[2U] * t);
        if (probe_x < 0 || probe_y < 0 || probe_z < 0 ||
            probe_x >= static_cast<std::int64_t>(VxlMap::width) ||
            probe_y >= static_cast<std::int64_t>(VxlMap::depth) ||
            probe_z >= static_cast<std::int64_t>(VxlMap::height)) {
            continue;
        }
        if (map.solid(static_cast<std::uint32_t>(probe_x),
                      static_cast<std::uint32_t>(probe_y),
                      static_cast<std::uint32_t>(probe_z)) &&
            std::array<std::int64_t, 3U>{probe_x, probe_y, probe_z} !=
                source_voxel) {
            return false;
        }
    }
    return true;
}

} // namespace

EmissiveVolume::EmissiveVolume() {
    cells_.assign(static_cast<std::size_t>(width) * depth * height * 4U, 0U);
}

void EmissiveVolume::clear() noexcept {
    std::ranges::fill(cells_, std::uint8_t{0U});
    sources_ = 0U;
}

void EmissiveVolume::build(const VxlMap& map, const EmissivePalette& palette,
                           const StaticLightField& placed) {
    clear();
    if (palette_is_empty(palette) && placed.empty()) {
        return;
    }

    // Bin emitters into their own cells first. Tokyo has roughly 8800 emissive
    // voxels but they cluster into a few hundred cells, and propagating from
    // cells rather than voxels is what makes this affordable at load.
    std::vector<std::array<float, 3U>> accumulated(
        static_cast<std::size_t>(width) * depth * height, std::array<float, 3U>{});
    std::vector<std::array<float, 3U>> accumulated_positions(
        accumulated.size(), std::array<float, 3U>{});
    std::vector<std::uint16_t> counts(accumulated.size(), 0U);

    if (!palette_is_empty(palette)) {
        for (std::uint32_t y{}; y < VxlMap::depth; ++y) {
            for (std::uint32_t x{}; x < VxlMap::width; ++x) {
                // Only surface voxels can emit into open air, and surface_z is
                // the cheap way to skip the solid interior of the world.
                const auto surface = map.surface_z(x, y);
                if (surface >= VxlMap::height) {
                    continue;
                }
                for (std::uint32_t z = surface; z < VxlMap::height; ++z) {
                    if (!map.solid(x, y, z)) {
                        continue;
                    }
                    const auto color = map.color(x, y, z);
                    if (!color.has_value()) {
                        continue;
                    }
                    const auto appearance =
                        emissive_appearance_at(palette, map, x, y, z, *color);
                    if (!appearance.has_value()) {
                        continue;
                    }
                    const auto index =
                        cell_index(x / cell_size, y / cell_size, z / cell_size);
                    const float scale = channel(appearance->intensity);
                    accumulated[index][0U] +=
                        channel(appearance->light.red) * scale;
                    accumulated[index][1U] +=
                        channel(appearance->light.green) * scale;
                    accumulated[index][2U] +=
                        channel(appearance->light.blue) * scale;
                    accumulated_positions[index][0U] +=
                        static_cast<float>(x) + 0.5F;
                    accumulated_positions[index][1U] +=
                        static_cast<float>(y) + 0.5F;
                    accumulated_positions[index][2U] +=
                        static_cast<float>(z) + 0.5F;
                    ++counts[index];
                }
            }
        }
    }

    for (const auto& light : placed.lights()) {
        const auto index = cell_index(light.cell[0U] / cell_size,
                                      light.cell[1U] / cell_size,
                                      light.cell[2U] / cell_size);
        accumulated[index][0U] += channel(light.color.red);
        accumulated[index][1U] += channel(light.color.green);
        accumulated[index][2U] += channel(light.color.blue);
        accumulated_positions[index][0U] +=
            static_cast<float>(light.cell[0U]) + 0.5F;
        accumulated_positions[index][1U] +=
            static_cast<float>(light.cell[1U]) + 0.5F;
        accumulated_positions[index][2U] +=
            static_cast<float>(light.cell[2U]) + 0.5F;
        ++counts[index];
    }

    std::vector<Source> sources;
    for (std::uint32_t z{}; z < height; ++z) {
        for (std::uint32_t y{}; y < depth; ++y) {
            for (std::uint32_t x{}; x < width; ++x) {
                const auto index = cell_index(x, y, z);
                if (counts[index] == 0U) {
                    continue;
                }
                // Average rather than sum: a dense sign face should be as bright
                // as a single tube, not proportionally to how many voxels it has.
                const float inverse = 1.0F / static_cast<float>(counts[index]);
                Source source;
                source.cell = {x, y, z};
                source.origin = {
                    accumulated_positions[index][0U] * inverse,
                    accumulated_positions[index][1U] * inverse,
                    accumulated_positions[index][2U] * inverse};
                source.color = {accumulated[index][0U] * inverse,
                                accumulated[index][1U] * inverse,
                                accumulated[index][2U] * inverse};
                sources.push_back(source);
            }
        }
    }
    sources_ = sources.size();
    if (sources.empty()) {
        return;
    }

    const auto cell_reach = static_cast<std::int64_t>(reach / cell_size) + 1;
    std::vector<std::array<float, 3U>> light(accumulated.size(), std::array<float, 3U>{});

    for (const auto& source : sources) {
        const auto origin = source.origin;
        for (std::int64_t offset_z = -cell_reach; offset_z <= cell_reach; ++offset_z) {
            for (std::int64_t offset_y = -cell_reach; offset_y <= cell_reach; ++offset_y) {
                for (std::int64_t offset_x = -cell_reach; offset_x <= cell_reach;
                     ++offset_x) {
                    const auto target_x = static_cast<std::int64_t>(source.cell[0U]) + offset_x;
                    const auto target_y = static_cast<std::int64_t>(source.cell[1U]) + offset_y;
                    const auto target_z = static_cast<std::int64_t>(source.cell[2U]) + offset_z;
                    if (target_x < 0 || target_y < 0 || target_z < 0 ||
                        target_x >= static_cast<std::int64_t>(width) ||
                        target_y >= static_cast<std::int64_t>(depth) ||
                        target_z >= static_cast<std::int64_t>(height)) {
                        continue;
                    }
                    const std::array<float, 3U> centre{
                        static_cast<float>(target_x * cell_size) + cell_size * 0.5F,
                        static_cast<float>(target_y * cell_size) + cell_size * 0.5F,
                        static_cast<float>(target_z * cell_size) + cell_size * 0.5F,
                    };
                    const float dx = centre[0U] - origin[0U];
                    const float dy = centre[1U] - origin[1U];
                    const float dz = centre[2U] - origin[2U];
                    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (distance >= reach) {
                        continue;
                    }
                    // Quadratic to a hard zero, matching the placed-light falloff
                    // so a flare and a neon sign behave the same way.
                    const float linear = 1.0F - (distance / reach);
                    const float attenuation = linear * linear;
                    if (attenuation <= 0.002F) {
                        continue;
                    }
                    // Aim the occlusion ray at the nearest block centre inside
                    // the coarse destination cell. Using the cell centre for
                    // every ray introduces an artificial diagonal whenever the
                    // fixture is not itself centred in its 4x4x4 bin; that
                    // diagonal can step around a one-voxel facade even though
                    // the straight fixture-to-cell path is blocked.
                    const std::array<float, 3U> target_min{
                        static_cast<float>(target_x * cell_size) + 0.5F,
                        static_cast<float>(target_y * cell_size) + 0.5F,
                        static_cast<float>(target_z * cell_size) + 0.5F};
                    const std::array<float, 3U> target_max{
                        target_min[0U] + static_cast<float>(cell_size - 1U),
                        target_min[1U] + static_cast<float>(cell_size - 1U),
                        target_min[2U] + static_cast<float>(cell_size - 1U)};
                    const std::array<float, 3U> occlusion_target{
                        std::clamp(origin[0U], target_min[0U], target_max[0U]),
                        std::clamp(origin[1U], target_min[1U], target_max[1U]),
                        std::clamp(origin[2U], target_min[2U], target_max[2U])};
                    if (!light_reaches(map, origin, occlusion_target)) {
                        continue;
                    }
                    auto& target = light[cell_index(static_cast<std::uint32_t>(target_x),
                                                    static_cast<std::uint32_t>(target_y),
                                                    static_cast<std::uint32_t>(target_z))];
                    for (std::size_t axis{}; axis < 3U; ++axis) {
                        // Max, not sum: a street lined with signs should read as
                        // lit, not blow out to white where their ranges overlap.
                        target[axis] =
                            std::max(target[axis], source.color[axis] * attenuation);
                    }
                }
            }
        }
    }

    for (std::size_t index{}; index < light.size(); ++index) {
        for (std::size_t axis{}; axis < 3U; ++axis) {
            cells_[index * 4U + axis] = static_cast<std::uint8_t>(
                std::clamp(std::lround(light[index][axis] * 255.0F), 0L, 255L));
        }
        cells_[index * 4U + 3U] = 255U;
    }
}

std::array<float, 3U> EmissiveVolume::sample(float x, float y, float z) const noexcept {
    const auto clamp_axis = [](float value, std::uint32_t limit) {
        return static_cast<std::uint32_t>(
            std::clamp(value, 0.0F, static_cast<float>(limit) - 1.0F));
    };
    const auto cell_x = clamp_axis(x / cell_size, width);
    const auto cell_y = clamp_axis(y / cell_size, depth);
    const auto cell_z = clamp_axis(z / cell_size, height);
    const auto index = cell_index(cell_x, cell_y, cell_z) * 4U;
    return {channel(cells_[index]), channel(cells_[index + 1U]),
            channel(cells_[index + 2U])};
}

} // namespace battlespades::world
