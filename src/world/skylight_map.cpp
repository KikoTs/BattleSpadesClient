#include "battlespades/world/skylight_map.hpp"

#include <algorithm>

namespace battlespades::world {

SkylightMap::SkylightMap() {
    horizon_.assign(static_cast<std::size_t>(edge) * edge, open_sky);
}

std::uint8_t SkylightMap::compute_column(const VxlMap& map, std::uint32_t x,
                                         std::uint32_t y) const noexcept {
    // surface_z is the topmost solid voxel of the column, which is exactly the
    // height at which the sky stops reaching straight down.
    const auto surface = map.surface_z(x, y);
    return static_cast<std::uint8_t>(
        std::min<std::uint32_t>(surface, VxlMap::height - 1U));
}

void SkylightMap::rebuild(const VxlMap& map) {
    for (std::uint32_t y{}; y < edge; ++y) {
        for (std::uint32_t x{}; x < edge; ++x) {
            horizon_[(static_cast<std::size_t>(y) * edge) + x] = compute_column(map, x, y);
        }
    }
    dirty_ = true;
}

void SkylightMap::refresh_region(const VxlMap& map, std::uint32_t x0, std::uint32_t y0,
                                 std::uint32_t x1, std::uint32_t y1) {
    const auto clamp_edge = [](std::uint32_t value) {
        return std::min(value, edge - 1U);
    };
    const auto low_x = clamp_edge(std::min(x0, x1));
    const auto low_y = clamp_edge(std::min(y0, y1));
    const auto high_x = clamp_edge(std::max(x0, x1));
    const auto high_y = clamp_edge(std::max(y0, y1));
    for (std::uint32_t y = low_y; y <= high_y; ++y) {
        for (std::uint32_t x = low_x; x <= high_x; ++x) {
            auto& stored = horizon_[(static_cast<std::size_t>(y) * edge) + x];
            const auto computed = compute_column(map, x, y);
            if (stored != computed) {
                stored = computed;
                dirty_ = true;
            }
        }
    }
}

std::uint8_t SkylightMap::horizon_at(std::uint32_t x, std::uint32_t y) const noexcept {
    if (x >= edge || y >= edge) {
        return open_sky;
    }
    return horizon_[(static_cast<std::size_t>(y) * edge) + x];
}

} // namespace battlespades::world
