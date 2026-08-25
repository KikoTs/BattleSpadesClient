#pragma once

#include <algorithm>
#include <cstdint>

namespace battlespades::world {

/** Retail's three global KV6 detail levels. */
enum class ModelQualityTier : std::uint8_t {
    low = 0U,
    medium = 1U,
    high = 2U,
};

/**
 * Return the integer KV6 inverse scale used by retail models.py.
 *
 * Normal models use ``3 - max(model_detail, min_model_detail)``. Prefabs are
 * deliberately exempt and remain at authored resolution; sights request a
 * minimum detail of high, which likewise forces an inverse scale of one.
 */
[[nodiscard]] constexpr std::uint8_t
kv6_inverse_scale(ModelQualityTier model_detail,
                  ModelQualityTier minimum_detail = ModelQualityTier::low,
                  bool prefab = false) noexcept {
    if (prefab) {
        return 1U;
    }
    const auto effective = std::max(static_cast<std::uint8_t>(model_detail),
                                    static_cast<std::uint8_t>(minimum_detail));
    return static_cast<std::uint8_t>(3U - effective);
}

} // namespace battlespades::world
