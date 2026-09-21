#pragma once

#include "battlespades/settings/client_settings.hpp"

#include <cstdint>
#include <string_view>

namespace battlespades::render {

/**
 * Every renderer knob a quality tier controls, resolved in one place.
 *
 * Tier logic lives here rather than being spread across the frontend and the
 * renderer so it can be unit-tested without a GPU, and so a new technique has
 * exactly one table to be added to.
 *
 * `legacy` is the retail path: baked VXL illumination and authored two-light shading straight to
 * the backbuffer, no offscreen target, no post. It must stay reachable so
 * screenshot comparison against retail remains runnable.
 */
struct QualityProfile final {
    /** Reserved for an offscreen float target. False until that pass exists. */
    bool hdr_target{};
    /** Per-pixel directional lighting for terrain AND voxel models. */
    bool enhanced_lighting{};
    /**
     * Sun shadow cascades. 0 disables shadow mapping entirely.
     *
     * Currently only ever 0 or 1, and the renderer treats it as a boolean:
     * `WorldRenderer::submit` renders exactly one view, `shadow_view_id_base`,
     * and tests this field only for `> 0`. High and Ultra used to set 3 here,
     * which rendered identically to 1 while the debug overlay reported "SHADOW
     * 3" -- a profile field asserting a capability that does not exist is worse
     * than no field, because it makes the gap invisible in exactly the place
     * someone would look for it.
     *
     * Real splits are still worth having: one eye-centred cascade means distant
     * shadows do not exist at all, and they pop as the camera moves. Raise this
     * again in the same change that makes the renderer loop over splits, not
     * before. `render_views.hpp` already reserves four cascade view ids.
     */
    std::uint8_t shadow_cascades{};
    std::uint16_t shadow_resolution{};
    /** 1 = single hardware comparison tap, 9 = fixed spiral-disc PCF. */
    std::uint8_t shadow_pcf_taps{1U};
    /**
     * Filter radius in shadow texels, for the 9-tap path only.
     *
     * The penumbra a filter can produce is bounded by how far its widest tap
     * reaches, so a 3x3 box one texel across is nearly as hard-edged as no
     * filter -- which is what made bridge shadows look stamped on. Spreading the
     * same nine taps wider buys a real soft edge for free.
     *
     * Above roughly three texels the taps separate far enough that light starts
     * leaking through thin geometry, and a bridge deck is exactly one block
     * thick, so do not raise these without looking at a bridge.
     */
    float shadow_softness{};
    /** Reserved for screen-space AO. All shipped profiles keep this at zero. */
    std::uint8_t ssao_samples{};
    bool ssao_half_resolution{true};
    /** Bounded forward light array; 0 disables dynamic lights. */
    std::uint8_t dynamic_lights{};
    /** Evaluate the light array per particle as well as per surface. */
    bool particle_lighting{};
    /** Reserved bloom pyramid levels. Zero until the pass exists. */
    std::uint8_t bloom_levels{};
    /**
     * Self-illumination gain for authored emissive voxels; 0 disables them.
     *
     * Retail had no emissive map blocks at all, so Legacy must leave this at 0
     * for parity captures to stay meaningful.
     */
    float emissive_gain{};
    /** Retail particle-pool capacity fraction, from the effect-quality setting. */
    float effect_scale{1.0F};
};

/** Human-readable tier name for the debug overlay. */
[[nodiscard]] std::string_view quality_profile_name(
    settings::ShaderQuality quality) noexcept;

/**
 * Convert an atmosphere highlight into the matte response of a painted voxel.
 *
 * The sky-derived value describes light intensity, not material gloss. Passing
 * it straight through gave every stone, dirt, cloth, and weapon cube the same
 * broad plastic sheen. The current VXL format has no per-voxel roughness
 * channel, so enhanced rendering conservatively caps the lobe; Legacy never
 * calls this path and retains the recovered map/model lighting equations.
 */
[[nodiscard]] constexpr float matte_voxel_specular(float authored) noexcept {
    if (!(authored > 0.0F)) {
        return 0.0F;
    }
    return authored < 0.08F ? authored : 0.08F;
}

/**
 * Resolves the active profile.
 *
 * Effect quality stays an independent axis: a player may want dense particles
 * on a machine that cannot afford shadows, and retail exposed the two sliders
 * separately.
 */
[[nodiscard]] QualityProfile profile_for(settings::ShaderQuality quality,
                                         settings::QualityLevel effect_quality) noexcept;

} // namespace battlespades::render
