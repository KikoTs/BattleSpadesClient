#include "battlespades/world/map_spawn.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace battlespades::world {
namespace {

/** VxlMap reports this for a column with no solid voxel at all. */
constexpr std::uint16_t no_surface{std::numeric_limits<std::uint16_t>::max()};
/** Matches the body half-width the movement code clips against. */
constexpr double body_radius{0.45};
/** Keep clear of the map edges so the out-of-bounds-is-solid rule never wins. */
constexpr std::uint32_t edge_margin{2U};

/**
 * Whether a player standing on this column would be clear of geometry.
 *
 * `allow_water` relaxes the dry-land preference: the forced solid bed at the
 * bottom of the world means an open-water column reports a surface, and spawning
 * chest-deep in the sea is a poor default but better than failing outright.
 */
[[nodiscard]] bool column_is_standable(const VxlMap& map, std::uint32_t x,
                                       std::uint32_t y, bool allow_water,
                                       double& anchor_z) noexcept {
    const auto surface = map.surface_z(x, y);
    // z=239 is the forced indestructible bed and IS standable -- it is the floor
    // of open water. Only genuinely out-of-range surfaces are rejected here;
    // whether the body actually fits is the clearance loop's job below.
    if (surface == no_surface || surface < 2U || surface >= VxlMap::height) {
        return false;
    }
    if (!allow_water && surface >= 238U) {
        return false;
    }
    anchor_z = static_cast<double>(surface) - player_contact_offset(false, false);

    const double centre_x = static_cast<double>(x) + 0.5;
    const double centre_y = static_cast<double>(y) + 0.5;
    // The same vertical span the block-placement overlap test uses, so a spawn
    // can never occupy a cell the game would refuse to let a block occupy.
    const auto low_z = static_cast<std::int64_t>(std::floor(anchor_z - 0.6));
    const auto high_z = static_cast<std::int64_t>(std::floor(anchor_z + 2.25));
    for (const double offset_x : {-body_radius, body_radius}) {
        for (const double offset_y : {-body_radius, body_radius}) {
            for (auto probe_z = low_z; probe_z < high_z; ++probe_z) {
                if (clip_at(&map, centre_x + offset_x, centre_y + offset_y,
                            static_cast<double>(probe_z))) {
                    return false;
                }
            }
        }
    }

    // Reuse the real ground predicate rather than approximating it; it samples
    // exactly the four corners and rejects the one-voxel ledge the clearance
    // loop above lets through.
    PlayerMovementState state;
    state.position = {centre_x, centre_y, anchor_z};
    return grounded(&map, state);
}

} // namespace

MapSpawn resolve_map_spawn(const VxlMap& map, std::uint32_t start_x,
                           std::uint32_t start_y) noexcept {
    constexpr std::uint32_t maximum_ring{64U};
    const auto clamp_axis = [](std::int64_t value, std::uint32_t limit) {
        return static_cast<std::uint32_t>(std::clamp<std::int64_t>(
            value, edge_margin, static_cast<std::int64_t>(limit - edge_margin)));
    };

    // Dry land first, then relax into water rather than failing.
    for (const bool allow_water : {false, true}) {
        for (std::uint32_t ring{}; ring <= maximum_ring; ++ring) {
            const auto low = -static_cast<std::int64_t>(ring);
            const auto high = static_cast<std::int64_t>(ring);
            for (auto offset_y = low; offset_y <= high; ++offset_y) {
                for (auto offset_x = low; offset_x <= high; ++offset_x) {
                    // Only the ring's perimeter is new; the interior was covered
                    // by a smaller ring already.
                    if (ring > 0U && std::abs(offset_x) != high &&
                        std::abs(offset_y) != high) {
                        continue;
                    }
                    const auto probe_x =
                        clamp_axis(static_cast<std::int64_t>(start_x) + offset_x,
                                   VxlMap::width);
                    const auto probe_y =
                        clamp_axis(static_cast<std::int64_t>(start_y) + offset_y,
                                   VxlMap::depth);
                    double anchor_z{};
                    if (column_is_standable(map, probe_x, probe_y, allow_water,
                                            anchor_z)) {
                        MapSpawn spawn;
                        spawn.position = {static_cast<double>(probe_x) + 0.5,
                                          static_cast<double>(probe_y) + 0.5, anchor_z};
                        spawn.derived = true;
                        return spawn;
                    }
                }
            }
        }
    }

    // Nothing safe within reach. High in the air over the start column: the
    // player falls, which is recoverable and obviously wrong, instead of being
    // buried, which looks like a rendering failure.
    MapSpawn spawn;
    spawn.position = {static_cast<double>(start_x) + 0.5,
                      static_cast<double>(start_y) + 0.5, 0.0};
    spawn.derived = false;
    return spawn;
}

} // namespace battlespades::world
