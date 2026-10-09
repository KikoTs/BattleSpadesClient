#include "battlespades/world/vxl_map.hpp"
#include "battlespades/world/map_catalog.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <string_view>
#include <tuple>

namespace battlespades::world {
namespace {

constexpr std::size_t area = VxlMap::width * VxlMap::depth;
constexpr std::size_t voxel_count = area * VxlMap::height;
constexpr std::uint16_t no_surface = std::numeric_limits<std::uint16_t>::max();
// A legal 240-high stream needs at most one color and one span header per
// voxel. Bound files before reading them, rather than allocating arbitrary
// input and another 267 MiB of terrain before discovering malformed spans.
constexpr std::size_t maximum_source_bytes = voxel_count * 8U;

[[nodiscard]] std::size_t index(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
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
    std::uint32_t maximum_colored_z{};
};

[[nodiscard]] std::optional<SourceShape> inspect(std::span<const std::byte> bytes) {
    std::size_t position{};
    SourceShape shape{};
    while (position < bytes.size()) {
        if (shape.columns == area || bytes.size() - position < 4U) {
            return std::nullopt;
        }
        for (;;) {
            const auto words = std::to_integer<std::uint8_t>(bytes[position]);
            const auto top_start = std::to_integer<std::uint32_t>(bytes[position + 1U]);
            const auto top_end = std::to_integer<std::uint32_t>(bytes[position + 2U]);
            // Empty tops use the adjacent sentinel pair (height, height-1).
            if (top_start > top_end + 1U)
                return std::nullopt;
            const auto top_words = top_end >= top_start ? top_end - top_start + 1U : 0U;
            if (top_words != 0U) {
                shape.maximum_colored_z = std::max(shape.maximum_colored_z, top_end);
            }
            shape.maximum_z = std::max({shape.maximum_z,
                                        top_start,
                                        top_end,
                                        std::to_integer<std::uint32_t>(bytes[position + 3U])});
            if (words == 0U) {
                const auto advance = 4U * (1U + top_words);
                if (advance > bytes.size() - position) {
                    return std::nullopt;
                }
                position += advance;
                break;
            }
            const auto advance = static_cast<std::size_t>(words) * 4U;
            if (words < top_words + 1U || advance > bytes.size() - position) {
                return std::nullopt;
            }
            const auto bottom_words = words - top_words - 1U;
            position += advance;
            // The next span header reads four bytes. A non-terminal span that
            // left only 1..3 bytes used to over-read the heap buffer (fuzz
            // audit 2026-09-29, payload 01 f9 03 00 0a 00).
            if (bytes.size() - position < 4U) {
                return std::nullopt;
            }
            const auto next_start = std::to_integer<std::uint32_t>(bytes[position + 1U]);
            const auto next_air = std::to_integer<std::uint32_t>(bytes[position + 3U]);
            if (bottom_words > next_air || next_air - bottom_words < top_end + 1U ||
                next_start < next_air || next_start <= top_start) {
                return std::nullopt;
            }
            if (bottom_words != 0U) {
                shape.maximum_colored_z = std::max(shape.maximum_colored_z, next_air - 1U);
            }
        }
        ++shape.columns;
    }
    return position == bytes.size() ? std::optional{shape} : std::nullopt;
}

[[nodiscard]] std::optional<std::string> assignment_format(std::string_view text) {
    // Recognize the inert, top-level legacy metadata spelling only. Scan past
    // comments, quoted strings and bracketed data so an example in a docstring
    // or a nested dictionary cannot accidentally select the map's format.
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3U);
    std::optional<std::string> result;
    std::size_t depth{};
    bool line_start{true};
    for (std::size_t position{}; position < text.size();) {
        const auto current = text[position];
        if (current == '\n') {
            line_start = true;
            ++position;
            continue;
        }
        if (line_start && depth == 0U && text.substr(position).starts_with("vxl_format")) {
            auto line = text.substr(position + 10U);
            line = line.substr(0U, line.find('\n'));
            const auto trim = [](std::string_view value) {
                const auto first = value.find_first_not_of(" \t\r");
                return first == std::string_view::npos ? std::string_view{} : value.substr(first);
            };
            line = trim(line);
            if (line.starts_with('=') && !line.starts_with("==")) {
                line = trim(line.substr(1U));
                if (!line.empty() && (line.front() == '\'' || line.front() == '"')) {
                    const auto end = line.find(line.front(), 1U);
                    if (end != std::string_view::npos &&
                        line.substr(1U, end - 1U).find('\\') == std::string_view::npos) {
                        const auto tail = trim(line.substr(end + 1U));
                        if (tail.empty() || tail.starts_with('#')) {
                            // Python metadata takes the last assignment in a
                            // file; sibling sidecars use first-key precedence.
                            result = std::string{line.substr(1U, end - 1U)};
                        }
                    }
                }
            }
        }
        line_start = false;
        if (current == '#') {
            const auto end = text.find('\n', position);
            position = end == std::string_view::npos ? text.size() : end;
        } else if (current == '\'' || current == '"') {
            const auto triple = position + 2U < text.size() &&
                                text[position + 1U] == current && text[position + 2U] == current;
            const std::size_t delimiter_size = triple ? 3U : 1U;
            const auto delimiter = text.substr(position, delimiter_size);
            position += delimiter_size;
            while (position < text.size()) {
                if (text[position] == '\\') {
                    position += std::min<std::size_t>(2U, text.size() - position);
                } else if (text.substr(position).starts_with(delimiter)) {
                    position += delimiter_size;
                    break;
                } else {
                    ++position;
                }
            }
        } else {
            if (current == '(' || current == '[' || current == '{') ++depth;
            if ((current == ')' || current == ']' || current == '}') && depth > 0U) --depth;
            ++position;
        }
    }
    return result;
}

[[nodiscard]] std::optional<VxlDecodeProfile> disk_profile(const std::filesystem::path& path,
                                                           std::string& error) {
    // Match server metadata precedence; files without this key do not mask
    // another sibling's value. Nothing in a legacy map script is executed.
    for (const auto* extension : {".json", ".ugc", ".txt", ".vxl.json"}) {
        auto sidecar = path;
        sidecar.replace_extension(extension);
        std::error_code code;
        const auto size = std::filesystem::file_size(sidecar, code);
        if (code || size > 1024U * 1024U)
            continue;
        std::ifstream input{sidecar, std::ios::binary};
        std::string text(static_cast<std::size_t>(size), '\0');
        if (!input.read(text.data(), static_cast<std::streamsize>(text.size()))) continue;
        const auto document = nlohmann::json::parse(text, nullptr, false);
        std::optional<std::string> format;
        if (document.is_object() && document.contains("vxl_format")) {
            const auto& value = document["vxl_format"];
            format = value.is_string() ? value.get<std::string>() : std::string{};
        } else if (document.is_discarded()) {
            format = assignment_format(text);
        }
        if (!format) continue;
        std::ranges::transform(*format, format->begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (*format == "auto") return VxlDecodeProfile::automatic;
        if (*format == "retail") return VxlDecodeProfile::retail;
        if (*format == "classic64") return VxlDecodeProfile::classic64;
        error = "vxl_format must be auto, retail or classic64: " + sidecar.string();
        return std::nullopt;
    }
    auto stem = path.stem().string();
    std::ranges::transform(
        stem, stem.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    // 20thCenturyTown is a stock 64-high retail map with authored flare
    // markers, although the environment catalog lists its WW1 alias instead.
    return find_official_map_environment(stem).has_value() || stem == "20thcenturytown"
               ? VxlDecodeProfile::retail
               : VxlDecodeProfile::automatic;
}

} // namespace

VxlLoadResult VxlMap::load_file(const std::filesystem::path& path, VxlDecodeProfile profile) {
    std::ifstream input{path, std::ios::binary | std::ios::ate};
    if (!input) {
        return {std::nullopt, "unable to open VXL file: " + path.string()};
    }
    const auto size = input.tellg();
    if (size <= 0 || static_cast<std::uint64_t>(size) > maximum_source_bytes) {
        return {std::nullopt, "empty or oversized VXL file: " + path.string()};
    }
    input.seekg(0);
    std::vector<char> raw(static_cast<std::size_t>(size));
    if (!input.read(raw.data(), static_cast<std::streamsize>(raw.size()))) {
        return {std::nullopt, "unable to read complete VXL file: " + path.string()};
    }
    if (profile == VxlDecodeProfile::automatic) {
        std::string error;
        const auto resolved = disk_profile(path, error);
        if (!resolved)
            return {std::nullopt, std::move(error)};
        profile = *resolved;
    }
    const auto bytes = std::as_bytes(std::span{raw});
    return load(bytes, profile);
}

VxlLoadResult VxlMap::load(std::span<const std::byte> bytes, VxlDecodeProfile profile) {
    if (bytes.size() > maximum_source_bytes) {
        return {std::nullopt, "oversized VXL span stream"};
    }
    const auto shape = inspect(bytes);
    if (!shape || shape->columns == 0U) {
        return {std::nullopt, "malformed or truncated VXL span stream"};
    }
    const auto edge = static_cast<std::uint32_t>(std::sqrt(shape->columns));
    if (profile == VxlDecodeProfile::automatic) {
        profile =
            shape->columns == area && shape->maximum_z <= 64U && shape->maximum_colored_z < 64U
                ? VxlDecodeProfile::classic64
                : VxlDecodeProfile::retail;
    }
    if (profile == VxlDecodeProfile::classic64 &&
        (shape->columns != area || shape->maximum_z > 64U || shape->maximum_colored_z >= 64U)) {
        return {std::nullopt, "Classic VXL must be 512 x 512 x 64"};
    }
    if (profile == VxlDecodeProfile::canonical240 && shape->columns != area) {
        return {std::nullopt, "MapSync VXL must be 512 x 512 x 240"};
    }
    if (edge * edge != shape->columns || edge > width || shape->maximum_z > height ||
        shape->maximum_colored_z >= height) {
        return {std::nullopt, "VXL dimensions are incompatible with Battle Builder"};
    }

    VxlMap map;
    map.solid_bits_.assign((voxel_count + 7U) / 8U, 0U);
    map.colors_.assign(voxel_count, 0U);
    map.implicit_bits_.assign((voxel_count + 7U) / 8U, 0U);
    map.surfaces_.assign(area, no_surface);
    map.chunk_solids_.assign(std::size_t{(width / 16U) * (depth / 16U) * (height / 16U)}, 0U);
    map.source_edge_ = edge;
    // Some canonical 240-high columns legally reference sentinel z=240 in
    // the fourth span-header byte. The server clamps their legacy-map shift
    // to zero. Unsigned `239 - 240` wrapped to UINT_MAX here, moving every
    // network MapSync voxel up one row and desynchronising collision.
    map.source_z_shift_ = profile == VxlDecodeProfile::classic64      ? 176U
                          : profile == VxlDecodeProfile::canonical240 ? 0U
                          : shape->maximum_z < 239U                   ? 239U - shape->maximum_z
                                                                      : 0U;
    const auto offset = (width - edge) / 2U;
    std::size_t position{};
    // Explicit colour words only: vxl.pyd matches chroma markers against the
    // stored colour records, never against implicit interior voxels.
    std::vector<std::uint32_t> marker_candidates;
    const auto note_marker =
        [&](std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t color_value) {
            if (profile == VxlDecodeProfile::retail && z <= max_damageable_z &&
                is_vxl_chroma_marker(color_value)) {
                marker_candidates.push_back(static_cast<std::uint32_t>(index(x, y, z)));
            }
        };

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
                    note_marker(x, y, top_start + map.source_z_shift_ + item, inherited_color);
                }
                position += top_length * 4U;
                has_surface = has_surface || top_length != 0U;
                if (words == 0U) {
                    if (has_surface) {
                        for (std::uint32_t z = top_end + 1U; z + map.source_z_shift_ < height;
                             ++z) {
                            map.put_implicit(x, y, z + map.source_z_shift_, inherited_color);
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
                const auto next_air =
                    std::to_integer<std::uint8_t>(bytes[position + bottom_length * 4U + 3U]);
                if (bottom_length > next_air) {
                    return {std::nullopt, "invalid VXL cave ceiling"};
                }
                const auto bottom_start = next_air - bottom_length;
                if (bottom_start < top_end + 1U) {
                    return {std::nullopt, "overlapping VXL spans"};
                }
                for (std::uint32_t z = top_end + 1U; z < bottom_start; ++z) {
                    map.put_implicit(x, y, z + map.source_z_shift_, inherited_color);
                }
                for (std::uint32_t item{}; item < bottom_length; ++item) {
                    const auto color = read_u32(bytes, position + item * 4U);
                    map.put(x, y, bottom_start + map.source_z_shift_ + item, color);
                    note_marker(x, y, bottom_start + map.source_z_shift_ + item, color);
                }
                position += bottom_length * 4U;
            }
        }
    }
    if (position != bytes.size()) {
        return {std::nullopt, "VXL stream has trailing columns"};
    }
    // Retail always installs a collision bed, including empty water columns.
    // vxl.pyd sub_10029900 (map finaliser 0x1002A380) sets every z=239 cell
    // solid AND overwrites its colour with one map-wide value, so authored
    // z=239 colours are intentionally discarded here as well.
    for (std::uint32_t y{}; y < depth; ++y) {
        for (std::uint32_t x{}; x < width; ++x) {
            map.put(x, y, height - 1U, 0U);
        }
    }
    // The finaliser runs the chroma-marker cleanup right after the bed.
    if (profile == VxlDecodeProfile::retail)
        map.remove_chroma_markers(marker_candidates);
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
        count_chunk_solid(x, y, z, true);
    }
    colors_[voxel] = color_value;
    clear_implicit(voxel);
    auto& surface = surfaces_[x + y * width];
    surface = std::min(surface, static_cast<std::uint16_t>(z));
}

void VxlMap::put_implicit(std::uint32_t x, std::uint32_t y, std::uint32_t z,
                          std::uint32_t color_value) noexcept {
    put(x, y, z, color_value);
    if (x < width && y < depth && z < height && !implicit_bits_.empty()) {
        const auto voxel = index(x, y, z);
        implicit_bits_[voxel >> 3U] |= static_cast<std::uint8_t>(1U << (voxel & 7U));
    }
}

void VxlMap::clear_implicit(std::size_t voxel) noexcept {
    if (!implicit_bits_.empty()) {
        implicit_bits_[voxel >> 3U] &= static_cast<std::uint8_t>(~(1U << (voxel & 7U)));
    }
}

bool VxlMap::implicit_interior(std::uint32_t x, std::uint32_t y,
                               std::uint32_t z) const noexcept {
    if (x >= width || y >= depth || z >= height || implicit_bits_.empty()) {
        return false;
    }
    const auto voxel = index(x, y, z);
    return (implicit_bits_[voxel >> 3U] & (1U << (voxel & 7U))) != 0U;
}

std::uint32_t VxlMap::color_word(std::size_t voxel, std::uint32_t x, std::uint32_t y,
                                 std::uint32_t z) const noexcept {
    const auto stored = colors_[voxel];
    if (!ground_table_set_ || implicit_bits_.empty() ||
        (implicit_bits_[voxel >> 3U] & (1U << (voxel & 7U))) == 0U) {
        return stored;
    }
    // vxl.pyd 0x10029C80 / 0x10003390: table[z] + 0x010101 * (rand() & 3),
    // skipped on the x == 0 / y == 0 edges and the z == 239 bed. The add is
    // unclamped, so a channel at 253..255 carries into its neighbour exactly
    // as retail's does. A position hash stands in for rand(): stable across
    // re-meshes, same 0..3 distribution.
    std::uint32_t rgb = ground_table_[z];
    if (x != 0U && y != 0U && z != height - 1U) {
        auto hash = (x * 73856093U) ^ (y * 19349663U) ^ (z * 83492791U);
        hash ^= hash >> 13U;
        hash *= 0x5bd1e995U;
        hash ^= hash >> 15U;
        rgb += 0x010101U * (hash & 3U);
    }
    return (stored & 0xFF000000U) | (rgb & 0x00FFFFFFU);
}

std::array<std::uint32_t, VxlMap::height> VxlMap::generate_ground_color_table(
    std::span<const std::array<std::uint8_t, 4U>> rows) noexcept {
    std::array<std::uint32_t, height> table{};
    const auto pack = [](const std::array<std::uint8_t, 4U>& row) {
        return (static_cast<std::uint32_t>(row[0U]) << 16U) |
               (static_cast<std::uint32_t>(row[1U]) << 8U) |
               static_cast<std::uint32_t>(row[2U]);
    };
    // The binary's cursor walks an int and writes table[cursor]; bound it to
    // the table (retail itself overruns for z >= 240 rows).
    std::int32_t cursor{};
    const std::array<std::uint8_t, 4U>* previous = nullptr;
    for (const auto& row : rows) {
        const auto target = static_cast<std::int32_t>(row[3U]);
        if (previous == nullptr) {
            for (; cursor <= target; ++cursor) {
                if (cursor < static_cast<std::int32_t>(height)) {
                    table[static_cast<std::size_t>(cursor)] = pack(row);
                }
            }
        } else {
            const auto previous_z = static_cast<std::int32_t>((*previous)[3U]);
            for (; cursor <= target; ++cursor) {
                const double t = static_cast<double>(target - cursor) /
                                 static_cast<double>(target - previous_z);
                const double u = 1.0 - t;
                const auto channel = [&](std::size_t c) {
                    return static_cast<std::uint32_t>(static_cast<std::int32_t>(
                               t * static_cast<double>((*previous)[c]) +
                               u * static_cast<double>(row[c]))) &
                           0xFFU;
                };
                if (cursor < static_cast<std::int32_t>(height)) {
                    table[static_cast<std::size_t>(cursor)] =
                        (channel(0U) << 16U) | (channel(1U) << 8U) | channel(2U);
                }
            }
        }
        previous = &row;
    }
    if (previous != nullptr) {
        // The tail loop writes table[cursor - 1 .. 238] with the last row;
        // 239 keeps its earlier value.
        for (auto z = std::max<std::int32_t>(cursor - 1, 0);
             z < static_cast<std::int32_t>(height) - 1; ++z) {
            table[static_cast<std::size_t>(z)] = pack(*previous);
        }
    }
    return table;
}

void VxlMap::set_ground_colors(std::span<const std::array<std::uint8_t, 4U>> rows) noexcept {
    if (rows.empty()) {
        ground_table_set_ = false;
        ground_table_ = {};
    } else {
        ground_table_ = generate_ground_color_table(rows);
        ground_table_set_ = true;
    }
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
    const auto value = color_word(index(x, y, z), x, y, z);
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

void VxlMap::write_rgb(std::uint32_t x, std::uint32_t y, std::uint32_t z,
                       VxlColor color_value) noexcept {
    const auto voxel = index(x, y, z);
    // Writing a colour makes the cell explicit (retail colours it on exposure).
    clear_implicit(voxel);
    auto& stored = colors_[voxel];
    // Keep the stored alpha byte: it is baked VXL lighting, not part of the
    // RGB that damage and paint rewrite.
    stored = (stored & 0xFF000000U) |
             (static_cast<std::uint32_t>(color_value.red) << 16U) |
             (static_cast<std::uint32_t>(color_value.green) << 8U) |
             static_cast<std::uint32_t>(color_value.blue);
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
    const auto key = static_cast<std::uint32_t>(index(x, y, z));
    damaged_.erase(key);
    user_health_.erase(key);
    ++revision_;
    return true;
}

void VxlMap::remove_voxel_bits(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
    const auto voxel = index(x, y, z);
    solid_bits_[voxel >> 3U] &= static_cast<std::uint8_t>(~(1U << (voxel & 7U)));
    colors_[voxel] = 0U;
    clear_implicit(voxel);
    damaged_.erase(static_cast<std::uint32_t>(voxel));
    user_health_.erase(static_cast<std::uint32_t>(voxel));
    --solid_voxels_;
    count_chunk_solid(x, y, z, false);
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
}

bool VxlMap::clear_voxel(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
    if (x >= width || y >= depth || z + 1U >= height || !solid(x, y, z)) {
        return false;
    }
    remove_voxel_bits(x, y, z);
    ++revision_;
    return true;
}

void VxlMap::remove_chroma_markers(std::vector<std::uint32_t>& candidates) noexcept {
    // vxl.pyd sub_10029FD0 walks y, then x, then z (ascending) and edits in
    // place. Visit the recorded explicit words in exactly that order.
    const auto coordinates = [](std::uint32_t voxel) {
        const auto x = voxel % width;
        const auto y = (voxel / width) % depth;
        const auto z = voxel / static_cast<std::uint32_t>(area);
        return std::array<std::uint32_t, 3U>{x, y, z};
    };
    std::sort(candidates.begin(), candidates.end(),
              [&](std::uint32_t left, std::uint32_t right) {
                  const auto a = coordinates(left);
                  const auto b = coordinates(right);
                  return std::tie(a[1U], a[0U], a[2U]) < std::tie(b[1U], b[0U], b[2U]);
              });
    constexpr std::array<std::array<std::int32_t, 2U>, 4U> neighbour_order{{
        {{0, 1}}, {{0, -1}}, {{1, 0}}, {{-1, 0}},
    }};
    for (const auto voxel : candidates) {
        const auto cell = coordinates(voxel);
        const auto x = cell[0U];
        const auto y = cell[1U];
        const auto z = cell[2U];
        // The colour must still be a marker when the walk reaches it: an
        // earlier removal may have repainted this cell from a neighbour.
        if (!solid(x, y, z) || !is_vxl_chroma_marker(colors_[voxel])) continue;
        // Both cells above must be air (cells above the map count as air).
        if (z >= 1U && solid(x, y, z - 1U)) continue;
        if (z >= 2U && solid(x, y, z - 2U)) continue;
        remove_voxel_bits(x, y, z);
        const auto below = z + 1U;
        if (below >= height || !solid(x, y, below)) continue;
        // The newly exposed voxel below takes the first solid neighbour
        // colour at its own height, in the binary's +y, -y, +x, -x order.
        for (const auto& offset : neighbour_order) {
            const auto nx = static_cast<std::int64_t>(x) + offset[0U];
            const auto ny = static_cast<std::int64_t>(y) + offset[1U];
            if (nx < 0 || ny < 0 || nx >= static_cast<std::int64_t>(width) ||
                ny >= static_cast<std::int64_t>(depth)) {
                continue;
            }
            const auto ux = static_cast<std::uint32_t>(nx);
            const auto uy = static_cast<std::uint32_t>(ny);
            if (!solid(ux, uy, below)) continue;
            const auto target = index(x, y, below);
            colors_[target] = color_word(index(ux, uy, below), ux, uy, below);
            clear_implicit(target);
            break;
        }
    }
}

std::uint64_t VxlMap::revision() const noexcept { return revision_; }

float VxlMap::damage_fraction(std::uint32_t x, std::uint32_t y,
                              std::uint32_t z) const noexcept {
    const auto damaged = damaged_block(x, y, z);
    if (!damaged.has_value()) {
        return 0.0F;
    }
    const auto initial = initial_health(x, y, z);
    if (!(initial > 0.0F)) {
        return 0.0F;
    }
    const auto fraction = 1.0F - damaged->health / initial;
    return std::clamp(fraction, 0.0F, std::nextafter(1.0F, 0.0F));
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
    const auto health = initial_health(x, y, z) * (1.0F - bounded);
    const auto found = damaged_.find(key);
    if (found != damaged_.end() && found->second.health == health) {
        return false;
    }
    const auto original = found != damaged_.end() ? found->second.original_color
                                                  : color(x, y, z).value_or(VxlColor{});
    damaged_[key] = DamagedBlock{health, original};
    ++revision_;
    return true;
}

bool VxlMap::clear_damage(std::uint32_t x, std::uint32_t y,
                          std::uint32_t z) noexcept {
    if (x >= width || y >= depth || z >= height) {
        return false;
    }
    if (damaged_.erase(static_cast<std::uint32_t>(index(x, y, z))) == 0U) {
        return false;
    }
    ++revision_;
    return true;
}

void VxlMap::set_health_multiplier(float multiplier) noexcept {
    health_multiplier_ = std::isfinite(multiplier) && multiplier > 0.0F ? multiplier : 1.0F;
}

float VxlMap::health_multiplier() const noexcept { return health_multiplier_; }

float VxlMap::initial_health(std::uint32_t x, std::uint32_t y,
                             std::uint32_t z) const noexcept {
    if (const auto user = user_block_health(x, y, z); user.has_value()) {
        return *user;
    }
    return default_block_health * health_multiplier_;
}

std::optional<float> VxlMap::user_block_health(std::uint32_t x, std::uint32_t y,
                                               std::uint32_t z) const noexcept {
    if (x >= width || y >= depth || z >= height) {
        return std::nullopt;
    }
    const auto found = user_health_.find(static_cast<std::uint32_t>(index(x, y, z)));
    return found == user_health_.end() ? std::nullopt : std::optional<float>{found->second};
}

bool VxlMap::set_user_block_health(std::uint32_t x, std::uint32_t y, std::uint32_t z,
                                   float health) noexcept {
    if (x >= width || y >= depth || z >= height || !std::isfinite(health)) {
        return false;
    }
    user_health_[static_cast<std::uint32_t>(index(x, y, z))] = health;
    return true;
}

bool VxlMap::add_user_block(std::uint32_t x, std::uint32_t y, std::uint32_t z,
                            VxlColor color_value, float health,
                            bool replace_solids) noexcept {
    if (x >= width || y >= depth || z > max_damageable_z || !std::isfinite(health)) {
        return false;
    }
    if (!replace_solids && solid(x, y, z)) {
        return false;
    }
    if (!set_voxel(x, y, z, color_value)) {
        return false;
    }
    const auto cell = static_cast<std::uint32_t>(index(x, y, z));
    if (ugc_user_blocks_) {
        // is_in_ugc_mode(): user_blocks.pop(key) -- the block is untracked.
        user_health_.erase(cell);
        return true;
    }
    // is_in_classic_mode(): health = DEFAULT_BLOCK_HEALTH, then the multiplier.
    const float initial = classic_user_blocks_ ? default_block_health : health;
    user_health_[cell] = initial * health_multiplier_;
    return true;
}

std::optional<DamagedBlock> VxlMap::damaged_block(std::uint32_t x, std::uint32_t y,
                                                  std::uint32_t z) const noexcept {
    if (!solid(x, y, z)) {
        return std::nullopt;
    }
    const auto found = damaged_.find(static_cast<std::uint32_t>(index(x, y, z)));
    return found == damaged_.end() ? std::nullopt : std::optional<DamagedBlock>{found->second};
}

BlockDamageOutcome VxlMap::add_damage(std::uint32_t x, std::uint32_t y, std::uint32_t z,
                                      float amount) noexcept {
    if (x >= width || y >= depth || z > max_damageable_z || !std::isfinite(amount) ||
        amount <= 0.0F || !solid(x, y, z)) {
        return BlockDamageOutcome::ignored;
    }
    const auto key = static_cast<std::uint32_t>(index(x, y, z));
    auto found = damaged_.find(key);
    if (found == damaged_.end()) {
        found = damaged_
                    .emplace(key, DamagedBlock{initial_health(x, y, z),
                                               color(x, y, z).value_or(VxlColor{})})
                    .first;
    }
    found->second.health -= amount;
    if (found->second.health <= 0.0F) {
        remove_voxel_bits(x, y, z);
        ++revision_;
        return BlockDamageOutcome::destroyed;
    }
    write_rgb(x, y, z, retail_dim(color(x, y, z).value_or(VxlColor{}), amount));
    ++revision_;
    return BlockDamageOutcome::damaged;
}

bool VxlMap::set_damaged_block(std::uint32_t x, std::uint32_t y, std::uint32_t z,
                               float remaining, VxlColor original) noexcept {
    if (x >= width || y >= depth || z >= height || !solid(x, y, z) ||
        !std::isfinite(remaining)) {
        return false;
    }
    damaged_[static_cast<std::uint32_t>(index(x, y, z))] = DamagedBlock{remaining, original};
    write_rgb(x, y, z, retail_dim(original, initial_health(x, y, z) - remaining));
    ++revision_;
    return true;
}

bool VxlMap::recolor_voxel(std::uint32_t x, std::uint32_t y, std::uint32_t z,
                           VxlColor color_value) noexcept {
    if (x >= width || y >= depth || z >= height || !solid(x, y, z)) {
        return false;
    }
    write_rgb(x, y, z, color_value);
    ++revision_;
    return true;
}

std::uint64_t VxlMap::solid_voxels() const noexcept { return solid_voxels_; }

void VxlMap::count_chunk_solid(std::uint32_t x, std::uint32_t y, std::uint32_t z,
                               bool added) noexcept {
    // vxl.pyd sub_10003D10: word_13CC4AD8[(x>>4) + 32*((y>>4) + 32*(z>>4))]
    // counts solids per 16-cube; the +0x3EB34D0 total moves on 0 <-> 1.
    const auto chunk = static_cast<std::size_t>((x >> 4U) + (width / 16U) *
                                                ((y >> 4U) + (depth / 16U) * (z >> 4U)));
    if (chunk >= chunk_solids_.size()) {
        return;
    }
    auto& count = chunk_solids_[chunk];
    if (added) {
        if (count++ == 0U) ++non_empty_chunks_;
    } else if (count > 0U) {
        if (--count == 0U && non_empty_chunks_ > 0U) --non_empty_chunks_;
    }
}
std::uint32_t VxlMap::source_edge() const noexcept { return source_edge_; }
std::uint32_t VxlMap::source_z_shift() const noexcept { return source_z_shift_; }

std::uint8_t retail_dim(std::uint8_t value, float damage) noexcept {
    if (!std::isfinite(damage)) {
        return value;
    }
    // Python 2 round(): halves away from zero, then int().
    const auto magnitude = static_cast<std::int64_t>(std::floor(std::fabs(damage) + 0.5F));
    const auto rounded = damage >= 0.0F ? magnitude : -magnitude;
    const auto channel = static_cast<std::int64_t>(value);
    // Python's >> floors toward negative infinity.
    const auto product = channel * rounded;
    const auto shifted = product >= 0 ? (product >> 3) : -((-product + 7) >> 3);
    return static_cast<std::uint8_t>(std::clamp<std::int64_t>(channel - shifted, 0, 255));
}

VxlColor retail_dim(VxlColor color_value, float damage) noexcept {
    return {retail_dim(color_value.red, damage), retail_dim(color_value.green, damage),
            retail_dim(color_value.blue, damage), color_value.alpha};
}

bool is_vxl_chroma_marker(std::uint32_t color_value) noexcept {
    const auto masked = color_value & 0x00F0F0F0U;
    return masked == 0x0000F000U || masked == 0x000000F0U;
}

} // namespace battlespades::world
