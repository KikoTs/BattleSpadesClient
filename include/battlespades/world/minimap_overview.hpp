#pragma once

#include "battlespades/world/vxl_map.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace battlespades::world {

/**
 * Builds retail's one-texel-per-column overhead colour image.
 *
 * Output rows use the client's top-left convention: pixel (x,y) represents
 * world column (x,y). Retail stores the same image vertically flipped because
 * OpenGL uploads begin at the lower-left; keeping the final visual convention
 * here avoids flipping it again in every UI crop.
 */
[[nodiscard]] std::vector<std::uint8_t>
build_minimap_overview_rgba(const VxlMap& map);

/**
 * Refreshes an inclusive world-column rectangle in an existing 512x512 RGBA
 * overview. Invalid buffers fail closed and leave their contents unchanged.
 */
[[nodiscard]] bool refresh_minimap_overview_rgba(
    const VxlMap& map, std::span<std::uint8_t> pixels,
    std::uint32_t minimum_x, std::uint32_t minimum_y,
    std::uint32_t maximum_x, std::uint32_t maximum_y) noexcept;

} // namespace battlespades::world
