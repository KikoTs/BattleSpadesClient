#include "battlespades/frontend/player_name_projection.hpp"

#include "battlespades/world/vxl_map.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

void expect(bool value, const char* message) {
    if (!value)
        throw std::runtime_error{message};
}

battlespades::world::VxlMap empty_world() {
    std::vector<std::byte> empty_columns;
    empty_columns.reserve(static_cast<std::size_t>(battlespades::world::VxlMap::width) *
                          battlespades::world::VxlMap::depth * 4U);
    for (std::size_t column{};
         column < static_cast<std::size_t>(battlespades::world::VxlMap::width) *
                      battlespades::world::VxlMap::depth;
         ++column) {
        empty_columns.insert(empty_columns.end(),
                             {std::byte{0U}, std::byte{1U}, std::byte{0U}, std::byte{0U}});
    }
    auto loaded = battlespades::world::VxlMap::load(empty_columns);
    expect(static_cast<bool>(loaded), "player-name fixture VXL must parse");
    return std::move(*loaded.map);
}

void projection_tracks_retail_camera_and_scale() {
    const auto centered = battlespades::frontend::project_retail_player_name(
        {10.0, 10.0, 20.0}, {0.0, 10.0, 21.0}, 0.0, 0.0, 75.0, 1600U, 900U);
    expect(centered.has_value(), "a player directly ahead must have a label");
    expect(std::abs(centered->x_pixels - 800.0) < 1.0e-9,
           "forward label must be horizontally centered");
    expect(std::abs(centered->y_pixels - 450.0) < 1.0e-9,
           "head-height label must be vertically centered for this fixture");
    expect(centered->font_pixels >= 8.0 && centered->font_pixels <= 30.0,
           "retail distance scaling must remain readable and bounded");

    const auto behind = battlespades::frontend::project_retail_player_name(
        {10.0, 10.0, 20.0}, {20.0, 10.0, 21.0}, 0.0, 0.0, 75.0, 1600U, 900U);
    expect(!behind.has_value(), "players behind the camera must not receive labels");
}

void visibility_fails_closed_at_voxel_walls() {
    auto map = empty_world();
    expect(battlespades::frontend::retail_player_name_has_line_of_sight(
               map, {10.5, 10.5, 20.5}, {0.5, 10.5, 21.5}),
           "open air must expose a player name");
    expect(map.set_voxel(5U, 10U, 20U, {80U, 80U, 80U, 255U}),
           "visibility fixture wall voxel must be accepted");
    expect(!battlespades::frontend::retail_player_name_has_line_of_sight(
               map, {10.5, 10.5, 20.5}, {0.5, 10.5, 21.5}),
           "terrain between eye and player must hide the name");
}

void normal_play_requires_the_crosshair_to_touch_the_player() {
    auto map = empty_world();
    const battlespades::world::Vec3 eye{10.5, 10.5, 20.5};
    const battlespades::world::Vec3 forward{-1.0, 0.0, 0.0};
    const auto direct = battlespades::frontend::retail_player_name_crosshair_hit(
        map, eye, forward, {0.5, 10.5, 20.5}, false);
    expect(direct.has_value() && *direct > 9.0 && *direct < 10.0,
           "aiming through the standing body must reveal its name");
    expect(!battlespades::frontend::retail_player_name_crosshair_hit(
                map, eye, forward, {0.5, 12.0, 20.5}, false)
                .has_value(),
           "a nearby player outside the crosshair must remain anonymous");
    expect(!battlespades::frontend::retail_player_name_crosshair_hit(
                map, eye, forward, {20.5, 10.5, 20.5}, false)
                .has_value(),
           "a player behind the camera must remain anonymous");

    expect(map.set_voxel(5U, 10U, 20U, {80U, 80U, 80U, 255U}),
           "crosshair fixture wall voxel must be accepted");
    expect(!battlespades::frontend::retail_player_name_crosshair_hit(
                map, eye, forward, {0.5, 10.5, 20.5}, false)
                .has_value(),
           "an aimed player behind terrain must not leak a name");
}

void classic_labels_do_not_reveal_enemies_or_dead_player_targets() {
    using battlespades::frontend::protocol_player_name_visible;
    for (const auto protocol : {std::uint8_t{3U}, std::uint8_t{4U}}) {
        for (const auto team : {std::uint8_t{2U}, std::uint8_t{3U}}) {
            const auto other_team = static_cast<std::uint8_t>(team == 2U ? 3U : 2U);
            expect(protocol_player_name_visible(protocol, team, team, false),
                   "Classic retains aimed teammate identification");
            expect(!protocol_player_name_visible(protocol, team, other_team, false),
                   "Classic aiming at an enemy must not disclose their name");
            expect(!protocol_player_name_visible(protocol, team, other_team, true) &&
                       !protocol_player_name_visible(protocol, team, team, true),
                   "Classic death cameras must not add overhead labels");
            expect(!protocol_player_name_visible(protocol, 0U, team, true) &&
                       !protocol_player_name_visible(protocol, 0U, team, false),
                   "Classic spectators must not inherit retail name overlays");
        }
    }
    expect(protocol_player_name_visible(168U, 2U, 3U, false) &&
               protocol_player_name_visible(168U, 0U, 3U, true),
           "Classic+ and standard retain their existing label behavior");
}

} // namespace

int main() {
    try {
        projection_tracks_retail_camera_and_scale();
        visibility_fails_closed_at_voxel_walls();
        normal_play_requires_the_crosshair_to_touch_the_player();
        classic_labels_do_not_reveal_enemies_or_dead_player_targets();
        std::cout << "player-name projection tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
