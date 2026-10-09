#pragma once

#include <cstdint>
#include <string_view>

namespace battlespades::world {
class VxlMap;

/** 0.75's horizontal visibility boundary; never enlarge a lower user setting. */
[[nodiscard]] constexpr double protocol_fog_distance(std::uint8_t protocol, double requested) noexcept {
    return protocol == 3 && requested > 128.0 ? 128.0 : requested;
}

/** Bounded surface sample; called once per map, never during rendering.
 * Chooses among sky families matching terrain colors, with stable results for
 * the same map and seed. Color is a heuristic, not authored map metadata.
 */
[[nodiscard]] std::string_view choose_classic_skydome(const VxlMap& map,
                                                     std::uint64_t seed) noexcept;
} // namespace battlespades::world
