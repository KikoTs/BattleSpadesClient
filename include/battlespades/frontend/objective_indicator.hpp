#pragma once

#include "battlespades/ui/draw_list.hpp"
#include "battlespades/world/player_movement.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace battlespades::frontend {

/** Retail world-billboard art selected by a server packet-43 icon ordinal. */
struct ObjectiveBillboardStyle final {
    std::string_view icon_asset;
    /** MinimapZone constructs its companion billboard with scale=2.5. */
    double world_scale{2.5};
};

/**
 * Resolve the second entry of retail MODE_ZONE_ICONS.
 *
 * The packet ordinal, not the locally selected mode, owns the result. This is
 * important during live mode/map transitions and for servers that combine
 * objective types. Unknown/NONE rows fail closed and draw no world marker.
 */
[[nodiscard]] std::optional<ObjectiveBillboardStyle>
objective_zone_billboard_style(std::uint8_t icon_id) noexcept;

/**
 * Resolve a packet-41 icon name to a shipped client asset.
 *
 * Packet strings are untrusted network input. Only retail billboard names
 * present in the preserved asset set are accepted; paths and extensions are
 * deliberately rejected so a malformed server cannot trigger arbitrary
 * texture loads on the render thread.
 */
[[nodiscard]] std::optional<std::string_view>
objective_packet_billboard_asset(std::string_view icon_name) noexcept;

/** Shared neutral/team/spectator visibility gate used by packets 41 and 43. */
[[nodiscard]] bool objective_visible_to_team(std::uint8_t visible_team,
                                             std::uint8_t local_team) noexcept;

/** Screen-space presentation of one server-authored objective location. */
struct ObjectiveIndicatorProjection final {
    double x_pixels{};
    double y_pixels{};
    double size_pixels{};
    double rotation_degrees{};
    double distance{};
    /** False draws the objective art; true draws the directional pointer art. */
    bool pointer{};
};

/** Server-authored packet-43 volume after integer bounds are normalized. */
struct ObjectiveZoneVolume final {
    world::Vec3 minimum{};
    world::Vec3 maximum{};
    std::array<std::uint8_t, 3U> color{};
    std::uint8_t visible_team{};
    bool solid{};
};

/**
 * Test retail's eye-and-feet player volume against one objective zone.
 *
 * `player_anchor` is the Protocol 168 eye/physics anchor. Retail checks the
 * horizontal point with its 0.3-block tolerance and derives the feet plane
 * from the standing/crouching contact offset. Malformed volumes fail closed.
 */
[[nodiscard]] bool player_inside_objective_zone(
    const ObjectiveZoneVolume& zone, world::Vec3 player_anchor,
    bool crouching) noexcept;

/**
 * Resolve the full-screen tint used while the local player occupies a zone.
 *
 * Hidden zones and retail's pure-white sentinel do not tint. The returned
 * alpha is the recovered BASE_ZONE_TINT_ALPHA (150), ready for the HUD pass.
 */
[[nodiscard]] std::optional<ui::ColorRgba8> objective_zone_player_tint(
    const ObjectiveZoneVolume& zone, world::Vec3 player_anchor,
    bool crouching, std::uint8_t local_team) noexcept;

/**
 * Project retail's world objective billboard and clamp off-camera targets.
 *
 * This is presentation-only: the world position, team visibility, icon and
 * tracking owner all originate in Protocol 168 state. It intentionally does
 * not raycast terrain because retail objective billboards remain navigational
 * cues through geometry. Invalid camera/packet data fails closed.
 */
[[nodiscard]] std::optional<ObjectiveIndicatorProjection>
project_objective_indicator(world::Vec3 eye,
                            world::Vec3 target,
                            double yaw_degrees,
                            double pitch_degrees,
                            double fov_y_degrees,
                            std::uint32_t window_width,
                            std::uint32_t window_height,
                            double world_scale = 2.5) noexcept;

inline constexpr std::string_view objective_pointer_asset{
    "png/ui/pointer_icon.png"};
inline constexpr std::array<std::string_view, 15U>
    objective_billboard_assets{
        objective_pointer_asset,
        "png/ui/base_icon.png",
        "png/ui/MultiHill.png",
        "png/ui/OccupationTarget.png",
        "png/ui/diamond_dropoff.png",
        "png/ui/vip_icon.png",
        "png/ui/vip_icon_256x256.png",
        "png/ui/tc_billboard_a.png",
        "png/ui/tc_billboard_b.png",
        "png/ui/tc_billboard_c.png",
        "png/ui/tc_billboard_d.png",
        "png/ui/tc_billboard_e.png",
        "png/ui/tc_billboard_f.png",
        "png/ui/tc_billboard_g.png",
        "png/ui/spawn_icon.png"};

} // namespace battlespades::frontend
