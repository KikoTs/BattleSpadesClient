#include "battlespades/world/replicated_shot.hpp"

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using namespace battlespades::world;

void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] VxlMap empty_world() {
    std::vector<std::byte> bytes;
    bytes.reserve(static_cast<std::size_t>(VxlMap::width) * VxlMap::depth * 4U);
    for (std::size_t column{};
         column < static_cast<std::size_t>(VxlMap::width) * VxlMap::depth;
         ++column) {
        bytes.insert(bytes.end(), {std::byte{0U}, std::byte{1U},
                                   std::byte{0U}, std::byte{0U}});
    }
    auto loaded = VxlMap::load(bytes);
    expect(static_cast<bool>(loaded), "synthetic VXL must parse");
    return std::move(*loaded.map);
}

constexpr VxlColor stone{120U, 96U, 80U, 255U};

void observer_hit_is_visual_and_non_mutating() {
    auto map = empty_world();
    expect(map.set_voxel(5U, 10U, 20U, stone), "shot fixture must be writable");
    const auto revision = map.revision();
    const auto impacts = replicated_hitscan_impacts(
        map, {10.5F, 10.5F, 20.5F}, {-1.0F, 0.0F, 0.0F}, 17U, 42U, true);
    expect(impacts.size() == 1U, "a pistol shot must replay one scenery contact");
    expect(impacts.front().cell.x == 5U && impacts.front().normal[0U] == 1,
           "observer replay returned the wrong cell or face");
    expect(impacts.front().kind == TerrainImpactKind::bullet &&
               !impacts.front().destroyed && impacts.front().source_tool == 17U,
           "observer replay must remain a cosmetic bullet contact");
    expect(map.solid(5U, 10U, 20U) && map.revision() == revision,
           "observer replay must never mutate authoritative terrain");
}

void shotgun_contacts_are_unique_and_bounded() {
    auto map = empty_world();
    for (std::uint32_t y{0U}; y < 32U; ++y) {
        for (std::uint32_t z{8U}; z < 32U; ++z) {
            expect(map.set_voxel(5U, y, z, stone), "shotgun wall must be writable");
        }
    }
    const auto impacts = replicated_hitscan_impacts(
        map, {10.5F, 16.5F, 20.5F}, {-1.0F, 0.0F, 0.0F}, 9U, 91U, false);
    expect(!impacts.empty() && impacts.size() <= 10U,
           "shotgun replay must emit at most its authored pellet count");
    for (std::size_t left{}; left < impacts.size(); ++left) {
        for (std::size_t right{left + 1U}; right < impacts.size(); ++right) {
            expect(!(impacts[left].cell.x == impacts[right].cell.x &&
                     impacts[left].cell.y == impacts[right].cell.y &&
                     impacts[left].cell.z == impacts[right].cell.z),
                   "multiple pellets must not duplicate one block burst");
        }
    }
}

void packet_seed_replays_cpython_axis_spread() {
    auto map = empty_world();
    for (std::uint32_t y{90U}; y <= 105U; ++y) {
        for (std::uint32_t z{90U}; z <= 105U; ++z) {
            expect(map.set_voxel(5U, y, z, stone), "spread wall must be writable");
        }
    }
    const auto impacts = replicated_hitscan_impacts(
        map, {100.5F, 100.5F, 100.5F}, {-1.0F, 0.0F, 0.0F}, 17U, 42U, false);
    expect(impacts.size() == 1U && impacts.front().cell.x == 5U &&
               impacts.front().cell.y == 97U && impacts.front().cell.z == 99U,
           "ShootFeedback seed 42 must reproduce CPython's three-axis spread");
}

void non_hitscan_tools_do_not_invent_contacts() {
    const auto map = empty_world();
    expect(replicated_hitscan_impacts(
               map, {10.5F, 10.5F, 20.5F}, {-1.0F, 0.0F, 0.0F}, 12U, 1U, false)
               .empty(),
           "rocket tools must remain on projectile replication");
    expect(replicated_hitscan_impacts(
               map, {10.5F, 10.5F, 20.5F}, {-1.0F, 0.0F, 0.0F}, 2U, 1U, false)
               .empty(),
           "melee tools must remain on authoritative terrain damage");
}

} // namespace

int main() {
    try {
        observer_hit_is_visual_and_non_mutating();
        shotgun_contacts_are_unique_and_bounded();
        packet_seed_replays_cpython_axis_spread();
        non_hitscan_tools_do_not_invent_contacts();
        std::cout << "replicated shot tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
