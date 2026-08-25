#include "battlespades/world/emissive_set.hpp"
#include "battlespades/world/emissive_volume.hpp"

#include <array>
#include <cstddef>
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

[[nodiscard]] VxlColor rgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
    return VxlColor{red, green, blue, 255U};
}

[[nodiscard]] VxlMap empty_world() {
    std::vector<std::byte> bytes;
    bytes.reserve(static_cast<std::size_t>(VxlMap::width) *
                  VxlMap::depth * 4U);
    for (std::size_t column{};
         column < static_cast<std::size_t>(VxlMap::width) * VxlMap::depth;
         ++column) {
        bytes.insert(bytes.end(),
                     {std::byte{0U}, std::byte{1U},
                      std::byte{0U}, std::byte{0U}});
    }
    auto loaded = VxlMap::load(bytes);
    expect(static_cast<bool>(loaded), "synthetic emissive world must parse");
    return std::move(*loaded.map);
}

void tokyo_neon_is_classified_as_emissive() {
    const auto tokyo = emissive_palette_for("TokyoNeon");
    expect(!palette_is_empty(tokyo), "TokyoNeon must have an authored palette");
    // The three colour families actually present in the map's voxel data: the
    // near-white tube, blue around hue 219, green around hue 126.
    constexpr std::array<std::array<std::uint8_t, 3U>, 8U> neon{{
        {0xF7U, 0xFFU, 0xDFU}, {0xF6U, 0xFFU, 0xDAU}, {0xF7U, 0xFAU, 0xEBU},
        {0x50U, 0x86U, 0xEBU}, {0x35U, 0x6DU, 0xD4U},
        {0x58U, 0xE7U, 0x67U}, {0x85U, 0xF1U, 0x90U}, {0x77U, 0xE8U, 0x82U},
    }};
    for (const auto& colour : neon) {
        expect(emissive_intensity(tokyo, rgb(colour[0U], colour[1U], colour[2U])) > 0U,
               "a shipped Tokyo neon colour must be emissive");
    }
}

void tolerance_absorbs_the_map_bake_shift() {
    // No prefab colour survives the map bake byte-identically; each channel
    // shifts by one to three. An exact-match allowlist would match nothing, so
    // the tolerance is load-bearing rather than a comfort margin.
    const auto tokyo = emissive_palette_for("TokyoNeon");
    expect(emissive_intensity(tokyo, rgb(0x34U, 0x6CU, 0xD4U)) > 0U,
           "the prefab source blue must still match the baked map blue");
    expect(emissive_intensity(tokyo, rgb(0xF4U, 0xFCU, 0xDCU)) > 0U,
           "the prefab source tube white must still match the baked map white");
    // But the tolerance must stay tight enough to be an identification.
    expect(emissive_intensity(tokyo, rgb(0x20U, 0x50U, 0xB0U)) == 0U,
           "a merely similar blue must not be swept in by the tolerance");
}

void ordinary_bright_blocks_never_glow() {
    // The whole reason for an allowlist over a colour rule. Every one of these
    // defeats a saturation-and-value or a luminance threshold.
    const auto tokyo = emissive_palette_for("TokyoNeon");
    struct Case final {
        const char* what;
        std::array<std::uint8_t, 3U> colour;
    };
    constexpr std::array<Case, 6U> cases{{
        {"a white wall", {0xE1U, 0xE1U, 0xE1U}},
        {"a bright red flag", {0xE1U, 0x20U, 0x12U}},
        {"Egypt's ocean", {0x12U, 0x84U, 0xC5U}},
        {"a pure green editor marker", {0x00U, 0xFFU, 0x00U}},
        {"the blue team marker", {0x00U, 0x28U, 0xBEU}},
        {"the green team marker", {0x00U, 0xBEU, 0x2AU}},
    }};
    for (const auto& entry : cases) {
        expect(emissive_intensity(tokyo, rgb(entry.colour[0U], entry.colour[1U],
                                             entry.colour[2U])) == 0U,
               std::string{entry.what} + " must not be treated as a light source");
    }
}

void maps_without_a_palette_emit_nothing() {
    // A palette cannot misfire on a map it does not name, which is the whole
    // safety argument for authoring over inference.
    for (const auto* name : {"Training", "AncientEgypt", "Alcatraz", "NoSuchMap"}) {
        const auto palette = emissive_palette_for(name);
        expect(palette_is_empty(palette),
               std::string{name} + " must have no emissive palette");
        // Even Tokyo's own neon colours must not glow on a map without a palette.
        expect(emissive_intensity(palette, rgb(0x58U, 0xE7U, 0x67U)) == 0U,
               std::string{name} + " must classify nothing as emissive");
    }
}

void team_markers_sit_just_outside_the_neon_windows() {
    // These are gameplay markers found in maps with no neon at all, and they sit
    // barely over a degree of hue outside the neon families. If a future change
    // widens a swatch until these match, neon identification is broken.
    const auto tokyo = emissive_palette_for("TokyoNeon");
    expect(emissive_intensity(tokyo, rgb(0x00U, 0x28U, 0xBEU)) == 0U &&
               emissive_intensity(tokyo, rgb(0x00U, 0xBEU, 0x2AU)) == 0U,
           "team markers must never be emissive; there is a hue cliff here");
}

/**
 * Every newly authored palette lights its own map's fixtures.
 *
 * One representative swatch each, at the exact as-baked value, so a typo in a
 * hex or a wrong array size fails loudly instead of quietly lighting nothing --
 * an empty palette and a mis-typed one look identical in a screenshot.
 */
void authored_palettes_light_their_own_fixtures() {
    struct Case final {
        const char* map;
        VxlColor color;
        const char* what;
    };
    const std::array<Case, 5U> cases{{
        {"CityOfChicago", rgb(0xFAU, 0xFFU, 0x50U), "gas street lamp"},
        {"Atlantis", rgb(0xFFU, 0xCCU, 0x07U), "brazier flame"},
        {"London", rgb(0xF5U, 0x88U, 0x16U), "gas lamp prop"},
        {"Invasion", rgb(0xFFU, 0xFFU, 0x00U), "volcanic fire"},
        {"TokyoNeon", rgb(0xFFU, 0xFFU, 0x04U), "traffic signal"},
    }};
    for (const auto& entry : cases) {
        const auto palette = emissive_palette_for(entry.map);
        expect(emissive_intensity(palette, entry.color) > 0U,
               std::string{entry.map} + " does not light its " + entry.what);
    }
    const auto chicago = emissive_palette_for("CityOfChicago");
    expect(chicago.cast_gain > emissive_palette_for("TokyoNeon").cast_gain,
           "sparse Chicago fixtures need more cast gain than dense Tokyo tubes");
    const auto heart =
        emissive_intensity(chicago, rgb(0xE4U, 0x33U, 0x34U));
    const auto lamp =
        emissive_intensity(chicago, rgb(0xFAU, 0xFFU, 0x50U));
    expect(heart > 0U && heart < lamp,
           "Chicago hearts must glow more softly than its street lamps");

    const auto heart_appearance =
        emissive_appearance(chicago, rgb(0xE4U, 0x33U, 0x34U));
    expect(heart_appearance.has_value() &&
               heart_appearance->surface.blue >
                   heart_appearance->surface.green * 2U &&
               heart_appearance->light.blue >
                   heart_appearance->light.green * 3U,
           "Chicago heart glass and spill must resolve to saturated neon pink");
    const auto lamp_appearance =
        emissive_appearance(chicago, rgb(0xFAU, 0xFFU, 0x50U));
    expect(lamp_appearance.has_value() &&
               lamp_appearance->light.red > lamp_appearance->light.green &&
               lamp_appearance->light.green > lamp_appearance->light.blue,
           "Chicago windows must cast warm occupied-room light");
}

void emissive_light_leaves_the_fixture_without_crossing_a_wall() {
    auto open = empty_world();
    expect(open.set_voxel(100U, 100U, 100U,
                          rgb(0xFAU, 0xFFU, 0x50U)),
           "Chicago lamp fixture must place");
    EmissiveVolume open_volume;
    open_volume.build(open, emissive_palette_for("CityOfChicago"), {});
    const auto arriving = open_volume.sample(104.5F, 100.5F, 100.5F);
    expect(arriving[0U] > 0.05F && arriving[1U] > 0.05F,
           "fixture light must leave its own solid voxel and reach nearby air");

    auto occluded = empty_world();
    expect(occluded.set_voxel(100U, 100U, 100U,
                              rgb(0xFAU, 0xFFU, 0x50U)) &&
               occluded.set_voxel(102U, 100U, 100U,
                                   rgb(0x40U, 0x40U, 0x40U)),
           "occluded fixture setup must place");
    EmissiveVolume blocked_volume;
    blocked_volume.build(occluded,
                         emissive_palette_for("CityOfChicago"), {});
    const auto blocked = blocked_volume.sample(104.5F, 100.5F, 100.5F);
    expect(blocked[0U] < 0.01F && blocked[1U] < 0.01F,
           "an intervening wall must stop Chicago lamp spill");

    auto neon = empty_world();
    expect(neon.set_voxel(100U, 100U, 100U,
                          rgb(0xE4U, 0x33U, 0x34U)),
           "Chicago heart fixture must place");
    EmissiveVolume neon_volume;
    neon_volume.build(neon, emissive_palette_for("CityOfChicago"), {});
    const auto pink = neon_volume.sample(104.5F, 100.5F, 100.5F);
    expect(pink[0U] > 0.10F && pink[2U] > pink[1U] * 3.0F,
           "Chicago heart must cast visible magenta light into nearby air");
}

void chicago_white_lamps_require_the_recovered_post_shape() {
    const auto chicago = emissive_palette_for("CityOfChicago");
    constexpr auto glass = VxlColor{0xFFU, 0xFFU, 0xFFU, 0xFFU};
    constexpr auto post = VxlColor{0x30U, 0x31U, 0x32U, 0xFFU};

    auto isolated = empty_world();
    expect(isolated.set_voxel(100U, 100U, 213U, glass),
           "isolated white fixture test voxel must place");
    expect(!emissive_appearance_at(chicago, isolated, 100U, 100U, 213U,
                                   glass)
                .has_value(),
           "a white wall or road mark must not glow without a lamp post");

    auto lamp = empty_world();
    expect(lamp.set_voxel(100U, 100U, 213U, glass),
           "streetlamp glass must place");
    for (std::uint32_t z{216U}; z <= 223U; ++z) {
        expect(lamp.set_voxel(103U, 100U, z, post),
               "streetlamp post run must place");
    }
    const auto appearance =
        emissive_appearance_at(chicago, lamp, 100U, 100U, 213U, glass);
    expect(appearance.has_value() && appearance->surface.red == 0xFFU &&
               appearance->surface.green > 0xF0U &&
               appearance->light.red > appearance->light.green &&
               appearance->light.green > appearance->light.blue,
           "white glass over the recovered dark post must emit warm lamp light");

    // Chicago's other sizeable #FFFFFF cluster is building trim at z=170..173.
    // Even an unfortunate dark support below it must not turn the architecture
    // into a light source.
    auto trim = empty_world();
    expect(trim.set_voxel(100U, 100U, 170U, glass),
           "white building trim must place");
    for (std::uint32_t z{173U}; z <= 180U; ++z) {
        expect(trim.set_voxel(100U, 100U, z, post),
               "trim support must place");
    }
    expect(!emissive_appearance_at(chicago, trim, 100U, 100U, 170U, glass)
                .has_value(),
           "Chicago building trim must remain non-emissive");

    EmissiveVolume volume;
    volume.build(lamp, chicago, {});
    const auto spill = volume.sample(96.5F, 100.5F, 213.5F);
    expect(spill[0U] > 0.05F && spill[0U] > spill[1U] &&
               spill[1U] > spill[2U],
           "shape-gated streetlamp glass must cast visible warm spill");
}

void london_fixtures_use_shape_and_height_without_lighting_the_road() {
    const auto london = emissive_palette_for("London");
    constexpr auto glass = VxlColor{0xE8U, 0xFEU, 0xFEU, 0xFFU};
    constexpr auto amber = VxlColor{0xF5U, 0x88U, 0x16U, 0xFFU};
    constexpr auto tail = VxlColor{0x62U, 0x15U, 0x18U, 0xFFU};
    constexpr auto post = VxlColor{0x30U, 0x31U, 0x32U, 0xFFU};
    constexpr auto bus_sign = VxlColor{0xFFU, 0xFFU, 0xFFU, 0xFFU};
    constexpr auto bus_panel = VxlColor{0xE8U, 0xE9U, 0xE9U, 0xFFU};
    constexpr auto bus_cap = VxlColor{0xE4U, 0x33U, 0x34U, 0xFFU};

    auto fixtures = empty_world();

    // Big Ben's four dials occupy z=144..158 in the shipped map. Its glass is
    // safe only in that recovered height band.
    expect(fixtures.set_voxel(100U, 100U, 150U, glass),
           "London clock glass must place");
    const auto clock =
        emissive_appearance_at(london, fixtures, 100U, 100U, 150U, glass);
    expect(clock.has_value() && clock->intensity == 255U &&
               clock->surface.red > clock->surface.blue &&
               clock->light.red > clock->light.green,
           "Big Ben clock glass must emit readable warm light");

    // A London taxi has two white headlights, each paired vertically with its
    // amber marker two voxels below. An isolated road-white voxel must not pass.
    expect(fixtures.set_voxel(120U, 120U, 223U, glass) &&
               fixtures.set_voxel(120U, 120U, 225U, amber),
           "London taxi light signature must place");
    const auto headlight =
        emissive_appearance_at(london, fixtures, 120U, 120U, 223U, glass);
    expect(headlight.has_value() && headlight->intensity == 255U &&
               headlight->surface.green > 0xF0U,
           "paired London taxi headlight must emit");
    const auto taillight = emissive_appearance(london, tail);
    expect(taillight.has_value() && taillight->light.red >
               taillight->light.green * 6U,
           "London taxi rear lamps must cast saturated red light");
    expect(fixtures.set_voxel(121U, 120U, 223U, glass),
           "isolated London road-white voxel must place");
    expect(!emissive_appearance_at(london, fixtures, 121U, 120U, 223U,
                                   glass)
                .has_value(),
           "unpaired London road-white must not become a headlight");

    // London streetlights use paired 5x2 glass panels in a tall neutral-metal
    // housing. The pole begins eleven blocks under the glass plane.
    for (std::uint32_t y{139U}; y <= 140U; ++y) {
        for (std::uint32_t x{138U}; x <= 142U; ++x) {
            expect(fixtures.set_voxel(x, y, 194U, glass),
                   "London lamp panel must place");
        }
    }
    for (std::uint32_t z{205U}; z <= 210U; ++z) {
        expect(fixtures.set_voxel(140U, 140U, z, post),
               "London lamp post must place");
    }
    const auto lamp =
        emissive_appearance_at(london, fixtures, 140U, 140U, 194U, glass);
    expect(lamp.has_value() && lamp->intensity == 250U &&
               lamp->light.red > lamp->light.green &&
               lamp->light.green > lamp->light.blue,
           "recovered London lamp housing must cast warm light");

    // The same glass is used for large pavement sheets around z=230. Even a
    // nearby dark block is not enough without the recovered housing height.
    for (std::uint32_t x{160U}; x <= 168U; ++x) {
        expect(fixtures.set_voxel(x, 160U, 230U, glass),
               "London pavement strip must place");
    }
    expect(!emissive_appearance_at(london, fixtures, 164U, 160U, 230U,
                                   glass)
                .has_value(),
           "London kerbs and pavement must remain non-emissive");

    // Six further street/riverside fixtures use solid 3x3x3 white heads on
    // dark posts. The full head and post are both required because this exact
    // white also paints roads, signs and station trim.
    for (std::uint32_t z{226U}; z <= 228U; ++z) {
        for (std::uint32_t y{199U}; y <= 201U; ++y) {
            for (std::uint32_t x{199U}; x <= 201U; ++x) {
                expect(fixtures.set_voxel(x, y, z, glass),
                       "London cube-lamp head must place");
            }
        }
    }
    for (std::uint32_t z{229U}; z <= 234U; ++z) {
        expect(fixtures.set_voxel(200U, 200U, z, post),
               "London cube-lamp post must place");
    }
    const auto box_lamp =
        emissive_appearance_at(london, fixtures, 200U, 200U, 227U, glass);
    expect(box_lamp.has_value() && box_lamp->intensity == 160U &&
               box_lamp->light.red > box_lamp->light.green,
           "post-mounted London cube lamp must cast warm light");

    auto unsupported_cube = empty_world();
    for (std::uint32_t z{226U}; z <= 228U; ++z) {
        for (std::uint32_t y{199U}; y <= 201U; ++y) {
            for (std::uint32_t x{199U}; x <= 201U; ++x) {
                expect(unsupported_cube.set_voxel(x, y, z, glass),
                       "unsupported white cube must place");
            }
        }
    }
    expect(!emissive_appearance_at(london, unsupported_cube, 200U, 200U,
                                   227U, glass)
                .has_value(),
           "white cube without a post must remain matte");

    // Underground platforms use exact six-voxel fluorescent strips. Identical
    // strips outside the three station volumes are ordinary white trim.
    for (std::uint32_t x{190U}; x <= 195U; ++x) {
        expect(fixtures.set_voxel(x, 330U, 231U, glass),
               "London Underground fluorescent strip must place");
    }
    const auto station_strip =
        emissive_appearance_at(london, fixtures, 192U, 330U, 231U, glass);
    expect(station_strip.has_value() && station_strip->intensity == 165U &&
               station_strip->light.blue > station_strip->light.red,
           "London Underground fluorescent strip must cast cool light");
    for (std::uint32_t x{100U}; x <= 105U; ++x) {
        expect(fixtures.set_voxel(x, 100U, 231U, glass),
               "ordinary six-voxel London trim must place");
    }
    expect(!emissive_appearance_at(london, fixtures, 102U, 100U, 231U,
                                   glass)
                .has_value(),
           "six-voxel white trim outside a station must remain matte");

    // Both station entrances carry a lit red-and-white Underground roundel.
    // The same colours elsewhere remain paint rather than light.
    constexpr auto roundel_red = VxlColor{0xE4U, 0x33U, 0x34U, 0xFFU};
    expect(fixtures.set_voxel(300U, 317U, 216U, roundel_red) &&
               fixtures.set_voxel(301U, 317U, 218U, glass),
           "London Underground roundel must place");
    const auto roundel_ring = emissive_appearance_at(
        london, fixtures, 300U, 317U, 216U, roundel_red);
    const auto roundel_bar =
        emissive_appearance_at(london, fixtures, 301U, 317U, 218U, glass);
    expect(roundel_ring.has_value() && roundel_bar.has_value() &&
               roundel_ring->light.red > roundel_ring->light.green * 4U &&
               roundel_bar->light.red > roundel_bar->light.blue,
           "London Underground roundel ring and name bar must emit");
    expect(!emissive_appearance_at(london, fixtures, 100U, 100U, 216U,
                                   roundel_red)
                .has_value(),
           "ordinary London red paint must remain matte");

    // Four bus stops use a 2x1x4 white sign with a red cap and a nearby 3x1x2
    // shelter light. Neither white material is safe without the paired prefab
    // geometry: both also occur in ordinary road and building trim.
    for (std::uint32_t z{216U}; z <= 219U; ++z) {
        for (std::uint32_t x{200U}; x <= 201U; ++x) {
            expect(fixtures.set_voxel(x, 200U, z, bus_sign),
                   "London bus-stop sign face must place");
        }
    }
    expect(fixtures.set_voxel(202U, 200U, 215U, bus_cap),
           "London bus-stop red cap must place");
    for (std::uint32_t z{220U}; z <= 221U; ++z) {
        for (std::uint32_t x{210U}; x <= 212U; ++x) {
            expect(fixtures.set_voxel(x, 200U, z, bus_panel),
                   "London bus shelter light panel must place");
        }
    }
    const auto sign =
        emissive_appearance_at(london, fixtures, 200U, 200U, 216U, bus_sign);
    const auto shelter =
        emissive_appearance_at(london, fixtures, 210U, 200U, 220U, bus_panel);
    expect(sign.has_value() && shelter.has_value() &&
               sign->light.red > sign->light.blue &&
               shelter->light.red > shelter->light.green,
           "paired London bus-stop sign and shelter panel must cast warm light");

    auto isolated_bus_panel = empty_world();
    for (std::uint32_t z{220U}; z <= 221U; ++z) {
        for (std::uint32_t x{210U}; x <= 212U; ++x) {
            expect(isolated_bus_panel.set_voxel(x, 200U, z, bus_panel),
                   "isolated London white panel must place");
        }
    }
    expect(!emissive_appearance_at(london, isolated_bus_panel, 210U, 200U,
                                   220U, bus_panel)
                .has_value(),
           "ordinary six-voxel white trim must not become a bus shelter");

    // The small bridge lantern caps have exact map-wide materials confined to
    // that prefab. Adjacent rail metal must stay matte.
    const auto bridge_cap =
        emissive_appearance(london, rgb(0x87U, 0x8AU, 0x8DU));
    expect(bridge_cap.has_value() && bridge_cap->light.red >
               bridge_cap->light.green,
           "London bridge rail lantern cap must cast warm light");
    expect(!emissive_appearance(london, rgb(0x82U, 0x84U, 0x87U)).has_value(),
           "London bridge rail metal beside a lantern must remain matte");

    // Recovered Parliament panes sit in repeated slots on the y=299 facade.
    // The brown frame at the same coordinate band is not a light source.
    constexpr auto cool_pane = VxlColor{0x2AU, 0x39U, 0x3DU, 0xFFU};
    constexpr auto brown_frame = VxlColor{0x3CU, 0x23U, 0x01U, 0xFFU};
    expect(fixtures.set_voxel(362U, 299U, 215U, cool_pane),
           "London Parliament window pane must place");
    const auto occupied_window =
        emissive_appearance_at(london, fixtures, 362U, 299U, 215U, cool_pane);
    expect(occupied_window.has_value() &&
               occupied_window->surface.red > occupied_window->surface.blue &&
               occupied_window->light.red > occupied_window->light.green,
           "London Parliament window must resolve to warm occupied-room light");
    expect(!emissive_appearance_at(london, fixtures, 362U, 299U, 215U,
                                   brown_frame)
                .has_value(),
           "London Parliament brown window frame must remain matte");
    constexpr auto tower_pane = VxlColor{0x55U, 0x6BU, 0x76U, 0xFFU};
    expect(fixtures.set_voxel(347U, 296U, 204U, tower_pane) &&
               fixtures.set_voxel(354U, 303U, 205U, tower_pane),
           "London Parliament recessed window back planes must place");
    expect(emissive_appearance_at(london, fixtures, 347U, 296U, 204U,
                                  tower_pane)
               .has_value() &&
               emissive_appearance_at(london, fixtures, 354U, 303U, 205U,
                                      tower_pane)
                   .has_value(),
           "London Parliament tower panes must light");
    expect(!emissive_appearance_at(london, fixtures, 347U, 297U, 204U,
                                   brown_frame)
                .has_value(),
           "London Parliament tower frame between panes must remain matte");
    constexpr auto courtyard_pane =
        VxlColor{0x47U, 0x30U, 0x00U, 0xFFU};
    constexpr auto courtyard_frame =
        VxlColor{0x57U, 0x3AU, 0x00U, 0xFFU};
    expect(fixtures.set_voxel(363U, 241U, 220U, courtyard_pane),
           "London courtyard pane must place");
    expect(emissive_appearance_at(london, fixtures, 363U, 241U, 220U,
                                  courtyard_pane)
               .has_value(),
           "visible London courtyard window pane must light");
    expect(!emissive_appearance_at(london, fixtures, 363U, 241U, 220U,
                                   courtyard_frame)
                .has_value(),
           "London courtyard window frame must remain matte");

    expect(london.cast_gain > emissive_palette_for("TokyoNeon").cast_gain,
           "sparse London fixtures need stronger cast gain than Tokyo tubes");

    EmissiveVolume volume;
    volume.build(fixtures, london, {});
    const auto spill = volume.sample(135.5F, 140.5F, 194.5F);
    expect(spill[0U] > 0.05F && spill[0U] > spill[1U] &&
               spill[1U] > spill[2U],
           "London streetlamp must cast visible warm spill through the smog");
}

/**
 * Palettes must not reach into each other's colours.
 *
 * The tolerance windows are wide enough to absorb the map bake's channel shift,
 * which means widening one by a single step can silently make it swallow another
 * map's fixture colour. Chicago's lamp family is the live example: at tolerance
 * 5 it begins to overlap SpookyMansion's #FFFF52 candle, and the two maps would
 * start lighting each other's vocabulary. Since intensity differs per map, that
 * shows up as a fixture lit at the wrong strength rather than as an obvious bug.
 */
void palettes_do_not_claim_another_maps_fixtures() {
    struct Foreign final {
        const char* map;
        VxlColor color;
        const char* owner;
    };
    const std::array<Foreign, 6U> foreign{{
        {"CityOfChicago", rgb(0xFFU, 0xFFU, 0x52U), "SpookyMansion's candle"},
        {"CityOfChicago", rgb(0xF7U, 0xFFU, 0xDFU), "Tokyo's neon tube"},
        {"SpookyMansion", rgb(0xFAU, 0xFFU, 0x50U), "Chicago's street lamp"},
        {"London", rgb(0xFFU, 0xCCU, 0x07U), "Atlantis's brazier"},
        {"Invasion", rgb(0xFAU, 0xFAU, 0xC8U), "SpookyMansion's cream flame"},
        {"Atlantis", rgb(0xFFU, 0xFFU, 0x00U), "Invasion's volcanic fire"},
    }};
    for (const auto& entry : foreign) {
        const auto palette = emissive_palette_for(entry.map);
        expect(emissive_intensity(palette, entry.color) == 0U,
               std::string{entry.map} + " claims " + entry.owner);
    }
}

/**
 * Maps the survey measured as having no fixtures must stay dark.
 *
 * These are the ones a brightness or saturation heuristic gets wrong, and each
 * was checked by cluster shape rather than by colour: Egypt's amber is terrain
 * plates, CastleWars' bright set is an achromatic strip, DragonIsland's is lit
 * jungle canopy, and DockLands' pure white is a solid painted cube.
 */
void measured_fixtureless_maps_stay_dark() {
    struct Case final {
        const char* map;
        VxlColor color;
    };
    const std::array<Case, 4U> cases{{
        {"AncientEgypt", rgb(0xFFU, 0xC4U, 0x00U)},
        {"CastleWars", rgb(0xFFU, 0xFFU, 0xFFU)},
        {"DragonIsland", rgb(0x78U, 0x8EU, 0x32U)},
        {"WW2_DockLands", rgb(0xFFU, 0xFFU, 0xFFU)},
    }};
    for (const auto& entry : cases) {
        const auto palette = emissive_palette_for(entry.map);
        expect(palette_is_empty(palette),
               std::string{entry.map} + " gained a palette it was measured not to need");
        expect(emissive_intensity(palette, entry.color) == 0U,
               std::string{entry.map} + " lit a colour measured to be architecture");
    }
}

} // namespace

int main() {
    try {
        tokyo_neon_is_classified_as_emissive();
        authored_palettes_light_their_own_fixtures();
        emissive_light_leaves_the_fixture_without_crossing_a_wall();
        chicago_white_lamps_require_the_recovered_post_shape();
        london_fixtures_use_shape_and_height_without_lighting_the_road();
        palettes_do_not_claim_another_maps_fixtures();
        measured_fixtureless_maps_stay_dark();
        tolerance_absorbs_the_map_bake_shift();
        ordinary_bright_blocks_never_glow();
        maps_without_a_palette_emit_nothing();
        team_markers_sit_just_outside_the_neon_windows();
        std::cout << "emissive set tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
