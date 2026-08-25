#include "battlespades/world/replicated_shot.hpp"

#include "battlespades/world/voxel_raycast.hpp"
#include "battlespades/world/weapon_catalog.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <unordered_set>

namespace battlespades::world {
namespace {

/** CPython 2.7 random.Random for the one-byte Shoot packet seed. */
class RetailRandom final {
public:
    explicit RetailRandom(std::uint8_t seed) noexcept {
        seed_array(static_cast<std::uint32_t>(seed));
    }

    [[nodiscard]] double random() noexcept {
        const std::uint32_t first = next() >> 5U;
        const std::uint32_t second = next() >> 6U;
        return (static_cast<double>(first) * 67'108'864.0 +
                static_cast<double>(second)) /
               9'007'199'254'740'992.0;
    }

private:
    static constexpr std::size_t state_size{624U};
    static constexpr std::size_t middle{397U};
    std::array<std::uint32_t, state_size> state_{};
    std::size_t index_{state_size};

    void seed_single(std::uint32_t seed) noexcept {
        state_[0U] = seed;
        for (std::size_t index{1U}; index < state_size; ++index) {
            const std::uint32_t previous = state_[index - 1U];
            state_[index] = 1'812'433'253U * (previous ^ (previous >> 30U)) +
                            static_cast<std::uint32_t>(index);
        }
    }

    void seed_array(std::uint32_t seed) noexcept {
        seed_single(19'650'218U);
        std::size_t state_index{1U};
        std::size_t key_index{};
        for (std::size_t count{state_size}; count > 0U; --count) {
            const std::uint32_t previous = state_[state_index - 1U];
            state_[state_index] =
                (state_[state_index] ^
                 ((previous ^ (previous >> 30U)) * 1'664'525U)) +
                seed + static_cast<std::uint32_t>(key_index);
            ++state_index;
            if (state_index >= state_size) {
                state_[0U] = state_[state_size - 1U];
                state_index = 1U;
            }
            // A one-byte retail seed becomes one little-endian key word.
            key_index = 0U;
        }
        for (std::size_t count{state_size - 1U}; count > 0U; --count) {
            const std::uint32_t previous = state_[state_index - 1U];
            state_[state_index] =
                (state_[state_index] ^
                 ((previous ^ (previous >> 30U)) * 1'566'083'941U)) -
                static_cast<std::uint32_t>(state_index);
            ++state_index;
            if (state_index >= state_size) {
                state_[0U] = state_[state_size - 1U];
                state_index = 1U;
            }
        }
        state_[0U] = 0x80000000U;
        index_ = state_size;
    }

    void twist() noexcept {
        constexpr std::uint32_t upper_mask{0x80000000U};
        constexpr std::uint32_t lower_mask{0x7FFFFFFFU};
        constexpr std::uint32_t matrix{0x9908B0DFU};
        for (std::size_t position{}; position < state_size; ++position) {
            const std::uint32_t joined =
                (state_[position] & upper_mask) |
                (state_[(position + 1U) % state_size] & lower_mask);
            state_[position] = state_[(position + middle) % state_size] ^
                               (joined >> 1U) ^
                               ((joined & 1U) != 0U ? matrix : 0U);
        }
        index_ = 0U;
    }

    [[nodiscard]] std::uint32_t next() noexcept {
        if (index_ >= state_size) {
            twist();
        }
        std::uint32_t value = state_[index_++];
        value ^= value >> 11U;
        value ^= (value << 7U) & 0x9D2C5680U;
        value ^= (value << 15U) & 0xEFC60000U;
        value ^= value >> 18U;
        return value;
    }
};

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
