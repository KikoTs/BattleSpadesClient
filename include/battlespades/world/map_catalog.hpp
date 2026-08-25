#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::world {

/**
 * How confident we are that a map's skydome is the one retail used.
 *
 * Retail shipped no client-side map-to-skydome table at all: the client is told
 * its skydome by the server through packet 51, and no shipped binary contains a
 * single `<Dome>.txt` literal. Anything here reconstructs server-side data, so
 * the confidence is worth surfacing rather than hiding.
 */
enum class SkydomeConfidence : std::uint8_t {
    /** Recovered from a per-map `.txtc` metadata sidecar retail actually shipped. */
    evidenced,
    /** A dome-unique sky layer is visibly present in retail's loading art. */
    observed,
    /** Name or era similarity only. */
    inferred,
    /** No table entry; a same-named dome on disk, or the neutral default. */
    fallback,
};

/** One selectable map. */
struct MapCatalogEntry final {
    /** Basename without extension, e.g. `TokyoNeon`. Never carries `.vxl`. */
    std::string stem;
    /** Retail spelling, e.g. `Tokyo Neon`; the loading art matches on this exactly. */
    std::string display_name;
    /** Always suffixed `.txt`, which WorldRenderer::set_skydome requires. */
    std::string skydome;
    std::filesystem::path path;
    SkydomeConfidence confidence{SkydomeConfidence::fallback};
    /** False when the resolved dome is not actually on disk. */
    bool skydome_present{};
    /** True when the map has an authored emissive palette. */
    bool emissive{};
};

/**
 * Locally shipped visual identity for a stock map advertised by a server.
 *
 * The string views point into the static official-map table and remain valid
 * for the process lifetime.
 */
struct OfficialMapEnvironment final {
    std::string_view stem;
    std::string_view skydome;
    SkydomeConfidence confidence{SkydomeConfidence::fallback};
};

/**
 * Match a server map label to one of the locally shipped official maps.
 *
 * Matching ignores harmless case, spacing and punctuation drift, so
 * `CityOfChicago`, `City of Chicago` and `City_Of_Chicago.vxl` identify the
 * same stock map. Unknown/UGC labels return null rather than receiving a
 * plausible but incorrect local atmosphere.
 */
[[nodiscard]] std::optional<OfficialMapEnvironment>
find_official_map_environment(std::string_view map_identity);

/**
 * Resolve the stock global ambient bed for a map/environment pair.
 *
 * Map-specific exceptions win over the skydome family (Chicago deliberately
 * uses `amb_oldchicago`, for example). Unknown UGC environments fall back to
 * retail's neutral `amb_rural`; a server-authored CreateAmbientSound packet
 * replaces this fallback as soon as it arrives.
 */
[[nodiscard]] std::string_view
default_map_ambient(std::string_view map_identity,
                    std::string_view skydome);

/**
 * Every map on disk, sorted by display name.
 *
 * Enumerated at runtime rather than generated, so a UGC map dropped into the
 * tree appears without a rebuild. Filters to `.vxl`: the shipped maps directory
 * holds 57 entries but only 27 maps, the rest being loading thumbnails and
 * metadata sidecars, and an unfiltered scan offers rows that cannot load.
 */
[[nodiscard]] std::vector<MapCatalogEntry> scan_map_catalog(
    const std::filesystem::path& asset_root);

/** The retail spelling of a stem, e.g. `TokyoNeon` to `Tokyo Neon`. */
[[nodiscard]] std::string map_display_name(std::string_view stem);

/**
 * The skydome for a map stem.
 *
 * Prefers a dome of the same name on disk, so an author who ships one gets it
 * without a table entry, then the recovered table, then the flat-cyan daytime
 * default, which is both guaranteed present and the most neutral lighting to
 * judge a map against.
 */
[[nodiscard]] std::string resolve_map_skydome(const std::filesystem::path& asset_root,
                                              std::string_view stem,
                                              SkydomeConfidence& confidence);

} // namespace battlespades::world
