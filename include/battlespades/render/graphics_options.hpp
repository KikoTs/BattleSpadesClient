#pragma once

#include "battlespades/render/post_settings.hpp"
#include "battlespades/render/quality_profile.hpp"
#include "battlespades/settings/client_settings.hpp"

namespace battlespades::render {

/**
 * Applies the Graphics tab's shadow rows to a tier profile.
 *
 * `automatic` leaves the tier's own shadow fields alone, so a player who never
 * touches the rows renders exactly as before they existed. The Retail tier
 * (no enhanced lighting) never casts sun shadows whatever the rows say.
 */
[[nodiscard]] constexpr QualityProfile with_shadow_options(
    QualityProfile profile, const settings::GraphicsSettings& graphics) noexcept {
    if (!profile.enhanced_lighting) {
        return profile;
    }
    switch (graphics.shadow_quality) {
    case settings::ShadowQuality::automatic:
        break;
    case settings::ShadowQuality::off:
        profile.shadow_cascades = 0U;
        profile.shadow_resolution = 0U;
        break;
    case settings::ShadowQuality::low:
        profile.shadow_cascades = 1U;
        profile.shadow_resolution = 1024U;
        profile.shadow_pcf_taps = 1U;
        profile.shadow_softness = 0.0F;
        break;
    case settings::ShadowQuality::medium:
        profile.shadow_cascades = 1U;
        profile.shadow_resolution = 1536U;
        profile.shadow_pcf_taps = 9U;
        profile.shadow_softness = 1.6F;
        break;
    case settings::ShadowQuality::high:
        profile.shadow_cascades = 1U;
        profile.shadow_resolution = 2048U;
        profile.shadow_pcf_taps = 9U;
        profile.shadow_softness = 2.0F;
        break;
    case settings::ShadowQuality::ultra:
        profile.shadow_cascades = 1U;
        profile.shadow_resolution = 4096U;
        profile.shadow_pcf_taps = 9U;
        profile.shadow_softness = 2.4F;
        break;
    }
    switch (graphics.shadow_distance) {
    case settings::ShadowDistance::automatic:
        profile.shadow_distance = 0.0F;
        break;
    case settings::ShadowDistance::near:
        profile.shadow_distance = 48.0F;
        break;
    case settings::ShadowDistance::medium:
        profile.shadow_distance = 96.0F;
        break;
    case settings::ShadowDistance::far:
        profile.shadow_distance = 160.0F;
        break;
    }
    return profile;
}

/** Bloom intensity for each Graphics-tab step. */
[[nodiscard]] constexpr float bloom_intensity(settings::EffectLevel level) noexcept {
    switch (level) {
    case settings::EffectLevel::off:
        return 0.0F;
    case settings::EffectLevel::low:
        return 0.35F;
    case settings::EffectLevel::medium:
        return 0.6F;
    case settings::EffectLevel::high:
        return 1.0F;
    }
    return 0.0F;
}

/** Motion blur shutter fraction per step: 90, 180 and 270 degrees. */
[[nodiscard]] constexpr float motion_blur_shutter(settings::EffectLevel level) noexcept {
    switch (level) {
    case settings::EffectLevel::off:
        return 0.0F;
    case settings::EffectLevel::low:
        return 0.25F;
    case settings::EffectLevel::medium:
        return 0.5F;
    case settings::EffectLevel::high:
        return 0.75F;
    }
    return 0.0F;
}

/**
 * The world post chain for a settings snapshot.
 *
 * Image-quality and accessibility choices (render scale, upscaling,
 * sharpening, brightness, gamma, colour vision) apply in every tier, Retail
 * included. Ambient occlusion, bloom and motion blur are Enhanced effects:
 * the Retail tier stays faithful and never draws them.
 */
[[nodiscard]] constexpr PostSettings post_settings_for(
    const settings::GraphicsSettings& graphics) noexcept {
    PostSettings post;
    post.render_scale = static_cast<float>(graphics.render_scale);
    post.upscale = graphics.upscale == settings::UpscaleFilter::bilinear
                       ? UpscaleFilter::bilinear
                       : UpscaleFilter::edge_adaptive;
    post.sharpness = static_cast<float>(graphics.sharpness);
    post.brightness = static_cast<float>(graphics.brightness);
    post.gamma = static_cast<float>(graphics.gamma);
    switch (graphics.color_vision) {
    case settings::ColorVision::off:
        post.color_vision = ColorVisionMode::off;
        break;
    case settings::ColorVision::protanopia:
        post.color_vision = ColorVisionMode::protanopia;
        break;
    case settings::ColorVision::deuteranopia:
        post.color_vision = ColorVisionMode::deuteranopia;
        break;
    case settings::ColorVision::tritanopia:
        post.color_vision = ColorVisionMode::tritanopia;
        break;
    }
    if (graphics.compatibility_shader()) {
        return post;
    }
    switch (graphics.ambient_occlusion) {
    case settings::EffectLevel::off:
        post.ambient_occlusion = AmbientOcclusion::off;
        break;
    case settings::EffectLevel::low:
        post.ambient_occlusion = AmbientOcclusion::low;
        break;
    case settings::EffectLevel::medium:
        post.ambient_occlusion = AmbientOcclusion::medium;
        break;
    case settings::EffectLevel::high:
        post.ambient_occlusion = AmbientOcclusion::high;
        break;
    }
    post.bloom = graphics.bloom != settings::EffectLevel::off;
    post.bloom_intensity = post.bloom ? bloom_intensity(graphics.bloom) : PostSettings{}.bloom_intensity;
    post.motion_blur = motion_blur_shutter(graphics.motion_blur);
    return post;
}

/** World-texture sampling for a settings snapshot. */
[[nodiscard]] constexpr TextureFiltering texture_filtering_for(
    const settings::GraphicsSettings& graphics) noexcept {
    return {graphics.smooth_textures, graphics.anisotropic_filtering};
}

} // namespace battlespades::render
