#include "battlespades/render/quality_profile.hpp"
#include "battlespades/render/render_views.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using battlespades::render::profile_for;
using battlespades::render::QualityProfile;
using battlespades::render::quality_profile_name;
using battlespades::render::matte_voxel_specular;
using battlespades::settings::QualityLevel;
using battlespades::settings::ShaderQuality;

void expect(bool value, const std::string& message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

constexpr std::array<ShaderQuality, 5U> tiers{
    ShaderQuality::compatibility, ShaderQuality::low, ShaderQuality::medium,
    ShaderQuality::high, ShaderQuality::ultra};

void legacy_disables_every_enhancement() {
    // Legacy is the retail-parity path. Anything that renders the world
    // through an offscreen target or alters its shading breaks screenshot
    // comparison against retail, so every one of these must stay off.
    const auto profile = profile_for(ShaderQuality::compatibility, QualityLevel::high);
    expect(!profile.hdr_target, "legacy must draw straight to the backbuffer");
    expect(!profile.enhanced_lighting, "legacy must keep the baked retail tables");
    expect(profile.shadow_cascades == 0U, "legacy must cast no shadows");
    expect(profile.ssao_samples == 0U, "legacy must have no ambient occlusion pass");
    expect(profile.dynamic_lights == 0U, "legacy must have no dynamic lights");
    expect(profile.bloom_levels == 0U, "legacy must have no bloom");
}

void every_tier_above_legacy_is_lit_and_tonemapped() {
    for (const auto tier : tiers) {
        if (tier == ShaderQuality::compatibility) {
            continue;
        }
        const auto profile = profile_for(tier, QualityLevel::medium);
        expect(!profile.hdr_target && profile.ssao_samples == 0U && profile.bloom_levels == 0U,
               "tier " + std::string{quality_profile_name(tier)} +
                   " must not report unimplemented HDR/SSAO/bloom passes");
        expect(profile.enhanced_lighting,
               "tier " + std::string{quality_profile_name(tier)} +
                   " must use per-pixel lighting");
    }
}

void tiers_are_monotonic() {
    // A higher tier must never do less work than a lower one, or the settings
    // menu is lying about what the slider does.
    QualityProfile previous = profile_for(ShaderQuality::compatibility, QualityLevel::high);
    for (const auto tier : tiers) {
        if (tier == ShaderQuality::compatibility) {
            continue;
        }
        const auto profile = profile_for(tier, QualityLevel::high);
        const std::string name{quality_profile_name(tier)};
        expect(profile.shadow_cascades >= previous.shadow_cascades,
               name + " regressed shadow cascades");
        expect(profile.shadow_resolution >= previous.shadow_resolution,
               name + " regressed shadow resolution");
        expect(profile.ssao_samples >= previous.ssao_samples,
               name + " regressed ambient occlusion samples");
        expect(profile.dynamic_lights >= previous.dynamic_lights,
               name + " regressed dynamic light count");
        expect(profile.bloom_levels >= previous.bloom_levels,
               name + " regressed bloom levels");
        previous = profile;
    }
}

void shadow_settings_are_self_consistent() {
    for (const auto tier : tiers) {
        const auto profile = profile_for(tier, QualityLevel::high);
        const std::string name{quality_profile_name(tier)};
        if (profile.shadow_cascades == 0U) {
            expect(profile.shadow_resolution == 0U,
                   name + " reserves a shadow map it will never render into");
        } else {
            expect(profile.shadow_resolution > 0U,
                   name + " enables cascades with no resolution");
            expect(profile.shadow_cascades <= battlespades::render::shadow_view_count,
                   name + " requests more cascades than there are reserved views");
            // WorldRenderer::submit renders exactly one shadow view and tests
            // shadow_cascades only for > 0. A tier claiming more renders
            // identically while the debug overlay reports the larger number, so
            // the shortfall hides in the one place someone would check. Raise
            // this bound in the same change that loops over splits.
            expect(profile.shadow_cascades == 1U,
                   name + " claims cascades the renderer does not draw");
            expect(profile.shadow_pcf_taps == 1U || profile.shadow_pcf_taps == 9U,
                   name + " has an unsupported PCF tap count");
            // The renderer packs softness and the soft-path switch into one
            // uniform component, so a nine-tap tier with zero softness silently
            // collapses back to a single tap -- exactly the hard-edged bug this
            // replaced, and invisible in a screenshot without a reference.
            if (profile.shadow_pcf_taps > 1U) {
                expect(profile.shadow_softness > 0.0F,
                       name + " takes nine taps at zero radius, so all nine land "
                              "on the same texel");
                // Past roughly three texels the taps spread far enough to sample
                // past one-block-thick geometry and leak light through a bridge.
                expect(profile.shadow_softness <= 3.0F,
                       name + " filters wide enough to leak light through a "
                              "one-block bridge deck");
            } else {
                expect(profile.shadow_softness == 0.0F,
                       name + " sets a filter radius the single-tap path ignores");
            }
        }
    }
}

void particle_lighting_requires_dynamic_lights() {
    for (const auto tier : tiers) {
        const auto profile = profile_for(tier, QualityLevel::high);
        if (profile.particle_lighting) {
            expect(profile.dynamic_lights > 0U,
                   std::string{quality_profile_name(tier)} +
                       " lights particles with no lights to light them by");
        }
    }
}

void particle_lighting_matches_the_visible_tier_contract() {
    for (const auto tier : {ShaderQuality::compatibility, ShaderQuality::low,
                            ShaderQuality::medium}) {
        expect(!profile_for(tier, QualityLevel::high).particle_lighting,
               std::string{quality_profile_name(tier)} +
                   " must preserve the unlit retail particle path");
    }
    expect(profile_for(ShaderQuality::high, QualityLevel::high).particle_lighting &&
               profile_for(ShaderQuality::ultra, QualityLevel::high).particle_lighting,
           "only High and Ultra may evaluate the bounded light array per particle");
}

void effect_quality_is_an_independent_axis() {
    // A player may want dense particles on a machine that cannot afford
    // shadows, so the two sliders must not be coupled.
    const auto legacy_dense = profile_for(ShaderQuality::compatibility, QualityLevel::high);
    const auto ultra_sparse = profile_for(ShaderQuality::ultra, QualityLevel::low);
    expect(legacy_dense.effect_scale > ultra_sparse.effect_scale,
           "effect quality must be independent of the shader tier");
    expect(legacy_dense.effect_scale == 1.0F, "high effect quality must not scale bursts down");
    expect(profile_for(ShaderQuality::medium, QualityLevel::low).effect_scale == 0.1F &&
               profile_for(ShaderQuality::medium, QualityLevel::medium).effect_scale == 0.5F,
           "effect tiers must preserve retail's 0.1/0.5/1.0 pool proportions");
    for (const auto tier : tiers) {
        expect(profile_for(tier, QualityLevel::low).effect_scale > 0.0F,
               "no tier may scale effects to nothing");
    }
}

void every_tier_has_a_name() {
    for (const auto tier : tiers) {
        expect(!quality_profile_name(tier).empty(), "every tier must be nameable in the UI");
    }
    expect(quality_profile_name(ShaderQuality::compatibility) == "RETAIL",
           "the compatibility tier is presented as Legacy");
    expect(quality_profile_name(ShaderQuality::ultra) == "ULTRA", "ultra must be named");
}

void enhanced_voxels_have_a_bounded_matte_highlight() {
    expect(matte_voxel_specular(-1.0F) == 0.0F &&
               matte_voxel_specular(0.0F) == 0.0F,
           "invalid or absent authored gloss must stay matte");
    expect(matte_voxel_specular(0.04F) == 0.04F,
           "an already-subtle atmosphere highlight must survive");
    expect(matte_voxel_specular(0.30F) == 0.08F,
           "sky intensity must not turn every voxel material into plastic");
}

void reserved_view_ranges_do_not_overlap() {
    using namespace battlespades::render;
    // The static_asserts in the header cover the ordering; this pins the
    // property that the UI sorts strictly after the composite, which is what
    // keeps the HUD out of tonemapping.
    expect(composite_view_id < ui_window_view_id && ui_window_view_id < ui_canvas_view_id,
           "UI views must sort after the composite or the HUD gets tonemapped");
    expect(shadow_view_id_base + shadow_view_count <= world_view_id,
           "shadow cascades must resolve before the world samples them");
}

} // namespace

int main() {
    try {
        legacy_disables_every_enhancement();
        every_tier_above_legacy_is_lit_and_tonemapped();
        tiers_are_monotonic();
        shadow_settings_are_self_consistent();
        particle_lighting_requires_dynamic_lights();
        particle_lighting_matches_the_visible_tier_contract();
        effect_quality_is_an_independent_axis();
        every_tier_has_a_name();
        enhanced_voxels_have_a_bounded_matte_highlight();
        reserved_view_ranges_do_not_overlap();
        std::cout << "quality profile tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
