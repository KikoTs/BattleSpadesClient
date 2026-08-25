#pragma once

#include "battlespades/world/emissive_set.hpp"
#include "battlespades/world/static_light_field.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace battlespades::world {

/**
 * Coarse volume of light cast BY emissive blocks onto their surroundings.
 *
 * Self-illumination makes a neon sign look bright; it cannot make the wall
 * opposite it glow green, or a lantern pool light on the street below. That
 * spill is what makes a night map feel inhabited rather than decorated, and it
 * needs light actually propagated through the world.
 *
 * Per-voxel propagation is not affordable: 512x512x240 at one byte per channel
 * is a quarter of a gigabyte. So light is accumulated into 4-block cells --
 * 128x128x60, four bytes each, under 4 MB, about the cost of one shadow map --
 * and sampled with a single trilinear tap. Emitters are binned into their own
 * cells first, which collapses Tokyo's ~8800 emissive voxels into a few hundred
 * sources and makes the propagation cheap enough to run at map load.
 *
 * Occlusion is checked by stepping the voxel map between source and destination
 * at voxel resolution, so a one-voxel facade still blocks light even though the
 * cells are four blocks wide.
 */
class EmissiveVolume final {
public:
    /** Cell edge in blocks. Four keeps the volume small and a sign's own cell tight. */
    static constexpr std::uint32_t cell_size{4U};
    static constexpr std::uint32_t width{VxlMap::width / cell_size};
    static constexpr std::uint32_t depth{VxlMap::depth / cell_size};
    static constexpr std::uint32_t height{VxlMap::height / cell_size};
    /** How far light travels, in blocks. */
    static constexpr float reach{14.0F};

    EmissiveVolume();

    /**
     * Rebuilds the whole volume. Intended for map load, not per frame.
     *
     * `palette` selects which voxel colours emit; `placed` adds player-placed
     * lights so a flare block spills through the same path.
     */
    void build(const VxlMap& map, const EmissivePalette& palette,
               const StaticLightField& placed);

    void clear() noexcept;

    /** RGBA8, x-major within a row, rows within a slice. Alpha is unused. */
    [[nodiscard]] std::span<const std::uint8_t> data() const noexcept { return cells_; }
    [[nodiscard]] bool empty() const noexcept { return sources_ == 0U; }
    [[nodiscard]] std::size_t source_count() const noexcept { return sources_; }

    /** Light at a world position, for tests and for CPU-side queries. */
    [[nodiscard]] std::array<float, 3U> sample(float x, float y, float z) const noexcept;

private:
    std::vector<std::uint8_t> cells_;
    std::size_t sources_{};
};

} // namespace battlespades::world
