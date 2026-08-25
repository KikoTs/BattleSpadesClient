#pragma once

#include "battlespades/world/vxl_map.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace battlespades::world {

/**
 * Per-column skylight horizon: how far down the sky can still reach.
 *
 * Ambient light in this renderer is hemispheric skylight, and until now every
 * surface received all of it — so the inside of a bunker was lit exactly as
 * brightly as the roof above it. Interiors are dark in reality because the sky
 * is occluded, and in a voxel world that occlusion is almost free to compute:
 * one height per column is enough to know whether a point is under cover.
 *
 * Stores the topmost solid voxel of each column, so a fragment deeper than its
 * column's horizon is indoors or underground and its skylight is attenuated by
 * how far below cover it sits. Canonical z grows downward, so a SMALLER stored
 * value means the cover is higher up.
 *
 * Deliberately renderer-free: this is a pure function of the map, so the
 * geometry is unit-testable without a GPU.
 */
class SkylightMap final {
public:
    static constexpr std::uint32_t edge{VxlMap::width};
    /** No sky reaches a column whose horizon is this; used for empty columns. */
    static constexpr std::uint8_t open_sky{0U};

    SkylightMap();

    /** Recomputes every column. Cheap enough for a map load, not per frame. */
    void rebuild(const VxlMap& map);

    /**
     * Recomputes one square region, for a terrain edit.
     *
     * Digging a hole in a roof must brighten the room below it, so an edit has
     * to refresh the affected columns rather than wait for a full rebuild.
     */
    void refresh_region(const VxlMap& map, std::uint32_t x0, std::uint32_t y0,
                        std::uint32_t x1, std::uint32_t y1);

    /** One byte per column, row-major in x-major order, values 0..239. */
    [[nodiscard]] std::span<const std::uint8_t> data() const noexcept { return horizon_; }
    [[nodiscard]] std::uint8_t horizon_at(std::uint32_t x, std::uint32_t y) const noexcept;

    /** True once any column changed, so the renderer knows to re-upload. */
    [[nodiscard]] bool dirty() const noexcept { return dirty_; }
    void clear_dirty() noexcept { dirty_ = false; }

private:
    [[nodiscard]] std::uint8_t compute_column(const VxlMap& map, std::uint32_t x,
                                              std::uint32_t y) const noexcept;

    std::vector<std::uint8_t> horizon_;
    bool dirty_{true};
};

} // namespace battlespades::world
