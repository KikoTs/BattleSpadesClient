#pragma once

#include "battlespades/world/vxl_map.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace battlespades::world {

struct ChunkKey;

/**
 * A light that lives at a fixed world position until its block is destroyed.
 *
 * Retail's flare block registered exactly this: `add_static_point_light` with
 * the placed block's own palette colour and a radius from
 * `FLAREBLOCK_LIGHT_RADIUS`, then baked the result into terrain vertex colours
 * and dirtied every chunk in the radius box. Block fire and block goo used the
 * same mechanism at a shorter radius.
 */
struct StaticLight final {
    std::array<std::uint32_t, 3U> cell{};
    VxlColor color{};
    float radius{5.0F};
};

/**
 * Bakeable point lights over the terrain.
 *
 * Deliberately CPU-side and sampled at mesh time rather than evaluated per
 * fragment. That is what retail did, and it suits this client for the same
 * reason: chunk meshes already rebuild incrementally on terrain edits, so a
 * light costs nothing per frame and any number of them can coexist. The
 * trade-off is that a light cannot move -- which is exactly the contract a
 * placed block has.
 *
 * Renderer-free, so the falloff and accumulation are unit-testable.
 */
class StaticLightField final {
public:
    /** Retail FLAREBLOCK_LIGHT_RADIUS. */
    static constexpr float flare_block_radius{5.0F};
    /** Retail block fire and block goo shared this shorter radius. */
    static constexpr float block_fire_radius{3.0F};
    /** Bounded so a griefer cannot make every chunk re-mesh forever. */
    static constexpr std::size_t maximum_lights{512U};

    /** Returns false when the position already holds a light or the pool is full. */
    [[nodiscard]] bool add(StaticLight light);
    /** Removes any light at this cell, e.g. when its block is destroyed. */
    [[nodiscard]] bool remove_at(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept;
    void clear() noexcept;

    [[nodiscard]] std::span<const StaticLight> lights() const noexcept { return lights_; }
    [[nodiscard]] bool empty() const noexcept { return lights_.empty(); }

    /**
     * Accumulated light arriving at one voxel corner, in 0..1 per channel.
     *
     * Sums contributions and clamps, rather than taking a maximum, so two lamps
     * side by side are brighter than one. Falloff is a smooth quadratic to a
     * hard cutoff at the radius: the hard edge is what makes the dirty-box
     * bound exact, and without it a light would subtly affect chunks the
     * re-mesh never touched.
     */
    [[nodiscard]] std::array<float, 3U> sample(float x, float y, float z) const noexcept;

    /**
     * Every chunk a light at this cell can affect, for dirtying after an edit.
     *
     * The box is the radius inflated by one chunk, because a light just outside
     * a chunk still reaches vertices inside it.
     */
    [[nodiscard]] std::vector<ChunkKey> affected_chunks(const StaticLight& light,
                                                        std::uint32_t chunk_edge) const;

private:
    std::vector<StaticLight> lights_;
};

} // namespace battlespades::world
