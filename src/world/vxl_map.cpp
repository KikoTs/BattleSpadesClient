#include "battlespades/world/vxl_map.hpp"

#include <algorithm>
#include <cmath>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>

namespace battlespades::world {
namespace {

constexpr std::size_t area = VxlMap::width * VxlMap::depth;
constexpr std::size_t voxel_count = area * VxlMap::height;
constexpr std::uint16_t no_surface = std::numeric_limits<std::uint16_t>::max();

[[nodiscard]] std::size_t index(std::uint32_t x, std::uint32_t y,
                                std::uint32_t z) noexcept {
    return x + y * VxlMap::width + z * area;
}

[[nodiscard]] std::uint32_t read_u32(std::span<const std::byte> bytes,
                                     std::size_t position) noexcept {
    return std::to_integer<std::uint32_t>(bytes[position]) |
           (std::to_integer<std::uint32_t>(bytes[position + 1U]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[position + 2U]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[position + 3U]) << 24U);
}

struct SourceShape final {
    std::uint32_t columns{};
    std::uint32_t maximum_z{};
};

[[nodiscard]] std::optional<SourceShape> inspect(std::span<const std::byte> bytes) {
    std::size_t position{};
    SourceShape shape{};
    while (position < bytes.size()) {
        if (bytes.size() - position < 4U) {
            return std::nullopt;
        }
        for (;;) {
            const auto words = std::to_integer<std::uint8_t>(bytes[position]);
            shape.maximum_z = std::max({shape.maximum_z,
                                        std::to_integer<std::uint32_t>(bytes[position + 1U]),
                                        std::to_integer<std::uint32_t>(bytes[position + 2U]),
                                        std::to_integer<std::uint32_t>(bytes[position + 3U])});
            if (words == 0U) {
                const auto top_start = std::to_integer<std::uint8_t>(bytes[position + 1U]);
                const auto top_end = std::to_integer<std::uint8_t>(bytes[position + 2U]);
                const auto top_words = top_end >= top_start ? top_end - top_start + 1U : 0U;
                const auto advance = 4U * (1U + top_words);
                if (advance > bytes.size() - position) {
                    return std::nullopt;
                }
                position += advance;
                break;
            }
            const auto advance = static_cast<std::size_t>(words) * 4U;
            if (advance > bytes.size() - position) {
                return std::nullopt;
            }
            position += advance;
            if (position >= bytes.size()) {
                return std::nullopt;
            }
        }
        ++shape.columns;
    }
    return position == bytes.size() ? std::optional{shape} : std::nullopt;
}

} // namespace

VxlLoadResult VxlMap::load_file(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        return {std::nullopt, "unable to open VXL file: " + path.string()};
    }
    std::vector<char> raw{std::istreambuf_iterator<char>{input}, {}};
    const auto bytes = std::as_bytes(std::span{raw});
    return load(bytes);
}

VxlLoadResult VxlMap::load(std::span<const std::byte> bytes) {
    const auto shape = inspect(bytes);
    if (!shape || shape->columns == 0U) {
        return {std::nullopt, "malformed or truncated VXL span stream"};
    }
    const auto edge = static_cast<std::uint32_t>(std::sqrt(shape->columns));
    if (edge * edge != shape->columns || edge > width || shape->maximum_z >= 241U) {
        return {std::nullopt, "VXL dimensions are incompatible with Battle Builder"};
    }

    VxlMap map;
    map.solid_bits_.assign((voxel_count + 7U) / 8U, 0U);
    map.colors_.assign(voxel_count, 0U);
    map.surfaces_.assign(area, no_surface);
    map.source_edge_ = edge;
    // Some canonical 240-high columns legally reference sentinel z=240 in
    // the fourth span-header byte. The server clamps their legacy-map shift
    // to zero. Unsigned `239 - 240` wrapped to UINT_MAX here, moving every
    // network MapSync voxel up one row and desynchronising collision.
    map.source_z_shift_ =
        shape->maximum_z < 239U ? 239U - shape->maximum_z : 0U;
    const auto offset = (width - edge) / 2U;
    std::size_t position{};

    for (std::uint32_t source_y{}; source_y < edge; ++source_y) {
        for (std::uint32_t source_x{}; source_x < edge; ++source_x) {
            const auto x = source_x + offset;
            const auto y = source_y + offset;
            bool has_surface{};
            std::uint32_t inherited_color{};
            for (;;) {
                if (bytes.size() - position < 4U) {
                    return {std::nullopt, "truncated VXL column header"};
                }
                const auto words = std::to_integer<std::uint8_t>(bytes[position]);
                const auto top_start = std::to_integer<std::uint8_t>(bytes[position + 1U]);
                const auto top_end = std::to_integer<std::uint8_t>(bytes[position + 2U]);
                position += 4U;
                const auto top_length = top_end >= top_start ? top_end - top_start + 1U : 0U;
                if (top_length * 4U > bytes.size() - position) {
                    return {std::nullopt, "truncated VXL surface colors"};
                }
                for (std::uint32_t item{}; item < top_length; ++item) {
                    inherited_color = read_u32(bytes, position + item * 4U);
                    map.put(x, y, top_start + map.source_z_shift_ + item, inherited_color);
                }
                position += top_length * 4U;
                has_surface = has_surface || top_length != 0U;
                if (words == 0U) {
                    if (has_surface) {
                        for (std::uint32_t z = top_end + 1U;
                             z + map.source_z_shift_ < height; ++z) {
                            map.put(x, y, z + map.source_z_shift_, inherited_color);
                        }
                    }
                    break;
                }
                if (words < top_length + 1U) {
                    return {std::nullopt, "invalid VXL span word count"};
                }
                const auto bottom_length = words - top_length - 1U;
                if (bottom_length * 4U + 4U > bytes.size() - position) {
                    return {std::nullopt, "truncated VXL cave span"};
                }
                const auto next_air = std::to_integer<std::uint8_t>(
                    bytes[position + bottom_length * 4U + 3U]);
                if (bottom_length > next_air) {
                    return {std::nullopt, "invalid VXL cave ceiling"};
                }
                const auto bottom_start = next_air - bottom_length;
                if (bottom_start < top_end + 1U) {
                    return {std::nullopt, "overlapping VXL spans"};
                }
                for (std::uint32_t z = top_end + 1U; z < bottom_start; ++z) {
                    map.put(x, y, z + map.source_z_shift_, inherited_color);
                }
                for (std::uint32_t item{}; item < bottom_length; ++item) {
                    const auto color = read_u32(bytes, position + item * 4U);
                    map.put(x, y, bottom_start + map.source_z_shift_ + item, color);
                }
                position += bottom_length * 4U;
            }
        }
    }
    if (position != bytes.size()) {
        return {std::nullopt, "VXL stream has trailing columns"};
    }
    // Retail always installs a collision bed, including empty water columns.
    for (std::uint32_t y{}; y < depth; ++y) {
        for (std::uint32_t x{}; x < width; ++x) {
            map.put(x, y, height - 1U, 0U);
        }
    }
    return {std::move(map), {}};
}

void VxlMap::put(std::uint32_t x, std::uint32_t y, std::uint32_t z,
                 std::uint32_t color_value) noexcept {
    if (x >= width || y >= depth || z >= height) {
        return;
    }
    const auto voxel = index(x, y, z);
    const auto mask = static_cast<std::uint8_t>(1U << (voxel & 7U));
    if ((solid_bits_[voxel >> 3U] & mask) == 0U) {
        solid_bits_[voxel >> 3U] |= mask;
        ++solid_voxels_;
    }
    colors_[voxel] = color_value;
    auto& surface = surfaces_[x + y * width];
    surface = std::min(surface, static_cast<std::uint16_t>(z));
}

bool VxlMap::solid(std::uint32_t x, std::uint32_t y, std::uint32_t z) const noexcept {
    if (x >= width || y >= depth || z >= height) {
        return false;
    }
    const auto voxel = index(x, y, z);
    return (solid_bits_[voxel >> 3U] & (1U << (voxel & 7U))) != 0U;
}

std::optional<VxlColor> VxlMap::color(std::uint32_t x, std::uint32_t y,
                                      std::uint32_t z) const noexcept {
    if (!solid(x, y, z)) {
        return std::nullopt;
    }
    const auto value = colors_[index(x, y, z)];
    const auto alpha_byte = static_cast<std::uint8_t>(value >> 24U);
    return VxlColor{static_cast<std::uint8_t>(value >> 16U),
                    static_cast<std::uint8_t>(value >> 8U),
                    static_cast<std::uint8_t>(value),
                    alpha_byte == 0U
                        ? static_cast<std::uint8_t>(0U)
                        : static_cast<std::uint8_t>(alpha_byte * 2U - 1U)};
}

std::uint16_t VxlMap::surface_z(std::uint32_t x, std::uint32_t y) const noexcept {
    return x < width && y < depth ? surfaces_[x + y * width] : no_surface;
}

bool VxlMap::set_voxel(std::uint32_t x, std::uint32_t y, std::uint32_t z,
                       VxlColor color_value) noexcept {
    if (x >= width || y >= depth || z >= height) {
        return false;
    }
    // Round-trips through color(): the stored alpha byte a decodes as a*2-1.
    const auto alpha_byte =
        static_cast<std::uint32_t>((static_cast<std::uint32_t>(color_value.alpha) + 1U) / 2U);
    put(x, y, z,
        (alpha_byte << 24U) | (static_cast<std::uint32_t>(color_value.red) << 16U) |
            (static_cast<std::uint32_t>(color_value.green) << 8U) |
            static_cast<std::uint32_t>(color_value.blue));
    damage_.erase(static_cast<std::uint32_t>(index(x, y, z)));
    ++revision_;
    return true;
}

bool VxlMap::clear_voxel(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
    if (x >= width || y >= depth || z + 1U >= height || !solid(x, y, z)) {
        return false;
    }
    const auto voxel = index(x, y, z);
    solid_bits_[voxel >> 3U] &= static_cast<std::uint8_t>(~(1U << (voxel & 7U)));
    colors_[voxel] = 0U;
    damage_.erase(static_cast<std::uint32_t>(voxel));
    --solid_voxels_;
    auto& surface = surfaces_[x + y * width];
    if (surface == z) {
        surface = no_surface;
        for (auto below = z + 1U; below < height; ++below) {
            if (solid(x, y, below)) {
                surface = static_cast<std::uint16_t>(below);
                break;
            }
        }
    }
    ++revision_;
    return true;
}

std::uint64_t VxlMap::revision() const noexcept { return revision_; }

float VxlMap::damage_fraction(std::uint32_t x, std::uint32_t y,
                              std::uint32_t z) const noexcept {
    if (!solid(x, y, z)) {
        return 0.0F;
    }
    const auto found = damage_.find(static_cast<std::uint32_t>(index(x, y, z)));
    return found == damage_.end() ? 0.0F : found->second;
}

bool VxlMap::set_damage_fraction(std::uint32_t x, std::uint32_t y,
                                 std::uint32_t z, float fraction) noexcept {
    if (x >= width || y >= depth || z + 1U >= height || !solid(x, y, z) ||
        !std::isfinite(fraction)) {
        return false;
    }
    if (fraction <= 0.0F) {
        return clear_damage(x, y, z);
    }
    const float bounded = std::min(fraction, std::nextafter(1.0F, 0.0F));
    const auto key = static_cast<std::uint32_t>(index(x, y, z));
    const auto found = damage_.find(key);
    if (found != damage_.end() && found->second == bounded) {
        return false;
    }
    damage_[key] = bounded;
    ++revision_;
    return true;
}

bool VxlMap::clear_damage(std::uint32_t x, std::uint32_t y,
                          std::uint32_t z) noexcept {
    if (x >= width || y >= depth || z >= height) {
        return false;
    }
    if (damage_.erase(static_cast<std::uint32_t>(index(x, y, z))) == 0U) {
        return false;
    }
    ++revision_;
    return true;
}

std::uint64_t VxlMap::solid_voxels() const noexcept { return solid_voxels_; }
std::uint32_t VxlMap::source_edge() const noexcept { return source_edge_; }
std::uint32_t VxlMap::source_z_shift() const noexcept { return source_z_shift_; }

} // namespace battlespades::world
