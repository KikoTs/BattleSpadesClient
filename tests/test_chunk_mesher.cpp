#include "battlespades/world/chunk_mesh.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using battlespades::world::ChunkKey;
using battlespades::world::ChunkMesher;
using battlespades::world::ChunkMesherConfig;
using battlespades::world::ChunkTracker;
using battlespades::world::VxlColor;
using battlespades::world::VxlMap;

void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

/**
 * A valid 512x512 span stream with no authored voxels. Every column is one
 * terminating header with an empty surface run, so the loaded world contains
 * only the forced z=239 bed and mutation tests start from a known baseline.
 */
[[nodiscard]] VxlMap empty_world() {
    std::vector<std::byte> bytes;
    bytes.reserve(static_cast<std::size_t>(VxlMap::width) * VxlMap::depth * 4U);
    for (std::size_t column{}; column < static_cast<std::size_t>(VxlMap::width) * VxlMap::depth;
         ++column) {
        bytes.push_back(std::byte{0U});
        bytes.push_back(std::byte{1U});
        bytes.push_back(std::byte{0U});
        bytes.push_back(std::byte{0U});
    }
    auto loaded = VxlMap::load(bytes);
    expect(static_cast<bool>(loaded), "synthetic empty world must parse");
    expect(loaded.map->solid_voxels() ==
               static_cast<std::uint64_t>(VxlMap::width) * VxlMap::depth,
           "empty world must contain exactly the forced floor");
    return std::move(*loaded.map);
}

constexpr VxlColor stone{120U, 96U, 80U, 255U};

} // namespace

int main() {
    try {
        const ChunkMesher mesher;
        expect(mesher.chunks_per_axis() == 32U, "512-wide map must split into 32 chunks");

        auto map = empty_world();
        const ChunkKey interior{6U, 6U};
        const auto baseline = mesher.mesh(map, interior);
        // An interior floor-only chunk exposes exactly one upward face per
        // column: bottom faces are against below-world fill and side faces
        // are culled through the shared map on both sides of the border.
        expect(baseline.face_count() == 256U, "interior floor chunk must expose 256 top faces");
        expect(baseline.vertices.size() == 1'024U && baseline.indices.size() == 1'536U,
               "floor chunk vertex/index counts must match its faces");
        {
            ChunkMesherConfig water_config;
            water_config.bed_water_color = {7U, 11U, 19U, 255U};
            const auto recolored = ChunkMesher{water_config}.mesh(map, interior);
            const auto expected = (19U << 16U) | (11U << 8U) | 7U;
            expect(!recolored.vertices.empty() && recolored.vertices.front().abgr == expected,
                   "UGC water RGB must recolor transparent forced-bed voxels");
        }

        // Determinism: identical input produces identical buffers.
        {
            const auto again = mesher.mesh(map, interior);
            expect(again.vertices.size() == baseline.vertices.size() &&
                       again.indices == baseline.indices,
                   "meshing must be deterministic");
            bool identical{true};
            for (std::size_t i{}; i < again.vertices.size(); ++i) {
                const auto& a = again.vertices[i];
                const auto& b = baseline.vertices[i];
                identical = identical && a.x == b.x && a.y == b.y && a.z == b.z &&
                            a.abgr == b.abgr && a.face == b.face &&
                            a.occlusion == b.occlusion &&
                            a.noise_corner == b.noise_corner &&
                            a.directional_influence == b.directional_influence &&
                            a.static_light == b.static_light && a.ao_u == b.ao_u &&
                            a.ao_v == b.ao_v && a.edge_u == b.edge_u &&
                            a.edge_v == b.edge_v;
            }
            expect(identical, "meshing must reproduce identical vertices");
        }

        // One isolated voxel exposes all six faces with no corner occlusion.
        expect(map.set_voxel(100U, 100U, 100U, stone), "set_voxel must accept in-range voxel");
        {
            const auto meshed = mesher.mesh(map, interior);
            expect(meshed.face_count() == baseline.face_count() + 6U,
                   "a single isolated voxel must add six faces");
            for (const auto& vertex : meshed.vertices) {
                if (vertex.z <= 101.0F) {
                    expect(vertex.occlusion == 0U, "an isolated voxel has no occluded corner");
                }
            }
            const auto recovered = std::ranges::find_if(
                meshed.vertices, [](const battlespades::world::ChunkVertex& vertex) {
                    return vertex.face == 0U && vertex.x == 100.0F &&
                           vertex.y == 100.0F && vertex.z == 100.0F;
                });
            expect(recovered != meshed.vertices.end(),
                   "isolated -x face must expose its first retail corner");
            expect(recovered->ao_u == 0.375F && recovered->ao_v == 0.625F &&
                       recovered->edge_u == 0.25F &&
                       recovered->edge_v == 0.255859375F &&
                       recovered->noise_corner == 2U &&
                       recovered->directional_influence == 255U &&
                       (recovered->static_light >> 24U) == 255U,
                   "isolated terrain vertex must carry exact retail AO/edge/noise/light fields");
            expect(meshed.minimum[2U] == 100.0F, "mesh bounds must include the floating voxel");
        }

        // Two adjacent voxels share one interior face pair: ten exposed faces.
        expect(map.set_voxel(101U, 100U, 100U, stone), "second voxel must be accepted");
        expect(mesher.mesh(map, interior).face_count() == baseline.face_count() + 10U,
               "two adjacent voxels must add ten faces");
        expect(map.clear_voxel(101U, 100U, 100U), "clear_voxel must remove the second voxel");
        expect(map.clear_voxel(100U, 100U, 100U), "clear_voxel must remove the first voxel");
        expect(mesher.mesh(map, interior).face_count() == baseline.face_count(),
               "clearing mutations must restore the baseline mesh");
        expect(map.revision() == 4U, "each successful mutation must advance the revision");

        // Emissive presentation is deliberately independent from canonical VXL
        // colour. Chicago's authored red heart glass must mesh as hot pink, keep
        // its recovered emission byte, and remain equally bright on every face
        // even when legacy baked shading is requested.
        {
            auto emissive_map = empty_world();
            constexpr VxlColor chicago_heart{0xE4U, 0x33U, 0x34U, 0xFFU};
            expect(emissive_map.set_voxel(100U, 100U, 100U, chicago_heart),
                   "heart fixture must be accepted");
            ChunkMesherConfig emissive_config;
            emissive_config.bake_shading = true;
            emissive_config.emissive =
                battlespades::world::emissive_palette_for("CityOfChicago");
            const ChunkMesher emissive_mesher{emissive_config};
            const auto heart_mesh = emissive_mesher.mesh(emissive_map, interior);
            constexpr std::uint32_t expected =
                (235U << 24U) | (0xD2U << 16U) | (0x48U << 8U) | 0xFFU;
            std::size_t heart_vertices{};
            for (const auto& vertex : heart_mesh.vertices) {
                if (vertex.x < 100.0F || vertex.x > 101.0F ||
                    vertex.y < 100.0F || vertex.y > 101.0F ||
                    vertex.z < 100.0F || vertex.z > 101.0F) {
                    continue;
                }
                ++heart_vertices;
                expect(vertex.abgr == expected,
                       "every heart face must stay unshaded hot pink");
            }
            expect(heart_vertices == 24U,
                   "an isolated heart fixture must emit six four-vertex faces");
        }

        // A sealed voxel surrounded on all six sides contributes no geometry:
        // a solid 3x3x3 cube exposes only its 54 outer faces.
        for (std::uint32_t z{99U}; z <= 101U; ++z) {
            for (std::uint32_t y{99U}; y <= 101U; ++y) {
                for (std::uint32_t x{99U}; x <= 101U; ++x) {
                    expect(map.set_voxel(x, y, z, stone), "cube voxel must be accepted");
                }
            }
        }
        expect(mesher.mesh(map, interior).face_count() == baseline.face_count() + 54U,
               "a sealed 3x3x3 cube must expose exactly its 54 outer faces");
        for (std::uint32_t z{99U}; z <= 101U; ++z) {
            for (std::uint32_t y{99U}; y <= 101U; ++y) {
                for (std::uint32_t x{99U}; x <= 101U; ++x) {
                    expect(map.clear_voxel(x, y, z), "cube voxel must clear");
                }
            }
        }

        // Chunk-border neighbors: the shared face at x=112 is culled from
        // both chunk 6 and chunk 7 even though the voxels live apart.
        const ChunkKey east{7U, 6U};
        const auto east_baseline = mesher.mesh(map, east);
        expect(map.set_voxel(111U, 100U, 100U, stone), "border voxel must be accepted");
        expect(map.set_voxel(112U, 100U, 100U, stone), "east border voxel must be accepted");
        expect(mesher.mesh(map, interior).face_count() == baseline.face_count() + 5U,
               "border voxel must cull its +x face against the neighboring chunk");
        expect(mesher.mesh(map, east).face_count() == east_baseline.face_count() + 5U,
               "east border voxel must cull its -x face against the neighboring chunk");

        // Corner occlusion: a voxel resting on the bed darkens the side-face
        // corners that touch the floor plane relative to its open corners.
        // Shading now normally happens in fs_world from the occlusion level
        // stored per vertex, so this asserts the baking path explicitly.
        expect(map.set_voxel(50U, 50U, 238U, stone), "grounded voxel must be accepted");
        {
            const ChunkMesher baking_mesher{ChunkMesherConfig{.bake_shading = true}};
            const auto grounded = baking_mesher.mesh(map, ChunkKey{3U, 3U});
            bool saw_occluded{};
            std::uint32_t open_color{};
            std::uint32_t occluded_color{};
            for (const auto& vertex : grounded.vertices) {
                if (vertex.face != 0U || vertex.x != 50.0F || vertex.y < 50.0F ||
                    vertex.y > 51.0F) {
                    continue;
                }
                if (vertex.occlusion == 0U) {
                    open_color = vertex.abgr;
                } else {
                    saw_occluded = true;
                    occluded_color = vertex.abgr;
                }
            }
            expect(saw_occluded, "floor-adjacent side corners must report occlusion");
            expect((occluded_color & 0xFFU) < (open_color & 0xFFU),
                   "occluded corners must bake darker vertex colors");
        }

        // Dirty tracking: border voxels invalidate every adjacent chunk that
        // can see them; draining is bounded and deterministic.
        ChunkTracker tracker;
        expect(tracker.dirty_count() == 0U, "tracker must start clean");
        tracker.mark_voxel(111U, 100U);
        expect(tracker.dirty_count() == 2U, "border voxel must dirty both adjacent chunks");
        // (112,112) dirties chunks (7,7), (6,7), (7,6), (6,6); the last two
        // are already dirty from the border voxel above.
        tracker.mark_voxel(112U, 112U);
        expect(tracker.dirty_count() == 4U,
               "corner voxel must dirty its chunk and its three border neighbors");
        const auto first = tracker.take(1U);
        expect(first.size() == 1U && first.front() == ChunkKey{6U, 6U},
               "draining must follow deterministic row-major order");
        const auto rest = tracker.take(16U);
        expect(rest.size() == 3U && tracker.dirty_count() == 0U,
               "draining must clear the remaining dirty chunks");
        tracker.mark_all();
        expect(tracker.dirty_count() == 1'024U, "mark_all must dirty the whole grid");

        std::cout << "chunk mesher: baseline_faces=" << baseline.face_count() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
