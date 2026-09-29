#pragma once

#include "battlespades/world/terrain_effects.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace battlespades::world {

/**
 * Recreate the terrain-contact half of a server-relayed ShootFeedback(8).
 *
 * The result is presentation only. It never mutates VXL or invents player
 * damage; authoritative Damage(37) and ShootResponse(9) retain those roles.
 *
 * `hip_accuracy` is the shooter's current Weapon.accuracy (bloomed by
 * accuracy_spread for variable-accuracy guns). Absent, the class base value
 * is used. A zoomed shot still uses accuracy_zoom, exactly like retail.
 */
[[nodiscard]] std::vector<TerrainImpactEvent>
replicated_hitscan_impacts(const VxlMap& map,
                           std::array<float, 3U> origin,
                           std::array<float, 3U> orientation,
                           std::uint8_t tool_id,
                           std::uint8_t seed,
                           bool zoomed,
                           std::optional<double> hip_accuracy = std::nullopt);

/** One pellet of a replicated hitscan shot: its direction and first contact. */
struct ReplicatedPellet final {
    std::array<float, 3U> direction{};
    std::optional<std::array<float, 3U>> contact;
};

/**
 * Every pellet of a shot, in retail order (one Tracer per pellet in
 * Character.shoot), traced to its first solid contact within weapon range.
 */
[[nodiscard]] std::vector<ReplicatedPellet>
replicated_hitscan_pellets(const VxlMap& map,
                           std::array<float, 3U> origin,
                           std::array<float, 3U> orientation,
                           std::uint8_t tool_id,
                           std::uint8_t seed,
                           bool zoomed,
                           std::optional<double> hip_accuracy = std::nullopt);

/**
 * Observer-side replica of one remote shooter's Weapon.accuracy_spread.
 *
 * Retail runs prep_shoot()/shot_weapon() on every Character, so a remote
 * automatic's impact cloud blooms and recovers like the shooter's own.
 */
struct ObservedShotBloom final {
    std::uint8_t tool_id{0xFFU};
    double spread{};
    double time_seconds{};
};

/**
 * Advance `state` to `now_seconds`, return the Weapon.accuracy this shot
 * uses (bloom before the increase, as prep_shoot), then apply shot_weapon's
 * per-shot increase. Non-variable weapons return their class accuracy.
 */
[[nodiscard]] double observe_hitscan_bloom(ObservedShotBloom& state,
                                           std::uint8_t tool_id,
                                           double now_seconds) noexcept;

} // namespace battlespades::world
