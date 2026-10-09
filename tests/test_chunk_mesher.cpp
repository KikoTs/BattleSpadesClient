#include "battlespades/world/chunk_mesh.hpp"
#include "battlespades/world/vxl_map.hpp"
#include "classic_water_fixture.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string_view>
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

// Optional real-map timing probe. Buffer fingerprints exclude struct padding
// and let optimization runs verify exact geometry, colours and AO coordinates.
int benchmark_map(const char* path) {
    const auto loaded = VxlMap::load_file(path);
    expect(static_cast<bool>(loaded), "Benchmark map must load");
    const ChunkMesher mesher;
    std::vector<double> timings;
    std::uint64_t fingerprint{14695981039346656037ULL};
    const auto hash = [&fingerprint](std::uint32_t value) {
        fingerprint = (fingerprint ^ value) * 1099511628211ULL;
    };
    std::size_t faces{};
    double total_ms{};
    for (std::uint32_t y{}; y < mesher.chunks_per_axis(); ++y) {
        for (std::uint32_t x{}; x < mesher.chunks_per_axis(); ++x) {
            const auto begin = std::chrono::steady_clock::now();
            const auto mesh = mesher.mesh(*loaded.map, {x, y});
            const auto elapsed = std::chrono::duration<double, std::milli>{
                std::chrono::steady_clock::now() - begin}.count();
            timings.push_back(elapsed);
            total_ms += elapsed;
            faces += mesh.face_count();
            hash(x); hash(y);
            for (const auto& vertex : mesh.vertices) {
                hash(std::bit_cast<std::uint32_t>(vertex.x));
                hash(std::bit_cast<std::uint32_t>(vertex.y));
                hash(std::bit_cast<std::uint32_t>(vertex.z));
                hash(vertex.abgr); hash(vertex.face); hash(vertex.occlusion);
                hash(vertex.noise_corner); hash(vertex.directional_influence);
                hash(vertex.static_light); hash(std::bit_cast<std::uint32_t>(vertex.retail_baked_light));
                hash(std::bit_cast<std::uint32_t>(vertex.ao_u));
                hash(std::bit_cast<std::uint32_t>(vertex.ao_v));
                hash(std::bit_cast<std::uint32_t>(vertex.edge_u));
                hash(std::bit_cast<std::uint32_t>(vertex.edge_v));
            }
            for (const auto index : mesh.indices) hash(index);
        }
    }
    std::ranges::sort(timings);
    std::cout << "terrain benchmark: chunks=" << timings.size() << " faces=" << faces
              << " fingerprint=" << fingerprint << " total_ms=" << total_ms
              << " p50_ms=" << timings[timings.size() / 2U]
              << " p95_ms=" << timings[(timings.size() * 95U) / 100U]
              << " max_ms=" << timings.back() << '\n';
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view{argv[1]} == "--benchmark")
            return benchmark_map(argv[2]);
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

        // Water colors must survive the entire Classic decode/mesh path, not
        // only minimap sampling. Alpha is baked light, including an authored
        // all-zero black tile; it does not mean 'use the uniform sea color'.
        {
            const auto bytes = battlespades::test::classic_water_bytes();
            auto loaded = VxlMap::load(bytes, battlespades::world::VxlDecodeProfile::classic64);
            expect(static_cast<bool>(loaded), "Classic water fixture must load");
            expect(loaded.map->source_profile() == battlespades::world::VxlDecodeProfile::classic64 &&
                   loaded.map->source_z_shift() == 176U,
                   "Classic import must retain its resolved profile");
            ChunkMesherConfig water_config;
            water_config.bed_water_color = {7U, 11U, 19U, 255U};
            const auto mesh = ChunkMesher{water_config}.mesh(*loaded.map, interior);
            expect(mesh.face_count() == 256U, "Classic water must expose one top face per column");
            for (std::size_t face{}; face < mesh.face_count(); ++face) {
                const auto& first = mesh.vertices[face * 4U];
                // Every top quad covers exactly one authored voxel. Its first
                // corner is not necessarily the minimum, so inspect all four.
                auto x = first.x, y = first.y;
                for (std::size_t corner{}; corner < 4U; ++corner) {
                    x = std::min(x, mesh.vertices[face * 4U + corner].x);
                    y = std::min(y, mesh.vertices[face * 4U + corner].y);
                }
                const auto color = battlespades::test::classic_water_color(
                    static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
                const auto packed = (static_cast<std::uint32_t>(color.blue) << 16U) |
                                    (static_cast<std::uint32_t>(color.green) << 8U) | color.red;
                for (std::size_t corner{}; corner < 4U; ++corner) {
                    const auto& vertex = mesh.vertices[face * 4U + corner];
                    expect(vertex.z == 239.0F && vertex.abgr == packed,
                           "Classic water grid must retain every authored RGB, including black");
                }
            }
            // The same maximum z produces the same offset for retail. It must
            // not activate Classic sky/fog or change retail's uniform water.
            loaded = VxlMap::load(bytes, battlespades::world::VxlDecodeProfile::retail);
            expect(static_cast<bool>(loaded) && loaded.map->source_z_shift() == 176U &&
                   loaded.map->source_profile() == battlespades::world::VxlDecodeProfile::retail,
                   "Retail and Classic can share offset176 without sharing a source profile");
            const auto retail_mesh = ChunkMesher{water_config}.mesh(*loaded.map, interior);
            for (const auto& vertex : retail_mesh.vertices)
                expect(vertex.abgr == ((19U << 16U) | (11U << 8U) | 7U),
                       "Explicit retail64 must retain its uniform fallback bed");
        }
        {
            auto loaded = VxlMap::load(battlespades::test::classic_water_bytes(true),
                                      battlespades::world::VxlDecodeProfile::automatic);
            expect(static_cast<bool>(loaded) && loaded.map->synthetic_bed(0U, 0U) &&
                   !loaded.map->synthetic_bed(2U, 2U),
                   "Only an empty Classic column should receive a synthetic water bed");
            expect(loaded.map->source_profile() == battlespades::world::VxlDecodeProfile::classic64,
                   "Automatic import must expose its resolved source profile");
            expect(loaded.map->set_voxel(0U, 0U, 239U, {0U, 0U, 0U, 0U}) &&
                   !loaded.map->synthetic_bed(0U, 0U),
                   "An explicit black replacement must stop using the fallback water color");
            expect(!loaded.map->synthetic_bed(512U, 0U) && !loaded.map->synthetic_bed(0U, 512U),
                   "Synthetic bed lookup must reject coordinates outside the map");
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
                            a.edge_v == b.edge_v && a.retail_baked_light == b.retail_baked_light;
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
                       recovered->directional_influence == 0U && recovered->retail_baked_light == 1.0F &&
                       (recovered->static_light >> 24U) == 255U,
                   "isolated terrain vertex must carry exact retail AO/edge/noise/light fields");
            expect(meshed.minimum[2U] == 100.0F, "mesh bounds must include the floating voxel");
        }

        // Original vxl.pyd light smoothing uses current + occupied tangent neighbours,
        // then sub_100051C0 multiplies RGB. It never fills gl_Vertex.w from these bytes.
        // The light byte itself is retail's recomputed diagonal sun shadow
        // (sub_10004470): 127 minus 18, 16, ... for solid cells at
        // (x, y-k, z-k), k = 1..9. The file/set_voxel light byte is ignored.
        {
            auto lights = empty_world();
            expect(lights.set_voxel(100,100,100,{128,64,32,0}), "light fixture");
            expect(lights.set_voxel(101,100,100,{128,64,32,0}), "light neighbour");
            const auto check_corner = [&](float expected) {
                const auto mesh = mesher.mesh(lights,interior);
                const auto corner = std::ranges::find_if(mesh.vertices, [](const auto& v) {
                    return v.face==4 && v.x==101 && v.y==100 && v.z==100 && v.abgr==0x00204080;
                });
                expect(corner!=mesh.vertices.end(), "shared light corner must be emitted");
                expect(std::abs(corner->retail_baked_light-expected)<0.000001F &&
                       corner->directional_influence==0, "baked light must not bypass directional shading");
            };
            check_corner(1.0F); // Unshadowed: the stored zero light byte is not used.
            expect(lights.set_voxel(100,99,99,{10,10,10,255}), "first diagonal occluder");
            check_corner((109.0F + 127.0F) / 254.0F); // 127 - 18 averaged with 127.
            expect(lights.set_voxel(100,98,98,{10,10,10,255}), "second diagonal occluder");
            check_corner((93.0F + 127.0F) / 254.0F); // 127 - 18 - 16.
        }

        // Retail static-light vertex kernel (vxl.pyd 0x10022360, P3-13): the
        // strongest light's att = 1 - d^2/r^2 from its voxel centre, times
        // max(0, N.L) against the true face normal, baked into colour2 RGB,
        // with directional_influence = att (0 where N.L <= 0).
        {
            auto lit = empty_world();
            expect(lit.set_voxel(100U, 100U, 100U, stone), "kernel fixture");
            battlespades::world::StaticLightField field;
            expect(field.add({{100U, 100U, 98U}, {255U, 0U, 0U, 255U}, 5.0F}), "kernel light");
            ChunkMesherConfig retail_config;
            retail_config.static_lights = &field;
            retail_config.retail_static_light_kernel = true;
            const auto mesh = ChunkMesher{retail_config}.mesh(lit, interior);
            const auto find = [&](std::uint8_t face) {
                return std::ranges::find_if(mesh.vertices, [&](const auto& v) {
                    return v.face == face && v.x == 100.0F && v.y == 100.0F && v.z == 100.0F;
                });
            };
            const auto top = find(4U);
            expect(top != mesh.vertices.end(), "kernel top corner must be emitted");
            // d = (0.5, 0.5, -1.5): att = 1 - 2.75/25 = 0.89; N.L = 1.5/sqrt(2.75).
            const auto red = static_cast<int>(top->static_light & 0xFFU);
            expect(std::abs(red - 205) <= 1 && ((top->static_light >> 8U) & 0xFFFFU) == 0U,
                   "top-face kernel light must be rgb * att * N.L");
            expect(std::abs(static_cast<int>(top->directional_influence) - 227) <= 1,
                   "w must carry att at a lit vertex");
            const auto bottom = find(5U);
            expect(bottom == mesh.vertices.end() ||
                       ((bottom->static_light & 0x00FFFFFFU) == 0U &&
                        bottom->directional_influence == 0U),
                   "a face turned away from the light gets neither light nor w");
            // The light sits +0.5 east of the corner, so the -x face is unlit.
            const auto west = find(0U);
            expect(west != mesh.vertices.end() && (west->static_light & 0x00FFFFFFU) == 0U &&
                       west->directional_influence == 0U,
                   "the -x face turned away from the light stays unlit");
            // sub_1000C5F0 decodes the kernel's N as the TRUE face normal (map_vert's
            // x/y swap does not apply here): a light due west lights the -x face.
            battlespades::world::StaticLightField west_field;
            expect(west_field.add({{98U, 100U, 100U}, {255U, 0U, 0U, 255U}, 5.0F}), "west light");
            retail_config.static_lights = &west_field;
            const auto west_mesh = ChunkMesher{retail_config}.mesh(lit, interior);
            const auto west_lit = std::ranges::find_if(west_mesh.vertices, [](const auto& v) {
                return v.face == 0U && v.x == 100.0F && v.y == 100.0F && v.z == 100.0F;
            });
            // d = (-1.5, 0.5, 0.5): att = 1 - 2.75/25 = 0.89; N.L = 1.5/sqrt(2.75).
            expect(west_lit != west_mesh.vertices.end() &&
                       std::abs(static_cast<int>(west_lit->static_light & 0xFFU) - 205) <= 1 &&
                       std::abs(static_cast<int>(west_lit->directional_influence) - 227) <= 1,
                   "the -x face facing a west light must use the true normal");
            const auto plain = ChunkMesher{}.mesh(lit, interior);
            expect(std::ranges::all_of(plain.vertices, [](const auto& v) {
                       return v.directional_influence == 0U;
                   }),
                   "without the kernel no vertex bypasses directional light");
        }

        // The finaliser's z=239 bed is stored as colour zero, but retail reads
        // it back as the ground-colour row with light byte 253 (full light).
        // Decoding it as light 0 painted every water surface near-black.
        {
            const auto bed_mesh = mesher.mesh(empty_world(), interior);
            const auto bed = std::ranges::find_if(bed_mesh.vertices, [](const auto& v) {
                return v.face == 4U && v.z == 239.0F;
            });
            expect(bed != bed_mesh.vertices.end(), "the bed must expose its top face");
            const auto water = ChunkMesherConfig{}.bed_water_color;
            expect(bed->retail_baked_light == 1.0F,
                   "the water bed must be fully lit, as retail light byte 253");
            expect((bed->abgr & 0x00FFFFFFU) ==
                       ((static_cast<std::uint32_t>(water.blue) << 16U) |
                        (static_cast<std::uint32_t>(water.green) << 8U) | water.red),
                   "the water bed must carry the configured ground/water colour");
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
