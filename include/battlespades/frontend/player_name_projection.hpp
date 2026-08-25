#pragma once

#include "battlespades/world/player_movement.hpp"

#include <cstdint>
#include <optional>

namespace battlespades::world {
class VxlMap;
}

namespace battlespades::frontend {

/** Screen-space result of retail's billboarded player-name text. */
struct PlayerNameProjection final {
    double x_pixels{};
    double y_pixels{};
    double font_pixels{};
    double distance{};
};

/**
 * Project a player-head point using the same camera basis as the world pass.
 *
 * Retail draws the text at `PLAYER_NAME_SCALE * distance^0.7`, which makes
 * distant labels shrink gently rather than remaining fixed-size UI.  The
 * result is top-left-origin window pixels for the renderer-neutral HUD pass.
 */
[[nodiscard]] std::optional<PlayerNameProjection> project_retail_player_name(
    world::Vec3 eye,
    world::Vec3 player_position,
    double yaw_degrees,
    double pitch_degrees,
    double fov_y_degrees,
    std::uint32_t window_width,
    std::uint32_t window_height) noexcept;

/**
 * Fail-closed voxel visibility test used before drawing a player name.
 *
 * The final fraction of the ray is ignored so the remote character's own
 * occupied cell cannot hide its label.  A wall anywhere before it does.
 */
[[nodiscard]] bool retail_player_name_has_line_of_sight(const world::VxlMap& map,
                                                        world::Vec3 eye,
                                                        world::Vec3 player_position) noexcept;

/**
 * Resolve the nearest point where the camera ray enters a remote player body.
 *
 * Normal gameplay does not draw billboard names over every peer.  Retail
 * reveals one fixed-size name beside the crosshair only while the aim ray is
 * actually over a living player's collision prism.  Terrain is tested before
 * the body so a name never leaks through a wall.
 */
[[nodiscard]] std::optional<double> retail_player_name_crosshair_hit(
    const world::VxlMap& map,
    world::Vec3 eye,
    world::Vec3 forward,
    world::Vec3 player_position,
    bool crouching) noexcept;

inline constexpr double retail_player_name_scale{0.0075};
inline constexpr double retail_player_name_range{80.0};
inline constexpr double retail_crosshair_name_range{100.0};

} // namespace battlespades::frontend
