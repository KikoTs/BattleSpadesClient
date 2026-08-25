#include "battlespades/render/skydome_animation.hpp"
#include "battlespades/world/map_atmosphere.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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

void skydome_time_matches_retail_draw_counter() {
    using battlespades::render::retail_skydome_time;
    expect(retail_skydome_time(-1.0F) == 0.0F,
           "skydome time must not scroll backwards before its epoch");
    expect(retail_skydome_time(0.5F) == 30.0F &&
               retail_skydome_time(1.0F) == 60.0F,
           "wall time must reproduce retail's one-counter-step-per-60Hz-draw clock");
}

[[nodiscard]] std::filesystem::path asset_root() {
    return std::filesystem::path{AOS_TEST_ASSET_ROOT};
}

[[nodiscard]] std::vector<std::string> every_dome() {
    std::vector<std::string> names;
    std::error_code code;
    for (const auto& entry : std::filesystem::directory_iterator{asset_root() / "mesh", code}) {
        if (!entry.is_directory()) {
            continue;
        }
        const auto stem = entry.path().filename().string();
        if (std::filesystem::is_regular_file(entry.path() / (stem + ".txt"))) {
            names.push_back(stem + ".txt");
        }
    }
    std::ranges::sort(names);
    return names;
}

[[nodiscard]] float luminance(const std::array<float, 3U>& color) {
    return 0.2126F * color[0U] + 0.7152F * color[1U] + 0.0722F * color[2U];
}

void every_shipped_dome_derives() {
    const auto domes = every_dome();
    expect(domes.size() >= 25U,
           "expected the full shipped skydome set, found " + std::to_string(domes.size()));
    std::vector<std::string> failures;
    for (const auto& dome : domes) {
        MapAtmosphere atmosphere;
        std::string error;
        if (!derive_map_atmosphere(asset_root(), dome, atmosphere, error)) {
            failures.push_back(dome + ": " + error);
        }
    }
    if (!failures.empty()) {
        std::string report = "domes that failed to derive:";
        for (const auto& entry : failures) {
            report += "\n  " + entry;
        }
        throw std::runtime_error{report};
    }
}

void no_map_is_too_dark_to_play() {
    // The whole point of the chroma/level split. LunarBase's raw sky luminance
    // is about 3% of Classic_B's; taken literally it is a black screen.
    for (const auto& dome : every_dome()) {
        const auto atmosphere = resolve_map_atmosphere(asset_root(), dome);
        expect(atmosphere.ambient_intensity >= 0.17F,
               dome + " fell below the playability floor (" +
                   std::to_string(atmosphere.ambient_intensity) + ")");
        expect(atmosphere.exposure >= 1.0F && atmosphere.exposure <= 3.0F,
               dome + " has an out-of-range exposure");
    }
}

void night_maps_stay_darker_than_day_maps() {
    const auto tokyo = resolve_map_atmosphere(asset_root(), "Tokyo.txt");
    const auto lunar = resolve_map_atmosphere(asset_root(), "LunarBase.txt");
    const auto classic = resolve_map_atmosphere(asset_root(), "Classic_B.txt");
    const auto egypt = resolve_map_atmosphere(asset_root(), "Egypt.txt");
    expect(tokyo.ambient_intensity < classic.ambient_intensity,
           "a moonlit night city must stay dimmer than a clear day sky");
    expect(lunar.ambient_intensity < egypt.ambient_intensity,
           "the lunar surface must stay dimmer than a desert noon");
    // Raised but not daylit: the floor must not erase the difference.
    expect(tokyo.ambient_intensity < 0.75F, "Tokyo must still read as night");
}

void night_maps_keep_their_colour() {
    // Chroma is untouched by the level floor, which is why a raised night sky
    // stays blue instead of going grey.
    const auto tokyo = resolve_map_atmosphere(asset_root(), "Tokyo.txt");
    expect(tokyo.sky_ambient[2U] > tokyo.sky_ambient[0U],
           "Tokyo's night sky ambient must remain blue-shifted after flooring");
}

void chicago_keeps_smoke_without_erasing_the_streets() {
    const auto chicago =
        resolve_map_atmosphere(asset_root(), "Chicago.txt");
    expect(chicago.fog_density <= 1.30F,
           "Chicago fog must not erase buildings at ordinary engagement range");
    expect(chicago.ambient_intensity >= 0.47F,
           "Chicago interiors need enough fill to remain navigable");
    expect(chicago.exposure >= 1.85F,
           "Chicago's authored lamps need enough exposure to read as practical lights");
}

void ambient_chroma_is_unit_luminance() {
    for (const auto& dome : every_dome()) {
        const auto atmosphere = resolve_map_atmosphere(asset_root(), dome);
        // Sky and sun are pure chroma: unit luminance, with the level carried
        // separately so a night map can be raised without losing its colour.
        for (const auto* color : {&atmosphere.sky_ambient, &atmosphere.sun_color}) {
            const float level = luminance(*color);
            expect(std::abs(level - 1.0F) < 0.12F,
                   dome + " has a chroma vector whose luminance is " +
                       std::to_string(level) + ", expected ~1.0");
        }
        // The ground term is deliberately NOT unit luminance: it carries the
        // measured sky-to-ground level ratio, because a hemispheric ambient that
        // is equally bright above and below contributes no shape at all.
        const float ground_level = luminance(atmosphere.ground_ambient);
        expect(ground_level > 0.40F && ground_level < 0.90F,
               dome + " ground ambient level is " + std::to_string(ground_level) +
                   ", outside the sky-to-ground ratio range");
        expect(ground_level < luminance(atmosphere.sky_ambient),
               dome + " ground bounce must be dimmer than skylight or floors and "
                      "ceilings light identically");
        // No channel may starve, or that channel goes black wherever it lights.
        for (const auto* color : {&atmosphere.sky_ambient, &atmosphere.ground_ambient,
                                  &atmosphere.sun_color}) {
            expect(std::ranges::all_of(*color, [](float channel) { return channel > 0.35F; }),
                   dome + " has a starved colour channel");
        }
    }
}

void ambient_is_nearly_neutral_so_blocks_keep_their_colour() {
    // A saturated sky must tint the world, not repaint it. Before the chroma
    // floor was raised, Classic_B's cyan gradient cut every block's red channel
    // by about a quarter and neutral grey stone rendered visibly teal.
    for (const auto& dome : every_dome()) {
        const auto atmosphere = resolve_map_atmosphere(asset_root(), dome);
        const auto& sky = atmosphere.sky_ambient;
        const float lowest = std::min({sky[0U], sky[1U], sky[2U]});
        const float highest = std::max({sky[0U], sky[1U], sky[2U]});
        expect(highest / std::max(lowest, 1.0e-4F) < 1.30F,
               dome + " ambient spread is " + std::to_string(highest / lowest) +
                   "; that much hue would visibly recolour every block");
    }
}

void sun_direction_is_a_unit_vector_above_the_horizon() {
    std::size_t with_key{};
    for (const auto& dome : every_dome()) {
        const auto atmosphere = resolve_map_atmosphere(asset_root(), dome);
        const auto& direction = atmosphere.sun_direction;
        const float length = std::sqrt(direction[0U] * direction[0U] +
                                        direction[1U] * direction[1U] +
                                        direction[2U] * direction[2U]);
        expect(std::abs(length - 1.0F) < 0.01F,
               dome + " sun direction is not normalised");
        ++with_key;
        // -z is up; a light must come from above.
        expect(direction[2U] < 0.0F,
               dome + " has a key light coming from below the horizon");
    }
    expect(with_key >= 10U,
           "expected at least ten domes to ship a recoverable sun or moon, found " +
               std::to_string(with_key));
}

void overcast_domes_keep_geometry_readable() {
    // A dome with no sun must not get a full-strength key, or it casts a
    // direction the painted sky contradicts. It must not drop to zero either:
    // hemispheric ambient separates only up from down, so on a uniform sky
    // every wall of a corridor would light identically and the voxel geometry
    // would stop reading. Both bounds matter.
    for (const auto& dome : {"Classic_B.txt", "User_Grassland.txt"}) {
        const auto atmosphere = resolve_map_atmosphere(asset_root(), dome);
        expect(atmosphere.key_intensity > 0.05F,
               std::string{dome} + " has no directional light at all; walls would "
                                   "be indistinguishable");
        expect(atmosphere.key_intensity < 0.35F,
               std::string{dome} + " ships no sun layer and must not get a "
                                   "full-strength key light");
    }
    // A dome that does ship a sun must out-key one that does not.
    const auto egypt = resolve_map_atmosphere(asset_root(), "Egypt.txt");
    const auto grassland = resolve_map_atmosphere(asset_root(), "User_Grassland.txt");
    expect(egypt.key_intensity > grassland.key_intensity,
           "a desert noon with a real sun must be more directional than an overcast sky");
}

void derived_fog_matches_the_retail_authored_colour() {
    // Retail's artists eyedropped the sky gradient's horizon band to author
    // FOG_COLORS, so sampling it should reproduce their values closely. These
    // are retail shared/constants.py FOG_COLORS entries.
    struct Expected final {
        const char* dome;
        std::array<int, 3U> retail;
        int tolerance;
    };
    constexpr std::array<Expected, 4U> cases{{
        {"Classic_B.txt", {111, 215, 223}, 6},
        {"Egypt.txt", {195, 116, 77}, 12},
        {"Alcatraz.txt", {78, 69, 67}, 12},
        {"User_Desert.txt", {195, 116, 77}, 24},
    }};
    for (const auto& entry : cases) {
        const auto atmosphere = resolve_map_atmosphere(asset_root(), entry.dome);
        for (std::size_t channel{}; channel < 3U; ++channel) {
            const int derived = static_cast<int>(atmosphere.fog_color[channel]);
            const int delta = std::abs(derived - entry.retail[channel]);
            expect(delta <= entry.tolerance,
                   std::string{entry.dome} + " fog channel " + std::to_string(channel) +
                       " derived " + std::to_string(derived) + " vs retail " +
                       std::to_string(entry.retail[channel]) + " (delta " +
                       std::to_string(delta) + ")");
        }
    }
}

void a_missing_dome_keeps_a_safe_default() {
    MapAtmosphere atmosphere;
    std::string error;
    expect(!derive_map_atmosphere(asset_root(), "NoSuchDome.txt", atmosphere, error),
           "a missing dome must fail rather than invent lighting");
    expect(!error.empty(), "a failed derivation must explain itself");
    // resolve() must still hand back something renderable.
    const auto resolved = resolve_map_atmosphere(asset_root(), "NoSuchDome.txt");
    expect(resolved.ambient_intensity > 0.0F,
           "a missing dome must still resolve to a lit default");
}

void path_traversal_is_rejected() {
    MapAtmosphere atmosphere;
    std::string error;
    for (const auto* name : {"../secret.txt", "a/b.txt", "a\\b.txt", "Tokyo", ""}) {
        expect(!derive_map_atmosphere(asset_root(), name, atmosphere, error),
               std::string{"unsafe dome name accepted: "} + name);
    }
}

void the_clamp_is_idempotent() {
    auto atmosphere = resolve_map_atmosphere(asset_root(), "Tokyo.txt");
    const auto once = atmosphere;
    clamp_atmosphere_for_play(atmosphere);
    expect(atmosphere.ambient_intensity == once.ambient_intensity &&
               atmosphere.key_intensity == once.key_intensity &&
               atmosphere.exposure == once.exposure,
           "clamping an already-clamped atmosphere must change nothing");
}

/**
 * The horizon probe must never land in a gradient's bottom clamp block.
 *
 * Several domes pad their lowest rows with one flat colour, and on Chicago and
 * MayanJungle that padding is effectively black. The fixed 2-degree probe
 * (V=0.889) walked straight into it on their taller 128-row gradients, so their
 * fog colour derived as near-black -- Chicago's authored orange fire-glow over a
 * burning city was thrown away, and its thick fog pulled the mid-distance toward
 * black instead of toward the fire.
 *
 * Guarded by luminance rather than by an exact colour so the test survives a
 * change in probe elevation: the point is that fog is not black, not that it is
 * one particular hex.
 */
void fog_never_derives_from_a_gradient_clamp_block() {
    for (const auto& dome : every_dome()) {
        const auto atmosphere = resolve_map_atmosphere(asset_root(), dome);
        const auto& fog = atmosphere.fog_color;
        const double level = (0.2126 * fog[0U] + 0.7152 * fog[1U] + 0.0722 * fog[2U]) / 255.0;
        expect(level > 0.02,
               dome + " derives a near-black fog colour (" + std::to_string(level) +
                   "), which is what sampling a gradient's bottom clamp looks like");
    }
}

/** Every authored override must survive the playability clamp intact. */
void authored_overrides_are_not_clamped_away() {
    struct Expectation final {
        const char* dome;
        const char* tag;
    };
    // One representative per correction class: a moon keyed down, a vacuum sun
    // keyed up, a dark-albedo map lifted, and a shared-asset pair.
    static constexpr std::array<Expectation, 5U> expectations{{
        {"Tokyo.txt", "Tokyo"},
        {"LunarBase.txt", "LunarBase"},
        {"Colosseum.txt", "Colosseum"},
        {"Classic_B.txt", "ClassicB"},
        {"User_Grassland.txt", "ClassicB"},
    }};
    for (const auto& expectation : expectations) {
        const auto atmosphere = resolve_map_atmosphere(asset_root(), expectation.dome);
        expect(atmosphere.source.find(expectation.tag) != std::string::npos,
               std::string{expectation.dome} + " did not apply its authored override");
    }

    // LunarBase is the sharpest case: a bare white sun in a black vacuum, where
    // the sky-brightness formula returns 0.38 and the authored value is 0.95. If
    // the clamp ever swallows that, the map silently returns to being lit by its
    // own vacuum.
    const auto lunar = resolve_map_atmosphere(asset_root(), "LunarBase.txt");
    expect(lunar.key_intensity > 0.85F,
           "LunarBase lost its authored vacuum key to a clamp (" +
               std::to_string(lunar.key_intensity) + ")");
    expect(lunar.key_intensity > lunar.ambient_intensity * 2.0F,
           "LunarBase's sun should dominate its ambient by a wide margin");
}

/**
 * Classic_B.txt and User_Grassland.txt bind the same gradient asset.
 *
 * They must therefore resolve identically apart from provenance. A divergence
 * here would be an authoring inconsistency rather than an artistic choice, and
 * it is easy to introduce by giving one of them its own override entry.
 */
void domes_sharing_a_gradient_resolve_identically() {
    const auto classic_b = resolve_map_atmosphere(asset_root(), "Classic_B.txt");
    const auto grassland = resolve_map_atmosphere(asset_root(), "User_Grassland.txt");
    expect(std::fabs(classic_b.key_intensity - grassland.key_intensity) < 1.0e-6F &&
               std::fabs(classic_b.ambient_intensity - grassland.ambient_intensity) < 1.0e-6F &&
               std::fabs(classic_b.fog_density - grassland.fog_density) < 1.0e-6F &&
               std::fabs(classic_b.specular_strength - grassland.specular_strength) < 1.0e-6F,
           "Classic_B and User_Grassland share one gradient but resolved differently");
}

/**
 * The brightest maps must not blow out.
 *
 * Classic_B is the binding case and the reason its ambient is authored at all.
 * BlockNess uses this dome, 45% of its surface sits above V 0.85, and its 90th
 * percentile albedo is 0.919. A fully lit face receives albedo * (ambient + key),
 * so keeping snow inside range requires ambient + key <= 1 / 0.919.
 *
 * Written as the inequality rather than as the literal 0.74, so raising the
 * overcast key constant -- which is shared with every sunless dome and would not
 * obviously implicate a snow map -- fails here instead of silently clipping
 * BlockNess to a flat white sheet.
 */
void the_brightest_maps_stay_inside_the_tonemap() {
    constexpr float block_ness_p90_albedo = 0.919F;
    for (const auto* dome : {"Classic_B.txt", "User_Grassland.txt"}) {
        const auto atmosphere = resolve_map_atmosphere(asset_root(), dome);
        const float lit = atmosphere.ambient_intensity + atmosphere.key_intensity;
        expect(lit * block_ness_p90_albedo <= 1.0F,
               std::string{dome} + " lights a 0.919-albedo surface to " +
                   std::to_string(lit * block_ness_p90_albedo) +
                   "; BlockNess's snow will clip to flat white");
    }
}

/** An authored sun direction must arrive normalised and above the horizon. */
void authored_sun_directions_are_normalised() {
    // Invasion is the only dome authoring one today: every warm layer in its sky
    // sits near azimuth 172 at low elevation, while the sunless fallback lights
    // from ~64 degrees, so the world was lit from a direction its sky denies.
    const auto invasion = resolve_map_atmosphere(asset_root(), "Invasion.txt");
    const auto& direction = invasion.sun_direction;
    const float length = std::sqrt((direction[0U] * direction[0U]) +
                                   (direction[1U] * direction[1U]) +
                                   (direction[2U] * direction[2U]));
    expect(std::fabs(length - 1.0F) < 1.0e-4F,
           "Invasion's authored sun direction is not a unit vector");
    // Canonical z grows downward, so a light above the horizon has negative z.
    expect(direction[2U] < 0.0F, "Invasion's authored sun points below the horizon");
}

} // namespace

int main() {
    try {
        skydome_time_matches_retail_draw_counter();
        every_shipped_dome_derives();
        no_map_is_too_dark_to_play();
        night_maps_stay_darker_than_day_maps();
        night_maps_keep_their_colour();
        chicago_keeps_smoke_without_erasing_the_streets();
        ambient_chroma_is_unit_luminance();
        ambient_is_nearly_neutral_so_blocks_keep_their_colour();
        sun_direction_is_a_unit_vector_above_the_horizon();
        overcast_domes_keep_geometry_readable();
        derived_fog_matches_the_retail_authored_colour();
        fog_never_derives_from_a_gradient_clamp_block();
        authored_overrides_are_not_clamped_away();
        domes_sharing_a_gradient_resolve_identically();
        the_brightest_maps_stay_inside_the_tonemap();
        authored_sun_directions_are_normalised();
        a_missing_dome_keeps_a_safe_default();
        path_traversal_is_rejected();
        the_clamp_is_idempotent();
        std::cout << "map atmosphere tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
