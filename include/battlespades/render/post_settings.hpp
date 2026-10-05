#pragma once

#include <cstdint>

namespace battlespades::render {

/** How a scene rendered below the window's resolution is brought up to it. */
enum class UpscaleFilter : std::uint8_t {
    /** One bilinear tap; the cheapest, softest result. */
    bilinear,
    /** Edge-adaptive spatial upscale in the style of AMD FSR 1 EASU. */
    edge_adaptive,
};

/** Colour-vision-deficiency correction applied to the world image (not the HUD). */
enum class ColorVisionMode : std::uint8_t {
    off,
    protanopia,
    deuteranopia,
    tritanopia,
};

/** Screen-space ambient occlusion quality; `off` costs nothing. */
enum class AmbientOcclusion : std::uint8_t {
    off,
    low,
    medium,
    high,
};

/**
 * Optional world post-processing, set once per frame by the frontend.
 *
 * With every field at its default the world draws straight into the
 * backbuffer exactly as before, so the Retail tier and every existing capture
 * test keep their pixels. Anything else routes the world and first-person
 * views through an offscreen scene target and a fullscreen post chain that
 * resolves into the backbuffer before the UI views draw, so the HUD and menus
 * are never scaled, blurred, tinted or bloomed.
 */
struct PostSettings final {
    /** Scene resolution relative to the window, 0.5 .. 2.0. */
    float render_scale{1.0F};
    UpscaleFilter upscale{UpscaleFilter::edge_adaptive};
    /** Contrast-adaptive sharpening (FSR 1 RCAS style), 0 = off .. 1. */
    float sharpness{0.0F};
    AmbientOcclusion ambient_occlusion{AmbientOcclusion::off};
    bool bloom{false};
    /** 0 .. 1. */
    float bloom_intensity{0.5F};
    /** Bright-pass threshold; set per frame from the map atmosphere. */
    float bloom_threshold{0.72F};
    /**
     * Camera motion blur as a shutter fraction of the frame interval
     * (0 = off, 0.5 = a 180-degree shutter). The blur length is the camera's
     * own motion since the previous presented frame times this fraction, so
     * it is the same exposure at any frame rate. The first-person view and
     * the HUD are never blurred.
     */
    float motion_blur{0.0F};
    /** Additive display-space brightness, -0.25 .. 0.25. */
    float brightness{0.0F};
    /** Display gamma adjustment: output = pow(colour, 1 / gamma), 0.7 .. 1.6. */
    float gamma{1.0F};
    ColorVisionMode color_vision{ColorVisionMode::off};

    /** True when the world must go through the offscreen post chain. */
    [[nodiscard]] constexpr bool active() const noexcept {
        return render_scale != 1.0F || sharpness > 0.0F ||
               ambient_occlusion != AmbientOcclusion::off || bloom || motion_blur > 0.0F ||
               brightness != 0.0F || gamma != 1.0F || color_vision != ColorVisionMode::off;
    }

    [[nodiscard]] friend constexpr bool operator==(const PostSettings&,
                                                   const PostSettings&) = default;
};

/**
 * What the running backend can do with PostSettings. A feature reported
 * false is ignored by the renderer (the world draws as if it were off), so the
 * frontend can hide or disable its row with a reason.
 */
struct PostCapabilities final {
    /** Offscreen scene target and every fullscreen pass loaded. */
    bool chain{};
    /** Needs a depth buffer the post passes can sample. */
    bool ambient_occlusion{};
    /** Needs a depth buffer the post passes can sample. */
    bool motion_blur{};
    bool bloom{};
    bool edge_adaptive_upscale{};

    [[nodiscard]] friend constexpr bool operator==(const PostCapabilities&,
                                                   const PostCapabilities&) = default;
};

/** Sampler treatment of world-space textures (skydome, particles, AO atlas). */
struct TextureFiltering final {
    /** Linear filtering; false samples texels point-sampled (crisp). */
    bool smooth{true};
    /** Hardware anisotropic filtering at the device maximum. */
    bool anisotropic{false};

    [[nodiscard]] friend constexpr bool operator==(const TextureFiltering&,
                                                   const TextureFiltering&) = default;
};

} // namespace battlespades::render
