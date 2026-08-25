#include "battlespades/world/map_catalog.hpp"
#include "battlespades/world/map_spawn.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace battlespades::world;

void expect(bool value, const std::string& message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] std::filesystem::path asset_root() {
    return std::filesystem::path{AOS_TEST_ASSET_ROOT};
}

void the_catalog_finds_the_shipped_maps_and_nothing_else() {
    const auto entries = scan_map_catalog(asset_root());
    expect(entries.size() >= 25U,
           "expected the shipped map set, found " + std::to_string(entries.size()));
    for (const auto& entry : entries) {
        // The maps directory also holds loading thumbnails and metadata
        // sidecars; offering those as rows would give entries that cannot load.
        expect(entry.path.extension() == ".vxl",
               entry.stem + " is not a map file");
        expect(std::filesystem::is_regular_file(entry.path),
               entry.stem + " does not exist on disk");
        // A stem is joined onto an asset directory downstream.
        expect(entry.stem.find('.') == std::string::npos &&
                   entry.stem.find("..") == std::string::npos &&
                   entry.stem.find('/') == std::string::npos &&
                   entry.stem.find('\\') == std::string::npos,
               entry.stem + " is not a safe basename");
    }
    // Sorted by display name so the browser is stable between runs.
    expect(std::ranges::is_sorted(entries, {}, &MapCatalogEntry::display_name),
           "the catalog must be ordered for a stable list");
}

void every_map_resolves_to_a_skydome_that_exists() {
    // A missing dome means set_skydome fails and the map loads with the previous
    // map's sky, which silently gives it the wrong lighting.
    std::vector<std::string> broken;
    for (const auto& entry : scan_map_catalog(asset_root())) {
        expect(entry.skydome.ends_with(".txt"),
               entry.stem + " skydome must carry the .txt suffix set_skydome requires");
        if (!entry.skydome_present) {
            broken.push_back(entry.stem + " -> " + entry.skydome);
        }
    }
    if (!broken.empty()) {
        std::string report = "maps whose skydome is not on disk:";
        for (const auto& entry : broken) {
            report += "\n  " + entry;
        }
        throw std::runtime_error{report};
    }
}

void the_recovered_skydome_mismatches_are_pinned() {
    // These are the rows where the dome name does NOT follow the map name. They
    // are the whole reason a table exists, so pin them: a regression here gives
    // the map a plausible-looking but wrong sky.
    struct Row final {
        const char* stem;
        const char* skydome;
    };
    const std::array<Row, 6U> rows{{
        {"TokyoNeon", "Tokyo.txt"},
        {"AncientEgypt", "Egypt.txt"},
        {"CityOfChicago", "Chicago.txt"},
        {"TheColosseum", "Colosseum.txt"},
        {"SpookyMansion", "BranCastle.txt"},
        {"DragonIsland", "SecretBase.txt"},
    }};
    for (const auto& row : rows) {
        SkydomeConfidence confidence{};
        const auto resolved = resolve_map_skydome(asset_root(), row.stem, confidence);
        expect(resolved == row.skydome,
               std::string{row.stem} + " resolved to " + resolved + ", expected " +
                   row.skydome);
    }
}

void display_names_match_the_retail_spelling() {
    // The loading screen matches its per-map art on an exact string, so a wrong
    // spelling silently falls back to the blueprint placeholder.
    expect(map_display_name("TokyoNeon") == "Tokyo Neon", "TokyoNeon");
    expect(map_display_name("AncientEgypt") == "Ancient Egypt", "AncientEgypt");
    expect(map_display_name("CityOfChicago") == "City Of Chicago", "CityOfChicago");
    expect(map_display_name("WW1") == "WW1", "an all-caps stem must not be split");
    expect(map_display_name("Training") == "Training", "a single word is unchanged");
}

void official_server_labels_resolve_to_local_environments() {
    const auto compact = find_official_map_environment("CityOfChicago");
    const auto spaced = find_official_map_environment("City of Chicago");
    const auto filename =
        find_official_map_environment("maps/City_Of_Chicago.vxl");
    expect(compact.has_value() && spaced.has_value() && filename.has_value(),
           "harmless server map-name spelling drift must still identify stock maps");
    expect(compact->stem == "CityOfChicago" &&
               compact->skydome == "Chicago.txt" &&
               spaced->stem == compact->stem &&
               filename->stem == compact->stem,
           "all Chicago spellings must select the same local atmosphere");
    expect(!find_official_map_environment("Some UGC Map").has_value(),
           "an unknown map must never be assigned a guessed official atmosphere");
}

void an_unknown_map_falls_back_rather_than_failing() {
    SkydomeConfidence confidence{};
    const auto resolved = resolve_map_skydome(asset_root(), "SomeUgcMap", confidence);
    expect(resolved == "Classic_B.txt",
           "an unknown map must fall back to the neutral default dome");
    expect(confidence == SkydomeConfidence::fallback,
           "a fallback must be reported as such so the UI can say it is guessing");
}

void every_environment_resolves_a_shipped_ambient_bed() {
    for (const auto& entry : scan_map_catalog(asset_root())) {
        const auto ambient =
            default_map_ambient(entry.stem, entry.skydome);
        expect(!ambient.empty(), entry.stem + " has no ambient bed");
        expect(std::filesystem::is_regular_file(
                   asset_root() / "ambients" /
                   (std::string{ambient} + ".ogg")),
               entry.stem + " ambient asset is missing: " +
                   std::string{ambient});
    }
    expect(default_map_ambient("City of Chicago", "Chicago.txt") ==
               "amb_oldchicago",
           "Chicago's authored old-city bed must beat the dome fallback");
    expect(default_map_ambient("London", "London.txt") == "amb_city",
           "London must use the city ambient family");
    expect(default_map_ambient("Some UGC Map", "WW2_DockLands.txt") ==
               "amb_harbour",
           "unknown maps still inherit a known server-selected environment");
}

// --- spawn derivation -------------------------------------------------------

/** Air above the forced indestructible bed; the same shape other tests use. */
[[nodiscard]] VxlMap bare_world() {
    std::vector<std::byte> bytes;
    bytes.reserve(static_cast<std::size_t>(VxlMap::width) * VxlMap::depth * 4U);
    for (std::size_t column{};
         column < static_cast<std::size_t>(VxlMap::width) * VxlMap::depth; ++column) {
        bytes.insert(bytes.end(), {std::byte{0U}, std::byte{1U}, std::byte{0U},
                                   std::byte{0U}});
    }
    auto loaded = VxlMap::load(bytes);
    expect(static_cast<bool>(loaded), "synthetic VXL must parse");
    return std::move(*loaded.map);
}

void the_spawn_rule_reproduces_the_authored_training_anchor() {
    // Training's authored spawn is {140.5, 76.5, 230.75}, and its surface there
    // is 233, so anchor = 233 - 2.25. Placing a platform at 233 must therefore
    // reproduce the authored anchor exactly. If this drifts, the derivation is
    // wrong rather than merely differently tuned.
    auto map = bare_world();
    constexpr VxlColor stone{120U, 96U, 80U, 255U};
    expect(map.set_voxel(140U, 76U, 233U, stone), "platform voxel must be accepted");
    const auto spawn = resolve_map_spawn(map, 140U, 76U);
    expect(spawn.derived, "a standable platform must yield a derived spawn");
    expect(std::abs(spawn.position.z - 230.75) < 1.0e-9,
           "anchor must be surface minus the standing contact offset, got " +
               std::to_string(spawn.position.z));
    expect(std::abs(spawn.position.x - 140.5) < 1.0e-9 &&
               std::abs(spawn.position.y - 76.5) < 1.0e-9,
           "a standable start column must be used directly, not searched past");
}

void a_spawn_is_never_inside_geometry() {
    // The failure that produced a black screen on Alcatraz: an anchor buried in
    // rock. Bury the start column under a tall pillar and require the resolver
    // to stand on top of it or move aside, never inside it.
    auto map = bare_world();
    constexpr VxlColor stone{120U, 96U, 80U, 255U};
    for (std::uint32_t z{200U}; z < 239U; ++z) {
        expect(map.set_voxel(256U, 256U, z, stone), "pillar voxel must be accepted");
    }
    const auto spawn = resolve_map_spawn(map, 256U, 256U);
    expect(spawn.derived, "a world with solid ground must be standable");
    const auto cell_x = static_cast<std::uint32_t>(spawn.position.x);
    const auto cell_y = static_cast<std::uint32_t>(spawn.position.y);
    const auto anchor_cell = static_cast<std::uint32_t>(std::floor(spawn.position.z));
    expect(!map.solid(cell_x, cell_y, anchor_cell),
           "the eye must never sit inside a solid voxel");
    // And the feet must be clear too, not merely the eye.
    const auto feet_cell = static_cast<std::uint32_t>(std::floor(spawn.position.z + 2.2));
    expect(!map.solid(cell_x, cell_y, feet_cell),
           "the body must not intersect geometry");
}

void a_map_with_only_the_bed_still_stands_above_it() {
    // Every map has the forced solid bed at the bottom, so a resolver that only
    // accepted dry land would fail outright on open water. It must relax rather
    // than hand back an undeivable position.
    const auto map = bare_world();
    const auto spawn = resolve_map_spawn(map, 256U, 256U);
    expect(spawn.derived, "the bed must be standable on the relaxed pass");
    expect(spawn.position.z < 239.0,
           "the anchor must sit above the bed, not inside or below it");
}

} // namespace

int main() {
    try {
        the_catalog_finds_the_shipped_maps_and_nothing_else();
        every_map_resolves_to_a_skydome_that_exists();
        the_recovered_skydome_mismatches_are_pinned();
        display_names_match_the_retail_spelling();
        official_server_labels_resolve_to_local_environments();
        an_unknown_map_falls_back_rather_than_failing();
        every_environment_resolves_a_shipped_ambient_bed();
        the_spawn_rule_reproduces_the_authored_training_anchor();
        a_spawn_is_never_inside_geometry();
        a_map_with_only_the_bed_still_stands_above_it();
        std::cout << "map catalog and spawn tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
