#pragma once

#include "battlespades/world/chunk_mesh.hpp"

#include <bgfx/bgfx.h>

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

} // namespace battlespades::render
