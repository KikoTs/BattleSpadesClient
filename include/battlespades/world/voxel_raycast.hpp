#pragma once

#include "battlespades/world/voxel_collapse.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace battlespades::world {

/** First solid cell crossed by a continuous world-space ray. */
struct VoxelRayHit final {
    VoxelCell cell{};
    /** Outward normal of the face through which the ray entered the cell. */
    std::array<std::int32_t, 3U> normal{};
    std::array<float, 3U> position{};
    float distance{};
};

/**
 * Traverse canonical AoS voxels with Amanatides-Woo stepping.
 *
 * The function reads terrain only. It never damages a block and is therefore
 * safe for observer-side shot presentation, audio occlusion, and other
 * packet-driven cosmetic simulations.
 */
[[nodiscard]] std::optional<VoxelRayHit>
trace_first_solid(const VxlMap& map,
                  std::array<float, 3U> origin,
                  std::array<float, 3U> direction,
                  float maximum_distance) noexcept;

/** Result of the bounded five-ray terrain audibility query. */
struct AcousticRaycast final {
    static constexpr std::uint8_t sample_count{5U};
    float transmission{1.0F};
    std::uint8_t blocked_samples{};
};

/**
 * Estimate how much direct sound reaches the listener through live VXL.
 *
 * Five nearby rays keep a doorway or a thin railing from acting like an
 * infinitely thick wall. The last part of each ray is excluded because many
 * packet sounds originate on the face or centre of the block that produced
 * them. A fully enclosed sound remains audible, but strongly attenuated.
 */
[[nodiscard]] AcousticRaycast
raycast_acoustic_path(const VxlMap& map,
                      std::array<float, 3U> listener,
                      std::array<float, 3U> source) noexcept;

} // namespace battlespades::world
