#include "battlespades/world/static_light_field.hpp"

#include "battlespades/world/chunk_mesh.hpp"

#include <algorithm>
#include <cmath>

namespace battlespades::world {
namespace {

[[nodiscard]] constexpr float channel(std::uint8_t value) noexcept {
    return static_cast<float>(value) / 255.0F;
}

} // namespace

bool StaticLightField::add(StaticLight light) {
    if (lights_.size() >= maximum_lights || light.radius <= 0.0F) {
        return false;
    }
    const auto existing = std::ranges::find_if(lights_, [&](const StaticLight& entry) {
        return entry.cell == light.cell;
    });
    if (existing != lights_.end()) {
        // Replacing rather than stacking: two lights in one voxel is not a
        // thing a player can see, and stacking would let repeated placement on
        // the same cell blow out the accumulation.
        *existing = light;
        rebuild_buckets();
        return true;
    }
    lights_.push_back(light);
    rebuild_buckets();
    return true;
}

bool StaticLightField::remove_at(std::uint32_t x, std::uint32_t y,
                                 std::uint32_t z) noexcept {
    const std::array<std::uint32_t, 3U> cell{x, y, z};
    const auto found = std::ranges::find(lights_, cell, &StaticLight::cell);
    if (found == lights_.end()) {
        return false;
    }
    lights_.erase(found);
    rebuild_buckets();
    return true;
}

void StaticLightField::clear() noexcept {
    lights_.clear();
    buckets_.clear();
    max_radius_ = 0.0F;
}

void StaticLightField::rebuild_buckets() {
    constexpr auto columns = VxlMap::width / bucket_edge;
    constexpr auto rows = VxlMap::depth / bucket_edge;
    buckets_.assign(static_cast<std::size_t>(columns) * rows, {});
    max_radius_ = 0.0F;
    for (std::size_t index{}; index < lights_.size(); ++index) {
        const auto& light = lights_[index];
        const auto bucket_x = std::min(light.cell[0U] / bucket_edge, columns - 1U);
        const auto bucket_y = std::min(light.cell[1U] / bucket_edge, rows - 1U);
        buckets_[static_cast<std::size_t>(bucket_y) * columns + bucket_x].push_back(
            static_cast<std::uint16_t>(index));
        max_radius_ = std::max(max_radius_, light.radius);
    }
}

std::array<float, 3U> StaticLightField::sample(float x, float y,
                                               float z) const noexcept {
    std::array<float, 3U> total{};
    if (lights_.empty() || buckets_.empty()) {
        return total;
    }
    constexpr auto columns = static_cast<std::int64_t>(VxlMap::width / bucket_edge);
    constexpr auto rows = static_cast<std::int64_t>(VxlMap::depth / bucket_edge);
    const auto bucket_of = [](float value, std::int64_t limit) {
        const auto bucket = static_cast<std::int64_t>(std::floor(value)) /
                            static_cast<std::int64_t>(bucket_edge);
        return std::clamp<std::int64_t>(bucket, 0, limit - 1);
    };
    // A light affects a point only inside its radius, so the candidate
    // buckets are the ones overlapping the point's radius box.
    const auto low_x = bucket_of(x - max_radius_ - 1.0F, columns);
    const auto high_x = bucket_of(x + max_radius_ + 1.0F, columns);
    const auto low_y = bucket_of(y - max_radius_ - 1.0F, rows);
    const auto high_y = bucket_of(y + max_radius_ + 1.0F, rows);
    for (auto bucket_y = low_y; bucket_y <= high_y; ++bucket_y) {
        for (auto bucket_x = low_x; bucket_x <= high_x; ++bucket_x) {
            const auto& bucket =
                buckets_[static_cast<std::size_t>(bucket_y * columns + bucket_x)];
            for (const auto index : bucket) {
                const auto& light = lights_[index];
                // Measure from the voxel centre: a light is the block, not its corner.
                const float dx = x - (static_cast<float>(light.cell[0U]) + 0.5F);
                const float dy = y - (static_cast<float>(light.cell[1U]) + 0.5F);
                const float dz = z - (static_cast<float>(light.cell[2U]) + 0.5F);
                const float distance_squared = dx * dx + dy * dy + dz * dz;
                const float radius_squared = light.radius * light.radius;
                if (distance_squared >= radius_squared) {
                    continue;
                }
                // Quadratic falloff to a hard zero at the radius. Squaring the linear
                // term keeps the light concentrated near its source instead of washing
                // a flat disc across the floor, and reaching exactly zero at the edge
                // is what makes the dirty-chunk box an exact bound.
                const float linear = 1.0F - std::sqrt(distance_squared / radius_squared);
                const float attenuation = linear * linear;
                total[0U] += channel(light.color.red) * attenuation;
                total[1U] += channel(light.color.green) * attenuation;
                total[2U] += channel(light.color.blue) * attenuation;
            }
        }
    }
    for (float& value : total) {
        value = std::clamp(value, 0.0F, 1.0F);
    }
    return total;
}

std::optional<StaticLightField::StrongestLight> StaticLightField::strongest(
    float x, float y, float z) const noexcept {
    if (lights_.empty() || buckets_.empty()) {
        return std::nullopt;
    }
    constexpr auto columns = static_cast<std::int64_t>(VxlMap::width / bucket_edge);
    constexpr auto rows = static_cast<std::int64_t>(VxlMap::depth / bucket_edge);
    const auto bucket_of = [](float value, std::int64_t limit) {
        const auto bucket = static_cast<std::int64_t>(std::floor(value)) /
                            static_cast<std::int64_t>(bucket_edge);
        return std::clamp<std::int64_t>(bucket, 0, limit - 1);
    };
    const auto low_x = bucket_of(x - max_radius_ - 1.0F, columns);
    const auto high_x = bucket_of(x + max_radius_ + 1.0F, columns);
    const auto low_y = bucket_of(y - max_radius_ - 1.0F, rows);
    const auto high_y = bucket_of(y + max_radius_ + 1.0F, rows);
    std::optional<StrongestLight> best;
    // Retail iterates the chunk's light map; visiting in insertion order keeps
    // the `best <= att` tie rule (the later light wins).
    std::size_t best_index{};
    for (auto bucket_y = low_y; bucket_y <= high_y; ++bucket_y) {
        for (auto bucket_x = low_x; bucket_x <= high_x; ++bucket_x) {
            const auto& bucket =
                buckets_[static_cast<std::size_t>(bucket_y * columns + bucket_x)];
            for (const auto index : bucket) {
                const auto& light = lights_[index];
                const std::array<float, 3U> centre{
                    static_cast<float>(light.cell[0U]) + 0.5F,
                    static_cast<float>(light.cell[1U]) + 0.5F,
                    static_cast<float>(light.cell[2U]) + 0.5F};
                const float dx = centre[0U] - x;
                const float dy = centre[1U] - y;
                const float dz = centre[2U] - z;
                const float radius_squared = light.radius * light.radius;
                const float distance_squared = dx * dx + dy * dy + dz * dz;
                if (distance_squared >= radius_squared) {
                    continue;
                }
                const float attenuation =
                    std::clamp(1.0F - distance_squared / radius_squared, 0.0F, 1.0F);
                if (!best.has_value() || best->attenuation < attenuation ||
                    (best->attenuation == attenuation && best_index < index)) {
                    best = StrongestLight{{channel(light.color.red), channel(light.color.green),
                                           channel(light.color.blue)},
                                          attenuation,
                                          centre};
                    best_index = index;
                }
            }
        }
    }
    return best;
}

bool StaticLightField::has_light_at(std::uint32_t x, std::uint32_t y,
                                    std::uint32_t z) const noexcept {
    if (lights_.empty() || buckets_.empty() || x >= VxlMap::width || y >= VxlMap::depth) {
        return false;
    }
    constexpr auto columns = VxlMap::width / bucket_edge;
    const auto& bucket =
        buckets_[static_cast<std::size_t>(y / bucket_edge) * columns + x / bucket_edge];
    return std::ranges::any_of(bucket, [&](std::uint16_t index) {
        const auto& cell = lights_[index].cell;
        return cell[0U] == x && cell[1U] == y && cell[2U] == z;
    });
}

std::vector<ChunkKey> StaticLightField::affected_chunks(
    const StaticLight& light, std::uint32_t chunk_edge) const {
    std::vector<ChunkKey> keys;
    if (chunk_edge == 0U) {
        return keys;
    }
    const auto reach = static_cast<std::int64_t>(std::ceil(light.radius));
    const auto chunks_per_axis = VxlMap::width / chunk_edge;
    // Inflate by one chunk: a light just outside a chunk still reaches vertices
    // inside it, so the box has to cover the neighbour too.
    const auto low_x = std::max<std::int64_t>(
        0, (static_cast<std::int64_t>(light.cell[0U]) - reach) / chunk_edge - 1);
    const auto low_y = std::max<std::int64_t>(
        0, (static_cast<std::int64_t>(light.cell[1U]) - reach) / chunk_edge - 1);
    const auto high_x = std::min<std::int64_t>(
        chunks_per_axis - 1,
        (static_cast<std::int64_t>(light.cell[0U]) + reach) / chunk_edge + 1);
    const auto high_y = std::min<std::int64_t>(
        chunks_per_axis - 1,
        (static_cast<std::int64_t>(light.cell[1U]) + reach) / chunk_edge + 1);
    for (auto chunk_y = low_y; chunk_y <= high_y; ++chunk_y) {
        for (auto chunk_x = low_x; chunk_x <= high_x; ++chunk_x) {
            keys.push_back(ChunkKey{static_cast<std::uint32_t>(chunk_x),
                                    static_cast<std::uint32_t>(chunk_y)});
        }
    }
    return keys;
}

} // namespace battlespades::world
