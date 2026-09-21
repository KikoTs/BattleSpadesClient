#include "battlespades/world/emissive_set.hpp"
#include "battlespades/world/map_spawn.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

using battlespades::world::EmissiveAppearance;
using battlespades::world::MapSpawn;
using battlespades::world::VxlMap;

struct Emitter final {
    std::uint32_t x{};
    std::uint32_t y{};
    std::uint32_t z{};
    bool pink{};
};

struct Cluster final {
    bool pink{};
    std::vector<Emitter> voxels;
};

struct Vantage final {
    MapSpawn spawn;
    double score{std::numeric_limits<double>::max()};
};

[[nodiscard]] constexpr std::uint32_t key(std::uint32_t x, std::uint32_t y,
                                          std::uint32_t z) noexcept {
    return x | (y << 9U) | (z << 18U);
}

[[nodiscard]] bool pink_light(const EmissiveAppearance& appearance) noexcept {
    return appearance.light.blue >
           static_cast<std::uint16_t>(appearance.light.green) * 2U;
}

[[nodiscard]] bool clear_view(const VxlMap& map, const MapSpawn& spawn,
                              double target_x, double target_y,
                              double target_z) noexcept {
    const auto dx = target_x - spawn.position.x;
    const auto dy = target_y - spawn.position.y;
    const auto dz = target_z - spawn.position.z;
    const auto length = std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
    if (length < 2.0) {
        return false;
    }
    // Stop just before the selected solid fixture. Reaching another solid
    // first means the emitter is hidden behind a wall from this camera.
    constexpr double step{0.2};
    for (double distance = step; distance < length - 0.8; distance += step) {
        const auto x = spawn.position.x + ((dx / length) * distance);
        const auto y = spawn.position.y + ((dy / length) * distance);
        const auto z = spawn.position.z + ((dz / length) * distance);
        if (x < 0.0 || y < 0.0 || z < 0.0 ||
            x >= static_cast<double>(VxlMap::width) ||
            y >= static_cast<double>(VxlMap::depth) ||
            z >= static_cast<double>(VxlMap::height)) {
            return false;
        }
        if (map.solid(static_cast<std::uint32_t>(std::floor(x)),
                      static_cast<std::uint32_t>(std::floor(y)),
                      static_cast<std::uint32_t>(std::floor(z)))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<Vantage>
find_vantage(const VxlMap& map, double target_x, double target_y,
             double target_z) noexcept {
    std::optional<Vantage> best;
    constexpr std::array<double, 5U> distances{8.0, 12.0, 16.0, 22.0, 30.0};
    constexpr double pi{3.14159265358979323846};
    for (const auto distance : distances) {
        for (std::uint32_t heading{}; heading < 16U; ++heading) {
            const auto radians = (static_cast<double>(heading) / 16.0) *
                                 (2.0 * pi);
            const auto requested_x = std::clamp(
                static_cast<std::int64_t>(
                    std::lround(target_x + std::cos(radians) * distance)),
                std::int64_t{2}, static_cast<std::int64_t>(VxlMap::width - 3U));
            const auto requested_y = std::clamp(
                static_cast<std::int64_t>(
                    std::lround(target_y + std::sin(radians) * distance)),
                std::int64_t{2}, static_cast<std::int64_t>(VxlMap::depth - 3U));
            const auto spawn =
                battlespades::world::resolve_map_spawn(
                    map, static_cast<std::uint32_t>(requested_x),
                    static_cast<std::uint32_t>(requested_y));
            if (!spawn.derived ||
                !clear_view(map, spawn, target_x, target_y, target_z)) {
                continue;
            }
            const auto dx = target_x - spawn.position.x;
            const auto dy = target_y - spawn.position.y;
            const auto dz = target_z - spawn.position.z;
            const auto actual_distance =
                std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
            // Prefer a close readable fixture without placing it far above or
            // below the crosshair. The latter usually means standing on a roof.
            const auto score = actual_distance + (std::abs(dz) * 0.45);
            if (!best.has_value() || score < best->score) {
                best = Vantage{spawn, score};
            }
        }
    }
    return best;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        std::cerr << "usage: aos_emissive_vantage MAP.vxl MAP_BASENAME "
                     "[RRGGBB]\n";
        return 2;
    }
    const auto loaded = VxlMap::load_file(std::filesystem::path{argv[1]});
    if (!loaded) {
        std::cerr << loaded.error << '\n';
        return 1;
    }
    const auto palette =
        battlespades::world::emissive_palette_for(std::string{argv[2]});
    std::optional<std::array<std::uint8_t, 3U>> probe;
    int probe_tolerance{4};
    if (argc == 4) {
        std::string value{argv[3]};
        if (value.starts_with("SLICEX=")) {
            std::array<std::uint32_t, 5U> bounds{};
            std::stringstream input{value.substr(7U)};
            char comma{};
            for (std::size_t index{}; index < bounds.size(); ++index) {
                if (!(input >> bounds[index]) ||
                    (index + 1U < bounds.size() && !(input >> comma)) ||
                    (index + 1U < bounds.size() && comma != ',')) {
                    std::cerr << "SLICEX requires x,y0,y1,z0,z1\n";
                    return 2;
                }
            }
            const auto x = bounds[0U];
            if (x >= VxlMap::width) {
                std::cerr << "SLICEX x is outside the map\n";
                return 2;
            }
            std::cout << "    ";
            for (auto y = bounds[1U]; y <= bounds[2U] && y < VxlMap::depth;
                 ++y) {
                std::cout << static_cast<char>('0' + (y % 10U));
            }
            std::cout << '\n';
            for (auto z = bounds[3U]; z <= bounds[4U] && z < VxlMap::height;
                 ++z) {
                std::cout << std::setw(3) << z << ' ';
                for (auto y = bounds[1U];
                     y <= bounds[2U] && y < VxlMap::depth; ++y) {
                    const auto color = loaded.map->color(x, y, z);
                    if (!color.has_value()) {
                        std::cout << ' ';
                        continue;
                    }
                    const auto maximum =
                        std::max({color->red, color->green, color->blue});
                    const auto minimum =
                        std::min({color->red, color->green, color->blue});
                    const auto spread = maximum - minimum;
                    if (maximum <= 0x38U && spread <= 0x18) {
                        std::cout << 'w';
                    } else if (color->blue > color->red + 8U &&
                               color->blue > color->green + 4U &&
                               maximum <= 0x90U) {
                        std::cout << 'W';
                    } else if (color->red > color->green + 12U &&
                               color->green >= color->blue) {
                        std::cout << 'b';
                    } else if (spread <= 10 && maximum <= 0x80U) {
                        std::cout << 'm';
                    } else {
                        std::cout << '#';
                    }
                }
                std::cout << '\n';
            }
            return 0;
        }
        if (value.starts_with("SLICEY=")) {
            std::array<std::uint32_t, 5U> bounds{};
            std::stringstream input{value.substr(7U)};
            char comma{};
            for (std::size_t index{}; index < bounds.size(); ++index) {
                if (!(input >> bounds[index]) ||
                    (index + 1U < bounds.size() && !(input >> comma)) ||
                    (index + 1U < bounds.size() && comma != ',')) {
                    std::cerr << "SLICEY requires y,x0,x1,z0,z1\n";
                    return 2;
                }
            }
            const auto y = bounds[0U];
            if (y >= VxlMap::depth) {
                std::cerr << "SLICEY y is outside the map\n";
                return 2;
            }
            std::cout << "    ";
            for (auto x = bounds[1U]; x <= bounds[2U] && x < VxlMap::width;
                 ++x) {
                std::cout << static_cast<char>('0' + (x % 10U));
            }
            std::cout << '\n';
            for (auto z = bounds[3U]; z <= bounds[4U] && z < VxlMap::height;
                 ++z) {
                std::cout << std::setw(3) << z << ' ';
                for (auto x = bounds[1U];
                     x <= bounds[2U] && x < VxlMap::width; ++x) {
                    const auto color = loaded.map->color(x, y, z);
                    if (!color.has_value()) {
                        std::cout << ' ';
                        continue;
                    }
                    const auto maximum =
                        std::max({color->red, color->green, color->blue});
                    const auto minimum =
                        std::min({color->red, color->green, color->blue});
                    const auto spread = maximum - minimum;
                    if (maximum <= 0x38U && spread <= 0x18) {
                        std::cout << 'w';
                    } else if (color->blue > color->red + 8U &&
                               color->blue > color->green + 4U &&
                               maximum <= 0x70U) {
                        std::cout << 'W';
                    } else if (color->red > color->green + 12U &&
                               color->green >= color->blue) {
                        std::cout << 'b';
                    } else if (spread <= 10 && maximum <= 0x80U) {
                        std::cout << 'm';
                    } else {
                        std::cout << '#';
                    }
                }
                std::cout << '\n';
            }
            return 0;
        }
        if (value.starts_with("RAY=")) {
            std::array<double, 6U> points{};
            std::stringstream input{value.substr(4U)};
            char comma{};
            for (std::size_t index{}; index < points.size(); ++index) {
                if (!(input >> points[index]) ||
                    (index + 1U < points.size() && !(input >> comma)) ||
                    (index + 1U < points.size() && comma != ',')) {
                    std::cerr << "RAY requires x0,y0,z0,x1,y1,z1\n";
                    return 2;
                }
            }
            const auto dx = points[3U] - points[0U];
            const auto dy = points[4U] - points[1U];
            const auto dz = points[5U] - points[2U];
            const auto length = std::sqrt(dx * dx + dy * dy + dz * dz);
            std::optional<std::array<std::uint32_t, 3U>> previous;
            for (double distance{}; distance <= length; distance += 0.05) {
                const auto fraction = length > 0.0 ? distance / length : 0.0;
                const auto sample_x = points[0U] + dx * fraction;
                const auto sample_y = points[1U] + dy * fraction;
                const auto sample_z = points[2U] + dz * fraction;
                if (sample_x < 0.0 || sample_y < 0.0 || sample_z < 0.0 ||
                    sample_x >= VxlMap::width || sample_y >= VxlMap::depth ||
                    sample_z >= VxlMap::height) {
                    continue;
                }
                const std::array voxel{
                    static_cast<std::uint32_t>(std::floor(sample_x)),
                    static_cast<std::uint32_t>(std::floor(sample_y)),
                    static_cast<std::uint32_t>(std::floor(sample_z))};
                if (previous == voxel || !loaded.map->solid(
                                             voxel[0U], voxel[1U], voxel[2U])) {
                    previous = voxel;
                    continue;
                }
                previous = voxel;
                const auto color =
                    loaded.map->color(voxel[0U], voxel[1U], voxel[2U]);
                std::cout << voxel[0U] << ',' << voxel[1U] << ',' << voxel[2U]
                          << " distance " << std::fixed << std::setprecision(2)
                          << distance;
                if (color.has_value()) {
                    const auto packed =
                        (static_cast<std::uint32_t>(color->red) << 16U) |
                        (static_cast<std::uint32_t>(color->green) << 8U) |
                        color->blue;
                    std::cout << " #" << std::uppercase << std::hex
                              << std::setw(6) << std::setfill('0') << packed
                              << std::dec;
                } else {
                    std::cout << " uncoloured";
                }
                std::cout << '\n';
            }
            return 0;
        }
        if (value.starts_with("BOX=")) {
            std::array<std::uint32_t, 6U> bounds{};
            std::stringstream input{value.substr(4U)};
            char comma{};
            for (std::size_t index{}; index < bounds.size(); ++index) {
                if (!(input >> bounds[index]) ||
                    (index + 1U < bounds.size() && !(input >> comma)) ||
                    (index + 1U < bounds.size() && comma != ',')) {
                    std::cerr << "BOX requires x0,y0,z0,x1,y1,z1\n";
                    return 2;
                }
            }
            struct ColorStats final {
                std::size_t count{};
                std::array<std::uint32_t, 3U> minimum{
                    VxlMap::width, VxlMap::depth, VxlMap::height};
                std::array<std::uint32_t, 3U> maximum{};
            };
            std::unordered_map<std::uint32_t, ColorStats> statistics;
            for (auto z = bounds[2U]; z <= bounds[5U] && z < VxlMap::height; ++z) {
                for (auto y = bounds[1U]; y <= bounds[4U] && y < VxlMap::depth;
                     ++y) {
                    for (auto x = bounds[0U]; x <= bounds[3U] && x < VxlMap::width;
                         ++x) {
                        const auto color = loaded.map->color(x, y, z);
                        if (!color.has_value()) {
                            continue;
                        }
                        const auto packed =
                            (static_cast<std::uint32_t>(color->red) << 16U) |
                            (static_cast<std::uint32_t>(color->green) << 8U) |
                            color->blue;
                        auto& stats = statistics[packed];
                        ++stats.count;
                        for (std::size_t axis{}; axis < 3U; ++axis) {
                            const auto coordinate =
                                std::array<std::uint32_t, 3U>{x, y, z}[axis];
                            stats.minimum[axis] =
                                std::min(stats.minimum[axis], coordinate);
                            stats.maximum[axis] =
                                std::max(stats.maximum[axis], coordinate);
                        }
                    }
                }
            }
            std::vector<std::pair<std::uint32_t, ColorStats>> ranked{
                statistics.begin(), statistics.end()};
            std::ranges::sort(ranked, [](const auto& left, const auto& right) {
                return left.second.count > right.second.count;
            });
            for (const auto& [color, stats] : ranked) {
                std::cout << '#' << std::uppercase << std::hex << std::setw(6)
                          << std::setfill('0') << color << std::dec << ' '
                          << stats.count << " bounds " << stats.minimum[0U] << ','
                          << stats.minimum[1U] << ',' << stats.minimum[2U] << ".."
                          << stats.maximum[0U] << ',' << stats.maximum[1U] << ','
                          << stats.maximum[2U] << '\n';
            }
            return 0;
        }
        if (value == "HIST" || value == "DARK") {
            const bool dark_only = value == "DARK";
            std::unordered_map<std::uint32_t, std::size_t> histogram;
            for (std::uint32_t y{}; y < VxlMap::depth; ++y) {
                for (std::uint32_t x{}; x < VxlMap::width; ++x) {
                    const auto surface = loaded.map->surface_z(x, y);
                    for (std::uint32_t z = surface; z < VxlMap::height; ++z) {
                        const auto color = loaded.map->color(x, y, z);
                        if (!color.has_value()) {
                            continue;
                        }
                        const auto maximum =
                            std::max({color->red, color->green, color->blue});
                        const auto minimum =
                            std::min({color->red, color->green, color->blue});
                        if ((!dark_only &&
                             (maximum < 100U || maximum - minimum > 35U)) ||
                            (dark_only &&
                             (maximum > 100U || maximum - minimum > 16U ||
                              z >= VxlMap::height - 2U))) {
                            continue;
                        }
                        if (dark_only) {
                            constexpr std::array<std::array<int, 3U>, 6U>
                                neighbours{{
                                    {{-1, 0, 0}}, {{1, 0, 0}},
                                    {{0, -1, 0}}, {{0, 1, 0}},
                                    {{0, 0, -1}}, {{0, 0, 1}},
                                }};
                            const auto exposed =
                                std::ranges::any_of(neighbours, [&](const auto& offset) {
                                    const auto nx =
                                        static_cast<std::int64_t>(x) + offset[0U];
                                    const auto ny =
                                        static_cast<std::int64_t>(y) + offset[1U];
                                    const auto nz =
                                        static_cast<std::int64_t>(z) + offset[2U];
                                    return nx >= 0 && ny >= 0 && nz >= 0 &&
                                           nx < VxlMap::width &&
                                           ny < VxlMap::depth &&
                                           nz < VxlMap::height &&
                                           !loaded.map->solid(
                                               static_cast<std::uint32_t>(nx),
                                               static_cast<std::uint32_t>(ny),
                                               static_cast<std::uint32_t>(nz));
                                });
                            if (!exposed) {
                                continue;
                            }
                        }
                        const auto packed =
                            (static_cast<std::uint32_t>(color->red) << 16U) |
                            (static_cast<std::uint32_t>(color->green) << 8U) |
                            color->blue;
                        ++histogram[packed];
                    }
                }
            }
            std::vector<std::pair<std::uint32_t, std::size_t>> ranked{
                histogram.begin(), histogram.end()};
            std::ranges::sort(ranked, [](const auto& left, const auto& right) {
                return left.second > right.second;
            });
            const auto count = std::min<std::size_t>(ranked.size(), 100U);
            for (std::size_t index{}; index < count; ++index) {
                std::cout << '#' << std::uppercase << std::hex << std::setw(6)
                          << std::setfill('0') << ranked[index].first << std::dec
                          << ' ' << ranked[index].second << '\n';
            }
            return 0;
        }
        if (value.starts_with("EXACT=")) {
            probe_tolerance = 0;
            value = value.substr(6U);
        }
        if (value.size() != 6U) {
            std::cerr << "color probe must be six hexadecimal digits\n";
            return 2;
        }
        try {
            const auto packed = std::stoul(value, nullptr, 16);
            probe = std::array<std::uint8_t, 3U>{
                static_cast<std::uint8_t>((packed >> 16U) & 0xFFU),
                static_cast<std::uint8_t>((packed >> 8U) & 0xFFU),
                static_cast<std::uint8_t>(packed & 0xFFU)};
        } catch (const std::exception&) {
            std::cerr << "color probe must be six hexadecimal digits\n";
            return 2;
        }
    }
    std::unordered_map<std::uint32_t, Emitter> emitters;
    for (std::uint32_t y{}; y < VxlMap::depth; ++y) {
        for (std::uint32_t x{}; x < VxlMap::width; ++x) {
            const auto surface = loaded.map->surface_z(x, y);
            for (std::uint32_t z = surface; z < VxlMap::height; ++z) {
                const auto color = loaded.map->color(x, y, z);
                if (!color.has_value()) {
                    continue;
                }
                if (probe.has_value()) {
                    const auto close = [](std::uint8_t left, std::uint8_t right) {
                        return std::abs(static_cast<int>(left) -
                                        static_cast<int>(right));
                    };
                    if (close(color->red, (*probe)[0U]) <= probe_tolerance &&
                        close(color->green, (*probe)[1U]) <= probe_tolerance &&
                        close(color->blue, (*probe)[2U]) <= probe_tolerance) {
                        emitters.emplace(key(x, y, z), Emitter{x, y, z, false});
                    }
                    continue;
                }
                const auto appearance =
                    battlespades::world::emissive_appearance_at(
                        palette, *loaded.map, x, y, z, *color);
                if (!appearance.has_value()) {
                    continue;
                }
                emitters.emplace(key(x, y, z),
                                 Emitter{x, y, z, pink_light(*appearance)});
            }
        }
    }

    std::unordered_set<std::uint32_t> visited;
    std::vector<Cluster> clusters;
    for (const auto& [start_key, start] : emitters) {
        if (!visited.insert(start_key).second) {
            continue;
        }
        Cluster cluster;
        cluster.pink = start.pink;
        std::vector<Emitter> pending{start};
        while (!pending.empty()) {
            const auto current = pending.back();
            pending.pop_back();
            cluster.voxels.push_back(current);
            constexpr std::array<std::array<int, 3U>, 6U> neighbours{{
                {{-1, 0, 0}}, {{1, 0, 0}}, {{0, -1, 0}},
                {{0, 1, 0}},  {{0, 0, -1}}, {{0, 0, 1}},
            }};
            for (const auto& offset : neighbours) {
                const auto nx = static_cast<std::int64_t>(current.x) + offset[0U];
                const auto ny = static_cast<std::int64_t>(current.y) + offset[1U];
                const auto nz = static_cast<std::int64_t>(current.z) + offset[2U];
                if (nx < 0 || ny < 0 || nz < 0 ||
                    nx >= VxlMap::width || ny >= VxlMap::depth ||
                    nz >= VxlMap::height) {
                    continue;
                }
                const auto neighbour_key =
                    key(static_cast<std::uint32_t>(nx),
                        static_cast<std::uint32_t>(ny),
                        static_cast<std::uint32_t>(nz));
                const auto found = emitters.find(neighbour_key);
                if (found == emitters.end() || found->second.pink != cluster.pink ||
                    !visited.insert(neighbour_key).second) {
                    continue;
                }
                pending.push_back(found->second);
            }
        }
        clusters.push_back(std::move(cluster));
    }

    std::ranges::sort(clusters, [](const Cluster& left, const Cluster& right) {
        return left.voxels.size() > right.voxels.size();
    });
    std::cout << "emitters " << emitters.size() << ", clusters "
              << clusters.size() << ", cast gain " << palette.cast_gain << '\n';
    const auto count =
        probe.has_value() ? clusters.size()
                          : std::min<std::size_t>(clusters.size(), 40U);
    for (std::size_t index{}; index < count; ++index) {
        const auto& cluster = clusters[index];
        std::uint64_t total_x{}, total_y{}, total_z{};
        std::array<std::uint32_t, 3U> minimum{
            VxlMap::width, VxlMap::depth, VxlMap::height};
        std::array<std::uint32_t, 3U> maximum{};
        for (const auto& voxel : cluster.voxels) {
            total_x += voxel.x;
            total_y += voxel.y;
            total_z += voxel.z;
            minimum[0U] = std::min(minimum[0U], voxel.x);
            minimum[1U] = std::min(minimum[1U], voxel.y);
            minimum[2U] = std::min(minimum[2U], voxel.z);
            maximum[0U] = std::max(maximum[0U], voxel.x);
            maximum[1U] = std::max(maximum[1U], voxel.y);
            maximum[2U] = std::max(maximum[2U], voxel.z);
        }
        const auto size = cluster.voxels.size();  // printed below as a count
        const auto voxels = static_cast<double>(size);
        const auto centre_x = static_cast<double>(total_x) / voxels;
        const auto centre_y = static_cast<double>(total_y) / voxels;
        const auto centre_z = static_cast<double>(total_z) / voxels;
        const auto vantage =
            find_vantage(*loaded.map, centre_x, centre_y, centre_z);
        std::cout << (probe.has_value() ? "MATCH "
                                       : (cluster.pink ? "PINK " : "WARM "))
                  << size
                  << " center " << centre_x << ',' << centre_y << ','
                  << centre_z << " bounds " << minimum[0U] << ',' << minimum[1U]
                  << ',' << minimum[2U] << ".." << maximum[0U] << ','
                  << maximum[1U] << ',' << maximum[2U];
        if (vantage.has_value()) {
            std::cout << " stand " << vantage->spawn.position.x << ','
                      << vantage->spawn.position.y << " look " << centre_x << ','
                      << centre_y << ',' << centre_z;
        } else {
            std::cout << " no-clear-stand";
        }
        std::cout << '\n';
    }
    return 0;
}
