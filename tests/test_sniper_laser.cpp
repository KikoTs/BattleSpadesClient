#include "battlespades/world/sniper_laser.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

void expect(bool value, const char* message) {
    if (!value) throw std::runtime_error{message};
}

void expect_near(float actual, float expected, const char* message) {
    if (std::fabs(actual - expected) > 1.0e-4F) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] battlespades::world::VxlMap empty_world() {
    using battlespades::world::VxlMap;
    std::vector<std::byte> bytes;
    bytes.reserve(static_cast<std::size_t>(VxlMap::width) * VxlMap::depth * 4U);
    for (std::size_t column{};
         column < static_cast<std::size_t>(VxlMap::width) * VxlMap::depth;
         ++column) {
        bytes.insert(bytes.end(),
                     {std::byte{0U}, std::byte{1U}, std::byte{0U}, std::byte{0U}});
    }
    auto loaded = VxlMap::load(bytes);
    expect(static_cast<bool>(loaded), "synthetic laser world must parse");
    return std::move(*loaded.map);
}

void activation_and_threat_fade_are_retail_exact() {
    using namespace battlespades::world;
    auto map = empty_world();
    SniperLaserInput input;
    input.server_enabled = true;
    input.owner_id = 4U;
    input.tool_id = 18U;
    input.team = 2U;
    input.zoomed = true;
    input.position = {10.0F, 10.0F, 10.0F};
    input.orientation = {1.0F, 0.0F, 0.0F};
    input.observer_position = {20.0F, 10.0F, 10.0F};
    const auto aimed = evaluate_sniper_laser(map, input);
    expect(aimed.visible, "zoomed server-enabled sniper must create a beam");
    expect(aimed.color == SniperLaserColor::blue,
           "wire team 2 must use the authored blue beam");
    expect_near(aimed.alpha, 1.0F,
                "a beam through the observer must use full retail warning alpha");

    input.observer_position = {20.0F, 22.0F, 10.0F};
    const auto edge = evaluate_sniper_laser(map, input);
    expect(!edge.visible && edge.alpha == 0.0F,
           "beam must disappear at the recovered twelve-block warning edge");
    input.zoomed = false;
    expect(!evaluate_sniper_laser(map, input).visible,
           "sniper beam must stop on the replicated zoom release");
    input.zoomed = true;
    input.tool_id = 17U;
    expect(!evaluate_sniper_laser(map, input).visible,
           "non-sniper tools must never inherit the effect");
}

void terrain_and_players_terminate_the_cosmetic_ray() {
    using namespace battlespades::world;
    auto map = empty_world();
    expect(map.set_voxel(20U, 10U, 10U, {255U, 255U, 255U, 255U}),
           "test terrain voxel must be accepted");
    SniperLaserInput input;
    input.server_enabled = true;
    input.owner_id = 4U;
    input.tool_id = 19U;
    input.team = 3U;
    input.zoomed = true;
    input.position = {10.5F, 10.5F, 10.5F};
    input.orientation = {1.0F, 0.0F, 0.0F};
    input.observer_position = {15.0F, 10.5F, 10.5F};
    const auto terrain = evaluate_sniper_laser(map, input);
    expect_near(terrain.distance, 9.5F,
                "beam must stop at the first live voxel face");
    expect(!terrain.player_hit, "terrain termination must not create a player spot");

    const std::vector<SniperLaserTarget> targets{{7U, {15.0F, 10.5F, 10.5F}, false, false}};
    const auto player = evaluate_sniper_laser(map, input, targets);
    expect(player.player_hit && player.distance < terrain.distance,
           "a living player before terrain must terminate the beam");
    expect(player.color == SniperLaserColor::green,
           "wire team 3 must use the authored green beam");
}

} // namespace

int main() {
    try {
        activation_and_threat_fade_are_retail_exact();
        terrain_and_players_terminate_the_cosmetic_ray();
        std::cout << "retail sniper laser tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
