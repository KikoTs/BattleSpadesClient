#include "battlespades/world/kv6_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <tuple>

namespace battlespades::world {
namespace {

[[nodiscard]] std::uint32_t read_u32(std::span<const std::byte> bytes,
                                     std::size_t position) noexcept {
    return std::to_integer<std::uint32_t>(bytes[position]) |
           (std::to_integer<std::uint32_t>(bytes[position + 1U]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[position + 2U]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[position + 3U]) << 24U);
}

[[nodiscard]] std::uint16_t read_u16(std::span<const std::byte> bytes,
                                     std::size_t position) noexcept {
    return static_cast<std::uint16_t>(
        std::to_integer<std::uint16_t>(bytes[position]) |
        (std::to_integer<std::uint16_t>(bytes[position + 1U]) << 8U));
}

[[nodiscard]] float read_f32(std::span<const std::byte> bytes, std::size_t position) noexcept {
    const auto raw = read_u32(bytes, position);
    float value{};
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

[[nodiscard]] bool fail(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

/** ChunkVertex::face decoded in the vertex's own (render) basis, as vs_world does. */
[[nodiscard]] constexpr std::array<float, 3U> render_face_normal(std::uint8_t face) noexcept {
    switch (face) {
    case 0U: return {-1.0F, 0.0F, 0.0F};
    case 1U: return {1.0F, 0.0F, 0.0F};
    case 2U: return {0.0F, -1.0F, 0.0F};
    case 3U: return {0.0F, 1.0F, 0.0F};
    case 4U: return {0.0F, 0.0F, -1.0F};
    default: return {0.0F, 0.0F, 1.0F};
    }
}

/** kv6.pyd sub_1000DC40: normalize(normalize(corner - centre) + face normal). */
[[nodiscard]] std::array<float, 3U> retail_model_normal(std::array<float, 3U> corner,
                                                        std::array<float, 3U> face,
                                                        std::array<float, 3U> centre) noexcept {
    std::array<float, 3U> radial{corner[0U] - centre[0U], corner[1U] - centre[1U],
                                 corner[2U] - centre[2U]};
    const float radial_length =
        std::sqrt(radial[0U] * radial[0U] + radial[1U] * radial[1U] + radial[2U] * radial[2U]);
    if (radial_length > 1.0e-6F) {
        for (auto& axis : radial) axis /= radial_length;
    } else {
        radial = {};
    }
    std::array<float, 3U> normal{radial[0U] + face[0U], radial[1U] + face[1U],
                                 radial[2U] + face[2U]};
    const float length =
        std::sqrt(normal[0U] * normal[0U] + normal[1U] * normal[1U] + normal[2U] * normal[2U]);
    if (length <= 1.0e-6F) {
        return face;
    }
    for (auto& axis : normal) axis /= length;
    return normal;
}

} // namespace

std::optional<Kv6Model> Kv6Model::load_file(const std::filesystem::path& path,
                                            std::string* error) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        if (error != nullptr) {
            *error = "unable to open KV6 file: " + path.string();
        }
        return std::nullopt;
    }
    std::vector<char> raw{std::istreambuf_iterator<char>{input}, {}};
    return load(std::as_bytes(std::span{raw}), error);
}

std::optional<Kv6Model> Kv6Model::load(std::span<const std::byte> bytes, std::string* error) {
    constexpr std::size_t header_size{32U};
    if (bytes.size() < header_size || std::to_integer<char>(bytes[0U]) != 'K' ||
        std::to_integer<char>(bytes[1U]) != 'v' || std::to_integer<char>(bytes[2U]) != 'x' ||
        std::to_integer<char>(bytes[3U]) != 'l') {
        static_cast<void>(fail(error, "not a Kvxl KV6 stream"));
        return std::nullopt;
    }

    Kv6Model model;
    model.size_x_ = read_u32(bytes, 4U);
    model.size_y_ = read_u32(bytes, 8U);
    model.size_z_ = read_u32(bytes, 12U);
    model.pivot_ = {read_f32(bytes, 16U), read_f32(bytes, 20U), read_f32(bytes, 24U)};
    const auto voxel_count = read_u32(bytes, 28U);
    // The shipped tool/casing models are all small; a hard bound keeps a
    // corrupt header from allocating the world.
    if (model.size_x_ == 0U || model.size_y_ == 0U || model.size_z_ == 0U ||
        model.size_x_ > 1'024U || model.size_y_ > 1'024U || model.size_z_ > 1'024U ||
        voxel_count > 4'000'000U) {
        static_cast<void>(fail(error, "KV6 dimensions are out of range"));
        return std::nullopt;
    }

    const std::size_t voxel_bytes = static_cast<std::size_t>(voxel_count) * 8U;
    const std::size_t xlen_bytes = static_cast<std::size_t>(model.size_x_) * 4U;
    const std::size_t ylen_bytes =
        static_cast<std::size_t>(model.size_x_) * model.size_y_ * 2U;
    if (bytes.size() < header_size + voxel_bytes + xlen_bytes + ylen_bytes) {
        static_cast<void>(fail(error, "truncated KV6 stream"));
        return std::nullopt;
    }

    // Column run lengths reconstruct each record's x/y (records are stored
    // x-major, then y, z ascending inside a column).
    const std::size_t ylen_base = header_size + voxel_bytes + xlen_bytes;
    model.voxels_.reserve(voxel_count);
    std::size_t record = header_size;
    std::size_t consumed = 0U;
    for (std::uint32_t x{}; x < model.size_x_; ++x) {
        for (std::uint32_t y{}; y < model.size_y_; ++y) {
            const auto run = read_u16(
                bytes, ylen_base + (static_cast<std::size_t>(x) * model.size_y_ + y) * 2U);
            for (std::uint16_t entry{}; entry < run; ++entry) {
                if (consumed >= voxel_count) {
                    static_cast<void>(fail(error, "KV6 run tables exceed the voxel count"));
                    return std::nullopt;
                }
                Voxel voxel;
                voxel.x = static_cast<std::uint16_t>(x);
                voxel.y = static_cast<std::uint16_t>(y);
                voxel.color = VxlColor{std::to_integer<std::uint8_t>(bytes[record + 2U]),
                                       std::to_integer<std::uint8_t>(bytes[record + 1U]),
                                       std::to_integer<std::uint8_t>(bytes[record]),
                                       255U};
                voxel.z = read_u16(bytes, record + 4U);
                voxel.visibility = std::to_integer<std::uint8_t>(bytes[record + 6U]);
                voxel.normal_index = std::to_integer<std::uint8_t>(bytes[record + 7U]);
                if (voxel.z >= model.size_z_) {
                    static_cast<void>(fail(error, "KV6 voxel outside the model bounds"));
                    return std::nullopt;
                }
                model.voxels_.push_back(voxel);
                record += 8U;
                ++consumed;
            }
        }
    }
    if (consumed != voxel_count) {
        static_cast<void>(fail(error, "KV6 run tables disagree with the voxel count"));
        return std::nullopt;
    }
    return model;
}

void Kv6Model::offset_pivots(std::array<float, 3U> offset) noexcept {
    for (std::size_t axis{}; axis < pivot_.size(); ++axis) {
        pivot_[axis] += offset[axis] / voxel_scale_;
    }
}

std::vector<Kv6Model> Kv6Model::articulated_classic_arms() const {
    if (size_x_ != 12U || size_y_ != 10U || size_z_ != 6U || voxel_scale_ != 1.0F) {
        return {};
    }
    // Classic p_arms encodes a bent arm in the x=0/1 slice: shoulder
    // (y=0,z=0), elbow (4,4), wrist (8,0). Unbend its two straight sections
    // into the OpenSpades shoulder/elbow frame. The original colors, glove
    // shape and sleeve markings come from this model, never the default arms.
    std::vector<Kv6Model> result(2U);
    for (std::size_t part = 0U; part < result.size(); ++part) {
        auto& segment = result[part];
        segment.size_x_ = 2U;
        segment.size_y_ = 3U;
        segment.size_z_ = 12U;
        segment.pivot_ = {0.5F, 1.5F, 0.0F};
        const int start = part == 0U ? 0 : 5;
        for (const auto& voxel : voxels_) {
            const int along = static_cast<int>(voxel.y) - start;
            if (voxel.x > 1U || along < 0 || along >= 4) {
                continue;
            }
            const int center = part == 0U ? voxel.y : 8 - voxel.y;
            const int across = static_cast<int>(voxel.z) - center + 1;
            if (across < 0 || across >= 3) {
                return {}; // A different authored pose needs its own adapter.
            }
            for (int layer = 0; layer < 3; ++layer) {
                segment.voxels_.push_back({voxel.x, static_cast<std::uint16_t>(across),
                    static_cast<std::uint16_t>(along * 3 + layer), voxel.color, 63U});
            }
        }
        if (segment.voxels_.empty()) {
            return {};
        }
    }
    return result;
}

Kv6Model Kv6Model::inverse_scaled(std::uint8_t inverse_scale) const {
    if (inverse_scale <= 1U || voxels_.empty()) {
        return *this;
    }
    inverse_scale = std::min<std::uint8_t>(inverse_scale, 3U);

    struct Cell final {
        std::uint32_t red{};
        std::uint32_t green{};
        std::uint32_t blue{};
        std::uint32_t count{};
        bool team_material{};
    };
    using Coordinate = std::tuple<std::uint16_t, std::uint16_t, std::uint16_t>;
    std::map<Coordinate, Cell> cells;
    const auto team_marker = [](const VxlColor& color) noexcept {
        return color.green == 0U && color.red == color.blue &&
               (color.red == 0U || color.red == 64U || color.red == 128U ||
                color.red == 192U);
    };
    for (const auto& voxel : voxels_) {
        const Coordinate coordinate{
            static_cast<std::uint16_t>(voxel.x / inverse_scale),
            static_cast<std::uint16_t>(voxel.y / inverse_scale),
            static_cast<std::uint16_t>(voxel.z / inverse_scale)};
        auto& cell = cells[coordinate];
        cell.red += voxel.color.red;
        cell.green += voxel.color.green;
        cell.blue += voxel.color.blue;
        ++cell.count;
        cell.team_material = cell.team_material || team_marker(voxel.color);
    }

    Kv6Model result;
    const auto ceil_divide = [inverse_scale](std::uint32_t value) {
        return (value + inverse_scale - 1U) / inverse_scale;
    };
    result.size_x_ = ceil_divide(size_x_);
    result.size_y_ = ceil_divide(size_y_);
    result.size_z_ = ceil_divide(size_z_);
    result.pivot_ = {pivot_[0U] / inverse_scale, pivot_[1U] / inverse_scale,
                     pivot_[2U] / inverse_scale};
    result.voxel_scale_ = voxel_scale_ * static_cast<float>(inverse_scale);
    result.voxels_.reserve(cells.size());
    for (const auto& [coordinate, cell] : cells) {
        const auto [x, y, z] = coordinate;
        VxlColor color{};
        if (cell.team_material) {
            // Retail collapses any grouped team marker to the base black band,
            // preserving recolouring instead of averaging it into the model.
            color = {0U, 0U, 0U, 255U};
        } else {
            color = {
                static_cast<std::uint8_t>(cell.red / cell.count),
                static_cast<std::uint8_t>(cell.green / cell.count),
                static_cast<std::uint8_t>(cell.blue / cell.count),
                255U,
            };
        }
        // Retail scale_kv6 (sub_100015D0) sets rebuilt normals to index 1.
        result.voxels_.push_back(Voxel{x, y, z, color, 0U, 1U});
    }
    return result;
}

void Kv6Model::apply_default_color(VxlColor team_color) noexcept {
    const auto scaled = [](std::uint8_t channel, float factor) {
        return static_cast<std::uint8_t>(std::min(
            255.0F, static_cast<float>(channel) * factor + 0.5F));
    };
    for (auto& voxel : voxels_) {
        if (voxel.color.green != 0U || voxel.color.red != voxel.color.blue) {
            continue;
        }
        float intensity{};
        switch (voxel.color.red) {
        case 0U:
        case 128U: intensity = 1.0F; break;
        case 64U: intensity = 0.7F; break;
        case 192U: intensity = 1.3F; break;
        default: continue;
        }
        voxel.color = VxlColor{scaled(team_color.red, intensity),
                               scaled(team_color.green, intensity),
                               scaled(team_color.blue, intensity),
                               team_color.alpha};
    }
}

void Kv6Model::apply_cosmetic_palette(std::array<std::uint8_t, 3U> palette) noexcept {
    for (auto& voxel : voxels_) {
        const auto original = voxel.color;
        if (original.green == 0U && original.red == original.blue &&
            (original.red == 0U || original.red == 64U || original.red == 128U || original.red == 192U)) continue;
        const auto light = (static_cast<unsigned>(original.red)*3U +
            static_cast<unsigned>(original.green)*6U + static_cast<unsigned>(original.blue))/10U;
        const auto shade = [light](std::uint8_t source, std::uint8_t target) {
            return static_cast<std::uint8_t>(std::min(255U,(static_cast<unsigned>(source)*3U +
                static_cast<unsigned>(target)*light*7U/180U)/10U));
        };
        voxel.color.red=shade(original.red,palette[0]);
        voxel.color.green=shade(original.green,palette[1]);
        voxel.color.blue=shade(original.blue,palette[2]);
    }
}

ChunkMesh Kv6Model::mesh(const ChunkMesherConfig& shading,
                         std::array<float, 3U> tint) const {
    // KV6 stores only surface voxels, so occupancy lookups back exposed-face
    // tests exactly like the world mesher's neighbor queries.
    std::set<std::tuple<std::uint16_t, std::uint16_t, std::uint16_t>> occupied;
    for (const auto& voxel : voxels_) {
        occupied.emplace(voxel.x, voxel.y, voxel.z);
    }
    const auto solid = [&](std::int32_t x, std::int32_t y, std::int32_t z) {
        if (x < 0 || y < 0 || z < 0) {
            return false;
        }
        return occupied.contains({static_cast<std::uint16_t>(x),
                                  static_cast<std::uint16_t>(y),
                                  static_cast<std::uint16_t>(z)});
    };

    struct Face final {
        std::array<std::int32_t, 3U> normal;
        std::array<std::array<std::uint32_t, 3U>, 4U> corners;
    };
    // Same winding convention as the world mesher (right-hand rule around
    // the outward normal in canonical axes).
    static constexpr std::array<Face, 6U> faces{{
        {{-1, 0, 0}, {{{0U, 0U, 0U}, {0U, 0U, 1U}, {0U, 1U, 1U}, {0U, 1U, 0U}}}},
        {{1, 0, 0}, {{{1U, 0U, 0U}, {1U, 1U, 0U}, {1U, 1U, 1U}, {1U, 0U, 1U}}}},
        {{0, -1, 0}, {{{0U, 0U, 0U}, {1U, 0U, 0U}, {1U, 0U, 1U}, {0U, 0U, 1U}}}},
        {{0, 1, 0}, {{{0U, 1U, 0U}, {0U, 1U, 1U}, {1U, 1U, 1U}, {1U, 1U, 0U}}}},
        {{0, 0, -1}, {{{0U, 0U, 0U}, {0U, 1U, 0U}, {1U, 1U, 0U}, {1U, 0U, 0U}}}},
        {{0, 0, 1}, {{{0U, 0U, 1U}, {1U, 0U, 1U}, {1U, 1U, 1U}, {0U, 1U, 1U}}}},
    }};

    // The vertex writer below converts authored KV6 axes to retail render
    // space as (x, -z, y), so a KV6 normal (nx, ny, nz) becomes (nx, -nz, ny).
    // ChunkVertex::face is decoded back into a normal against the RENDER basis
    // by vs_world.sc, which means the stored index must be permuted or four of
    // the six faces reconstruct the wrong direction: -y would light as -z, and
    // a player's flank would shade as though it were their top.
    //
    // Faces 0 and 1 are on the untouched x axis and map to themselves.
    static constexpr std::array<std::uint8_t, 6U> kv6_face_to_render{
        0U, 1U, 4U, 5U, 3U, 2U};

    // Per-corner ambient occlusion, the same three-probe test the world mesher
    // uses: look along each of the face's two tangent axes from the air cell in
    // front of it, plus diagonally. Two neighbours meeting at a corner fully
    // occlude it, so that case short-circuits to the darkest level.
    //
    // Worth knowing before tuning this: measured across all thirty shipped class
    // arm assets, fourteen of the fifteen upper arms are perfectly convex and
    // receive occlusion level 0 on every corner -- this changes nothing at all
    // for them. Only Soldier's upper arm is concave. The real beneficiary is the
    // held weapon, whose barrels, sights and magazines are full of crevices that
    // previously rendered as unshaded slabs.
    const auto corner_occlusion = [&](const std::array<std::int32_t, 3U>& normal,
                                      const std::array<std::uint32_t, 3U>& corner,
                                      std::int32_t air_x, std::int32_t air_y,
                                      std::int32_t air_z) -> std::uint8_t {
        // Exactly one component of a face normal is non-zero; the other two axes
        // are the tangents the corner is offset along.
        const std::size_t axis = normal[0U] != 0 ? 0U : (normal[1U] != 0 ? 1U : 2U);
        const std::size_t tangent_u = (axis + 1U) % 3U;
        const std::size_t tangent_v = (axis + 2U) % 3U;
        const auto step = [&](std::size_t which) {
            // A corner offset of 1 sits on the positive side of the voxel.
            return corner[which] == 1U ? 1 : -1;
        };
        const auto probe = [&](std::int32_t du, std::int32_t dv) {
            std::array<std::int32_t, 3U> offset{air_x, air_y, air_z};
            offset[tangent_u] += du;
            offset[tangent_v] += dv;
            return solid(offset[0U], offset[1U], offset[2U]);
        };
        const bool side_u = probe(step(tangent_u), 0);
        const bool side_v = probe(0, step(tangent_v));
        if (side_u && side_v) {
            return 3U;
        }
        const bool diagonal = probe(step(tangent_u), step(tangent_v));
        return static_cast<std::uint8_t>((side_u ? 1U : 0U) + (side_v ? 1U : 0U) +
                                         (diagonal ? 1U : 0U));
    };

    ChunkMesh result;
    constexpr auto float_maximum = std::numeric_limits<float>::max();
    result.minimum = {float_maximum, float_maximum, float_maximum};
    result.maximum = {-float_maximum, -float_maximum, -float_maximum};
    for (const auto& voxel : voxels_) {
        for (std::uint8_t face_index{}; face_index < faces.size(); ++face_index) {
            const auto& face = faces[face_index];
            if (solid(voxel.x + face.normal[0U], voxel.y + face.normal[1U],
                      voxel.z + face.normal[2U])) {
                continue;
            }
            const auto base_vertex = static_cast<std::uint32_t>(result.vertices.size());
            // Off by default now that fs_world lights models per pixel from the
            // stored face index; baking here as well would darken them twice.
            const float shade =
                shading.bake_shading ? shading.face_shade[face_index] : 1.0F;
            const auto scale = [&](std::uint8_t channel, float channel_tint) {
                return static_cast<std::uint32_t>(std::min(
                    255.0F,
                    static_cast<float>(channel) * shade * channel_tint + 0.5F));
            };
            // Alpha is the SELF-ILLUMINATION byte, not opacity. The emissive
            // block work repurposed it, and terrain writes 0 there
            // (chunk_mesher.cpp pack_abgr) -- which is exactly why the world
            // looks right and models did not. fs_world.sc applies it as
            // `lit += albedo * v_color0.a * u_emissiveParams.x` AFTER every
            // occlusion term, deliberately, because a light source is not dimmed
            // by shadow, by ambient occlusion or by being indoors.
            //
            // Leaving KV6 at 255 therefore handed every arm, weapon and player
            // voxel a full unshaded copy of its own albedo on top of its lit
            // result, scaled by the tier's emissive_gain. That is both halves of
            // the complaint at once: it is far too bright, and because the added
            // term is flat -- no AO, no shadow, no facing -- it compresses every
            // lit-to-shadow ratio on the model toward 1:1, which is the
            // definition of a plastic surface.
            //
            // Models are not light sources.
            const std::uint32_t abgr = (scale(voxel.color.blue, tint[2U]) << 16U) |
                                       (scale(voxel.color.green, tint[1U]) << 8U) |
                                       scale(voxel.color.red, tint[0U]);
            for (const auto& corner : face.corners) {
                // kv6.pyd sub_1001AA50 writes each authored KV6 coordinate to
                // its OpenGL VBO as (x - pivot.x, -(z - pivot.z),
                // y - pivot.y). Keep that conversion at the asset boundary;
                // all recovered DisplayList and draw_fps transforms operate on
                // these retail render-space vertices.
                //
                // The half-voxel is load bearing and was measured, not assumed.
                // sub_1001AA50 computes exactly three per-voxel bases --
                // `(x - px)*n + c` at 0x1001AE0B-0x1001AE33, `c - (z - pz)*n`
                // at 0x1001AE3A-0x1001AE4E and `(y - py)*n + c` at
                // 0x1001AE52-0x1001AE63 -- then builds each cube's corners from
                // two values it keeps live across the whole voxel loop:
                // flt_100212B0 = -0.5 and flt_100212B4 = +0.5 (scaled by the
                // voxel size n, and c = 0.5n - 0.5 is zero at the n = 1 every
                // shipped asset loads with). Retail therefore CENTRES the cube
                // on the pivot-relative coordinate; it does not span
                // [coord, coord + 1]. Emitting corners at coord + {0, 1} put
                // every KV6 model in the game half a voxel off on all three
                // axes.
                //
                // The aimed sight is where that was visible: character.pyx:2079
                // adds 0.025 to X, which is exactly half a voxel at the sight
                // scale of 0.05, and character.pyx:2087 leaves the pin at
                // 0.025 - 0.015 = 0.010, exactly half a voxel at the pin scale
                // of 0.02. Both constants exist to cancel this half-voxel pivot
                // bias, and both only cancel it under the centred convention.
                // Two different scales cancelling exactly is not a coincidence.
                constexpr float half_voxel{0.5F};
                const float centre_correction = half_voxel * voxel_scale_ - half_voxel;
                const float model_x =
                    (static_cast<float>(voxel.x + corner[0U]) - half_voxel -
                     pivot_[0U]) * voxel_scale_ + centre_correction;
                const float model_y =
                    centre_correction -
                    (static_cast<float>(voxel.z + corner[2U]) - half_voxel -
                     pivot_[2U]) * voxel_scale_;
                const float model_z =
                    (static_cast<float>(voxel.y + corner[1U]) - half_voxel -
                     pivot_[1U]) * voxel_scale_ + centre_correction;
                ChunkVertex vertex{
                    model_x,
                    model_y,
                    model_z,
                    abgr,
                    // Baked shade above uses the unpermuted KV6 index, so the
                    // vertex colour is unchanged; only the normal index moves.
                    kv6_face_to_render[face_index],
                    corner_occlusion(face.normal, corner,
                                     static_cast<std::int32_t>(voxel.x) +
                                         face.normal[0U],
                                     static_cast<std::int32_t>(voxel.y) +
                                         face.normal[1U],
                                     static_cast<std::int32_t>(voxel.z) +
                                         face.normal[2U]),
                    0U,
                    0U,
                };
                // Retail gl_Normal. sub_1001AA50 first writes the byte-7 table
                // normal (-nx, nz, -ny), but every non-billboard path then
                // calls sub_1000DC40 (or sub_1000E120 for team-colour groups),
                // which OVERWRITES each corner's normal with
                // normalize(normalize(P - C) + F): P the render-space corner,
                // F the quad's face normal and C the model centre
                // ((xsiz>>1) - px, -((zsiz>>1) - pz), (ysiz>>1) - py),
                // deliberately unscaled by the voxel size as in retail. The
                // table value is dead. Using it drew slab6's per-voxel normal
                // noise on every model, and the 255 sentinel as an embossed
                // cross on the held block. Reuse the terrain-only UV attribute
                // for this tagged KV6 normal; Enhanced keeps the face normal.
                vertex.static_light = 0x40000000U;
                const auto normal = retail_model_normal(
                    {model_x, model_y, model_z},
                    render_face_normal(kv6_face_to_render[face_index]),
                    {static_cast<float>(size_x_ >> 1U) - pivot_[0U],
                     -(static_cast<float>(size_z_ >> 1U) - pivot_[2U]),
                     static_cast<float>(size_y_ >> 1U) - pivot_[1U]});
                vertex.ao_u = normal[0U];
                vertex.ao_v = normal[1U];
                vertex.edge_u = normal[2U];
                result.vertices.push_back(vertex);
                result.minimum[0U] = std::min(result.minimum[0U], vertex.x);
                result.minimum[1U] = std::min(result.minimum[1U], vertex.y);
                result.minimum[2U] = std::min(result.minimum[2U], vertex.z);
                result.maximum[0U] = std::max(result.maximum[0U], vertex.x);
                result.maximum[1U] = std::max(result.maximum[1U], vertex.y);
                result.maximum[2U] = std::max(result.maximum[2U], vertex.z);
            }
            for (const auto index : {0U, 1U, 2U, 0U, 2U, 3U}) {
                result.indices.push_back(base_vertex + index);
            }
        }
    }
    if (result.vertices.empty()) {
        result.minimum = {};
        result.maximum = {};
    }
    return result;
}

} // namespace battlespades::world
