#pragma once

#include "battlespades/world/vxl_map.hpp"

#include <array>
#include <cstdint>
#include <optional>
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
    /**
     * Bounded so a griefer cannot make every chunk re-mesh forever. Sized to
     * the server's static-light cap: 20thCenturyTown alone restores 524 map
     * flare markers before any player places one.
     */
    static constexpr std::size_t maximum_lights{2048U};
    /** Edge of the xy bucket grid that keeps `sample` O(nearby lights). */
    static constexpr std::uint32_t bucket_edge{8U};

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

    /** The one light retail's vertex kernel keeps for a point. */
    struct StrongestLight final {
        /** Light colour, c / 255. */
        std::array<float, 3U> rgb{};
        /** clamp(1 - d^2 / r^2, 0, 1). */
        float attenuation{};
        /** The light's voxel centre in canonical coordinates. */
        std::array<float, 3U> position{};
    };

    /**
     * Retail vxl.pyd 0x10022360 light selection: over every light within
     * reach, `att = clamp(1 - d^2/r^2, 0, 1)` measured from the light
     * voxel's centre, keeping the largest (`best <= att`, so a later light
     * wins a tie). Empty when no light reaches the point.
     */
    [[nodiscard]] std::optional<StrongestLight> strongest(float x, float y,
                                                          float z) const noexcept;

    /** True when a light sits in this exact cell (retail colour-entry flag +4). */
    [[nodiscard]] bool has_light_at(std::uint32_t x, std::uint32_t y,
                                    std::uint32_t z) const noexcept;

    /**
     * Every chunk a light at this cell can affect, for dirtying after an edit.
     *
     * The box is the radius inflated by one chunk, because a light just outside
     * a chunk still reaches vertices inside it.
     */
    [[nodiscard]] std::vector<ChunkKey> affected_chunks(const StaticLight& light,
                                                        std::uint32_t chunk_edge) const;

private:
    void rebuild_buckets();

    std::vector<StaticLight> lights_;
    /** Per 8x8 column bucket, the indices of lights whose cell lies in it. */
    std::vector<std::vector<std::uint16_t>> buckets_;
    float max_radius_{};
};

} // namespace battlespades::world
