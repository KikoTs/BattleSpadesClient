#include "battlespades/world/classic_environment.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <algorithm>
#include <array>

namespace battlespades::world {

std::string_view choose_classic_skydome(const VxlMap& map, std::uint64_t seed) noexcept {
    std::uint32_t samples{}, snow{}, sand{}, green{}, dark{};
    // Ignore the water/bed layer: a large ocean must not drown out an island's
    // palette. 4,096 constant-time column lookups, regardless of map density.
    for (std::uint32_t y = 4U; y < VxlMap::depth; y += 8U) {
        for (std::uint32_t x = 4U; x < VxlMap::width; x += 8U) {
            const auto z = map.surface_z(x, y);
            if (z >= VxlMap::height - 2U) continue;
            const auto color = map.color(x, y, z);
            if (!color) continue;
            const auto r = static_cast<int>(color->red);
            const auto g = static_cast<int>(color->green);
            const auto b = static_cast<int>(color->blue);
            ++samples;
            snow += (std::min({r, g, b}) > 170 && std::max({r, g, b}) - std::min({r, g, b}) < 40) ? 1U : 0U;
            sand += (r > 100 && g > 70 && r > b * 1.25 && g > b * 1.1 && r >= g) ? 1U : 0U;
            green += (g > r * 1.1 && g > b * 1.1) ? 1U : 0U;
            dark += (r * 0.2126 + g * 0.7152 + b * 0.0722 < 70.0) ? 1U : 0U;
        }
    }
    // SplitMix64 gives independent variation even with nearby session seeds.
    seed += 0x9e3779b97f4a7c15ULL;
    seed = (seed ^ (seed >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    seed = (seed ^ (seed >> 27U)) * 0x94d049bb133111ebULL;
    seed ^= seed >> 31U;
    const auto select = [seed](const std::array<std::string_view, 3U>& choices) {
        return choices[static_cast<std::size_t>(seed % choices.size())];
    };
    if (samples == 0U) return "User_Grassland.txt";
    if (snow * 3U >= samples)
        return select({"ArcticBase.txt", "ArcticBase.txt", "Classic_B.txt"});
    if (green * 4U >= samples)
        return select({"User_Grassland.txt", "Classic.txt", "MayanJungle.txt"});
    if (sand * 3U >= samples)
        return select({"Egypt.txt", "Colosseum.txt", "Frontier.txt"});
    if (dark * 2U >= samples)
        return select({"SecretBase_Night.txt", "BranCastle.txt", "WW1.txt"});
    return select({"Classic_B.txt", "User_Grassland.txt", "Classic.txt"});
}

} // namespace battlespades::world
