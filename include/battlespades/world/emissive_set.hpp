#pragma once

#include "battlespades/world/vxl_map.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace battlespades::world {

/**
 * One authored light-emitting block colour.
 *
 * Retail could not mark a map voxel as emissive at all: the `.vxl` format's
 * spare byte is 0xFF with zero variation on every shipped map, and the token
 * "emissive" appears nowhere in the retail tree. Its only real emissive
 * lighting was the player-placed flare block, which registered a static point
 * light and baked it into terrain vertex colours.
 *
 * So map emission is a deliberate addition, and it is authored rather than
 * inferred. Every colour rule tried against the real voxel data fails badly:
 * a saturation-and-value threshold that catches Tokyo's neon also catches
 * AncientEgypt's ocean and golden sandstone, and a luminance threshold catches
 * white walls. Tokyo's own neon greens sit below any sane saturation gate.
 * An allowlist cannot misfire on a map it does not name.
 */
struct EmissiveSwatch final {
    std::array<std::uint8_t, 3U> rgb{};
    /**
     * Per-channel match tolerance.
     *
     * Four is the floor, not a comfort margin: no prefab colour survives the
     * map bake byte-identically -- each channel shifts by one to three -- so an
     * exact-match allowlist matches nothing at all.
     */
    std::uint8_t tolerance{4U};
    /** Self-illumination strength, 0..255 mapping to 0..1. */
    std::uint8_t intensity{255U};
    /**
     * Optional presentation tint for the emitter surface.
     *
     * The VXL color identifies the authored fixture, but it is not necessarily
     * the color of its light. Chicago's red heart glass, for example, is meant
     * to read as a hot pink neon tube rather than as a matte red block.
     */
    std::optional<std::array<std::uint8_t, 3U>> surface_rgb;
    /** Optional color cast onto nearby geometry; defaults to surface_rgb/source. */
    std::optional<std::array<std::uint8_t, 3U>> light_rgb;
};

/** The emissive colours of one named map. */
struct EmissivePalette final {
    std::string_view map_name;
    std::span<const EmissiveSwatch> swatches;
    /**
     * Map-specific gain after distance attenuation.
     *
     * Dense Tokyo tubes need less amplification than Chicago's isolated
     * windows. Keeping this beside the authored palette avoids a renderer-side
     * map-name switch and lets fixture density drive the art direction.
     */
    float cast_gain{2.2F};
};

/** Fully resolved presentation for one classified emitter voxel. */
struct EmissiveAppearance final {
    std::uint8_t intensity{};
    VxlColor surface{};
    VxlColor light{};
};

/**
 * Returns the palette for a map, or an empty one when it has none.
 *
 * `map_name` is the map's basename without extension, e.g. `TokyoNeon`.
 */
[[nodiscard]] EmissivePalette emissive_palette_for(std::string_view map_name) noexcept;

/**
 * Self-illumination for one voxel colour, 0 when it is not an emitter.
 *
 * Pure and allocation-free: the mesher calls this once per solid voxel, so it
 * stays a handful of comparisons.
 */
[[nodiscard]] std::uint8_t emissive_intensity(const EmissivePalette& palette,
                                              VxlColor color) noexcept;

/**
 * Resolves intensity plus independently authored surface and cast-light tints.
 *
 * Empty means the voxel is not an emitter. Keeping classification and color
 * resolution atomic prevents the mesher and light-volume builder from finding
 * different overlapping swatches.
 */
[[nodiscard]] std::optional<EmissiveAppearance>
emissive_appearance(const EmissivePalette& palette, VxlColor color) noexcept;

/**
 * Resolves an emitter whose identity depends on surrounding voxel shape.
 *
 * Most fixtures are safely identified by map-local colour and use
 * `emissive_appearance`. Chicago and London reuse their fixture colours on
 * ordinary trim, kerbs and pavement, however. This overload additionally sees
 * map topology and coordinates so it can require recovered lamp, clock and
 * vehicle signatures instead of making white architecture glow.
 */
[[nodiscard]] std::optional<EmissiveAppearance>
emissive_appearance_at(const EmissivePalette& palette, const VxlMap& map,
                       std::uint32_t x, std::uint32_t y, std::uint32_t z,
                       VxlColor color) noexcept;

/** True when the palette names any emitter at all. */
[[nodiscard]] bool palette_is_empty(const EmissivePalette& palette) noexcept;

} // namespace battlespades::world
