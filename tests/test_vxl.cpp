#include "battlespades/world/vxl_map.hpp"
#include "battlespades/world/minimap_overview.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>

namespace {
void expect(bool value, const char* message) {
    if (!value) { throw std::runtime_error{message}; }
}
}

int main() {
    try {
        constexpr std::array<std::byte, 3> truncated{};
        expect(!battlespades::world::VxlMap::load(truncated),
               "truncated VXL must fail closed");
        {
            // Fuzzer crash input: a non-terminal span leaves 2 bytes, so the
            // next span header would read past the heap buffer. Exact-size
            // heap allocation keeps ASan/debug-heap builds able to see it.
            const std::vector<std::byte> short_tail{
                std::byte{0x01U}, std::byte{0xF9U}, std::byte{0x03U},
                std::byte{0x00U}, std::byte{0x0AU}, std::byte{0x00U}};
            expect(!battlespades::world::VxlMap::load(
                       std::span<const std::byte>{short_tail}),
                   "span stream with a 1..3 byte tail must fail closed");
            for (std::size_t tail = 1U; tail < 4U; ++tail) {
                std::vector<std::byte> bytes{
                    std::byte{0x01U}, std::byte{0x10U}, std::byte{0x10U},
                    std::byte{0x00U}};
                bytes.resize(bytes.size() + tail, std::byte{0U});
                expect(!battlespades::world::VxlMap::load(
                           std::span<const std::byte>{bytes}),
                       "every short span-header tail must fail closed");
            }
        }
        constexpr std::array<std::byte, 8> sentinel_240_column{
            std::byte{0U}, std::byte{214U}, std::byte{214U}, std::byte{240U},
            std::byte{1U}, std::byte{2U}, std::byte{3U}, std::byte{0x7FU}};
        auto sentinel =
            battlespades::world::VxlMap::load(sentinel_240_column);
        expect(static_cast<bool>(sentinel), sentinel.error.c_str());
        expect(sentinel.map->source_z_shift() == 0U &&
                   sentinel.map->solid(255U, 255U, 214U) &&
                   !sentinel.map->solid(255U, 255U, 213U),
               "canonical z=240 sentinel must not underflow and shift terrain up");
        {
            // vxl.pyd generate_ground_color_table (0x1001D860) + the implicit
            // interior colouring of 0x10029C80 (P3-11).
            using battlespades::world::VxlMap;
            const std::array<std::array<std::uint8_t, 4U>, 2U> server_rows{
                {{59U, 58U, 55U, 238U}, {40U, 54U, 64U, 239U}}};
            const auto table = VxlMap::generate_ground_color_table(server_rows);
            expect(table[0U] == 0x3B3A37U && table[238U] == 0x3B3A37U &&
                       table[239U] == 0x283640U,
                   "the BattleSpades default rows give uniform (59,58,55) dirt");
            const std::array<std::array<std::uint8_t, 4U>, 2U> ramp{
                {{0U, 0U, 0U, 10U}, {100U, 200U, 50U, 20U}}};
            const auto ramped = VxlMap::generate_ground_color_table(ramp);
            expect(ramped[10U] == 0U && ramped[15U] == 0x326419U &&
                       ramped[20U] == 0x64C832U && ramped[238U] == 0x64C832U &&
                       ramped[239U] == 0U,
                   "later rows interpolate from the cursor; the tail stops at 238");
            auto& map = *sentinel.map;
            expect(!map.implicit_interior(255U, 255U, 214U) &&
                       map.implicit_interior(255U, 255U, 220U),
                   "only loader-filled interior cells are implicit");
            const auto inherited = map.color(255U, 255U, 220U);
            expect(inherited.has_value() && inherited->red == 3U && inherited->green == 2U &&
                       inherited->blue == 1U,
                   "without a table the interior keeps the legacy inherited colour");
            map.set_ground_colors(server_rows);
            const auto dirt = map.color(255U, 255U, 220U);
            expect(dirt.has_value() && dirt->red >= 59U && dirt->red <= 62U &&
                       dirt->green - 58U == dirt->red - 59U &&
                       dirt->blue - 55U == dirt->red - 59U,
                   "an exposed interior cell is table[z] plus a 0..3 grey jitter");
            const auto surface = map.color(255U, 255U, 214U);
            expect(surface.has_value() && surface->red == 3U,
                   "explicit surface colours never take the ground table");
            map.set_ground_colors({});
        }
        auto overview =
            battlespades::world::build_minimap_overview_rgba(*sentinel.map);
        const auto centre =
            (static_cast<std::size_t>(255U) * 512U + 255U) * 4U;
        expect(overview.size() == 512U * 512U * 4U &&
                   overview[centre + 0U] == 3U &&
                   overview[centre + 1U] == 2U &&
                   overview[centre + 2U] == 1U &&
                   overview[centre + 3U] == 255U,
               "minimap uses the topmost column colour as opaque RGBA");
        expect(sentinel.map->set_voxel(
                   255U, 255U, 100U,
                   battlespades::world::VxlColor{9U, 8U, 7U, 255U}),
               "minimap mutation fixture");
        expect(battlespades::world::refresh_minimap_overview_rgba(
                   *sentinel.map, overview, 255U, 255U, 255U, 255U) &&
                   overview[centre + 0U] == 9U &&
                   overview[centre + 1U] == 8U &&
                   overview[centre + 2U] == 7U,
               "dirty minimap refresh follows canonical terrain changes");
        expect(sentinel.map->set_voxel(
                   10U, 20U, 100U,
                   battlespades::world::VxlColor{33U, 44U, 55U, 255U}),
               "asymmetric minimap orientation fixture");
        expect(battlespades::world::refresh_minimap_overview_rgba(
                   *sentinel.map, overview, 10U, 20U, 10U, 20U),
               "asymmetric minimap column refresh");
        const auto asymmetric =
            (static_cast<std::size_t>(20U) * 512U + 10U) * 4U;
        expect(overview[asymmetric + 0U] == 33U &&
                   overview[asymmetric + 1U] == 44U &&
                   overview[asymmetric + 2U] == 55U,
               "minimap pixel (x,y) must represent world column (x,y) without transposition");
        const auto loaded = battlespades::world::VxlMap::load_file(AOS_TRAINING_VXL);
        expect(static_cast<bool>(loaded), loaded.error.c_str());
        expect(loaded.map->source_edge() == 512U, "Training must contain 512x512 columns");
        expect(loaded.map->solid_voxels() > 512ULL * 512ULL,
               "Training must contain terrain above the forced floor");
        expect(loaded.map->solid(0U, 0U, 239U), "retail floor must cover every column");
        std::cout << "Training.vxl: solids=" << loaded.map->solid_voxels()
                  << " z_shift=" << loaded.map->source_z_shift() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
