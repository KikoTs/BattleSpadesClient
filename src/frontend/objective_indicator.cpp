#include "battlespades/frontend/objective_indicator.hpp"

#include "battlespades/render/camera_basis.hpp"
#include "battlespades/shared/retail_constants.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace battlespades::frontend {
namespace {

[[nodiscard]] double dot(world::Vec3 value,
                         const std::array<double, 3U>& basis) noexcept {
    return value.x * basis[0U] + value.y * basis[1U] + value.z * basis[2U];
}

[[nodiscard]] bool finite(world::Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

} // namespace

std::optional<ObjectiveBillboardStyle>
objective_zone_billboard_style(std::uint8_t icon_id) noexcept {
    // shared.constants_gamemode.MODE_ZONE_ICONS, second column. H/I/J really
    // do reuse G in retail; do not fabricate assets for those ordinals.
    constexpr std::array<std::string_view, 18U> assets{
        "",
        "png/ui/base_icon.png",
        "png/ui/MultiHill.png",
        "png/ui/OccupationTarget.png",
        "png/ui/diamond_dropoff.png",
        "png/ui/vip_icon_256x256.png",
        "png/ui/base_icon.png",
        "png/ui/tc_billboard_a.png",
        "png/ui/tc_billboard_b.png",
        "png/ui/tc_billboard_c.png",
        "png/ui/tc_billboard_d.png",
        "png/ui/tc_billboard_e.png",
        "png/ui/tc_billboard_f.png",
        "png/ui/tc_billboard_g.png",
        "png/ui/tc_billboard_g.png",
        "png/ui/tc_billboard_g.png",
        "png/ui/tc_billboard_g.png",
        "png/ui/spawn_icon.png",
    };
    if (icon_id >= assets.size() || assets[icon_id].empty())
        return std::nullopt;
    return ObjectiveBillboardStyle{assets[icon_id], 2.5};
}

std::optional<std::string_view>
objective_packet_billboard_asset(std::string_view icon_name) noexcept {
    struct Entry final {
        std::string_view name;
        std::string_view asset;
    };
    constexpr std::array entries{
        Entry{"base_icon", "png/ui/base_icon.png"},
        Entry{"MultiHill", "png/ui/MultiHill.png"},
        Entry{"OccupationTarget", "png/ui/OccupationTarget.png"},
        Entry{"diamond_dropoff", "png/ui/diamond_dropoff.png"},
        Entry{"vip", "png/ui/vip_icon.png"},
        Entry{"vip_icon", "png/ui/vip_icon.png"},
        Entry{"vip_icon_256x256", "png/ui/vip_icon_256x256.png"},
        Entry{"tc_billboard_a", "png/ui/tc_billboard_a.png"},
        Entry{"tc_billboard_b", "png/ui/tc_billboard_b.png"},
        Entry{"tc_billboard_c", "png/ui/tc_billboard_c.png"},
        Entry{"tc_billboard_d", "png/ui/tc_billboard_d.png"},
        Entry{"tc_billboard_e", "png/ui/tc_billboard_e.png"},
        Entry{"tc_billboard_f", "png/ui/tc_billboard_f.png"},
        Entry{"tc_billboard_g", "png/ui/tc_billboard_g.png"},
        Entry{"spawn_icon", "png/ui/spawn_icon.png"},
    };
    const auto found = std::ranges::find(entries, icon_name, &Entry::name);
    if (found == entries.end())
        return std::nullopt;
    return found->asset;
}

bool objective_visible_to_team(std::uint8_t visible_team,
                               std::uint8_t local_team) noexcept {
    // Wire keys 0/1 are shared. Team 0 is spectator and sees the complete
    // mode overview. Team-owned rows are private to that exact wire team.
    return local_team == 0U || visible_team <= 1U || visible_team == local_team;
}

bool player_inside_objective_zone(const ObjectiveZoneVolume& zone,
                                  world::Vec3 player_anchor,
                                  bool crouching) noexcept {
    if (!finite(zone.minimum) || !finite(zone.maximum) || !finite(player_anchor)) {
        return false;
    }
    const world::Vec3 minimum{
        std::min(zone.minimum.x, zone.maximum.x),
        std::min(zone.minimum.y, zone.maximum.y),
        std::min(zone.minimum.z, zone.maximum.z)};
    const world::Vec3 maximum{
        std::max(zone.minimum.x, zone.maximum.x),
        std::max(zone.minimum.y, zone.maximum.y),
        std::max(zone.minimum.z, zone.maximum.z)};
    constexpr double xy = retail::BASE_PLAYER_ZONE_DISTANCE_TOLERANCE_XY;
    constexpr double eye = static_cast<double>(
        retail::BASE_PLAYER_ZONE_DISTANCE_TOLERANCE_ZEYES);
    constexpr double feet = retail::BASE_PLAYER_ZONE_DISTANCE_TOLERANCE_ZFEET;
    const double contact = crouching
                               ? retail::PLAYER_CROUCHING_POS_ABOVE_GROUND
                               : retail::PLAYER_STANDING_POS_ABOVE_GROUND;
    return player_anchor.x >= minimum.x - xy &&
           player_anchor.x <= maximum.x + xy &&
           player_anchor.y >= minimum.y - xy &&
           player_anchor.y <= maximum.y + xy &&
           player_anchor.z + eye >= minimum.z &&
           player_anchor.z + contact + feet <= maximum.z;
}

std::optional<ui::ColorRgba8> objective_zone_player_tint(
    const ObjectiveZoneVolume& zone, world::Vec3 player_anchor,
    bool crouching, std::uint8_t local_team) noexcept {
    if (!objective_visible_to_team(zone.visible_team, local_team) ||
        !player_inside_objective_zone(zone, player_anchor, crouching) ||
        (zone.color[0U] == 255U && zone.color[1U] == 255U &&
         zone.color[2U] == 255U)) {
        return std::nullopt;
    }
    return ui::ColorRgba8{
        zone.color[0U], zone.color[1U], zone.color[2U],
        static_cast<std::uint8_t>(retail::BASE_ZONE_TINT_ALPHA)};
}

std::optional<ObjectiveIndicatorProjection>
project_objective_indicator(world::Vec3 eye,
                            world::Vec3 target,
                            double yaw_degrees,
                            double pitch_degrees,
                            double fov_y_degrees,
                            std::uint32_t window_width,
                            std::uint32_t window_height,
                            double world_scale) noexcept {
    if (!finite(eye) || !finite(target) || !std::isfinite(yaw_degrees) ||
        !std::isfinite(pitch_degrees) || !std::isfinite(fov_y_degrees) ||
        !std::isfinite(world_scale) || window_width == 0U ||
        window_height == 0U || fov_y_degrees <= 1.0 ||
        fov_y_degrees >= 179.0 || world_scale <= 0.0) {
        return std::nullopt;
    }

    const world::Vec3 delta{
        target.x - eye.x, target.y - eye.y, target.z - eye.z};
    const double distance =
        std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
    if (!std::isfinite(distance) || distance <= 0.05)
        return std::nullopt;

    const auto basis = render::world_camera_basis(yaw_degrees, pitch_degrees);
    const double right = dot(delta, basis.right);
    const double up = dot(delta, basis.up);
    const double depth = dot(delta, basis.forward);
    const double tangent =
        std::tan(fov_y_degrees * std::numbers::pi / 360.0);
    const double aspect =
        static_cast<double>(window_width) / static_cast<double>(window_height);
    const double width = static_cast<double>(window_width);
    const double height = static_cast<double>(window_height);
    const double centre_x = width * 0.5;
    const double centre_y = height * 0.5;
    const double focal_pixels = height / (2.0 * tangent);

    constexpr double near_depth{0.05};
    constexpr double edge_margin_pixels{42.0};
    if (depth > near_depth) {
        const double ndc_x = right / (depth * tangent * aspect);
        const double ndc_y = up / (depth * tangent);
        const double screen_x = (ndc_x * 0.5 + 0.5) * width;
        const double screen_y = (0.5 - ndc_y * 0.5) * height;
        if (std::isfinite(screen_x) && std::isfinite(screen_y) &&
            screen_x >= edge_margin_pixels &&
            screen_x <= width - edge_margin_pixels &&
            screen_y >= edge_margin_pixels &&
            screen_y <= height - edge_margin_pixels) {
            const double projected_size = world_scale * focal_pixels / depth;
            return ObjectiveIndicatorProjection{
                screen_x,
                screen_y,
                std::clamp(projected_size, 24.0, 128.0),
                0.0,
                distance,
                false};
        }
    }

    // Project the target direction onto the screen plane. When it is behind
    // the camera, reverse that plane vector so the pointer chooses the nearer
    // screen edge instead of mirroring around the centre.
    double direction_x = right;
    double direction_y = -up;
    if (depth < -near_depth) {
        direction_x = -direction_x;
        direction_y = -direction_y;
    }
    if (std::abs(direction_x) < 1.0e-9 &&
        std::abs(direction_y) < 1.0e-9) {
        direction_y = 1.0;
    }
    const double half_width = std::max(1.0, centre_x - edge_margin_pixels);
    const double half_height = std::max(1.0, centre_y - edge_margin_pixels);
    const double factor = std::min(
        std::abs(direction_x) > 1.0e-9
            ? half_width / std::abs(direction_x)
            : std::numeric_limits<double>::infinity(),
        std::abs(direction_y) > 1.0e-9
            ? half_height / std::abs(direction_y)
            : std::numeric_limits<double>::infinity());
    const double x = centre_x + direction_x * factor;
    const double y = centre_y + direction_y * factor;
    // pointer_icon is authored pointing downward. Sprite rotations use
    // pyglet-compatible clockwise degrees around the destination centre.
    const double rotation =
        std::atan2(direction_y, direction_x) * 180.0 / std::numbers::pi - 90.0;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(rotation))
        return std::nullopt;
    return ObjectiveIndicatorProjection{x, y, 42.0, rotation, distance, true};
}

} // namespace battlespades::frontend
