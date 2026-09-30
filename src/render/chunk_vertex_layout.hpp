#pragma once

#include "battlespades/world/chunk_mesh.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace battlespades::render {

/**
 * The one terrain vertex layout, shared so it can be tested against the GPU.
 *
 * Private to the render library rather than a public header because the project
 * keeps bgfx out of `include/`; the offscreen layout test includes this
 * directly, which is the point -- the encoding below is not independently
 * checkable any other way.
 *
 * EVERY Uint8 attribute here is normalized. All three producers of these bytes
 * (chunk_mesher.cpp, kv6_model.cpp, terrain_effects.cpp) write plain small
 * integers -- face 0..5, corner occlusion 0..3 and retail noise corner 0..3 --
 * on that understanding, and vs_world.sc recovers them with `* 255.0 + 0.5`.
 *
 * Non-normalized would not merely be a different scale, it is a type error:
 * bgfx binds DXGI_FORMAT_R8G8B8A8_UINT (renderer_d3d11.cpp `s_attribType`) or
 * glVertexAttribIPointer (renderer_gl.cpp) to a register the compiled shader
 * declares as float32, and what arrives is not the integer. Measured on D3D11:
 * with Color1 non-normalized all six faces decode to index 0, so the Classic
 * face table collapses to a flat 0.85 and corner occlusion pins to level 0 --
 * that is, directional shading and ambient occlusion both silently vanish.
 * aos_world_vertex_layout_tests renders exactly that and fails if it regresses.
 */
[[nodiscard]] inline bgfx::VertexLayout chunk_vertex_layout() {
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3U, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4U, bgfx::AttribType::Uint8, true)
        // Face, scalar AO, retail noise corner, directional-light influence.
        .add(bgfx::Attrib::Color1, 4U, bgfx::AttribType::Uint8, true)
        // Baked static point light RGB; alpha marks a retail terrain vertex.
        .add(bgfx::Attrib::Color2, 4U, bgfx::AttribType::Uint8, true)
        // Retail AO atlas red-channel UV and green-channel edge UV.
        .add(bgfx::Attrib::TexCoord0, 4U, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 1U, bgfx::AttribType::Float)
        .end();
    return layout;
}

/**
 * The stride the layout must have.
 *
 * ChunkVertex static_asserts its own size, but nothing otherwise pins the
 * layout to it: adding a member without adding the matching attribute leaves
 * the stride short, and bgfx then walks the vertex buffer at the wrong pitch,
 * corrupting every vertex after the first.
 */
[[nodiscard]] inline bool chunk_vertex_layout_matches_struct(const bgfx::VertexLayout& layout) {
    return layout.getStride() == sizeof(world::ChunkVertex);
}

/**
 * The 40-byte GPU form of a terrain vertex (RenderTuning::packed_terrain).
 *
 * The same six attributes reach vs_world.sc with the same values; only the
 * position's storage narrows. Terrain positions are voxel corners (integers
 * up to 512), which a 16-bit float holds exactly, so the half -> float
 * conversion the GPU performs gives back the identical float32.
 *
 * The atlas coordinates and the baked light stay float32. Most atlas cells
 * are multiples of 1/512 and would fit a half, but retail's cells 0 and 1
 * carry 0.49 and 0.484140635, which do not, and nearly every chunk uses
 * them. Narrowing those needs the shader to look the cell up from a code,
 * which is a shader change and not this layout's business.
 *
 * KV6 models, effect cubes and the view model keep ChunkVertex: their
 * positions are not on the voxel grid.
 */
struct PackedTerrainVertex final {
    /** x, y, z and a constant 1.0; the shader reads the first three. */
    std::array<std::uint16_t, 4U> position{};
    std::uint32_t abgr{};
    std::uint8_t face{};
    std::uint8_t occlusion{};
    std::uint8_t noise_corner{};
    std::uint8_t directional_influence{};
    std::uint32_t static_light{};
    /** ao_u, ao_v, edge_u, edge_v. */
    std::array<float, 4U> atlas{};
    float retail_baked_light{1.0F};
};

static_assert(sizeof(PackedTerrainVertex) == 40U,
              "the packed terrain layout stride must match this exactly");

[[nodiscard]] inline bgfx::VertexLayout packed_terrain_vertex_layout() {
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 4U, bgfx::AttribType::Half)
        .add(bgfx::Attrib::Color0, 4U, bgfx::AttribType::Uint8, true)
        .add(bgfx::Attrib::Color1, 4U, bgfx::AttribType::Uint8, true)
        .add(bgfx::Attrib::Color2, 4U, bgfx::AttribType::Uint8, true)
        .add(bgfx::Attrib::TexCoord0, 4U, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 1U, bgfx::AttribType::Float)
        .end();
    return layout;
}

/**
 * The IEEE 754 binary16 encoding of `value`, or nothing when `value` is not
 * exactly representable (rounding is never acceptable here: the packed layout
 * exists only on the promise that the shader sees the same number).
 */
[[nodiscard]] constexpr std::optional<std::uint16_t> exact_half(float value) noexcept {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    const auto sign = static_cast<std::uint16_t>((bits >> 16U) & 0x8000U);
    const std::uint32_t exponent_field = (bits >> 23U) & 0xFFU;
    const std::uint32_t mantissa = bits & 0x007FFFFFU;
    if (exponent_field == 0U) {
        // Zero keeps its sign; a float32 subnormal is far below half's range.
        return mantissa == 0U ? std::optional<std::uint16_t>{sign} : std::nullopt;
    }
    if (exponent_field == 0xFFU) {
        return std::nullopt;
    }
    const int exponent = static_cast<int>(exponent_field) - 127;
    if (exponent > 15) {
        return std::nullopt;
    }
    if (exponent >= -14) {
        if ((mantissa & 0x1FFFU) != 0U) {
            return std::nullopt;
        }
        return static_cast<std::uint16_t>(
            sign | (static_cast<std::uint32_t>(exponent + 15) << 10U) | (mantissa >> 13U));
    }
    if (exponent < -24) {
        return std::nullopt;
    }
    // Half subnormal: value = h * 2^-24 with h in [1, 1023].
    const auto shift = static_cast<std::uint32_t>(-exponent - 1);
    const std::uint32_t significand = 0x00800000U | mantissa;
    if ((significand & ((1U << shift) - 1U)) != 0U) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(sign | (significand >> shift));
}

/**
 * Packs `source` into `target` (same length). False, with `target` left
 * partly written, when any position is not exactly a half float; the caller
 * then uploads the chunk in the wide layout instead.
 */
[[nodiscard]] inline bool pack_terrain_vertices(std::span<const world::ChunkVertex> source,
                                                std::span<PackedTerrainVertex> target) noexcept {
    if (source.size() != target.size()) {
        return false;
    }
    constexpr std::uint16_t half_one{0x3C00U};
    for (std::size_t index{}; index < source.size(); ++index) {
        const auto& vertex = source[index];
        const auto x = exact_half(vertex.x);
        const auto y = exact_half(vertex.y);
        const auto z = exact_half(vertex.z);
        if (!x || !y || !z) {
            return false;
        }
        target[index] = PackedTerrainVertex{{*x, *y, *z, half_one},
                                            vertex.abgr,
                                            vertex.face,
                                            vertex.occlusion,
                                            vertex.noise_corner,
                                            vertex.directional_influence,
                                            vertex.static_light,
                                            {vertex.ao_u, vertex.ao_v, vertex.edge_u,
                                             vertex.edge_v},
                                            vertex.retail_baked_light};
    }
    return true;
}

} // namespace battlespades::render
