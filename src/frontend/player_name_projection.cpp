#include "battlespades/frontend/player_name_projection.hpp"

#include "battlespades/render/camera_basis.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace battlespades::frontend {
namespace {

[[nodiscard]] double dot(world::Vec3 value, const std::array<double, 3U>& basis) noexcept {
    return value.x * basis[0U] + value.y * basis[1U] + value.z * basis[2U];
}

[[nodiscard]] bool finite(world::Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool solid(const world::VxlMap& map, world::Vec3 point) noexcept {
    if (!finite(point) || point.x < 0.0 || point.y < 0.0 || point.z < 0.0 ||
        point.x >= static_cast<double>(world::VxlMap::width) ||
        point.y >= static_cast<double>(world::VxlMap::depth) ||
        point.z >= static_cast<double>(world::VxlMap::height)) {
        return true;
    }
    return map.solid(static_cast<std::uint32_t>(std::floor(point.x)),
                     static_cast<std::uint32_t>(std::floor(point.y)),
                     static_cast<std::uint32_t>(std::floor(point.z)));
}

[[nodiscard]] std::optional<double> ray_box_entry(world::Vec3 origin,
                                                  world::Vec3 direction,
                                                  world::Vec3 minimum,
                                                  world::Vec3 maximum,
                                                  double limit) noexcept {
    double first{0.0};
    double last{limit};
    const auto clip_axis = [&](double ray_origin, double ray_direction, double low, double high) {
        constexpr double parallel_epsilon{1.0e-12};
        if (std::abs(ray_direction) <= parallel_epsilon) {
            return ray_origin >= low && ray_origin <= high;
        }
        double entry = (low - ray_origin) / ray_direction;
        double exit = (high - ray_origin) / ray_direction;
        if (entry > exit)
            std::swap(entry, exit);
        first = std::max(first, entry);
        last = std::min(last, exit);
        return first <= last;
    };
    if (!clip_axis(origin.x, direction.x, minimum.x, maximum.x) ||
        !clip_axis(origin.y, direction.y, minimum.y, maximum.y) ||
        !clip_axis(origin.z, direction.z, minimum.z, maximum.z) || last < 0.0 ||
        first > limit) {
        return std::nullopt;
    }
    return std::max(first, 0.0);
}

} // namespace

std::optional<PlayerNameProjection> project_retail_player_name(
    world::Vec3 eye,
    world::Vec3 player_position,
    double yaw_degrees,
    double pitch_degrees,
    double fov_y_degrees,
    std::uint32_t window_width,
    std::uint32_t window_height) noexcept {
    if (!finite(eye) || !finite(player_position) || !std::isfinite(yaw_degrees) ||
        !std::isfinite(pitch_degrees) || !std::isfinite(fov_y_degrees) || window_width == 0U ||
        window_height == 0U || fov_y_degrees <= 1.0 || fov_y_degrees >= 179.0) {
        return std::nullopt;
    }

    // gameScene.pyx line 1533 places the billboard one map unit above the
    // replicated character origin (map Z increases downward).
    const world::Vec3 label{player_position.x, player_position.y, player_position.z - 1.0};
    const world::Vec3 delta{label.x - eye.x, label.y - eye.y, label.z - eye.z};
    const double distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
    if (!std::isfinite(distance) || distance <= 0.05 || distance >= retail_player_name_range) {
        return std::nullopt;
    }

    const auto basis = render::world_camera_basis(yaw_degrees, pitch_degrees);
    const double depth = dot(delta, basis.forward);
    if (depth <= 0.05)
        return std::nullopt;

    const double tangent = std::tan(fov_y_degrees * std::numbers::pi / 360.0);
    const double aspect = static_cast<double>(window_width) / static_cast<double>(window_height);
    const double ndc_x = dot(delta, basis.right) / (depth * tangent * aspect);
    const double ndc_y = dot(delta, basis.up) / (depth * tangent);
    if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y) || std::abs(ndc_x) > 1.0 ||
        std::abs(ndc_y) > 1.0) {
        return std::nullopt;
    }

    // text.py creates the over-player face at 14 px.  Text3DRenderer then
    // applies PLAYER_NAME_SCALE * d^0.7 before perspective projection.
    constexpr double retail_font_height{14.0};
    const double focal_pixels = static_cast<double>(window_height) / (2.0 * tangent);
    const double scaled_font = retail_font_height * retail_player_name_scale *
                               std::pow(distance, 0.7) * focal_pixels / depth;
    return PlayerNameProjection{
        (ndc_x * 0.5 + 0.5) * static_cast<double>(window_width),
        (0.5 - ndc_y * 0.5) * static_cast<double>(window_height),
        std::clamp(scaled_font, 8.0, 30.0),
        distance,
    };
}

bool retail_player_name_has_line_of_sight(const world::VxlMap& map,
                                          world::Vec3 eye,
                                          world::Vec3 player_position) noexcept {
    if (!finite(eye) || !finite(player_position))
        return false;
    const world::Vec3 target{player_position.x, player_position.y, player_position.z - 1.0};
    const world::Vec3 delta{target.x - eye.x, target.y - eye.y, target.z - eye.z};
    const double distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
    if (!std::isfinite(distance) || distance <= 0.05 || distance >= retail_player_name_range)
        return false;
    const world::Vec3 direction{delta.x / distance, delta.y / distance, delta.z / distance};
    constexpr double interval{0.125};
    // Start outside the camera cell and stop before the remote body cell.
    for (double travelled = 0.25; travelled < distance - 0.55; travelled += interval) {
        if (solid(map,
                  {eye.x + direction.x * travelled,
                   eye.y + direction.y * travelled,
                   eye.z + direction.z * travelled})) {
            return false;
        }
    }
    return true;
}

std::optional<double> retail_player_name_crosshair_hit(const world::VxlMap& map,
                                                       world::Vec3 eye,
                                                       world::Vec3 forward,
                                                       world::Vec3 player_position,
                                                       bool crouching) noexcept {
    if (!finite(eye) || !finite(forward) || !finite(player_position))
        return std::nullopt;
    const double length =
        std::sqrt(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
    if (!std::isfinite(length) || length <= 1.0e-12)
        return std::nullopt;
    const world::Vec3 direction{forward.x / length, forward.y / length, forward.z / length};

    // PlayerMovement's retail collision prism is radius .45 around the
    // replicated eye anchor, ending at the standing/crouching contact plane.
    constexpr double radius{0.45};
    const double contact = crouching ? 1.35 : 2.25;
    const world::Vec3 minimum{
        player_position.x - radius, player_position.y - radius, player_position.z - radius};
    const world::Vec3 maximum{
        player_position.x + radius, player_position.y + radius, player_position.z + contact};
    const auto hit =
        ray_box_entry(eye, direction, minimum, maximum, retail_crosshair_name_range);
    if (!hit.has_value() || *hit <= 0.05)
        return std::nullopt;

    // Stop just before the player prism. The player's occupied voxel must not
    // hide its own name, while any earlier terrain must fail closed.
    constexpr double interval{0.125};
    for (double travelled = 0.25; travelled < *hit - 0.05; travelled += interval) {
        if (solid(map,
                  {eye.x + direction.x * travelled,
                   eye.y + direction.y * travelled,
                   eye.z + direction.z * travelled})) {
            return std::nullopt;
        }
    }
    return hit;
}

} // namespace battlespades::frontend
