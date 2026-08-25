#include "battlespades/frontend/objective_indicator.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void expect(bool value, const char* message) {
    if (!value)
        throw std::runtime_error{message};
}

void retail_zone_table_covers_every_server_objective() {
    using battlespades::frontend::objective_zone_billboard_style;
    expect(!objective_zone_billboard_style(0U).has_value(),
           "ZONE_ICON_NONE must remain an empty retail row");
    expect(objective_zone_billboard_style(1U)->icon_asset ==
               "png/ui/base_icon.png" &&
               objective_zone_billboard_style(6U)->icon_asset ==
                   "png/ui/base_icon.png",
           "Demolition and CTF must share the retail base billboard");
    expect(objective_zone_billboard_style(2U)->icon_asset ==
               "png/ui/MultiHill.png" &&
               objective_zone_billboard_style(3U)->icon_asset ==
                   "png/ui/OccupationTarget.png" &&
               objective_zone_billboard_style(4U)->icon_asset ==
                   "png/ui/diamond_dropoff.png" &&
               objective_zone_billboard_style(5U)->icon_asset ==
                   "png/ui/vip_icon_256x256.png",
           "objective modes must retain their distinct billboard art");
    expect(objective_zone_billboard_style(7U)->icon_asset ==
               "png/ui/tc_billboard_a.png" &&
               objective_zone_billboard_style(14U)->icon_asset ==
                   "png/ui/tc_billboard_g.png" &&
               objective_zone_billboard_style(16U)->icon_asset ==
                   "png/ui/tc_billboard_g.png" &&
               objective_zone_billboard_style(17U)->icon_asset ==
                   "png/ui/spawn_icon.png",
           "territory H-I-J must alias G and spawn must keep its own art");
    expect(!objective_zone_billboard_style(18U).has_value(),
           "unknown server icon ordinals must fail closed");
}

void packet_icons_and_visibility_are_bounded() {
    using battlespades::frontend::objective_packet_billboard_asset;
    using battlespades::frontend::objective_visible_to_team;
    expect(objective_packet_billboard_asset("vip") ==
               "png/ui/vip_icon.png" &&
               objective_packet_billboard_asset("base_icon") ==
                   "png/ui/base_icon.png",
           "known packet-41 retail names must resolve");
    expect(!objective_packet_billboard_asset("../settings").has_value() &&
               !objective_packet_billboard_asset("base_icon.png").has_value(),
           "packet-41 strings must never become arbitrary texture paths");
    expect(objective_visible_to_team(0U, 2U) &&
               objective_visible_to_team(1U, 3U) &&
               objective_visible_to_team(3U, 0U) &&
               objective_visible_to_team(2U, 2U) &&
               !objective_visible_to_team(3U, 2U),
           "neutral, spectator and exact-team objective visibility must match retail");
}

void projection_draws_world_icons_and_directional_pointers() {
    using battlespades::frontend::project_objective_indicator;
    const auto ahead = project_objective_indicator(
        {10.0, 10.0, 20.0}, {0.0, 10.0, 20.0}, 0.0, 0.0,
        75.0, 1600U, 900U);
    expect(ahead.has_value() && !ahead->pointer &&
               std::abs(ahead->x_pixels - 800.0) < 1.0e-9 &&
               std::abs(ahead->y_pixels - 450.0) < 1.0e-9,
           "an objective in the camera cone must draw its world icon");

    const auto right = project_objective_indicator(
        {10.0, 10.0, 20.0}, {10.0, 0.0, 20.0}, 0.0, 0.0,
        75.0, 1600U, 900U);
    expect(right.has_value() && right->pointer &&
               right->x_pixels > 800.0 && right->x_pixels <= 1600.0 - 42.0,
           "an off-cone objective must clamp to the matching screen edge");

    const auto behind = project_objective_indicator(
        {10.0, 10.0, 20.0}, {20.0, 10.0, 20.0}, 0.0, 0.0,
        75.0, 1600U, 900U);
    expect(behind.has_value() && behind->pointer,
           "an objective behind the player must remain discoverable");
    expect(!project_objective_indicator(
                {10.0, 10.0, 20.0}, {0.0, 10.0, 20.0}, 0.0, 0.0,
                0.0, 1600U, 900U)
                .has_value(),
           "invalid camera state must fail closed");
}

void base_zone_bounds_and_tint_match_retail() {
    using battlespades::frontend::ObjectiveZoneVolume;
    using battlespades::frontend::objective_zone_player_tint;
    using battlespades::frontend::player_inside_objective_zone;
    const ObjectiveZoneVolume blue{{10.0, 20.0, 8.0}, {20.0, 30.0, 14.0},
                                   {44U, 117U, 179U}, 2U, false};
    expect(player_inside_objective_zone(blue, {15.0, 25.0, 10.0}, false) &&
               player_inside_objective_zone(blue, {9.75, 25.0, 10.0}, true),
           "zone containment must include the recovered horizontal tolerance");
    expect(!player_inside_objective_zone(blue, {9.6, 25.0, 10.0}, false) &&
               !player_inside_objective_zone(blue, {15.0, 25.0, 13.0}, false),
           "zone containment must reject distant and feet-below-volume players");
    const auto tint = objective_zone_player_tint(
        blue, {15.0, 25.0, 10.0}, false, 2U);
    expect(tint == battlespades::ui::ColorRgba8{44U, 117U, 179U, 150U},
           "inside a visible base must use its packet color and alpha 150");
    expect(!objective_zone_player_tint(
                blue, {15.0, 25.0, 10.0}, false, 3U)
                .has_value(),
           "enemy-private zones must not tint the local view");
    auto white = blue;
    white.color = {255U, 255U, 255U};
    expect(!objective_zone_player_tint(
                white, {15.0, 25.0, 10.0}, false, 2U)
                .has_value(),
           "retail's pure-white zone sentinel must suppress screen tint");
}

} // namespace

int main() {
    try {
        retail_zone_table_covers_every_server_objective();
        packet_icons_and_visibility_are_bounded();
        projection_draws_world_icons_and_directional_pointers();
        base_zone_bounds_and_tint_match_retail();
        std::cout << "objective indicator tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
