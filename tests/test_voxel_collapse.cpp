#include "battlespades/world/chunk_mesh.hpp"
#include "battlespades/world/voxel_collapse.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using battlespades::world::ChunkKey;
using battlespades::world::ChunkMesher;
using battlespades::world::VxlColor;
using battlespades::world::VxlMap;
using battlespades::world::VoxelCell;
using battlespades::world::collapse_unsupported_components;
using battlespades::world::find_unsupported_components;

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
        bytes.push_back(std::byte{0U});
        bytes.push_back(std::byte{1U});
        bytes.push_back(std::byte{0U});
        bytes.push_back(std::byte{0U});
    }
    auto loaded = VxlMap::load(bytes);
    expect(static_cast<bool>(loaded), "synthetic empty VXL must parse");
    return std::move(*loaded.map);
}

constexpr VxlColor stone{120U, 96U, 80U, 255U};

void z239_bed_supports_its_z238_neighbor() {
    auto map = empty_world();
    expect(map.set_voxel(10U, 10U, 238U, stone), "bed neighbor must place");
    auto components = find_unsupported_components(map, {{10U, 10U, 237U}});
    expect(components.empty(),
           "the mandatory z239 bed must ground a face-adjacent z238 voxel");

    expect(!map.clear_voxel(10U, 10U, 239U),
           "tests must not manufacture a retail-impossible hole in the bed");
}

void edge_contact_supports_but_three_axis_corner_does_not() {
    auto map = empty_world();
    expect(map.set_voxel(10U, 10U, 237U, stone), "edge path head must place");
    expect(map.set_voxel(11U, 10U, 238U, stone), "edge path middle must place");
    // The forced floor already supplies (11,10,239).
    expect(find_unsupported_components(map, {{10U, 10U, 236U}}).empty(),
           "edge-adjacent path to bed must support a component");

    auto corner_map = empty_world();
    expect(corner_map.set_voxel(20U, 20U, 237U, stone), "corner head must place");
    expect(corner_map.set_voxel(21U, 21U, 238U, stone), "corner middle must place");
    const auto components =
        find_unsupported_components(corner_map, {{20U, 20U, 236U}});
    expect(components.size() == 1U && components.front().size() == 1U &&
               components.front().front() == VoxelCell{20U, 20U, 237U},
           "three-axis corner contact must not count as structural support");
}

void collapse_captures_color_and_mutates_collision_atomically() {
    auto map = empty_world();
    expect(map.set_voxel(100U, 100U, 100U, stone), "first floating voxel must place");
    expect(map.set_voxel(101U, 100U, 100U, stone), "second floating voxel must place");
    const auto falling =
        collapse_unsupported_components(map, {{99U, 100U, 100U}});
    expect(falling.size() == 1U && falling.front().size() == 2U,
           "entire unsupported component must be captured");
    expect(falling.front().front().color.red == stone.red,
           "falling presentation must retain original voxel colors");
    expect(!map.solid(100U, 100U, 100U) && !map.solid(101U, 100U, 100U),
           "collapsed voxels must leave authoritative collision immediately");
}

void work_budget_fails_safe() {
    auto map = empty_world();
    for (std::uint32_t x{100U}; x < 110U; ++x) {
        expect(map.set_voxel(x, 100U, 100U, stone), "budget fixture must place");
    }
    expect(find_unsupported_components(map, {{99U, 100U, 100U}}, 5U).empty(),
           "budget exhaustion must never return a partial falling component");
}

void damage_state_darkens_mesh_and_clears_on_removal() {
    auto map = empty_world();
    expect(map.set_voxel(100U, 100U, 100U, stone), "damage fixture must place");
    const ChunkMesher mesher;
    const auto clean = mesher.mesh(map, ChunkKey{6U, 6U});
    expect(map.set_damage_fraction(100U, 100U, 100U, 0.4F),
           "sublethal damage must change presentation state");
    expect(map.damage_fraction(100U, 100U, 100U) == 0.4F,
           "damage fraction must round-trip");
    const auto damaged = mesher.mesh(map, ChunkKey{6U, 6U});
    expect(clean.vertices.size() == damaged.vertices.size(),
           "damage must not remove terrain geometry");

    bool saw_darker{};
    for (std::size_t index{}; index < clean.vertices.size(); ++index) {
        const auto& before = clean.vertices[index];
        const auto& after = damaged.vertices[index];
        if (before.z <= 101.0F && after.abgr != before.abgr) {
            saw_darker = (after.abgr & 0x00FFFFFFU) < (before.abgr & 0x00FFFFFFU);
            if (saw_darker) {
                break;
            }
        }
    }
    expect(saw_darker, "pre-break block vertices must become visibly darker");
    expect(map.clear_voxel(100U, 100U, 100U), "damaged voxel must clear");
    expect(map.damage_fraction(100U, 100U, 100U) == 0.0F,
           "removing a voxel must discard stale damage state");
}

} // namespace

int main() {
    try {
        z239_bed_supports_its_z238_neighbor();
        edge_contact_supports_but_three_axis_corner_does_not();
        collapse_captures_color_and_mutates_collision_atomically();
        work_budget_fails_safe();
        damage_state_darkens_mesh_and_clears_on_removal();
        std::cout << "voxel collapse parity tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
