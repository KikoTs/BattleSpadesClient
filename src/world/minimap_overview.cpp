#include "battlespades/world/minimap_overview.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>

namespace battlespades::world {
namespace {

constexpr std::size_t channels{4U};
constexpr std::size_t pixel_count{
    static_cast<std::size_t>(VxlMap::width) * VxlMap::depth};
constexpr std::size_t byte_count{pixel_count * channels};
constexpr auto no_surface = std::numeric_limits<std::uint16_t>::max();

void write_column(const VxlMap& map, std::span<std::uint8_t> pixels,
                  std::uint32_t x, std::uint32_t y) noexcept {
    const auto destination =
        (static_cast<std::size_t>(y) * VxlMap::width + x) * channels;
    const auto surface = map.surface_z(x, y);
    auto color =
        surface == no_surface || surface >= VxlMap::height
            ? std::optional<VxlColor>{}
            : map.color(x, y, surface);
    // Open water is the z=239 collision bed. The VXL finaliser gives every
    // bed cell one map-wide colour, the ground table's last row (stock
    // (40,54,64) slate blue); the loader stores 0 there, so without this the
    // retail slate-blue sea and rivers drew black.
    if (surface == VxlMap::height - 1U && map.has_ground_color_table()) {
        const auto rgb = map.ground_table_rgb(surface);
        color = VxlColor{static_cast<std::uint8_t>(rgb >> 16U),
                         static_cast<std::uint8_t>(rgb >> 8U),
                         static_cast<std::uint8_t>(rgb), 255U};
    }
    // The native generator writes RGB from get_color and uploads RGBA. Its
    // alpha initialization is outside the recovered loop; the resulting map
    // texture is opaque in retail, so make that invariant explicit.
    pixels[destination + 0U] = color.has_value() ? color->red : 0U;
    pixels[destination + 1U] = color.has_value() ? color->green : 0U;
    pixels[destination + 2U] = color.has_value() ? color->blue : 0U;
    pixels[destination + 3U] = 255U;
}

} // namespace

std::vector<std::uint8_t> build_minimap_overview_rgba(const VxlMap& map) {
    std::vector<std::uint8_t> pixels(byte_count, 0U);
    static_cast<void>(refresh_minimap_overview_rgba(
        map, pixels, 0U, 0U, VxlMap::width - 1U, VxlMap::depth - 1U));
    return pixels;
}

bool refresh_minimap_overview_rgba(
    const VxlMap& map, std::span<std::uint8_t> pixels,
    std::uint32_t minimum_x, std::uint32_t minimum_y,
    std::uint32_t maximum_x, std::uint32_t maximum_y) noexcept {
    if (pixels.size() != byte_count || minimum_x >= VxlMap::width ||
        minimum_y >= VxlMap::depth || minimum_x > maximum_x ||
        minimum_y > maximum_y) {
        return false;
    }
    maximum_x = std::min(maximum_x, VxlMap::width - 1U);
    maximum_y = std::min(maximum_y, VxlMap::depth - 1U);
    for (auto y = minimum_y; y <= maximum_y; ++y) {
        for (auto x = minimum_x; x <= maximum_x; ++x) {
            write_column(map, pixels, x, y);
        }
    }
    return true;
}

} // namespace battlespades::world
