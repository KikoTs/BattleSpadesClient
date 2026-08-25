#include "battlespades/world/chunk_mesh.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace battlespades::world {
namespace {

struct FaceGeometry final {
    std::array<std::int32_t, 3U> normal{};
    /** Corner offsets ordered counter-clockwise around the outward normal. */
    std::array<std::array<std::uint32_t, 3U>, 4U> corners{};
    /** Tangent axes used by the per-corner occlusion probes. */
    std::array<std::int32_t, 3U> tangent_u{};
    std::array<std::int32_t, 3U> tangent_v{};
    /** Corner position along each tangent axis, aligned with `corners`. */
    std::array<std::array<std::int32_t, 2U>, 4U> corner_uv{};
};

struct AtlasUv final {
    float u{};
    float v{};
};

/**
 * Corner order satisfies the right-hand rule around the outward normal in
 * canonical map axes (x east, y south, z down); the world renderer selects
 * its cull mode against this single winding convention.
 */
constexpr std::array<FaceGeometry, 6U> face_table{{
    // face 0: -x
    {{-1, 0, 0},
     {{{0U, 0U, 0U}, {0U, 0U, 1U}, {0U, 1U, 1U}, {0U, 1U, 0U}}},
     {0, 1, 0},
     {0, 0, 1},
     {{{-1, -1}, {-1, 1}, {1, 1}, {1, -1}}}},
    // face 1: +x
    {{1, 0, 0},
     {{{1U, 0U, 0U}, {1U, 1U, 0U}, {1U, 1U, 1U}, {1U, 0U, 1U}}},
     {0, 1, 0},
     {0, 0, 1},
     {{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}}},
    // face 2: -y
    {{0, -1, 0},
     {{{0U, 0U, 0U}, {1U, 0U, 0U}, {1U, 0U, 1U}, {0U, 0U, 1U}}},
     {1, 0, 0},
     {0, 0, 1},
     {{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}}},
    // face 3: +y
    {{0, 1, 0},
     {{{0U, 1U, 0U}, {0U, 1U, 1U}, {1U, 1U, 1U}, {1U, 1U, 0U}}},
     {1, 0, 0},
     {0, 0, 1},
     {{{-1, -1}, {-1, 1}, {1, 1}, {1, -1}}}},
    // face 4: -z (top, toward the sky)
    {{0, 0, -1},
     {{{0U, 0U, 0U}, {0U, 1U, 0U}, {1U, 1U, 0U}, {1U, 0U, 0U}}},
     {1, 0, 0},
     {0, 1, 0},
     {{{-1, -1}, {-1, 1}, {1, 1}, {1, -1}}}},
    // face 5: +z (bottom)
    {{0, 0, 1},
     {{{0U, 0U, 1U}, {1U, 0U, 1U}, {1U, 1U, 1U}, {0U, 1U, 1U}}},
     {1, 0, 0},
     {0, 1, 0},
     {{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}}},
}};

/**
 * ao_cube512's fifteen logical cells after retail vxl.pyd sub_1003B100.
 *
 * Coordinates include the exact six-texel inset (`6 / 1024`) used by the
 * preserved client. Cell 14 is the deliberately texture-neutral centre point.
 */
constexpr std::array<std::array<AtlasUv, 4U>, 15U> retail_atlas_cells{{
    {{{0.250000000F, 0.490000000F}, {0.250000000F, 0.255859375F},
      {0.494140625F, 0.255859375F}, {0.494140625F, 0.490000000F}}},
    {{{0.255859375F, 0.244140625F}, {0.255859375F, 0.005859375F},
      {0.484140635F, 0.005859375F}, {0.484140635F, 0.244140625F}}},
    {{{0.005859375F, 0.494140625F}, {0.005859375F, 0.255859375F},
      {0.244140625F, 0.255859375F}, {0.244140625F, 0.494140625F}}},
    {{{0.005859375F, 0.005859375F}, {0.244140625F, 0.005859375F},
      {0.244140625F, 0.244140625F}, {0.005859375F, 0.244140625F}}},
    {{{0.244140625F, 0.505859375F}, {0.244140625F, 0.744140625F},
      {0.005859375F, 0.744140625F}, {0.005859375F, 0.505859375F}}},
    {{{0.505859375F, 0.005859375F}, {0.744140625F, 0.005859375F},
      {0.744140625F, 0.244140625F}, {0.505859375F, 0.244140625F}}},
    {{{0.994140625F, 0.005859375F}, {0.994140625F, 0.244140625F},
      {0.755859375F, 0.244140625F}, {0.755859375F, 0.005859375F}}},
    {{{0.744140625F, 0.255859375F}, {0.744140625F, 0.494140625F},
      {0.505859375F, 0.494140625F}, {0.505859375F, 0.255859375F}}},
    {{{0.744140625F, 0.505859375F}, {0.744140625F, 0.744140625F},
      {0.505859375F, 0.744140625F}, {0.505859375F, 0.505859375F}}},
    {{{0.994140625F, 0.255859375F}, {0.994140625F, 0.494140625F},
      {0.755859375F, 0.494140625F}, {0.755859375F, 0.255859375F}}},
    {{{0.994140625F, 0.994140625F}, {0.755859375F, 0.994140625F},
      {0.755859375F, 0.755859375F}, {0.994140625F, 0.755859375F}}},
    {{{0.494140625F, 0.994140625F}, {0.255859375F, 0.994140625F},
      {0.255859375F, 0.755859375F}, {0.494140625F, 0.755859375F}}},
    {{{0.744140625F, 0.994140625F}, {0.505859375F, 0.994140625F},
      {0.505859375F, 0.755859375F}, {0.744140625F, 0.755859375F}}},
    {{{0.244140625F, 0.994140625F}, {0.005859375F, 0.994140625F},
      {0.005859375F, 0.755859375F}, {0.244140625F, 0.755859375F}}},
    {{{0.375000000F, 0.625000000F}, {0.375000000F, 0.625000000F},
      {0.375000000F, 0.625000000F}, {0.375000000F, 0.625000000F}}},
}};

constexpr std::array<std::array<std::uint8_t, 4U>, 4U> retail_atlas_rotations{{
    {{0U, 1U, 2U, 3U}},
    {{1U, 2U, 3U, 0U}},
    {{2U, 3U, 0U, 1U}},
    {{3U, 0U, 1U, 2U}},
}};

// Current canonical face order -> retail vxl record face order.
constexpr std::array<std::uint8_t, 6U> retail_face_index{{5U, 4U, 3U, 2U, 0U, 1U}};

// Current quad corner -> retail emitted quad corner for each canonical face.
constexpr std::array<std::array<std::uint8_t, 4U>, 6U> retail_corner_index{{
    {{3U, 0U, 1U, 2U}}, // -x
    {{1U, 2U, 3U, 0U}}, // +x
    {{1U, 2U, 3U, 0U}}, // -y
    {{3U, 0U, 1U, 2U}}, // +y
    {{0U, 1U, 2U, 3U}}, // top
    {{0U, 1U, 2U, 3U}}, // bottom
}};

constexpr std::array<std::uint8_t, 4U> retail_noise_corner{{0U, 1U, 3U, 2U}};

[[nodiscard]] constexpr AtlasUv retail_atlas_uv(std::uint8_t code,
                                                 std::uint8_t vertex) noexcept {
    const auto rotation = static_cast<std::size_t>(code / 15U);
    const auto cell = static_cast<std::size_t>(code % 15U);
    return retail_atlas_cells[cell][retail_atlas_rotations[rotation][vertex]];
}

/** Exact 8-neighbour atlas selector recovered from vxl.pyd sub_10004510. */
[[nodiscard]] constexpr std::uint8_t retail_pattern(bool a1, bool a2, bool a3, bool a4,
                                                     bool a5, bool a6, bool a7,
                                                     bool a8) noexcept {
    if (a1) {
        if (a2) {
            if (!a3 && !a4) return a8 ? 13U : 1U;
        } else if (!a3 && !a4) {
            return a7 ? static_cast<std::uint8_t>(2U * (a8 ? 1U : 0U) + 10U)
                      : static_cast<std::uint8_t>(a8 ? 11U : 0U);
        }
        if (a2) {
            if (a3 && !a4) return 2U;
        } else {
            if (!a3 && a4) return a7 ? 28U : 16U;
            if (a3 && a4) return 32U;
        }
        if (a2) {
            if (a3) {
                if (a4) return 3U;
            } else if (a4) {
                return 17U;
            }
        } else if (a3 && !a4) {
            return 4U;
        }
        return 14U;
    }
    if (a2) {
        if (a3) {
            if (a4) return 47U;
            return a5 ? 58U : 46U;
        }
        if (a4) return 49U;
        if (a8) return a5 ? 57U : 55U;
        return a5 ? 56U : 45U;
    }
    if (a3) {
        if (a4) return a6 ? 43U : 31U;
        if (a5) return a6 ? 42U : 40U;
        return a6 ? 41U : 30U;
    }
    if (a4) {
        if (a6) return a7 ? 27U : 25U;
        return a7 ? 26U : 15U;
    }
    if (a5) {
        if (a6) {
            if (!a7 && !a8) return 6U;
        } else if (!a7 && !a8) {
            return 5U;
        }
        if (a6) {
            if (a7 && !a8) return 7U;
        } else {
            if (!a7 && a8) return 21U;
            if (a7 && a8) return 37U;
        }
        if (a6) {
            if (a7) {
                if (a8) return 8U;
            } else if (a8) {
                return 22U;
            }
        } else if (a7 && !a8) {
            return 9U;
        }
        return 14U;
    }
    if (a6) {
        if (a7) return a8 ? 52U : 51U;
        return a8 ? 54U : 50U;
    }
    if (!a7) return a8 ? 20U : 14U;
    return a8 ? 36U : 35U;
}

/** Out-of-map probes are empty in the retail AO precomputation. */
[[nodiscard]] bool retail_solid(const VxlMap& map, std::int64_t x, std::int64_t y,
                                std::int64_t z) noexcept {
    return x >= 0 && y >= 0 && z >= 0 &&
           x < static_cast<std::int64_t>(VxlMap::width) &&
           y < static_cast<std::int64_t>(VxlMap::depth) &&
           z < static_cast<std::int64_t>(VxlMap::height) &&
           map.solid(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                     static_cast<std::uint32_t>(z));
}

[[nodiscard]] std::array<std::uint8_t, 6U> retail_ao_codes(
    const VxlMap& map, std::int64_t x, std::int64_t y, std::int64_t z) noexcept {
    const auto s = [&](std::int64_t dx, std::int64_t dy, std::int64_t dz) {
        return retail_solid(map, x + dx, y + dy, z + dz);
    };
    return {{
        retail_pattern(s(-1, 0, -1), s(0, 1, -1), s(1, 0, -1), s(0, -1, -1),
                       s(-1, -1, -1), s(-1, 1, -1), s(1, 1, -1), s(1, -1, -1)),
        retail_pattern(s(0, -1, 1), s(1, 0, 1), s(0, 1, 1), s(-1, 0, 1),
                       s(-1, -1, 1), s(1, -1, 1), s(1, 1, 1), s(-1, 1, 1)),
        retail_pattern(s(0, 1, 1), s(1, 1, 0), s(0, 1, -1), s(-1, 1, 0),
                       s(-1, 1, 1), s(1, 1, 1), s(1, 1, -1), s(-1, 1, -1)),
        retail_pattern(s(-1, -1, 0), s(0, -1, -1), s(1, -1, 0), s(0, -1, 1),
                       s(-1, -1, 1), s(-1, -1, -1), s(1, -1, -1), s(1, -1, 1)),
        retail_pattern(s(1, -1, 0), s(1, 0, -1), s(1, 1, 0), s(1, 0, 1),
                       s(1, -1, 1), s(1, -1, -1), s(1, 1, -1), s(1, 1, 1)),
        retail_pattern(s(-1, 0, 1), s(-1, 1, 0), s(-1, 0, -1), s(-1, -1, 0),
                       s(-1, -1, 1), s(-1, 1, 1), s(-1, 1, -1), s(-1, -1, -1)),
    }};
}

[[nodiscard]] std::array<std::uint8_t, 6U> retail_edge_codes(
    const VxlMap& map, std::int64_t x, std::int64_t y, std::int64_t z) noexcept {
    const auto empty = [&](std::int64_t dx, std::int64_t dy, std::int64_t dz) {
        return !retail_solid(map, x + dx, y + dy, z + dz);
    };
    const bool above_empty = empty(0, 0, -1);
    return {{
        retail_pattern(empty(-1, 0, 0), empty(0, 1, 0), empty(1, 0, 0),
                       empty(0, -1, 0), empty(-1, -1, 0), empty(-1, 1, 0),
                       empty(1, 1, 0), empty(1, -1, 0)),
        14U,
        static_cast<std::uint8_t>(above_empty ? 30U : 14U),
        static_cast<std::uint8_t>(above_empty ? 45U : 14U),
        static_cast<std::uint8_t>(above_empty ? 45U : 14U),
        static_cast<std::uint8_t>(above_empty ? 30U : 14U),
    }};
}

/**
 * Reconstructs gl_Vertex.w from the VXL light byte at one face corner.
 *
 * vxl.pyd sub_10030B60 averages the current voxel and the three occupied
 * neighbours sharing that corner on the face's solid side. The stored byte is
 * divided by 127 and saturated before averaging. VxlMap exposes the same value
 * as `2 * byte - 1`, so `(alpha + 1) / 254` is the exact inverse conversion.
 */
[[nodiscard]] float retail_directional_influence(
    const VxlMap& map, std::uint32_t x, std::uint32_t y, std::uint32_t z,
    const FaceGeometry& geometry, std::array<std::int32_t, 2U> uv) noexcept {
    const auto sample = [&](std::int32_t du, std::int32_t dv) -> std::optional<float> {
        const auto sample_x = static_cast<std::int64_t>(x) + geometry.tangent_u[0U] * du +
                              geometry.tangent_v[0U] * dv;
        const auto sample_y = static_cast<std::int64_t>(y) + geometry.tangent_u[1U] * du +
                              geometry.tangent_v[1U] * dv;
        const auto sample_z = static_cast<std::int64_t>(z) + geometry.tangent_u[2U] * du +
                              geometry.tangent_v[2U] * dv;
        if (!retail_solid(map, sample_x, sample_y, sample_z)) {
            return std::nullopt;
        }
        const auto color = map.color(static_cast<std::uint32_t>(sample_x),
                                     static_cast<std::uint32_t>(sample_y),
                                     static_cast<std::uint32_t>(sample_z));
        if (!color.has_value() || color->alpha == 0U) {
            return std::nullopt;
        }
        return std::min(1.0F,
                        (static_cast<float>(color->alpha) + 1.0F) / 254.0F);
    };

    float total{};
    std::uint32_t count{};
    for (const auto& [du, dv] :
         std::array<std::array<std::int32_t, 2U>, 4U>{{
             {{0, 0}}, {{uv[0U], 0}}, {{0, uv[1U]}}, {{uv[0U], uv[1U]}}}}) {
        if (const auto value = sample(du, dv); value.has_value()) {
            total += *value;
            ++count;
        }
    }
    return count == 0U ? 0.0F : total / static_cast<float>(count);
}

/**
 * The z=239 bed under empty water columns stores color zero, which decodes as
 * fully transparent. Until retail water rendering lands, those voxels take the
 * recovered loading-preview water tone so the sea floor is not a black plane.
 */
[[nodiscard]] bool occluder(const VxlMap& map, std::int64_t x, std::int64_t y,
                            std::int64_t z) noexcept {
    if (z >= static_cast<std::int64_t>(VxlMap::height)) {
        return true;
    }
    if (z < 0) {
        return false;
    }
    // The retail world is bounded, not toroidal, and its clip probes read
    // out-of-range x/y as solid; matching that here suppresses the giant
    // useless walls at the map border.
    if (x < 0 || y < 0 || x >= static_cast<std::int64_t>(VxlMap::width) ||
        y >= static_cast<std::int64_t>(VxlMap::depth)) {
        return true;
    }
    return map.solid(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                     static_cast<std::uint32_t>(z));
}

[[nodiscard]] std::uint8_t corner_occlusion(const VxlMap& map,
                                            const FaceGeometry& geometry,
                                            std::int64_t air_x, std::int64_t air_y,
                                            std::int64_t air_z,
                                            std::array<std::int32_t, 2U> uv) noexcept {
    const auto probe = [&](std::int32_t du, std::int32_t dv) {
        return occluder(map,
                        air_x + geometry.tangent_u[0U] * du + geometry.tangent_v[0U] * dv,
                        air_y + geometry.tangent_u[1U] * du + geometry.tangent_v[1U] * dv,
                        air_z + geometry.tangent_u[2U] * du + geometry.tangent_v[2U] * dv);
    };
    const bool side_u = probe(uv[0U], 0);
    const bool side_v = probe(0, uv[1U]);
    if (side_u && side_v) {
        return 3U;
    }
    const bool corner = probe(uv[0U], uv[1U]);
    return static_cast<std::uint8_t>((side_u ? 1U : 0U) + (side_v ? 1U : 0U) +
                                     (corner ? 1U : 0U));
}

/**
 * Packs a vertex colour, carrying self-illumination in the alpha byte.
 *
 * Terrain is opaque, so alpha was a constant 255 and the only attribute in the
 * 20-byte vertex with spare capacity that is known to be correctly normalized.
 * `fs_world` reads it as emission and writes a literal 1.0 to the framebuffer,
 * so nothing downstream sees a non-opaque terrain fragment.
 */
[[nodiscard]] std::uint32_t pack_abgr(VxlColor color, float shade,
                                      std::uint8_t emission) noexcept {
    const auto scale = [&](std::uint8_t channel) {
        const auto value = std::lround(static_cast<float>(channel) * shade);
        return static_cast<std::uint32_t>(std::clamp(value, 0L, 255L));
    };
    return (static_cast<std::uint32_t>(emission) << 24U) | (scale(color.blue) << 16U) |
           (scale(color.green) << 8U) | scale(color.red);
}

} // namespace

ChunkMesher::ChunkMesher(ChunkMesherConfig config) : config_{config} {
    if (config_.chunk_edge == 0U || VxlMap::width % config_.chunk_edge != 0U) {
        config_.chunk_edge = 16U;
    }
}

std::uint32_t ChunkMesher::chunks_per_axis() const noexcept {
    return VxlMap::width / config_.chunk_edge;
}

ChunkMesh ChunkMesher::mesh(const VxlMap& map, ChunkKey key) const {
    ChunkMesh result;
    result.key = key;
    const auto chunks = chunks_per_axis();
    if (key.x >= chunks || key.y >= chunks) {
        return result;
    }

    constexpr auto float_maximum = std::numeric_limits<float>::max();
    result.minimum = {float_maximum, float_maximum, float_maximum};
    result.maximum = {-float_maximum, -float_maximum, -float_maximum};

    const auto begin_x = key.x * config_.chunk_edge;
    const auto begin_y = key.y * config_.chunk_edge;
    for (std::uint32_t z{}; z < VxlMap::height; ++z) {
        for (auto y = begin_y; y < begin_y + config_.chunk_edge; ++y) {
            for (auto x = begin_x; x < begin_x + config_.chunk_edge; ++x) {
                if (!map.solid(x, y, z)) {
                    continue;
                }
                const auto stored = map.color(x, y, z);
                auto base = stored.has_value() && stored->alpha != 0U
                                ? *stored
                                : config_.bed_water_color;
                const auto appearance =
                    palette_is_empty(config_.emissive)
                        ? std::nullopt
                        : emissive_appearance_at(
                              config_.emissive, map, x, y, z,
                              stored.value_or(config_.bed_water_color));
                if (appearance.has_value()) {
                    // Only the rendered fixture is tinted; canonical VXL color
                    // and protocol replication remain byte-for-byte untouched.
                    base = appearance->surface;
                }
                if (map.damage_fraction(x, y, z) > 0.0F) {
                    // Retail keeps a damaged block in the map and presents a
                    // visibly darker version until Damage(37) reaches block
                    // health. Clear each low bit before halving so integer
                    // channels match the original renderer deterministically.
                    base.red = static_cast<std::uint8_t>((base.red & 0xFEU) >> 1U);
                    base.green = static_cast<std::uint8_t>((base.green & 0xFEU) >> 1U);
                    base.blue = static_cast<std::uint8_t>((base.blue & 0xFEU) >> 1U);
                }
                // Classified from the UNDAMAGED colour on purpose: a damaged
                // neon tube should keep glowing, and the halved channels above
                // would fall outside every swatch tolerance.
                const std::uint8_t emission =
                    appearance.has_value() ? appearance->intensity : 0U;
                const auto ao_codes = retail_ao_codes(map, x, y, z);
                const auto edge_codes = retail_edge_codes(map, x, y, z);
                for (std::uint8_t face{}; face < face_table.size(); ++face) {
                    const auto& geometry = face_table[face];
                    const auto air_x = static_cast<std::int64_t>(x) + geometry.normal[0U];
                    const auto air_y = static_cast<std::int64_t>(y) + geometry.normal[1U];
                    const auto air_z = static_cast<std::int64_t>(z) + geometry.normal[2U];
                    if (occluder(map, air_x, air_y, air_z)) {
                        continue;
                    }

                    std::array<std::uint8_t, 4U> occlusion{};
                    for (std::size_t corner{}; corner < occlusion.size(); ++corner) {
                        occlusion[corner] = corner_occlusion(
                            map, geometry, air_x, air_y, air_z, geometry.corner_uv[corner]);
                    }

                    const auto base_vertex = static_cast<std::uint32_t>(result.vertices.size());
                    for (std::size_t corner{}; corner < 4U; ++corner) {
                        const auto& offset = geometry.corners[corner];
                        const auto retail_face = retail_face_index[face];
                        const auto retail_corner = retail_corner_index[face][corner];
                        const auto ao_uv = retail_atlas_uv(ao_codes[retail_face], retail_corner);
                        const auto edge_uv =
                            retail_atlas_uv(edge_codes[retail_face], retail_corner);
                        const auto directional_influence = retail_directional_influence(
                            map, x, y, z, geometry, geometry.corner_uv[corner]);
                        // Sample placed lights at the vertex, one voxel out
                        // along the face normal. Sampling at the surface itself
                        // would read the lit block's own centre and make every
                        // face of a lamp uniformly bright; stepping into the air
                        // the face looks at gives real directional falloff.
                        std::uint32_t static_light{};
                        if (config_.static_lights != nullptr &&
                            !config_.static_lights->empty()) {
                            const auto arriving = config_.static_lights->sample(
                                static_cast<float>(x + offset[0U]) +
                                    static_cast<float>(geometry.normal[0U]) * 0.5F,
                                static_cast<float>(y + offset[1U]) +
                                    static_cast<float>(geometry.normal[1U]) * 0.5F,
                                static_cast<float>(z + offset[2U]) +
                                    static_cast<float>(geometry.normal[2U]) * 0.5F);
                            const auto quantise = [](float value) {
                                return static_cast<std::uint32_t>(
                                    std::clamp(std::lround(value * 255.0F), 0L, 255L));
                            };
                            static_light = (quantise(arriving[2U]) << 16U) |
                                           (quantise(arriving[1U]) << 8U) |
                                           quantise(arriving[0U]);
                        }
                        const auto authored_shade =
                            config_.bake_shading
                                ? config_.face_shade[face] *
                                      config_.occlusion_shade[occlusion[corner]]
                                : 1.0F;
                        // A source is self-lit. Applying baked directional
                        // shade to its albedo before the shader's emissive term
                        // made half of every window and neon tube look switched
                        // off even at maximum intensity.
                        const auto shade =
                            emission > 0U ? 1.0F : authored_shade;
                        // Color2 alpha is a material tag. Models/effects leave
                        // it zero, so only canonical terrain enters the retail
                        // AO-atlas branch of fs_world.
                        static_light |= 0xFF000000U;
                        const ChunkVertex vertex{
                            static_cast<float>(x + offset[0U]),
                            static_cast<float>(y + offset[1U]),
                            static_cast<float>(z + offset[2U]),
                            pack_abgr(base, shade, emission),
                            face,
                            occlusion[corner],
                            retail_noise_corner[retail_corner],
                            static_cast<std::uint8_t>(std::clamp(
                                std::lround(directional_influence * 255.0F), 0L, 255L)),
                            static_light,
                            ao_uv.u,
                            ao_uv.v,
                            edge_uv.u,
                            edge_uv.v,
                        };
                        result.vertices.push_back(vertex);
                        for (std::size_t axis{}; axis < 3U; ++axis) {
                            const auto position =
                                axis == 0U ? vertex.x : axis == 1U ? vertex.y : vertex.z;
                            result.minimum[axis] = std::min(result.minimum[axis], position);
                            result.maximum[axis] = std::max(result.maximum[axis], position);
                        }
                    }

                    // Split the quad across the diagonal with the more even
                    // occlusion sum so baked corner darkening interpolates
                    // without the classic anisotropic banding artifact.
                    const bool flip_diagonal =
                        occlusion[0U] + occlusion[2U] > occlusion[1U] + occlusion[3U];
                    const std::array<std::uint32_t, 6U> quad_indices =
                        flip_diagonal
                            ? std::array<std::uint32_t, 6U>{1U, 2U, 3U, 1U, 3U, 0U}
                            : std::array<std::uint32_t, 6U>{0U, 1U, 2U, 0U, 2U, 3U};
                    for (const auto quad_index : quad_indices) {
                        result.indices.push_back(base_vertex + quad_index);
                    }
                }
            }
        }
    }

    if (result.vertices.empty()) {
        const auto origin_x = static_cast<float>(begin_x);
        const auto origin_y = static_cast<float>(begin_y);
        result.minimum = {origin_x, origin_y, 0.0F};
        result.maximum = {origin_x, origin_y, 0.0F};
    }
    return result;
}

ChunkTracker::ChunkTracker(std::uint32_t chunk_edge)
    : chunk_edge_{chunk_edge == 0U || VxlMap::width % chunk_edge != 0U ? 16U : chunk_edge},
      chunks_{VxlMap::width / chunk_edge_},
      dirty_(static_cast<std::size_t>(chunks_) * chunks_, 0U) {}

void ChunkTracker::mark_all() {
    std::ranges::fill(dirty_, std::uint8_t{1U});
    dirty_count_ = dirty_.size();
}

void ChunkTracker::mark_chunk(std::uint32_t chunk_x, std::uint32_t chunk_y) {
    if (chunk_x >= chunks_ || chunk_y >= chunks_) {
        return;
    }
    auto& flag = dirty_[chunk_x + static_cast<std::size_t>(chunk_y) * chunks_];
    if (flag == 0U) {
        flag = 1U;
        ++dirty_count_;
    }
}

void ChunkTracker::mark_voxel(std::uint32_t x, std::uint32_t y) {
    if (x >= VxlMap::width || y >= VxlMap::depth) {
        return;
    }
    const auto chunk_x = x / chunk_edge_;
    const auto chunk_y = y / chunk_edge_;
    const auto local_x = x % chunk_edge_;
    const auto local_y = y % chunk_edge_;
    const auto low_x = local_x == 0U && chunk_x > 0U;
    const auto high_x = local_x == chunk_edge_ - 1U;
    const auto low_y = local_y == 0U && chunk_y > 0U;
    const auto high_y = local_y == chunk_edge_ - 1U;

    mark_chunk(chunk_x, chunk_y);
    if (low_x) {
        mark_chunk(chunk_x - 1U, chunk_y);
    }
    if (high_x) {
        mark_chunk(chunk_x + 1U, chunk_y);
    }
    if (low_y) {
        mark_chunk(chunk_x, chunk_y - 1U);
    }
    if (high_y) {
        mark_chunk(chunk_x, chunk_y + 1U);
    }
    if (low_x && low_y) {
        mark_chunk(chunk_x - 1U, chunk_y - 1U);
    }
    if (low_x && high_y) {
        mark_chunk(chunk_x - 1U, chunk_y + 1U);
    }
    if (high_x && low_y) {
        mark_chunk(chunk_x + 1U, chunk_y - 1U);
    }
    if (high_x && high_y) {
        mark_chunk(chunk_x + 1U, chunk_y + 1U);
    }
}

std::vector<ChunkKey> ChunkTracker::take(std::size_t limit) {
    std::vector<ChunkKey> taken;
    if (limit == 0U || dirty_count_ == 0U) {
        return taken;
    }
    taken.reserve(std::min(limit, dirty_count_));
    for (std::uint32_t y{}; y < chunks_ && taken.size() < limit; ++y) {
        for (std::uint32_t x{}; x < chunks_ && taken.size() < limit; ++x) {
            auto& flag = dirty_[x + static_cast<std::size_t>(y) * chunks_];
            if (flag != 0U) {
                flag = 0U;
                --dirty_count_;
                taken.push_back(ChunkKey{x, y});
            }
        }
    }
    return taken;
}

} // namespace battlespades::world
