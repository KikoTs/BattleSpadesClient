#include "battlespades/world/vxl_map.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using battlespades::world::VxlDecodeProfile;
using battlespades::world::VxlMap;

void expect(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error{message};
}

void append(std::vector<std::byte>& output, std::initializer_list<unsigned int> bytes) {
    for (const auto value : bytes)
        output.push_back(static_cast<std::byte>(value));
}

std::vector<std::byte> legacy_map(bool sentinel_water = true) {
    std::vector<std::byte> result;
    result.reserve(512U * 512U * 8U);
    for (unsigned int y = 0U; y < 512U; ++y) {
        for (unsigned int x = 0U; x < 512U; ++x) {
            if (sentinel_water && x == 1U && y == 2U) {
                append(result, {0U, 64U, 63U, 0U});
            } else if (x == 3U && y == 7U) {
                // Solid10..19, air20..24, solid25..63. Distinct surface and
                // cave-ceiling colors catch axis, ceiling and span mistakes.
                append(result, {4U,  10U, 10U, 0U,   11U, 22U, 33U, 127U, 44U, 55U, 66U, 127U,
                                77U, 88U, 99U, 127U, 0U,  25U, 25U, 20U,  12U, 23U, 34U, 127U});
            } else if (x == 12U && y == 34U) {
                append(result, {0U, 31U, 31U, 0U, 0U, 255U, 0U, 127U});
            } else {
                append(result, {0U, 30U, 30U, 0U, 10U, 20U, 30U, 127U});
            }
        }
    }
    return result;
}

void check_classic(const VxlMap& map) {
    expect(map.source_edge() == 512U && map.source_z_shift() == 176U,
           "Classic import has a fixed +176 offset independent of terrain extrema");
    expect(map.solid(3U, 7U, 186U) && !map.solid(3U, 7U, 185U),
           "Classic surface stays at source z + 176");
    expect(map.solid(3U, 7U, 195U) && !map.solid(3U, 7U, 196U) && !map.solid(3U, 7U, 200U) &&
               map.solid(3U, 7U, 201U),
           "Classic caves retain their ceiling and air gap");
    const auto ceiling = map.color(3U, 7U, 195U);
    expect(ceiling && ceiling->red == 99U && ceiling->green == 88U && ceiling->blue == 77U,
           "Classic ceiling colors remain BGR on disk and RGB in the world");
    const auto green = map.color(12U, 34U, 207U);
    expect(green && green->green == 255U && green->red == 0U && green->blue == 0U,
           "Classic green terrain is not interpreted as a retail flare marker");
    expect(map.surface_z(34U, 12U) == 206U, "Legacy map axes are not transposed");
}

struct TemporaryMaps {
    std::filesystem::path directory;
    TemporaryMaps() {
        directory = std::filesystem::temp_directory_path() /
                    ("battlespades-vxl-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        expect(std::filesystem::create_directory(directory), "create temporary VXL directory");
    }
    ~TemporaryMaps() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
    void write(std::string_view filename, std::span<const std::byte> bytes) const {
        std::ofstream output{directory / filename, std::ios::binary};
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        expect(static_cast<bool>(output), "write VXL fixture");
    }
    void metadata(std::string_view filename, std::string_view text) const {
        std::ofstream output{directory / filename, std::ios::binary};
        output << text;
        expect(static_cast<bool>(output), "write VXL metadata fixture");
    }
};

void malformed_tests(const std::vector<std::byte>& source) {
    auto malformed = source;
    malformed.pop_back();
    expect(!VxlMap::load(malformed, VxlDecodeProfile::classic64), "truncated legacy colors fail");
    malformed = source;
    malformed[1U] = std::byte{64U};
    malformed[2U] = std::byte{64U};
    expect(!VxlMap::load(malformed, VxlDecodeProfile::classic64),
           "64 is an air sentinel, never a valid Classic colored voxel");
    malformed = source;
    malformed[1U] = std::byte{33U};
    expect(!VxlMap::load(malformed, VxlDecodeProfile::classic64), "inverted surface spans fail");
    malformed = source;
    malformed[0U] = std::byte{1U};
    expect(!VxlMap::load(malformed, VxlDecodeProfile::classic64), "short word counts fail");
    const std::array backwards{std::byte{2U},
                               std::byte{10U},
                               std::byte{10U},
                               std::byte{0U},
                               std::byte{1U},
                               std::byte{2U},
                               std::byte{3U},
                               std::byte{127U},
                               std::byte{0U},
                               std::byte{15U},
                               std::byte{15U},
                               std::byte{20U},
                               std::byte{1U},
                               std::byte{2U},
                               std::byte{3U},
                               std::byte{127U}};
    expect(!VxlMap::load(backwards), "overlapping subsequent air/surface spans fail preflight");
    const std::array no_progress{
        std::byte{1U}, std::byte{20U}, std::byte{19U}, std::byte{0U},
        std::byte{0U}, std::byte{20U}, std::byte{19U}, std::byte{20U}};
    expect(!VxlMap::load(no_progress), "empty spans cannot chain without vertical progress");
    expect(!VxlMap::load(std::span{source}.first(8U), VxlDecodeProfile::classic64),
           "Classic dimensions require all 512-square columns");
    expect(!VxlMap::load(std::span{source}.first(8U), VxlDecodeProfile::canonical240),
           "Canonical MapSync requires all 512-square columns");
    malformed = source;
    append(malformed, {0U, 64U, 63U, 0U});
    expect(!VxlMap::load(malformed, VxlDecodeProfile::classic64), "extra trailing columns fail");
}

void disk_tests(const std::vector<std::byte>& source) {
    TemporaryMaps files;
    files.write("hallway.vxl", source);
    {
        const auto loaded = VxlMap::load_file(files.directory / "hallway.vxl");
        expect(static_cast<bool>(loaded), loaded.error.c_str());
        check_classic(*loaded.map);
        expect(loaded.map->surface_z(1U, 2U) == 239U,
               "Classic empty water columns have only the canonical bed");
    }
    files.metadata("hallway.json", R"({"vxl_format":"retail"})");
    files.metadata("hallway.ugc", R"({"vxl_format":"classic64"})");
    {
        const auto loaded = VxlMap::load_file(files.directory / "hallway.vxl");
        expect(static_cast<bool>(loaded), loaded.error.c_str());
        expect(loaded.map->source_z_shift() == 175U && !loaded.map->solid(12U, 34U, 206U),
               "JSON profile overrides detection and lower priority sidecars");
    }
    {
        const auto loaded =
            VxlMap::load_file(files.directory / "hallway.vxl", VxlDecodeProfile::classic64);
        expect(static_cast<bool>(loaded), loaded.error.c_str());
        check_classic(*loaded.map);
    }
    files.metadata("hallway.json", R"({"vxl_format":"bogus"})");
    expect(!VxlMap::load_file(files.directory / "hallway.vxl"),
           "invalid explicit format cannot silently change map coordinates");
    files.metadata("hallway.json", R"({"display_name":"Legacy hallway"})");
    files.metadata("hallway.ugc", "{}");
    files.metadata("hallway.vxl.json", R"({"vxl_format":"classic64"})");
    files.metadata("hallway.txt",
        "\xEF\xBB\xBFvxl_format = 'classic64'\n"
        "vxl_format = \"RETAIL\" # the last top-level assignment wins\n"
        "# vxl_format = 'classic64'\n"
        "description = '''Example metadata:\n"
        "vxl_format = 'classic64'\n'''\n"
        "def ignored_script():\n    vxl_format = 'classic64'\n"
        "raise RuntimeError('map metadata must never execute')\n");
    {
        const auto loaded = VxlMap::load_file(files.directory / "hallway.vxl");
        expect(static_cast<bool>(loaded), loaded.error.c_str());
        expect(loaded.map->source_z_shift() == 175U && !loaded.map->solid(12U, 34U, 206U),
               "legacy quoted format assignments respect duplicate and sidecar precedence");
    }
    files.metadata("hallway.txt",
        "# vxl_format = 'retail'\n"
        "description = \"\"\"Example metadata:\n"
        "vxl_format = 'retail'\n\"\"\"\n"
        "data = {'vxl_format': 'retail'}\n"
        "def ignored_script():\n    vxl_format = 'retail'\n");
    {
        const auto loaded = VxlMap::load_file(files.directory / "hallway.vxl");
        expect(static_cast<bool>(loaded), loaded.error.c_str());
        check_classic(*loaded.map);
    }
    files.metadata("hallway.txt", "vxl_format = 'bogus'\n");
    expect(!VxlMap::load_file(files.directory / "hallway.vxl"),
           "an invalid quoted legacy format fails like an invalid JSON format");
    files.write("20thCenturyTown.vxl", source);
    {
        const auto loaded = VxlMap::load_file(files.directory / "20thCenturyTown.vxl");
        expect(static_cast<bool>(loaded), loaded.error.c_str());
        expect(loaded.map->source_z_shift() == 175U && !loaded.map->solid(12U, 34U, 206U),
               "known 64-high stock retail maps retain marker handling");
    }
}

void canonical_tests(const std::vector<std::byte>& source) {
    auto canonical = source;
    std::size_t position{};
    while (position < canonical.size()) {
        const auto words = std::to_integer<unsigned int>(canonical[position]);
        const auto top = std::to_integer<unsigned int>(canonical[position + 1U]);
        const auto end = std::to_integer<unsigned int>(canonical[position + 2U]);
        const auto air = std::to_integer<unsigned int>(canonical[position + 3U]);
        canonical[position + 1U] = static_cast<std::byte>(top + 176U);
        canonical[position + 2U] = static_cast<std::byte>(end + 176U);
        canonical[position + 3U] = static_cast<std::byte>(air == 0U ? 0U : air + 176U);
        position += 4U * (words != 0U ? words : 1U + (end >= top ? end - top + 1U : 0U));
    }
    const auto loaded = VxlMap::load(canonical, VxlDecodeProfile::canonical240);
    expect(static_cast<bool>(loaded), loaded.error.c_str());
    expect(loaded.map->source_z_shift() == 0U && loaded.map->solid(12U, 34U, 207U) &&
               loaded.map->solid(3U, 7U, 195U) && !loaded.map->solid(3U, 7U, 196U),
           "authoritative normalized MapSync preserves geometry and authored green colors");
}
} // namespace

int main() {
    try {
        const auto source = legacy_map();
        malformed_tests(source);
        disk_tests(source);
        canonical_tests(source);
        {
            const auto no_sentinel = legacy_map(false);
            const auto loaded = VxlMap::load(no_sentinel, VxlDecodeProfile::automatic);
            expect(static_cast<bool>(loaded), loaded.error.c_str());
            check_classic(*loaded.map);
        }
        std::cout << "Legacy VXL import and canonical MapSync tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
