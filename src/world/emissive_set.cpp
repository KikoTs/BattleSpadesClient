#include "battlespades/world/emissive_set.hpp"

#include <algorithm>
#include <cstdlib>

namespace battlespades::world {
namespace {

[[nodiscard]] bool exact_rgb(VxlColor color, std::uint8_t red,
                             std::uint8_t green,
                             std::uint8_t blue) noexcept {
    return color.red == red && color.green == green && color.blue == blue;
}

[[nodiscard]] bool exact_rgb_at(const VxlMap& map, std::int64_t x,
                                std::int64_t y, std::int64_t z,
                                std::uint8_t red, std::uint8_t green,
                                std::uint8_t blue) noexcept {
    if (x < 0 || y < 0 || z < 0 ||
        x >= static_cast<std::int64_t>(VxlMap::width) ||
        y >= static_cast<std::int64_t>(VxlMap::depth) ||
        z >= static_cast<std::int64_t>(VxlMap::height)) {
        return false;
    }
    const auto color =
        map.color(static_cast<std::uint32_t>(x),
                  static_cast<std::uint32_t>(y),
                  static_cast<std::uint32_t>(z));
    return color.has_value() && exact_rgb(*color, red, green, blue);
}

[[nodiscard]] bool dark_neutral_at(const VxlMap& map, std::int64_t x,
                                   std::int64_t y,
                                   std::int64_t z) noexcept {
    if (x < 0 || y < 0 || z < 0 ||
        x >= static_cast<std::int64_t>(VxlMap::width) ||
        y >= static_cast<std::int64_t>(VxlMap::depth) ||
        z >= static_cast<std::int64_t>(VxlMap::height)) {
        return false;
    }
    const auto color =
        map.color(static_cast<std::uint32_t>(x),
                  static_cast<std::uint32_t>(y),
                  static_cast<std::uint32_t>(z));
    if (!color.has_value()) {
        return false;
    }
    const auto maximum = std::max({color->red, color->green, color->blue});
    const auto minimum = std::min({color->red, color->green, color->blue});
    return maximum <= 0x42U && maximum - minimum <= 4;
}

[[nodiscard]] bool neutral_metal_at(const VxlMap& map, std::int64_t x,
                                    std::int64_t y,
                                    std::int64_t z) noexcept {
    if (x < 0 || y < 0 || z < 0 ||
        x >= static_cast<std::int64_t>(VxlMap::width) ||
        y >= static_cast<std::int64_t>(VxlMap::depth) ||
        z >= static_cast<std::int64_t>(VxlMap::height)) {
        return false;
    }
    const auto color =
        map.color(static_cast<std::uint32_t>(x),
                  static_cast<std::uint32_t>(y),
                  static_cast<std::uint32_t>(z));
    if (!color.has_value()) {
        return false;
    }
    const auto maximum = std::max({color->red, color->green, color->blue});
    const auto minimum = std::min({color->red, color->green, color->blue});
    // London lamp housings/posts use #4A4C4D and #606264 ramps.
    return minimum >= 0x20U && maximum <= 0x70U &&
           maximum - minimum <= 8;
}

[[nodiscard]] bool chicago_streetlamp_glass(const EmissivePalette& palette,
                                             const VxlMap& map,
                                             std::uint32_t x, std::uint32_t y,
                                             std::uint32_t z,
                                             VxlColor color) noexcept {
    // Recovered from the shipped CityOfChicago VXL. Each park/road lamp uses
    // eight isolated #FFFFFF glass voxels around z=213/214. The other sizeable
    // #FFFFFF set is building trim at z=170..173, so the height gate is both
    // measured and load-bearing.
    if (palette.map_name != "CityOfChicago" || color.red != 0xFFU ||
        color.green != 0xFFU || color.blue != 0xFFU || z < 210U) {
        return false;
    }

    const auto dark_post = [&](std::int64_t probe_x, std::int64_t probe_y,
                               std::int64_t probe_z) {
        if (probe_x < 0 || probe_y < 0 || probe_z < 0 ||
            probe_x >= static_cast<std::int64_t>(VxlMap::width) ||
            probe_y >= static_cast<std::int64_t>(VxlMap::depth) ||
            probe_z >= static_cast<std::int64_t>(VxlMap::height)) {
            return false;
        }
        const auto post = map.color(static_cast<std::uint32_t>(probe_x),
                                    static_cast<std::uint32_t>(probe_y),
                                    static_cast<std::uint32_t>(probe_z));
        if (!post.has_value()) {
            return false;
        }
        const auto maximum = std::max({post->red, post->green, post->blue});
        const auto minimum = std::min({post->red, post->green, post->blue});
        // The authored post ramp is #212222..#393A3B. Keeping both a value
        // ceiling and a neutral-colour band rejects dark brick and foliage.
        return maximum <= 0x42U && maximum - minimum <= 4;
    };

    // A glass voxel can sit three blocks from the shared centre post. Search a
    // tight footprint and require eight consecutive dark voxels below the
    // housing; white road paint and windows have no such support.
    for (std::int64_t offset_y{-4}; offset_y <= 4; ++offset_y) {
        for (std::int64_t offset_x{-4}; offset_x <= 4; ++offset_x) {
            std::uint32_t run{};
            for (std::int64_t offset_z{3}; offset_z <= 20; ++offset_z) {
                if (dark_post(static_cast<std::int64_t>(x) + offset_x,
                              static_cast<std::int64_t>(y) + offset_y,
                              static_cast<std::int64_t>(z) + offset_z)) {
                    if (++run >= 8U) {
                        return true;
                    }
                } else {
                    run = 0U;
                }
            }
        }
    }
    return false;
}

[[nodiscard]] bool london_clock_glass(const EmissivePalette& palette,
                                      std::uint32_t z,
                                      VxlColor color) noexcept {
    // The shipped London VXL has four 11x11 clock dials at z=144..158. The
    // shared #E8FEFE material also occurs on kerbs around z=230, so height is
    // an essential part of the authored fixture identity.
    return palette.map_name == "London" &&
           exact_rgb(color, 0xE8U, 0xFEU, 0xFEU) && z >= 140U && z <= 160U;
}

[[nodiscard]] bool london_vehicle_headlight(const EmissivePalette& palette,
                                            const VxlMap& map,
                                            std::uint32_t x,
                                            std::uint32_t y,
                                            std::uint32_t z,
                                            VxlColor color) noexcept {
    if (palette.map_name != "London" ||
        !exact_rgb(color, 0xE8U, 0xFEU, 0xFEU) || z < 218U || z > 225U ||
        z + 2U >= VxlMap::height) {
        return false;
    }

    // Both baked London taxis preserve the prefab's two-block vertical lamp
    // signature: cool-white headlight at z, amber marker at z+2. Requiring
    // that pair rejects the identically coloured clock, kerbs and pavement.
    const auto marker = map.color(x, y, z + 2U);
    return marker.has_value() &&
           exact_rgb(*marker, 0xF5U, 0x88U, 0x16U);
}

[[nodiscard]] bool london_streetlamp_glass(const EmissivePalette& palette,
                                           const VxlMap& map,
                                           std::uint32_t x,
                                           std::uint32_t y,
                                           std::uint32_t z,
                                           VxlColor color) noexcept {
    if (palette.map_name != "London" ||
        !exact_rgb(color, 0xE8U, 0xFEU, 0xFEU) || z < 190U || z > 200U) {
        return false;
    }

    // Each of the four recovered double-head streetlights has two 5x2 glass
    // panels at z=194, separated by one row inside a #606264 housing. A 9x9
    // neighbourhood around any glass voxel therefore contains at least one
    // complete ten-voxel panel.
    std::uint32_t glass_in_layer{};
    for (std::int64_t offset_y{-4}; offset_y <= 4; ++offset_y) {
        for (std::int64_t offset_x{-4}; offset_x <= 4; ++offset_x) {
            const auto probe_x = static_cast<std::int64_t>(x) + offset_x;
            const auto probe_y = static_cast<std::int64_t>(y) + offset_y;
            if (probe_x < 0 || probe_y < 0 ||
                probe_x >= static_cast<std::int64_t>(VxlMap::width) ||
                probe_y >= static_cast<std::int64_t>(VxlMap::depth)) {
                continue;
            }
            const auto neighbour =
                map.color(static_cast<std::uint32_t>(probe_x),
                          static_cast<std::uint32_t>(probe_y), z);
            if (neighbour.has_value() &&
                exact_rgb(*neighbour, 0xE8U, 0xFEU, 0xFEU)) {
                ++glass_in_layer;
            }
        }
    }
    if (glass_in_layer < 10U) {
        return false;
    }

    // The housing is 12 blocks deep and the pole begins below it. Require five
    // consecutive neutral-metal voxels in that support footprint; signboards,
    // clock faces and pavement do not have this double-head-plus-pole shape.
    for (std::int64_t offset_y{-3}; offset_y <= 3; ++offset_y) {
        for (std::int64_t offset_x{-3}; offset_x <= 3; ++offset_x) {
            std::uint32_t run{};
            for (std::int64_t offset_z{5}; offset_z <= 30; ++offset_z) {
                if (neutral_metal_at(
                        map, static_cast<std::int64_t>(x) + offset_x,
                        static_cast<std::int64_t>(y) + offset_y,
                        static_cast<std::int64_t>(z) + offset_z)) {
                    if (++run >= 5U) {
                        return true;
                    }
                } else {
                    run = 0U;
                }
            }
        }
    }
    return false;
}

[[nodiscard]] bool london_box_lamp_glass(const EmissivePalette& palette,
                                         const VxlMap& map, std::uint32_t x,
                                         std::uint32_t y, std::uint32_t z,
                                         VxlColor color) noexcept {
    if (palette.map_name != "London" ||
        !exact_rgb(color, 0xE8U, 0xFEU, 0xFEU) || z < 226U || z > 228U) {
        return false;
    }

    // Six riverside/road fixtures are filled 3x3x3 glass cubes. Earlier
    // histogram-only work mistook their close-up silhouette for signboards;
    // the shipped map and rendered player captures both show a dark post below
    // every cube. Require the complete head plus that post so kerbs and actual
    // white signs remain matte.
    std::uint32_t glass{};
    for (std::int64_t probe_z{226}; probe_z <= 228; ++probe_z) {
        for (std::int64_t offset_y{-2}; offset_y <= 2; ++offset_y) {
            for (std::int64_t offset_x{-2}; offset_x <= 2; ++offset_x) {
                if (exact_rgb_at(map, static_cast<std::int64_t>(x) + offset_x,
                                 static_cast<std::int64_t>(y) + offset_y,
                                 probe_z, 0xE8U, 0xFEU, 0xFEU)) {
                    ++glass;
                }
            }
        }
    }
    if (glass < 27U) {
        return false;
    }

    for (std::int64_t offset_y{-2}; offset_y <= 2; ++offset_y) {
        for (std::int64_t offset_x{-2}; offset_x <= 2; ++offset_x) {
            std::uint32_t run{};
            for (std::int64_t probe_z{229}; probe_z <= 238; ++probe_z) {
                if (dark_neutral_at(
                        map, static_cast<std::int64_t>(x) + offset_x,
                        static_cast<std::int64_t>(y) + offset_y, probe_z)) {
                    if (++run >= 5U) {
                        return true;
                    }
                } else {
                    run = 0U;
                }
            }
        }
    }
    return false;
}

[[nodiscard]] bool london_underground_strip(const EmissivePalette& palette,
                                            const VxlMap& map,
                                            std::uint32_t x,
                                            std::uint32_t y,
                                            std::uint32_t z,
                                            VxlColor color) noexcept {
    if (palette.map_name != "London" ||
        !exact_rgb(color, 0xE8U, 0xFEU, 0xFEU) || z < 230U || z > 233U) {
        return false;
    }

    // Three underground platform/tunnel volumes contain repeated six-voxel
    // fluorescent strips. The same white is used by surface roads, so both the
    // authored station bounds and exact linear fixture length are required.
    const bool in_station =
        (x >= 189U && x <= 236U && y >= 326U && y <= 353U) ||
        (x >= 299U && x <= 325U && y >= 247U && y <= 306U) ||
        (x >= 343U && x <= 390U && y >= 326U && y <= 353U);
    if (!in_station) {
        return false;
    }

    const auto run_length = [&](std::int64_t step_x,
                                std::int64_t step_y) noexcept {
        std::uint32_t length{1U};
        for (std::int64_t direction : {-1, 1}) {
            for (std::int64_t distance{1}; distance <= 6; ++distance) {
                if (!exact_rgb_at(
                        map, static_cast<std::int64_t>(x) +
                                 direction * distance * step_x,
                        static_cast<std::int64_t>(y) +
                            direction * distance * step_y,
                        z, 0xE8U, 0xFEU, 0xFEU)) {
                    break;
                }
                ++length;
            }
        }
        return length;
    };
    return run_length(1, 0) == 6U || run_length(0, 1) == 6U;
}

[[nodiscard]] bool london_underground_roundel(const EmissivePalette& palette,
                                              std::uint32_t x,
                                              std::uint32_t y,
                                              std::uint32_t z,
                                              VxlColor color) noexcept {
    if (palette.map_name != "London" || z < 216U || z > 224U) {
        return false;
    }
    const bool first =
        x >= 300U && x <= 304U && y >= 317U && y <= 321U;
    const bool second =
        x >= 233U && x <= 237U && y >= 114U && y <= 118U;
    if (!first && !second) {
        return false;
    }

    // The two symmetric Underground roundels use a four-tone red ring and a
    // small cool-white name bar. Keep the navy centre dark for the logo's
    // contrast instead of turning the whole sign into a white rectangle.
    const bool white = exact_rgb(color, 0xE8U, 0xFEU, 0xFEU);
    const bool red =
        exact_rgb(color, 0xE4U, 0x33U, 0x34U) ||
        exact_rgb(color, 0xB0U, 0x23U, 0x23U) ||
        exact_rgb(color, 0x96U, 0x1CU, 0x1CU) ||
        exact_rgb(color, 0xC6U, 0x2BU, 0x2BU);
    return white || red;
}

[[nodiscard]] bool london_bus_stop_sign(const EmissivePalette& palette,
                                        const VxlMap& map, std::uint32_t x,
                                        std::uint32_t y, std::uint32_t z,
                                        VxlColor color) noexcept {
    if (palette.map_name != "London" ||
        !exact_rgb(color, 0xFFU, 0xFFU, 0xFFU) || z < 216U || z > 219U) {
        return false;
    }

    // All four baked London bus-stop signs preserve the prefab's 2x1x4 white
    // face and one red cap. White also paints road markings and large signs,
    // so both halves of that tiny authored signature are load-bearing.
    std::uint32_t white_face{};
    for (std::int64_t probe_z{216}; probe_z <= 219; ++probe_z) {
        for (std::int64_t offset_y{-2}; offset_y <= 2; ++offset_y) {
            for (std::int64_t offset_x{-2}; offset_x <= 2; ++offset_x) {
                if (exact_rgb_at(map, static_cast<std::int64_t>(x) + offset_x,
                                 static_cast<std::int64_t>(y) + offset_y,
                                 probe_z, 0xFFU, 0xFFU, 0xFFU)) {
                    ++white_face;
                }
            }
        }
    }
    if (white_face < 8U) {
        return false;
    }

    for (std::int64_t offset_y{-2}; offset_y <= 2; ++offset_y) {
        for (std::int64_t offset_x{-2}; offset_x <= 2; ++offset_x) {
            if (exact_rgb_at(map, static_cast<std::int64_t>(x) + offset_x,
                             static_cast<std::int64_t>(y) + offset_y, 215,
                             0xE4U, 0x33U, 0x34U)) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] bool london_bus_stop_shelter(const EmissivePalette& palette,
                                           const VxlMap& map,
                                           std::uint32_t x, std::uint32_t y,
                                           std::uint32_t z,
                                           VxlColor color) noexcept {
    if (palette.map_name != "London" ||
        !exact_rgb(color, 0xE8U, 0xE9U, 0xE9U) || z < 220U || z > 221U) {
        return false;
    }

    // The shelter light is a 3x1x2 panel. Require the complete panel before
    // doing the wider sign search; #E8E9E9 is also ordinary London trim.
    std::uint32_t panel{};
    for (std::int64_t probe_z{220}; probe_z <= 221; ++probe_z) {
        for (std::int64_t offset_y{-2}; offset_y <= 2; ++offset_y) {
            for (std::int64_t offset_x{-2}; offset_x <= 2; ++offset_x) {
                if (exact_rgb_at(map, static_cast<std::int64_t>(x) + offset_x,
                                 static_cast<std::int64_t>(y) + offset_y,
                                 probe_z, 0xE8U, 0xE9U, 0xE9U)) {
                    ++panel;
                }
            }
        }
    }
    if (panel < 6U) {
        return false;
    }

    // The sign sits 8-11 blocks from the shelter depending on rotation.
    // Pairing the two prefabs prevents other six-voxel white wall details from
    // becoming bus-stop lights.
    for (std::int64_t offset_y{-14}; offset_y <= 14; ++offset_y) {
        for (std::int64_t offset_x{-14}; offset_x <= 14; ++offset_x) {
            for (std::int64_t probe_z{216}; probe_z <= 219; ++probe_z) {
                const auto probe_x = static_cast<std::int64_t>(x) + offset_x;
                const auto probe_y = static_cast<std::int64_t>(y) + offset_y;
                if (!exact_rgb_at(map, probe_x, probe_y, probe_z, 0xFFU,
                                  0xFFU, 0xFFU)) {
                    continue;
                }
                if (london_bus_stop_sign(
                        palette, map, static_cast<std::uint32_t>(probe_x),
                        static_cast<std::uint32_t>(probe_y),
                        static_cast<std::uint32_t>(probe_z),
                        VxlColor{0xFFU, 0xFFU, 0xFFU, 0xFFU})) {
                    return true;
                }
            }
        }
    }
    return false;
}

[[nodiscard]] bool london_parliament_window(const EmissivePalette& palette,
                                             std::uint32_t x,
                                             std::uint32_t y,
                                             std::uint32_t z,
                                             VxlColor color) noexcept {
    if (palette.map_name != "London") {
        return false;
    }

    // The south Parliament facade bakes its shallow panes as different
    // charcoal/blue-greys because the source VXL carries per-voxel shading.
    // Coordinates recover the repeated framed slots; the material check keeps
    // the brown mullions and centre doorway matte.
    const bool south_shallow_pane =
        y == 299U && x >= 362U && x <= 394U &&
        (x - 362U) % 4U == 0U &&
        ((z >= 215U && z <= 218U) || (z >= 221U && z <= 224U));
    const auto maximum = std::max({color.red, color.green, color.blue});
    const auto minimum = std::min({color.red, color.green, color.blue});
    const bool cool_grey =
        color.blue >= static_cast<std::uint16_t>(color.red) + 3U &&
        color.blue >= static_cast<std::uint16_t>(color.green) + 2U;
    const bool charcoal = maximum <= 0x38U && maximum - minimum <= 0x0C;
    if (south_shallow_pane && maximum <= 0x70U &&
        (cool_grey || charcoal)) {
        return true;
    }

    // Both end towers put paired blue-grey panes on their x-facing walls.
    // Those baked pane shades are brighter than the centre wing's charcoal,
    // but their exact two planes and repeated four-voxel rows separate them
    // from the tower's brown frame.
    const bool south_tower_pane =
        (x == 347U || x == 409U) && (y == 296U || y == 299U) &&
        ((z >= 204U && z <= 207U) || (z >= 209U && z <= 212U) ||
         (z >= 215U && z <= 218U) || (z >= 220U && z <= 223U));
    if (south_tower_pane && maximum <= 0x90U &&
        (cool_grey || charcoal)) {
        return true;
    }

    // The broad forward-facing tower openings are ten voxels deep. Their
    // actual blue-grey back pane is y=303; lighting y=299 would only tint the
    // brown frame and leave the visible opening black.
    const bool south_tower_recess =
        y == 303U &&
        ((x >= 351U && x <= 356U) || (x >= 400U && x <= 405U)) &&
        ((z >= 204U && z <= 212U) || (z >= 215U && z <= 223U));
    if (south_tower_recess && maximum <= 0x90U &&
        (cool_grey || charcoal)) {
        return true;
    }

    // The courtyard facade uses an exact dark-brown pane material, #473000,
    // in repeated vertical slots. Its surrounding frame is #573A00. Lighting
    // the old guessed rear plane changed nothing because this front pane is
    // what the camera actually sees.
    const bool north_courtyard_pane =
        y == 241U && x >= 361U && x <= 397U &&
        z >= 217U && z <= 224U &&
        exact_rgb(color, 0x47U, 0x30U, 0x00U);
    return north_courtyard_pane;
}

/**
 * TokyoNeon.
 *
 * These are the AS-BAKED map values, not the source prefab values: the map bake
 * shifts each channel by one to three, so `#356DD4` here is prefab `#346CD4`.
 *
 * Ground truth for what retail considers a neon sign is its own ten dedicated
 * prefabs under `assets/original/ugc/kv6/UGC_prefab_signneon*.kv6`, categorised
 * as signs and banners in `shared/constants_prefabs.py`. Between them they use
 * exactly two hues -- blue around 219 degrees and green around 126 -- plus a
 * near-white tube. Those colours appear in TokyoNeon and in neither Training
 * nor AncientEgypt at any tolerance, which is what makes this an identification
 * rather than a guess. Tokyo's vocabulary is blue, green and white tube; it has
 * 31 magenta and 35 cyan voxels in the entire map, so a magenta/cyan neon look
 * would not be faithful.
 *
 * Deliberately NOT listed, pending someone looking at them on the parity rig:
 * the warm amber ramp (almost certainly lanterns, but colourimetrically
 * identical to Egypt's sandstone), `#E12012` red (88 voxels -- the ambiguous
 * "bright red flag" case), and `#0028BE`/`#00BE2A`, which are paired team
 * markers found in maps with no neon at all and sit barely over a degree
 * outside the neon hue windows. There is a cliff there; do not widen these.
 */
constexpr std::array<EmissiveSwatch, 18U> tokyo_swatches{{
    // Tube glass: brightest. 3315 voxels.
    {{0xF7U, 0xFFU, 0xDFU}, 4U, 255U},
    {{0xF6U, 0xFFU, 0xDAU}, 4U, 255U},
    {{0xF7U, 0xFAU, 0xEBU}, 4U, 240U},
    // Blue, hue about 219, two-tone with the brighter variant as the core.
    {{0x50U, 0x86U, 0xEBU}, 4U, 217U},
    {{0x35U, 0x6DU, 0xD4U}, 4U, 179U},
    // Green, hue about 126, three-tone. The two low-saturation cores sit below
    // any sane saturation gate, which is precisely why an allowlist wins here.
    {{0x58U, 0xE7U, 0x67U}, 4U, 217U},
    {{0x85U, 0xF1U, 0x90U}, 4U, 191U},
    {{0x77U, 0xE8U, 0x82U}, 4U, 179U},
    // Street lighting: the pale lantern blocks strung along the bridges and
    // walkways, and the warm amber ramp of traffic signals and paper lanterns.
    // Held out of the first pass because amber is colourimetrically identical to
    // AncientEgypt's sandstone, so it can only ever be a per-map entry -- which
    // is exactly what this table is. Lower intensity than the neon tubes: a
    // lantern should read as a warm point, not as a sign.
    {{0xE8U, 0xE8U, 0xD8U}, 6U, 200U},
    {{0xD0U, 0xD0U, 0xC8U}, 6U, 176U},
    {{0xDAU, 0xD4U, 0x10U}, 5U, 190U},
    {{0xFEU, 0xA4U, 0x14U}, 5U, 200U},
    {{0xFDU, 0xA3U, 0x14U}, 5U, 200U},
    {{0xFAU, 0xA1U, 0x13U}, 5U, 190U},
    {{0xF8U, 0x9FU, 0x13U}, 5U, 190U},
    {{0xF5U, 0x9EU, 0x13U}, 5U, 180U},
    {{0xEEU, 0x53U, 0x05U}, 5U, 170U},
    // Traffic-signal yellow. Tolerance 4 also absorbs #FFFF03 (24 voxels),
    // #FFFB03 (4) and #FFF803 (4). Deliberately NOT extended to the #998342
    // amber family despite its 4,000+ voxels: dozens of shades one step apart is
    // the signature of a baked shaded surface, not of placed fixtures.
    {{0xFFU, 0xFFU, 0x04U}, 4U, 205U},
}};

/**
 * CityOfChicago: gas street lamps over wet 1920s cobblestone.
 *
 * 1,371 voxels across a warm off-white family. Pitched below Tokyo's 240-255
 * neon tubes and near SpookyMansion's flames, because a lamp should pool light
 * rather than floodlight the street.
 *
 * Tolerance stays at 4 rather than 5: at 5 this family begins to overlap
 * SpookyMansion's #FFFF52 candle entry, and the two maps would start lighting
 * each other's colours.
 *
 * #E43334 is the repeated heart/sign family. Visual map inspection identifies
 * the repeated small clusters as authored neon hearts. Its VXL glass is red,
 * but its presentation and spill are hot pink, matching the fixture's intended
 * visual language instead of making it look like matte red masonry.
 *
 * Still held out: #E8E9E9, #AE7705, #BEA201, #A7F35C, #F79BBB, #FFFFFF and
 * #3A6ABC -- all form large contiguous architectural blobs.
 */
constexpr std::array<EmissiveSwatch, 5U> chicago_swatches{{
    // Window/lamp glass stays yellow at the source but casts a warmer amber
    // pool: enough red to suggest inhabited rooms, without orange-washing the
    // whole facade.
    {{0xFAU, 0xFFU, 0x50U}, 4U, 255U,
     std::array<std::uint8_t, 3U>{0xFFU, 0xDCU, 0x60U},
     std::array<std::uint8_t, 3U>{0xFFU, 0xB8U, 0x40U}},
    {{0xFFU, 0xFAU, 0x50U}, 4U, 255U,
     std::array<std::uint8_t, 3U>{0xFFU, 0xDCU, 0x60U},
     std::array<std::uint8_t, 3U>{0xFFU, 0xB8U, 0x40U}},
    {{0xEBU, 0xE7U, 0x5FU}, 4U, 235U,
     std::array<std::uint8_t, 3U>{0xF8U, 0xD2U, 0x64U},
     std::array<std::uint8_t, 3U>{0xFFU, 0xB1U, 0x3AU}},
    {{0xD8U, 0xD5U, 0x77U}, 4U, 220U,
     std::array<std::uint8_t, 3U>{0xF0U, 0xC8U, 0x70U},
     std::array<std::uint8_t, 3U>{0xF8U, 0xA8U, 0x36U}},
    // Saturated neon pink at the fixture and in its local spill.
    {{0xE4U, 0x33U, 0x34U}, 5U, 235U,
     std::array<std::uint8_t, 3U>{0xFFU, 0x48U, 0xD2U},
     std::array<std::uint8_t, 3U>{0xFFU, 0x23U, 0xBCU}},
}};

/**
 * Atlantis: torches and braziers.
 *
 * 339 voxels across eight sites, plus a centre flame pair. This is the cleanest
 * separation in the whole catalogue: a selector of saturation >= 0.95 AND
 * max(r,g,b) >= 0.70 picks exactly these voxels and leaks no architecture, since
 * the next most saturated bucket with a meaningful count sits at luminance 0.180
 * -- far under the value gate.
 */
constexpr std::array<EmissiveSwatch, 7U> atlantis_swatches{{
    {{0xFFU, 0xCCU, 0x07U}, 4U, 210U},
    {{0xFFU, 0xC9U, 0x07U}, 4U, 205U},
    {{0xFFU, 0xC4U, 0x07U}, 4U, 200U},
    // Dimmer body tones down the brazier bowl.
    {{0xE1U, 0xB3U, 0x07U}, 5U, 180U},
    {{0xC0U, 0x93U, 0x03U}, 5U, 160U},
    // The centre flame pair.
    {{0xFCU, 0xC8U, 0x08U}, 4U, 215U},
    {{0xF5U, 0x88U, 0x16U}, 4U, 195U},
}};

/**
 * London: gas lamp props under a smog rainstorm.
 *
 * The amber prop colour is shared by eight large bridge lamps and the tiny
 * marker lamps baked into London taxis. 200 of its 212 exact map-wide voxels
 * belong to the large lamps, so it is safe to select on colour alone.
 *
 * #E8FEFE is deliberately absent from this colour table. It forms the four
 * double-head streetlamp panels, the four Big Ben clock dials, taxi headlights
 * and six roadside signboards, but also paints 46x1x1 kerb lines and 3x43x1
 * pavement sheets. Real fixtures are classified by recovered shape/height
 * signatures in `emissive_appearance_at`, avoiding glowing roads and signs.
 */
constexpr std::array<EmissiveSwatch, 4U> london_swatches{{
    {{0xF5U, 0x88U, 0x16U}, 4U, 220U,
     std::array<std::uint8_t, 3U>{0xFFU, 0xD8U, 0x58U},
     std::array<std::uint8_t, 3U>{0xFFU, 0xADU, 0x38U}},
    // Exactly four baked voxels: the paired rear lamps of London's two taxis.
    // Tolerance is intentionally zero because the adjacent dark-red vehicle
    // body ramp is not a light source.
    {{0x62U, 0x15U, 0x18U}, 0U, 190U,
     std::array<std::uint8_t, 3U>{0xFFU, 0x38U, 0x30U},
     std::array<std::uint8_t, 3U>{0xFFU, 0x20U, 0x18U}},
    // Ten small bridge-rail lantern caps. These exact baked greys are confined
    // to the authored London bridge prefab; the neighbouring rail metal uses
    // other shades and remains matte.
    {{0x87U, 0x8AU, 0x8DU}, 0U, 180U,
     std::array<std::uint8_t, 3U>{0xF0U, 0xC8U, 0x76U},
     std::array<std::uint8_t, 3U>{0xFFU, 0xBBU, 0x48U}},
    {{0x84U, 0x87U, 0x8AU}, 0U, 165U,
     std::array<std::uint8_t, 3U>{0xD8U, 0xB0U, 0x68U},
     std::array<std::uint8_t, 3U>{0xFFU, 0xB6U, 0x42U}},
}};

/**
 * Invasion: volcanic fires.
 *
 * #FFFF00 (85 voxels across 41 clusters) and #FFC800 (28 across 16) match
 * SpookyMansion's approved candle statistics almost exactly -- small counts,
 * many separate clusters -- which is what a placed fixture looks like.
 *
 * CastleWars shares this dome but gets NO palette: its only isolated bright
 * cluster is a 64-voxel achromatic strip set, and zero saturation is precisely
 * the case this file already refuses for SpookyMansion's marble.
 */
constexpr std::array<EmissiveSwatch, 2U> invasion_swatches{{
    {{0xFFU, 0xFFU, 0x00U}, 5U, 190U},
    {{0xFFU, 0xC8U, 0x00U}, 5U, 200U},
}};

/**
 * SpookyMansion.
 *
 * Candle and lantern flames, identified from the map's own colour histogram: at
 * V >= 0.85 the only warm entries are a cream `#FAFAC8` (112 voxels) and a
 * candle yellow `#FFFF52` (70). Both counts are small and isolated, which is
 * what a placed light fixture looks like -- architecture appears in the
 * thousands.
 *
 * Deliberately NOT included:
 *  - `#FFFFFF` (127 voxels) and the other pure greys. Zero saturation makes them
 *    equally likely to be marble, bone or moonlit trim, and a mansion lit by
 *    glowing white masonry would look wrong.
 *  - The saturated red ramp `#F51808` and neighbours (~200 voxels across eight
 *    shades). Plausibly stained glass, which would be a lovely touch, but
 *    equally plausibly blood spatter -- and glowing blood is a worse mistake
 *    than an unlit window. Confirm on the rig before adding.
 */
constexpr std::array<EmissiveSwatch, 2U> spooky_swatches{{
    // Warm cream: the dominant flame colour. Lower intensity than Tokyo's neon
    // tubes -- a candle should pool light, not floodlight a corridor.
    {{0xFAU, 0xFAU, 0xC8U}, 5U, 205U},
    // Tolerance 4, not 5, and the difference is load-bearing: at 5 this window
    // reaches #FAFF50, which is CityOfChicago's gas street lamp, so the two maps
    // would light each other's vocabulary at each other's intensity. Four still
    // absorbs the map bake's channel shift, which is all it exists for.
    {{0xFFU, 0xFFU, 0x52U}, 4U, 190U},
}};

/**
 * Maps with authored light fixtures. Keyed by MAP, never by dome.
 *
 * Deliberately absent, each after being measured rather than assumed:
 * AncientEgypt (its amber ramp is 85 clusters of terrain and wall plates, the
 * largest 36x80x5), Alcatraz, DragonIsland (brightest entries are 9-voxel
 * foliage highlights), DoubleDragon and CastleWars (team markers in identical
 * blobs), Hiesville (zero voxels above V 0.85), Classic, BlockNess, Training,
 * LunarBase (every bright colour is hull slab or signage), and WW2_DockLands
 * (143 #FFFFFF voxels forming a solid painted cube).
 *
 * Held pending a look on the parity rig, NOT shipped blind: BranCastle's
 * #FFE701, GreatWall's gilt (right cluster shape but only luminance 0.588, well
 * under the flame gate the approved swatches clear), Crossroads' 8 yellow
 * voxels, and ArcticBase/WinterValley's indicator dots, which need shape gating
 * because a brightness rule alone selects 36,448 snow voxels.
 */
constexpr std::array<EmissivePalette, 6U> palettes{{
    {"TokyoNeon", tokyo_swatches},
    {"SpookyMansion", spooky_swatches},
    // Chicago's fixtures are sparse points rather than Tokyo-sized tubes, so
    // their attenuated volume needs more gain to reach neighbouring brick.
    {"CityOfChicago", chicago_swatches, 3.4F},
    {"Atlantis", atlantis_swatches},
    // London has only a handful of separated fixtures across the whole map.
    // Give their attenuated pools enough gain to read through the heavy smog.
    {"London", london_swatches, 3.1F},
    {"Invasion", invasion_swatches},
}};

[[nodiscard]] bool within(std::uint8_t left, std::uint8_t right,
                          std::uint8_t tolerance) noexcept {
    const auto difference = left > right ? left - right : right - left;
    return difference <= tolerance;
}

} // namespace

EmissivePalette emissive_palette_for(std::string_view map_name) noexcept {
    const auto found = std::ranges::find(palettes, map_name, &EmissivePalette::map_name);
    if (found == palettes.end()) {
        return EmissivePalette{map_name, {}};
    }
    return *found;
}

std::uint8_t emissive_intensity(const EmissivePalette& palette,
                                VxlColor color) noexcept {
    const auto appearance = emissive_appearance(palette, color);
    return appearance.has_value() ? appearance->intensity : 0U;
}

std::optional<EmissiveAppearance>
emissive_appearance(const EmissivePalette& palette, VxlColor color) noexcept {
    for (const auto& swatch : palette.swatches) {
        if (within(color.red, swatch.rgb[0U], swatch.tolerance) &&
            within(color.green, swatch.rgb[1U], swatch.tolerance) &&
            within(color.blue, swatch.rgb[2U], swatch.tolerance)) {
            const auto surface = swatch.surface_rgb.value_or(swatch.rgb);
            const auto light = swatch.light_rgb.value_or(surface);
            return EmissiveAppearance{
                swatch.intensity,
                VxlColor{surface[0U], surface[1U], surface[2U], 255U},
                VxlColor{light[0U], light[1U], light[2U], 255U}};
        }
    }
    return std::nullopt;
}

std::optional<EmissiveAppearance>
emissive_appearance_at(const EmissivePalette& palette, const VxlMap& map,
                       std::uint32_t x, std::uint32_t y, std::uint32_t z,
                       VxlColor color) noexcept {
    if (const auto by_color = emissive_appearance(palette, color);
        by_color.has_value()) {
        return by_color;
    }
    if (chicago_streetlamp_glass(palette, map, x, y, z, color)) {
        // Keep the glass nearly white while casting a warm sodium/gas-lamp
        // pool. This distinguishes the fixture from occupied windows.
        return EmissiveAppearance{
            250U, VxlColor{0xFFU, 0xF8U, 0xD8U, color.alpha},
            VxlColor{0xFFU, 0xCFU, 0x70U, color.alpha}};
    }
    if (london_clock_glass(palette, z, color)) {
        // Big Ben's dials remain readable cream-white through the fog and cast
        // the restrained gold halo seen in London's visual language.
        return EmissiveAppearance{
            255U, VxlColor{0xFFU, 0xF5U, 0xC8U, color.alpha},
            VxlColor{0xFFU, 0xC7U, 0x58U, color.alpha}};
    }
    if (london_vehicle_headlight(palette, map, x, y, z, color)) {
        return EmissiveAppearance{
            255U, VxlColor{0xFFU, 0xFEU, 0xEDU, color.alpha},
            VxlColor{0xFFU, 0xE8U, 0xB8U, color.alpha}};
    }
    if (london_streetlamp_glass(palette, map, x, y, z, color)) {
        return EmissiveAppearance{
            250U, VxlColor{0xFFU, 0xF4U, 0xD0U, color.alpha},
            VxlColor{0xFFU, 0xC0U, 0x54U, color.alpha}};
    }
    if (london_box_lamp_glass(palette, map, x, y, z, color)) {
        return EmissiveAppearance{
            160U, VxlColor{0xF6U, 0xF0U, 0xD4U, color.alpha},
            VxlColor{0xFFU, 0xC8U, 0x6AU, color.alpha}};
    }
    if (london_underground_strip(palette, map, x, y, z, color)) {
        return EmissiveAppearance{
            165U, VxlColor{0xF2U, 0xF8U, 0xF4U, color.alpha},
            VxlColor{0xE8U, 0xF2U, 0xFFU, color.alpha}};
    }
    if (london_underground_roundel(palette, x, y, z, color)) {
        const bool red =
            exact_rgb(color, 0xE4U, 0x33U, 0x34U) ||
            exact_rgb(color, 0xB0U, 0x23U, 0x23U) ||
            exact_rgb(color, 0x96U, 0x1CU, 0x1CU) ||
            exact_rgb(color, 0xC6U, 0x2BU, 0x2BU);
        if (red) {
            return EmissiveAppearance{
                140U, VxlColor{color.red, color.green, color.blue,
                               color.alpha},
                VxlColor{0xFFU, 0x20U, 0x18U, color.alpha}};
        }
        return EmissiveAppearance{
            170U, VxlColor{0xF8U, 0xF2U, 0xE4U, color.alpha},
            VxlColor{0xFFU, 0xD0U, 0x90U, color.alpha}};
    }
    if (london_bus_stop_sign(palette, map, x, y, z, color)) {
        return EmissiveAppearance{
            225U, VxlColor{0xFFU, 0xF8U, 0xD8U, color.alpha},
            VxlColor{0xFFU, 0xCAU, 0x68U, color.alpha}};
    }
    if (london_bus_stop_shelter(palette, map, x, y, z, color)) {
        return EmissiveAppearance{
            235U, VxlColor{0xFFU, 0xF2U, 0xC0U, color.alpha},
            VxlColor{0xFFU, 0xB8U, 0x40U, color.alpha}};
    }
    if (london_parliament_window(palette, x, y, z, color)) {
        return EmissiveAppearance{
            205U, VxlColor{0xFFU, 0xD6U, 0x72U, color.alpha},
            VxlColor{0xFFU, 0xA8U, 0x36U, color.alpha}};
    }
    return std::nullopt;
}

bool palette_is_empty(const EmissivePalette& palette) noexcept {
    return palette.swatches.empty();
}

} // namespace battlespades::world
