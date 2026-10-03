#pragma once

#include "battlespades/settings/client_settings.hpp"

#include <array>
#include <cstdint>

namespace battlespades::settings {

/**
 * One-click Graphics quality presets.
 *
 * A preset is not stored: the Preset row shows whichever preset every field it
 * controls matches, or Custom. Presets only touch settings that apply live
 * (no texture/model quality, antialiasing or API), so choosing one never asks
 * for a restart, and leave personal choices alone: window mode, resolution,
 * VSync, frame limit, field of view, render scale, motion blur, brightness,
 * gamma and colour vision.
 */
enum class GraphicsPreset : std::uint8_t {
    /** Faithful retail look: the Compatibility (Retail) tier, no effects. */
    retail,
    low,
    medium,
    high,
    ultra,
    custom,
};

/** The quality fields a preset controls. */
struct GraphicsPresetValues final {
    ShaderQuality shader_quality{ShaderQuality::medium};
    QualityLevel effect_quality{QualityLevel::medium};
    DrawDistance draw_distance{DrawDistance::high};
    ShadowQuality shadow_quality{ShadowQuality::automatic};
    ShadowDistance shadow_distance{ShadowDistance::automatic};
    EffectLevel ambient_occlusion{EffectLevel::off};
    EffectLevel bloom{EffectLevel::off};
    bool anisotropic_filtering{true};
};

inline constexpr std::array<GraphicsPreset, 5U> graphics_presets{
    GraphicsPreset::retail, GraphicsPreset::low, GraphicsPreset::medium,
    GraphicsPreset::high,   GraphicsPreset::ultra,
};

[[nodiscard]] constexpr GraphicsPresetValues graphics_preset_values(GraphicsPreset preset) noexcept {
    switch (preset) {
    case GraphicsPreset::retail:
        return {ShaderQuality::compatibility, QualityLevel::medium, DrawDistance::high,
                ShadowQuality::automatic,     ShadowDistance::automatic,
                EffectLevel::off,             EffectLevel::off,
                false};
    case GraphicsPreset::low:
        return {ShaderQuality::low, QualityLevel::low,      DrawDistance::low,
                ShadowQuality::off, ShadowDistance::automatic, EffectLevel::off,
                EffectLevel::off,   false};
    case GraphicsPreset::medium:
    case GraphicsPreset::custom:
        // The shipped defaults, so a fresh enhanced install reads Medium.
        return {};
    case GraphicsPreset::high:
        return {ShaderQuality::high,       QualityLevel::high,       DrawDistance::high,
                ShadowQuality::automatic,  ShadowDistance::automatic, EffectLevel::medium,
                EffectLevel::low,          true};
    case GraphicsPreset::ultra:
        return {ShaderQuality::ultra, QualityLevel::high,  DrawDistance::high,
                ShadowQuality::ultra, ShadowDistance::far, EffectLevel::high,
                EffectLevel::medium,  true};
    }
    return {};
}

/** Writes a preset's fields; `custom` changes nothing. */
constexpr void apply_graphics_preset(GraphicsSettings& graphics, GraphicsPreset preset) noexcept {
    if (preset == GraphicsPreset::custom) {
        return;
    }
    const auto values = graphics_preset_values(preset);
    graphics.shader_quality = values.shader_quality;
    graphics.effect_quality = values.effect_quality;
    graphics.draw_distance = values.draw_distance;
    graphics.shadow_quality = values.shadow_quality;
    graphics.shadow_distance = values.shadow_distance;
    graphics.ambient_occlusion = values.ambient_occlusion;
    graphics.bloom = values.bloom;
    graphics.anisotropic_filtering = values.anisotropic_filtering;
}

/** The preset every controlled field matches, or `custom`. */
[[nodiscard]] constexpr GraphicsPreset matching_graphics_preset(
    const GraphicsSettings& graphics) noexcept {
    for (const auto preset : graphics_presets) {
        auto candidate = graphics;
        apply_graphics_preset(candidate, preset);
        if (candidate == graphics) {
            return preset;
        }
    }
    return GraphicsPreset::custom;
}

} // namespace battlespades::settings
