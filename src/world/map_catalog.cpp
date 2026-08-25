#include "battlespades/world/map_catalog.hpp"

#include "battlespades/world/emissive_set.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace battlespades::world {
namespace {

struct SkydomeRow final {
    std::string_view stem;
    std::string_view skydome;
    SkydomeConfidence confidence;
};

struct AmbientRow final {
    std::string_view identity;
    std::string_view ambient;
};

/**
 * Reconstructed map-to-skydome table.
 *
 * Only four rows are evidenced, from the per-map `.txtc` sidecars retail shipped;
 * the rest are identified either by a dome-unique sky layer visible in retail's
 * loading art or, weakest, by name. Name similarity is explicitly NOT a rule --
 * retail's own UGC baseplates map SnowyBaseplate to ArcticBase and
 * WaterBaseplate to User_Grassland, and two rows below are outright mismatches.
 */
constexpr std::array<SkydomeRow, 27U> skydome_table{{
    {"Alcatraz", "Alcatraz.txt", SkydomeConfidence::inferred},
    {"AncientEgypt", "Egypt.txt", SkydomeConfidence::inferred},
    {"ArcticBase", "ArcticBase.txt", SkydomeConfidence::observed},
    {"Atlantis", "Atlantis.txt", SkydomeConfidence::observed},
    {"BlockNess", "Classic_B.txt", SkydomeConfidence::inferred},
    {"BranCastle", "BranCastle.txt", SkydomeConfidence::observed},
    // Volcanic cone, three layered ranges and three trailing fireballs are the
    // Invasion dome exactly; mesh/Classic has only a sphere and two cloud layers.
    {"CastleWars", "Invasion.txt", SkydomeConfidence::observed},
    {"CityOfChicago", "Chicago.txt", SkydomeConfidence::observed},
    {"Classic", "Classic.txt", SkydomeConfidence::inferred},
    {"Crossroads", "WW1.txt", SkydomeConfidence::inferred},
    // One diagonal meteor streak. SecretBase_Night is the only dome of the 27
    // with a meteor layer; stars alone do not discriminate, four domes have them.
    {"DoubleDragon", "SecretBase_Night.txt", SkydomeConfidence::observed},
    {"DragonIsland", "SecretBase.txt", SkydomeConfidence::evidenced},
    {"Frontier", "Frontier.txt", SkydomeConfidence::inferred},
    {"GreatWall", "GreatWall.txt", SkydomeConfidence::observed},
    {"Hiesville", "WW2.txt", SkydomeConfidence::inferred},
    {"Invasion", "Invasion.txt", SkydomeConfidence::inferred},
    {"London", "London.txt", SkydomeConfidence::inferred},
    {"LunarBase", "LunarBase.txt", SkydomeConfidence::observed},
    {"MayanJungle", "MayanJungle.txt", SkydomeConfidence::evidenced},
    {"SpookyMansion", "BranCastle.txt", SkydomeConfidence::evidenced},
    {"TheColosseum", "Colosseum.txt", SkydomeConfidence::inferred},
    {"ToTheBridge", "WW2_DockLands.txt", SkydomeConfidence::inferred},
    {"TokyoNeon", "Tokyo.txt", SkydomeConfidence::inferred},
    // No evidence of any kind, but it is what the tutorial already assumes.
    {"Training", "Classic_B.txt", SkydomeConfidence::inferred},
    {"Trenches", "WW1.txt", SkydomeConfidence::evidenced},
    {"WW1", "WW1.txt", SkydomeConfidence::observed},
    {"WinterValley", "ArcticBase.txt", SkydomeConfidence::inferred},
}};

/** Guaranteed present, and the most neutral sky to judge a map's lighting by. */
constexpr std::string_view default_skydome{"Classic_B.txt"};
constexpr std::string_view default_ambient{"amb_rural"};

/** Stock-map exceptions recovered from the map metadata loader. */
constexpr std::array<AmbientRow, 9U> map_ambient_table{{
    {"20thcenturytown", "amb_city"},
    {"alcatraz", "amb_alcatraz"},
    {"arcticbase", "amb_arctic"},
    {"castlewars", "amb_castlewars"},
    {"cityofchicago", "amb_oldchicago"},
    {"dragonisland", "amb_doomwind"},
    {"mayanjungle", "amb_jungle"},
    {"spookymansion", "amb_zombieisland"},
    {"trenches", "amb_ww_lighter"},
}};

/** Environment-family fallback used when a map has no explicit sound row. */
constexpr std::array<AmbientRow, 26U> skydome_ambient_table{{
    {"alcatraz", "amb_alcatraz"},
    {"arcticbase", "amb_arctic"},
    {"atlantis", "amb_harbour"},
    {"brancastle", "amb_castula"},
    {"chicago", "amb_oldchicago"},
    {"classic", "amb_rural"},
    {"classicb", "amb_rural"},
    {"colosseum", "amb_desert"},
    {"egypt", "amb_desert"},
    {"frontier", "amb_western"},
    {"greatwall", "amb_high"},
    {"invasion", "amb_invasion"},
    {"london", "amb_city"},
    {"lunarbase", "amb_moon"},
    {"mayanjungle", "amb_jungle"},
    {"secretbase", "amb_area51"},
    {"secretbasenight", "amb_area51"},
    {"tokyo", "amb_city"},
    {"userdesert", "amb_desert"},
    {"usergrassland", "amb_ww_lighter"},
    {"userlunar", "amb_moon"},
    {"usermountain", "amb_castula"},
    {"usertemple", "amb_invasion"},
    {"userurban", "amb_ww_lighter"},
    {"ww1", "amb_ww_lighter"},
    {"ww2", "amb_ww_coastalcold"},
}};

[[nodiscard]] std::string normalized_map_identity(std::string_view identity) {
    const auto separator = identity.find_last_of("/\\");
    if (separator != std::string_view::npos) {
        identity.remove_prefix(separator + 1U);
    }
    if (const auto extension = identity.find_last_of('.');
        extension != std::string_view::npos) {
        identity = identity.substr(0U, extension);
    }
    std::string normalized;
    normalized.reserve(identity.size());
    for (const auto character : identity) {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte) != 0) {
            normalized.push_back(static_cast<char>(std::tolower(byte)));
        }
    }
    return normalized;
}

[[nodiscard]] bool dome_exists(const std::filesystem::path& asset_root,
                               std::string_view dome) {
    if (!dome.ends_with(".txt")) {
        return false;
    }
    const std::string stem{dome.substr(0U, dome.size() - 4U)};
    std::error_code code;
    return std::filesystem::is_regular_file(
        asset_root / "mesh" / stem / std::string{dome}, code);
}

} // namespace

std::optional<OfficialMapEnvironment>
find_official_map_environment(std::string_view map_identity) {
    const auto wanted = normalized_map_identity(map_identity);
    if (wanted.empty()) {
        return std::nullopt;
    }
    for (const auto& row : skydome_table) {
        if (normalized_map_identity(row.stem) == wanted) {
            return OfficialMapEnvironment{row.stem, row.skydome, row.confidence};
        }
    }
    return std::nullopt;
}

std::string_view default_map_ambient(std::string_view map_identity,
                                     std::string_view skydome) {
    const auto map = normalized_map_identity(map_identity);
    if (const auto row =
            std::ranges::find(map_ambient_table, map, &AmbientRow::identity);
        row != map_ambient_table.end()) {
        return row->ambient;
    }
    const auto environment = normalized_map_identity(skydome);
    if (const auto row = std::ranges::find(
            skydome_ambient_table, environment, &AmbientRow::identity);
        row != skydome_ambient_table.end()) {
        return row->ambient;
    }
    // WW2_DockLands normalizes to ww2docklands and is the one shipped alias
    // that is not a direct skydome family name.
    if (environment == "ww2docklands") {
        return "amb_harbour";
    }
    return default_ambient;
}

std::string map_display_name(std::string_view stem) {
    // Insert a space before an uppercase letter that follows a lowercase one.
    // This reproduces retail's own spelling for every shipped stem, which the
    // loading screen needs: it matches its per-map art on an exact string.
    std::string result;
    result.reserve(stem.size() + 4U);
    for (std::size_t index{}; index < stem.size(); ++index) {
        const auto character = static_cast<unsigned char>(stem[index]);
        if (index > 0U && std::isupper(character) != 0 &&
            std::islower(static_cast<unsigned char>(stem[index - 1U])) != 0) {
            result.push_back(' ');
        }
        result.push_back(stem[index]);
    }
    return result;
}

std::string resolve_map_skydome(const std::filesystem::path& asset_root,
                                std::string_view stem,
                                SkydomeConfidence& confidence) {
    // A dome named after the map wins: an author who shipped one meant it, and
    // this makes UGC work with no table entry at all.
    const std::string same_name = std::string{stem} + ".txt";
    if (dome_exists(asset_root, same_name)) {
        const auto row = std::ranges::find(skydome_table, stem, &SkydomeRow::stem);
        confidence = row == skydome_table.end() ? SkydomeConfidence::fallback
                                                : row->confidence;
        return same_name;
    }
    if (const auto row = std::ranges::find(skydome_table, stem, &SkydomeRow::stem);
        row != skydome_table.end()) {
        confidence = row->confidence;
        return std::string{row->skydome};
    }
    confidence = SkydomeConfidence::fallback;
    return std::string{default_skydome};
}

std::vector<MapCatalogEntry> scan_map_catalog(const std::filesystem::path& asset_root) {
    std::vector<MapCatalogEntry> entries;
    std::error_code code;
    const std::array<std::filesystem::path, 2U> roots{
        asset_root / "maps",
        // User content, absent on a clean install; a missing directory is a
        // silent no-op rather than an error.
        asset_root.parent_path() / "user" / "maps",
    };
    for (const auto& root : roots) {
        for (std::filesystem::directory_iterator iterator{root, code}, end;
             !code && iterator != end; iterator.increment(code)) {
            if (!iterator->is_regular_file()) {
                continue;
            }
            const auto& path = iterator->path();
            // The shipped maps directory holds thumbnails and metadata sidecars
            // alongside the maps; offering those as rows would be dead entries.
            if (path.extension() != ".vxl") {
                continue;
            }
            MapCatalogEntry entry;
            entry.stem = path.stem().string();
            // A stem is joined onto an asset directory downstream, so anything
            // that could escape it is rejected outright rather than sanitised.
            const bool safe =
                !entry.stem.empty() && entry.stem.size() <= 64U &&
                entry.stem.find("..") == std::string::npos &&
                std::ranges::all_of(entry.stem, [](unsigned char character) {
                    return std::isalnum(character) != 0 || character == '_' ||
                           character == '-';
                });
            if (!safe) {
                continue;
            }
            entry.display_name = map_display_name(entry.stem);
            entry.skydome = resolve_map_skydome(asset_root, entry.stem, entry.confidence);
            entry.skydome_present = dome_exists(asset_root, entry.skydome);
            entry.emissive = !palette_is_empty(emissive_palette_for(entry.stem));
            entry.path = path;
            entries.push_back(std::move(entry));
        }
        code.clear();
    }
    std::ranges::sort(entries, {}, &MapCatalogEntry::display_name);
    return entries;
}

} // namespace battlespades::world
