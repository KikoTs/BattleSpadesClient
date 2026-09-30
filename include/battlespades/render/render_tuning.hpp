#pragma once

#include <cstddef>
#include <cstdint>

namespace battlespades::render {

/**
 * Renderer-internal optimisations that must never change a pixel.
 *
 * Every switch selects between two ways of submitting the SAME image: the
 * shaders, the vertex attribute values they receive and the depth/blend state
 * are identical either way. `aos_render_benchmark tuning=ab` renders both and
 * compares the captures byte for byte, which is what keeps that promise.
 *
 * All on by default. `legacy()` is the pre-tuning submission path, kept for
 * that comparison and as a field fallback: setting the environment variable
 * `BATTLESPADES_RENDER_TUNING=legacy` before launch selects it.
 */
struct RenderTuning final {
    /**
     * Upload terrain chunks with half-float positions (40-byte vertices) and
     * 16-bit indices where they fit, instead of 44-byte vertices and 32-bit
     * indices. A chunk whose positions are not exactly representable stays on
     * the wide layout, so the shader inputs never change.
     */
    bool packed_terrain{true};
    /** Send a world uniform only when its value differs from the last draw's. */
    bool cached_uniforms{true};
    /** Submit visible terrain nearest first, so hidden fragments fail depth early. */
    bool front_to_back_terrain{true};

    [[nodiscard]] static constexpr RenderTuning legacy() noexcept {
        return RenderTuning{false, false, false};
    }

    [[nodiscard]] friend constexpr bool operator==(const RenderTuning&,
                                                   const RenderTuning&) = default;
};

/** GPU buffer bytes held by resident terrain chunks. */
struct TerrainMemory final {
    std::uint64_t vertex_bytes{};
    std::uint64_t index_bytes{};
    std::size_t packed_chunks{};
    std::size_t wide_chunks{};
};

} // namespace battlespades::render
