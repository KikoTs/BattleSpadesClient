#include "battlespades/render/quality_profile.hpp"

namespace battlespades::render {

std::string_view quality_profile_name(settings::ShaderQuality quality) noexcept {
    switch (quality) {
    case settings::ShaderQuality::compatibility:
        // Shown as RETAIL: the audited retail lighting equations (D1).
        return "RETAIL";
    case settings::ShaderQuality::low:
        return "LOW";
    case settings::ShaderQuality::medium:
        return "MEDIUM";
    case settings::ShaderQuality::high:
        return "HIGH";
    case settings::ShaderQuality::ultra:
        return "ULTRA";
    }
    return "MEDIUM";
}

QualityProfile profile_for(settings::ShaderQuality quality,
                           settings::QualityLevel effect_quality) noexcept {
    QualityProfile profile;

    switch (quality) {
    case settings::ShaderQuality::compatibility:
        // Legacy: recovered VXL/KV6 lighting, straight to the backbuffer. No
        // offscreen target, so nothing downstream can alter a retail-parity
        // capture. Every enhancement stays off.
        break;

    case settings::ShaderQuality::low:
        // Lighting and a single tonemap, but no shadow map: the cascade pass
        // re-renders terrain geometry and is the first real cost on the frame.
        profile.enhanced_lighting = true;
        profile.dynamic_lights = 2U;
        // Two ALU and zero memory, so emission is affordable at every tier
        // above Legacy. Capped at 1.0 until the HDR target lands: a higher gain
        // simply clips inside an 8-bit pass instead of reading as brighter.
        profile.emissive_gain = 1.0F;
        break;

    case settings::ShaderQuality::medium:
        // One cascade covers the near field, which is where cast shadows are
        // actually read; distant shadows cost the same and are barely seen.
        profile.enhanced_lighting = true;
        profile.shadow_cascades = 1U;
        // Raised from 1024/1-tap. A single hardware comparison tap is a hard
        // edge by definition -- it is one bilinear lookup -- and Medium is the
        // default tier, so every player was seeing stamped-on shadows. Nine
        // spiral-disc taps at 1536 cost one extra megabyte of depth and eight
        // more samples on a pass that is already depth-only.
        profile.shadow_resolution = 1536U;
        profile.shadow_pcf_taps = 9U;
        profile.shadow_softness = 1.6F;
        profile.dynamic_lights = 4U;
        // Raised from 1.0: neon and lanterns are the only thing making a night
        // map feel alive until light spill lands, so they carry more weight.
        profile.emissive_gain = 1.4F;
        break;

    case settings::ShaderQuality::high:
        profile.enhanced_lighting = true;
        profile.shadow_cascades = 1U;
        profile.shadow_resolution = 1536U;
        profile.shadow_pcf_taps = 9U;
        profile.shadow_softness = 2.0F;
        profile.dynamic_lights = 8U;
        profile.particle_lighting = true;
        profile.emissive_gain = 1.15F;
        break;

    case settings::ShaderQuality::ultra:
        // A larger shadow map and the widest filter.
        // Everything here is refinement over High rather than a new capability
        // -- notably NOT more cascades, because the renderer still draws one.
        profile.enhanced_lighting = true;
        profile.shadow_cascades = 1U;
        profile.shadow_resolution = 2048U;
        profile.shadow_pcf_taps = 9U;
        // Wider in texels AND at higher resolution, so Ultra's penumbra is a
        // touch broader than High's while being better sampled inside it.
        profile.shadow_softness = 2.4F;
        profile.dynamic_lights = 8U;
        profile.particle_lighting = true;
        profile.emissive_gain = 1.3F;
        break;
    }

    // Effect quality is a separate axis on purpose: dense particles are
    // affordable on machines that cannot pay for shadows, and retail exposed
    // the two sliders independently.
    switch (effect_quality) {
    case settings::QualityLevel::low:
        profile.effect_scale = 0.10F;
        break;
    case settings::QualityLevel::medium:
        profile.effect_scale = 0.50F;
        break;
    case settings::QualityLevel::high:
        profile.effect_scale = 1.00F;
        break;
    }

    return profile;
}

} // namespace battlespades::render

