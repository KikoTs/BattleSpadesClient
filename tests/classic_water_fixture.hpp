#pragma once

#include "battlespades/world/vxl_map.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace battlespades::test {

/** Authored z=63 tiles, including genuine black with a zero baked-light byte. */
inline constexpr std::array<world::VxlColor, 3> classic_water_colors{{
    {144U, 24U, 12U, 0U}, {8U, 40U, 160U, 255U}, {0U, 0U, 0U, 0U}}};

[[nodiscard]] inline world::VxlColor classic_water_color(std::uint32_t x,
                                                         std::uint32_t y) {
    return classic_water_colors[(x / 2U + y / 2U) % classic_water_colors.size()];
}

[[nodiscard]] inline std::vector<std::byte> classic_water_bytes(bool empty_corner = false) {
    std::vector<std::byte> bytes;
    bytes.reserve(static_cast<std::size_t>(world::VxlMap::width) * world::VxlMap::depth * 8U);
    for (std::uint32_t y{}; y < world::VxlMap::depth; ++y) {
        for (std::uint32_t x{}; x < world::VxlMap::width; ++x) {
            if (empty_corner && x == 0U && y == 0U) {
                bytes.insert(bytes.end(), {std::byte{0}, std::byte{64}, std::byte{63}, std::byte{0}});
                continue;
            }
            const auto color = classic_water_color(x, y);
            bytes.insert(bytes.end(), {std::byte{0}, std::byte{63}, std::byte{63}, std::byte{0},
                std::byte{color.blue}, std::byte{color.green}, std::byte{color.red},
                std::byte{static_cast<std::uint8_t>((static_cast<unsigned>(color.alpha) + 1U) / 2U)}});
        }
    }
    return bytes;
}

} // namespace battlespades::test
