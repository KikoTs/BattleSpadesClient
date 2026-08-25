#include "battlespades/world/vxl_map.hpp"
#include "battlespades/world/minimap_overview.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <stdexcept>

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
