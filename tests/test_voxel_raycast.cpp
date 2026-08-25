#include "battlespades/world/voxel_raycast.hpp"

#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using namespace battlespades::world;

void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] VxlMap empty_world() {
    std::vector<std::byte> bytes;
    bytes.reserve(static_cast<std::size_t>(VxlMap::width) * VxlMap::depth * 4U);
    for (std::size_t column{};
         column < static_cast<std::size_t>(VxlMap::width) * VxlMap::depth;
         ++column) {
        bytes.insert(bytes.end(), {std::byte{0U}, std::byte{1U},
                                   std::byte{0U}, std::byte{0U}});
    }
    auto loaded = VxlMap::load(bytes);
    expect(static_cast<bool>(loaded), "synthetic VXL must parse");
    return std::move(*loaded.map);
}

constexpr VxlColor stone{96U, 104U, 112U, 255U};

void first_solid_reports_contact_face() {
    auto map = empty_world();
    expect(map.set_voxel(5U, 10U, 20U, stone), "ray fixture must be writable");
    const auto hit = trace_first_solid(
        map, {10.5F, 10.5F, 20.5F}, {-1.0F, 0.0F, 0.0F}, 20.0F);
    expect(hit.has_value(), "ray must find its first solid voxel");
    expect(hit->cell.x == 5U && hit->cell.y == 10U && hit->cell.z == 20U,
           "ray returned the wrong terrain cell");
    expect(hit->normal[0U] == 1 && hit->normal[1U] == 0 && hit->normal[2U] == 0,
           "ray returned the wrong entry-face normal");
    expect(std::abs(hit->position[0U] - 6.0F) < 0.001F,
           "ray contact must lie on the crossed voxel face");
}

void acoustic_path_is_open_without_cover() {
    const auto map = empty_world();
    const auto result = raycast_acoustic_path(
        map, {10.5F, 10.5F, 20.5F}, {20.5F, 10.5F, 20.5F});
    expect(result.blocked_samples == 0U &&
               std::abs(result.transmission - 1.0F) < 0.001F,
           "an unobstructed sound must retain full direct gain");
}

void solid_wall_muffles_all_samples() {
    auto map = empty_world();
    for (std::uint32_t y{9U}; y <= 11U; ++y) {
        for (std::uint32_t z{19U}; z <= 21U; ++z) {
            expect(map.set_voxel(15U, y, z, stone), "wall fixture must be writable");
        }
    }
    const auto result = raycast_acoustic_path(
        map, {10.5F, 10.5F, 20.5F}, {20.5F, 10.5F, 20.5F});
    expect(result.blocked_samples == AcousticRaycast::sample_count,
           "a solid wall must cover every acoustic sample");
    expect(std::abs(result.transmission - 0.35F) < 0.001F,
           "covered sound must use the deliberate low-frequency leakage floor");
}

void source_voxel_does_not_occlude_its_own_impact() {
    auto map = empty_world();
    expect(map.set_voxel(20U, 10U, 20U, stone), "source fixture must be writable");
    const auto result = raycast_acoustic_path(
        map, {10.5F, 10.5F, 20.5F}, {20.5F, 10.5F, 20.5F});
    expect(result.blocked_samples == 0U,
           "an impact source must not mistake its own voxel for intervening cover");
}

} // namespace

int main() {
    try {
        first_solid_reports_contact_face();
        acoustic_path_is_open_without_cover();
        solid_wall_muffles_all_samples();
        source_voxel_does_not_occlude_its_own_impact();
        std::cout << "voxel raycast tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
