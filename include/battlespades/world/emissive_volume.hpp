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

    /**
     * Trilinear light at a world position, filtered as the GPU samples the
     * uploaded volume (texel centres at (cell + 0.5) * cell_size, clamped at
     * the edges). Used to light models, which cannot probe the 3D texture
     * themselves when they are drawn in view space.
     */
    [[nodiscard]] std::array<float, 3U> sample_filtered(float x, float y,
                                                        float z) const noexcept;

private:
    std::vector<std::uint8_t> cells_;
    std::size_t sources_{};
};

/**
 * The local map light arriving at a model, split by source.
 *
 * Terrain receives placed (flare/fire) light through the mesher's per-vertex
 * bake and emissive spill through the 3D volume probe. A KV6 model has
 * neither: its vertices carry no bake, and the first-person view model is
 * drawn in view space, where the volume probe reads an unrelated cell. This
 * samples both sources on the CPU at the model's world position so the
 * renderer can add the same light to it (enhanced tiers only; retail's
 * model_frag reads only the two packet-45 lights and ambient).
 */
struct ModelLightSample final {
    /** StaticLightField::sample at the position, 0..1 per channel. */
    std::array<float, 3U> placed{};
    /** EmissiveVolume::sample_filtered at the position, 0..1 per channel. */
    std::array<float, 3U> cast{};
};

/** Either source may be null (not built yet); its term is then zero. */
[[nodiscard]] ModelLightSample sample_model_light(const StaticLightField* placed,
                                                  const EmissiveVolume* cast,
                                                  std::array<float, 3U> position) noexcept;

/**
 * The additive diffuse light the world shader applies to a model:
 * placed * placed_gain + cast * cast_gain, the same gains terrain uses
 * (u_emissiveParams.y for the flare bake, u_indirectParams.x for spill).
 * Pass cast_gain 0 for world-space models, whose shader probes the volume
 * itself. Both gains are 0 under the Retail tier, which yields black.
 */
[[nodiscard]] std::array<float, 3U> model_light_rgb(const ModelLightSample& sample,
                                                    float placed_gain,
                                                    float cast_gain) noexcept;

} // namespace battlespades::world
