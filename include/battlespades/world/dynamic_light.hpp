#pragma once

#include <array>

namespace battlespades::world {

/**
 * One short-lived, renderer-neutral point light in canonical map space.
 *
 * Positions use the same z-down coordinates as the VXL map. Colours are
 * linear-light RGB multipliers rather than bytes so an explosion can briefly
 * exceed display white before the world shader's tone mapper rolls it off.
 * The gameplay thread owns the instances; they never affect authority,
 * visibility, collision, damage, or replication.
 */
struct DynamicLight final {
    std::array<float, 3U> position{};
    std::array<float, 3U> color{1.0F, 1.0F, 1.0F};
    float radius{};
    float intensity{};
};

} // namespace battlespades::world
