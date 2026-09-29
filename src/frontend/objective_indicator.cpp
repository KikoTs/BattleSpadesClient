#include "battlespades/frontend/objective_indicator.hpp"

#include "battlespades/render/camera_basis.hpp"
#include "battlespades/shared/retail_constants.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

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
    return ObjectiveBillboardStyle{assets[icon_id], 2.5, 1.0};
}

std::optional<std::string>
objective_packet_billboard_asset(std::string_view icon_name) {
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
    if (found != entries.end())
        return std::string{found->asset};
    // Retail has no whitelist: the handler loads png/ui/<icon_name>.png and
    // an absent file drops the billboard. Accept only a bare identifier so a
    // server string can never escape the UI texture directory.
    const bool bare_identifier =
        !icon_name.empty() && icon_name.size() <= 64U &&
        std::ranges::all_of(icon_name, [](char character) {
            return (character >= 'a' && character <= 'z') ||
                   (character >= 'A' && character <= 'Z') ||
                   (character >= '0' && character <= '9') || character == '_' ||
                   character == '-';
        });
    if (!bare_identifier)
        return std::nullopt;
    return "png/ui/" + std::string{icon_name} + ".png";
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
                            double scale,
                            double initial_scale) noexcept {
    if (!finite(eye) || !finite(target) || !std::isfinite(yaw_degrees) ||
        !std::isfinite(pitch_degrees) || !std::isfinite(fov_y_degrees) ||
        !std::isfinite(scale) || !std::isfinite(initial_scale) ||
        window_width == 0U ||
        window_height == 0U || fov_y_degrees <= 1.0 ||
        fov_y_degrees >= 179.0 || scale <= 0.0 || initial_scale <= 0.0) {
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
    // MinimapBillboard.render (hud.pyd 0x1001B040):
    //   factor = initial_scale (<= 20 blocks), 0.6 (>= 100 blocks), else
    //            0.6 + (initial_scale - 0.6) * (d - 100) / -80
    //   s      = max(0.02 * scale * factor, min_scale = 0.02)
    //   set_variables(x, y, z - s / initial_scale, s, ...)
    // __init__ stores initial_scale = scale (the constructor argument): 2.5
    // for a MinimapZone billboard, the 1.8 default for Minimap.add_billboard.
    // MinimapZone.update resets a FULLSIZE zone's billboard.scale to 1.0, so
    // a steady zone draws with scale 1.0 and initial_scale 2.5 (retail
    // runtime probe, TC 2026-09-29).
    // aoslib.draw's Billboard quad spans +-s along the camera axes
    // (draw.pyd 0x10001730), placed 0.25 block from the eye along the
    // (cone-clamped) direction. So the 256 px canvas is 2 s / depth focal
    // lengths wide and rises s / initial_scale above the direction.
    const double distance_scale =
        distance <= 20.0 ? initial_scale
                         : distance >= 100.0
                               ? 0.6
                               : 0.6 + (initial_scale - 0.6) * (distance - 100.0) / -80.0;
    constexpr double min_scale{0.02};
    const double billboard_half = std::max(0.02 * scale * distance_scale, min_scale);
    constexpr double billboard_distance{0.25};
    constexpr double cone_radians{std::numbers::pi / 6.0};
    const auto canvas_at = [&](double cosine) {
        const double billboard_depth = billboard_distance * std::max(cosine, 0.05);
        return std::pair{2.0 * billboard_half * focal_pixels / billboard_depth,
                         billboard_half / initial_scale * focal_pixels / billboard_depth};
    };
    if (depth > near_depth && depth / distance >= std::cos(cone_radians)) {
        const auto [canvas_pixels, lift_pixels] = canvas_at(depth / distance);
        const double ndc_x = right / (depth * tangent * aspect);
        const double ndc_y = up / (depth * tangent);
        const double screen_x = (ndc_x * 0.5 + 0.5) * width;
        const double screen_y = (0.5 - ndc_y * 0.5) * height;
        if (std::isfinite(screen_x) && std::isfinite(screen_y)) {
            return ObjectiveIndicatorProjection{
                screen_x,
                screen_y - lift_pixels,
                canvas_pixels,
                0.0,
                distance,
                false};
        }
    }

    // MinimapBillboard.render (hud.pyd 0x1001B040): an objective outside the
    // view is not pushed to the screen edge. Its direction is clamped onto a
    // pi/6 (30 degree) cone around the view axis (clamp_point_to_cone) and
    // the billboard plus its pointer are drawn there, 0.25 block in front of
    // the eye. The old screen-edge placement hid the pointer under the
    // minimap and the ammo panels (live A/B 2026-09-29, CTF bases).
    double direction_x = right;
    double direction_y = -up;
    if (std::abs(direction_x) < 1.0e-9 &&
        std::abs(direction_y) < 1.0e-9) {
        direction_y = 1.0;
    }
    const double lateral = std::hypot(direction_x, direction_y);
    // Cone edge on the projection plane: tan(30 deg) of the focal length.
    const double cone_pixels = focal_pixels * std::tan(cone_radians);
    const double half_width = std::max(1.0, centre_x - edge_margin_pixels);
    const double half_height = std::max(1.0, centre_y - edge_margin_pixels);
    const double radius = std::min({cone_pixels, half_width, half_height});
    const auto [canvas_pixels, lift_pixels] = canvas_at(std::cos(cone_radians));
    const double x = centre_x + direction_x / lateral * radius;
    const double y = centre_y + direction_y / lateral * radius - lift_pixels;
    // pointer_icon is authored pointing downward. Sprite rotations use
    // pyglet-compatible clockwise degrees around the destination centre.
    const double rotation =
        std::atan2(direction_y, direction_x) * 180.0 / std::numbers::pi - 90.0;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(rotation))
        return std::nullopt;
    return ObjectiveIndicatorProjection{x, y, canvas_pixels, rotation, distance, true};
}

} // namespace battlespades::frontend
