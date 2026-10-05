#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace battlespades::world {

/**
 * Per-map atmosphere: everything terrain lighting needs in order to agree with
 * the sky the player is standing under.
 *
 * Colours are unit-luminance chroma vectors, NOT sRGB bytes. Each is scaled so
 * its Rec.709 luminance is 1.0 and the matching intensity scalar carries the
 * level. Individual channels may exceed 1.0 — a saturated blue sky normalised
 * to unit luminance has B > 1 — and that is deliberate: separating chroma from
 * level is what lets a night map be raised to a playable brightness without
 * turning it into daylight. LunarBase's raw sky luminance is 3% of Classic_B's;
 * scaling the world by that directly would make it unplayable, while scaling
 * only the level keeps the sky blue and the map legible.
 *
 * Directions are canonical map space (z grows downward, so "up" is -z) and
 * point FROM a surface TOWARD the light, matching u_sunDirection in
 * src/render/shaders/fs_world.sc.
 */
struct MapAtmosphere final {
    /** Unit vector toward the key light. Meaningless when key_intensity is 0. */
    std::array<float, 3U> sun_direction{0.36F, 0.26F, -0.90F};
    /** Unit-luminance chroma of the key light. */
    std::array<float, 3U> sun_color{1.0F, 1.0F, 1.0F};
    /** Key level. Exactly 0 for overcast domes that ship no sun or moon. */
    float key_intensity{0.62F};

    /** Hemispheric ambient: the sky term, for surfaces facing up (-z). */
    std::array<float, 3U> sky_ambient{1.0F, 1.0F, 1.0F};
    /** The ground bounce, for surfaces facing down (+z). */
    std::array<float, 3U> ground_ambient{1.0F, 1.0F, 1.0F};
    /** Ambient level after the playability floor. */
    float ambient_intensity{0.70F};

    /**
     * sRGB fog selected by the scene owner.
     *
     * Live official maps use the locally measured horizon; UGC and unknown
     * maps use server StateData so custom art direction remains intact.
     */
    std::array<std::uint8_t, 3U> fog_color{111U, 215U, 223U};
    /** The colour the sky is painting just above the horizon, for fog blending. */
    std::array<std::uint8_t, 3U> horizon_color{111U, 215U, 223U};
    /** Zenith colour; the correct backdrop clear behind a blended skydome. */
    std::array<std::uint8_t, 3U> zenith_color{111U, 215U, 223U};

    /** Exponent for the exp-squared fog curve. 1.0 is the default thickness. */
    float fog_density{1.0F};
    /** Extended-Reinhard white point; opens up as a scene gets darker. */
    float exposure{1.7F};
    /** Overcast skies want far less specular than a desert noon. */
    float specular_strength{0.14F};
    /** Post-process bloom bright-pass threshold (tonemapped); bright maps raise it. */
    float bloom_threshold{0.72F};

    /** Provenance for the debug overlay. Never affects rendering. */
    std::string source{"default"};
};

/** Never let a map be too dark to play. Idempotent. */
void clamp_atmosphere_for_play(MapAtmosphere& atmosphere) noexcept;

class VxlMap;

/** How bright a map's top surfaces are: luminance of the top voxel of each column, 0..1. */
struct MapSurfaceBrightness final {
    float mean{};
    float p95{};
    [[nodiscard]] bool valid() const noexcept { return mean > 0.0F; }
};

/** One pass over the column tops; cheap enough for the map loader thread. */
[[nodiscard]] MapSurfaceBrightness measure_map_surface_brightness(const VxlMap& map) noexcept;

/**
 * Fits a sky-derived atmosphere to what the map is built from. The
 * derivation only sees the sky, so a sand map under a desert sky blew out
 * to white (worse with bloom) while dark city blocks under a sunless sky
 * stayed murky. Lifts or lowers ambient and sun toward a mid-grey screen
 * value, opens the white point so the brightest surfaces keep detail, and
 * raises the bloom threshold so only real highlights glow. For maps with no
 * hand-tuned lighting (workshop/UGC); applying it twice is harmless only
 * from the same base, so callers start from the sky-derived atmosphere.
 */
void normalize_atmosphere_for_map(MapAtmosphere& atmosphere, const MapSurfaceBrightness& surface) noexcept;

/**
 * Derives a map's atmosphere from its shipped skydome assets.
 *
 * Reads `<asset_root>/mesh/<stem>/<stem>.txt`, its skysphere `.aos` and the
 * bound sky-gradient TGA, then integrates the gradient over the hemisphere for
 * ambient and locates the sun or moon layer for a key direction. Pure with
 * respect to the GPU: no bgfx, no image library, so the whole derivation is
 * unit-testable against the real assets.
 *
 * `definition_name` is a validated basename such as `Tokyo.txt`, never a path.
 * Returns false and leaves `atmosphere` untouched when the dome is missing or
 * malformed, so a bad UGC dome cannot blank the lighting.
 */
[[nodiscard]] bool derive_map_atmosphere(const std::filesystem::path& asset_root,
                                         std::string_view definition_name,
                                         MapAtmosphere& atmosphere,
                                         std::string& error);

/**
 * Per-dome art direction, applied over the derived atmosphere.
 *
 * Every field is optional on purpose: an override that carried a whole
 * MapAtmosphere would silently clobber derived values with struct defaults, so
 * a dome that only wants thicker fog would also flatten its own specular.
 */
struct AtmosphereOverride final {
    std::optional<float> key_intensity{};
    std::optional<float> fog_density{};
    std::optional<float> specular_strength{};
    std::optional<float> ambient_intensity{};
    /**
     * Extended-Reinhard white point.
     *
     * Beware the sign: the shader computes `L(1 + L/W^2) / (1 + L)`, so a LARGER
     * white point compresses MORE. Raising this darkens midtones; it does not
     * brighten them.
     */
    std::optional<float> exposure{};
    /**
     * Unit vector toward the key light, in canonical map space.
     *
     * Needed because a dome with no sun or moon layer falls back to a fixed
     * direction of roughly 64 degrees elevation, which several domes visibly
     * contradict: Invasion paints every warm layer within four degrees of
     * azimuth 172 at 10-34 degrees elevation, so the fallback lights the world
     * from a direction its own sky denies. Supplied vectors are normalised on
     * apply, so an approximate one is fine.
     */
    std::optional<std::array<float, 3U>> sun_direction{};
    std::string_view source{};
};

/**
 * The handful of domes where the derivation needs a human decision.
 *
 * Sampling the gradient reproduces retail's authored FOG_COLORS to within
 * 1-2/255 on most maps, so the derivation is trustworthy for colour. What it
 * cannot know is intent: that a moon should not key like a sun, or that a
 * haunted castle wants fog thick enough to give its candles something to glow
 * through. Returns null when the derived values stand unaltered.
 */
[[nodiscard]] const AtmosphereOverride*
authored_atmosphere_override(std::string_view definition_name) noexcept;

/** Full resolve: derive, apply overrides, clamp. Always yields something sane. */
[[nodiscard]] MapAtmosphere resolve_map_atmosphere(
    const std::filesystem::path& asset_root, std::string_view definition_name);

} // namespace battlespades::world
