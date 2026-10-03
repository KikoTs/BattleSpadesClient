// Graphics tab additions: how settings reach the renderer and the frame pacer.
//
// GPU-free. The renderer side of each effect is covered by the offscreen
// post-process render tests; this checks the contract in front of it: every
// default reproduces the old renderer exactly, the Retail tier never gets an
// Enhanced effect, presets touch only what they own, and the frame limiter
// asks the pacer for whole frames per 60 Hz tick.

#include "battlespades/frontend/frame_rate_meter.hpp"
#include "battlespades/frontend/render_interpolation.hpp"
#include "battlespades/render/graphics_options.hpp"
#include "battlespades/settings/graphics_apply.hpp"
#include "battlespades/settings/graphics_presets.hpp"
#include "battlespades/world/weapon_zoom.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

using namespace std::chrono_literals;
using namespace battlespades;
using settings::EffectLevel;
using settings::FrameLimit;
using settings::GraphicsSettings;

int failures{};

void expect(bool value, const std::string& message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message.c_str());
        ++failures;
    }
}

constexpr std::chrono::nanoseconds tick{16'666'667};

void defaults_reproduce_the_old_renderer() {
    const auto defaults = settings::retail_default_settings().graphics;
    expect(!render::post_settings_for(defaults).active(),
           "default settings must keep the world drawing straight to the backbuffer");
    for (const auto tier : {settings::ShaderQuality::compatibility, settings::ShaderQuality::low,
                            settings::ShaderQuality::medium, settings::ShaderQuality::high,
                            settings::ShaderQuality::ultra}) {
        auto graphics = defaults;
        graphics.shader_quality = tier;
        const auto base = render::profile_for(tier, graphics.effect_quality);
        const auto shaped = render::with_shadow_options(base, graphics);
        expect(shaped.shadow_cascades == base.shadow_cascades &&
                   shaped.shadow_resolution == base.shadow_resolution &&
                   shaped.shadow_pcf_taps == base.shadow_pcf_taps &&
                   shaped.shadow_softness == base.shadow_softness &&
                   shaped.shadow_distance == 0.0F,
               "Auto shadow rows must leave every tier's shadows exactly as before");
    }
    expect(world::zoom_fov_y_degrees(0.6, 75.0) == world::zoom_fov_y_degrees(0.6),
           "the retail FOV must reproduce retail's zoom projection bit for bit");
    expect(std::fabs(world::zoom_fov_y_degrees(1.0, 90.0) - 45.0) < 1e-12,
           "a wider FOV keeps retail's 2x magnification at full zoom");
    const auto texture = render::texture_filtering_for(defaults);
    expect(texture.smooth && texture.anisotropic, "world textures default to smooth + anisotropic");
}

void retail_tier_keeps_its_look() {
    auto graphics = settings::retail_default_settings().graphics;
    graphics.shader_quality = settings::ShaderQuality::compatibility;
    graphics.ambient_occlusion = EffectLevel::high;
    graphics.bloom = EffectLevel::high;
    graphics.motion_blur = EffectLevel::high;
    graphics.shadow_quality = settings::ShadowQuality::ultra;
    graphics.brightness = 0.1;
    graphics.color_vision = settings::ColorVision::deuteranopia;
    const auto post = render::post_settings_for(graphics);
    expect(post.ambient_occlusion == render::AmbientOcclusion::off && !post.bloom &&
               post.motion_blur == 0.0F,
           "Enhanced effects never reach the Retail tier");
    expect(post.brightness == 0.1F && post.color_vision == render::ColorVisionMode::deuteranopia,
           "accessibility choices still apply in the Retail tier");
    const auto profile = render::with_shadow_options(
        render::profile_for(graphics.shader_quality, graphics.effect_quality), graphics);
    expect(profile.shadow_cascades == 0U, "the Retail tier casts no sun shadows");

    graphics.shader_quality = settings::ShaderQuality::high;
    const auto enhanced = render::post_settings_for(graphics);
    expect(enhanced.ambient_occlusion == render::AmbientOcclusion::high && enhanced.bloom &&
               enhanced.bloom_intensity == 1.0F && enhanced.motion_blur == 0.75F,
           "Enhanced tiers receive the chosen effects");
    const auto shadows = render::with_shadow_options(
        render::profile_for(graphics.shader_quality, graphics.effect_quality), graphics);
    expect(shadows.shadow_resolution == 4096U && shadows.shadow_cascades == 1U,
           "Ultra shadows are a 4096 map");
    graphics.shadow_quality = settings::ShadowQuality::off;
    expect(render::with_shadow_options(render::profile_for(graphics.shader_quality,
                                                           graphics.effect_quality),
                                       graphics)
                   .shadow_cascades == 0U,
           "Shadow Quality Off turns sun shadows off in any tier");
}

void presets_own_only_quality_fields() {
    auto graphics = settings::retail_default_settings().graphics;
    expect(settings::matching_graphics_preset(graphics) == settings::GraphicsPreset::medium,
           "the defaults are the Medium preset");
    graphics.field_of_view = 95.0;
    graphics.motion_blur = EffectLevel::low;
    graphics.render_scale = 0.75;
    for (const auto preset : settings::graphics_presets) {
        auto candidate = graphics;
        settings::apply_graphics_preset(candidate, preset);
        expect(settings::matching_graphics_preset(candidate) == preset,
               "every preset must read back as itself");
        expect(candidate.field_of_view == 95.0 && candidate.motion_blur == EffectLevel::low &&
                   candidate.render_scale == 0.75,
               "presets never touch personal display choices");
        const auto plan = settings::plan_graphics_apply(
            settings::ClientSettings{.graphics = graphics},
            settings::ClientSettings{.graphics = candidate}, true);
        expect(!plan.restart_required(), "choosing a preset never needs a restart");
    }
}

void apply_plan_classifies_native_rows() {
    const auto plan = [](auto edit) {
        settings::ClientSettings before = settings::retail_default_settings();
        auto after = before;
        edit(after.graphics);
        return settings::plan_graphics_apply(before, after, true);
    };
    expect(plan([](GraphicsSettings& g) { g.field_of_view = 90.0; }).per_frame &&
               plan([](GraphicsSettings& g) { g.bloom = EffectLevel::low; }).per_frame &&
               plan([](GraphicsSettings& g) { g.render_scale = 0.5; }).per_frame &&
               plan([](GraphicsSettings& g) { g.frame_limit = FrameLimit::unlimited; }).per_frame &&
               plan([](GraphicsSettings& g) { g.anisotropic_filtering = false; }).per_frame,
           "native rows apply live every frame");
    const auto latency = plan([](GraphicsSettings& g) { g.low_latency = false; });
    expect(latency.restart_only && latency.restart_required(),
           "frame latency is a startup-only bgfx parameter");
}

void frame_limiter_asks_for_whole_frames_per_tick() {
    using frontend::limited_frame_period;
    // Match display keeps the long-standing behaviour.
    expect(limited_frame_period(FrameLimit::display, 144U, true, 144'000U, false, tick) ==
               frontend::render_interpolation_paced_period(true, 144'000U, false, tick),
           "Match Display is the old pacing");
    expect(limited_frame_period(FrameLimit::custom, 60U, true, 144'000U, false, tick) ==
               std::chrono::nanoseconds::zero(),
           "a 60 fps cap presents only the tick frame");
    expect(limited_frame_period(FrameLimit::custom, 120U, true, 60'000U, false, tick) == tick / 2,
           "a 120 fps cap is two frames a tick even on a 60 Hz display without VSync");
    expect(limited_frame_period(FrameLimit::custom, 144U, true, 240'000U, false, tick) == tick / 2,
           "a cap rounds down to whole frames per tick");
    expect(limited_frame_period(FrameLimit::custom, 240U, true, 144'000U, true, tick) == tick / 2,
           "VSync still bounds a cap above the display rate");
    expect(limited_frame_period(FrameLimit::unlimited, 0U, true, 60'000U, false, tick) == tick / 8,
           "Unlimited is the pacer's eight frames a tick");
    expect(limited_frame_period(FrameLimit::unlimited, 0U, false, 240'000U, false, tick) ==
               std::chrono::nanoseconds::zero(),
           "render interpolation off keeps one frame per tick");
}

void frame_rate_meter_reports_rate_and_worst_frame() {
    frontend::FrameRateMeter meter;
    frontend::FrameRateMeter::Clock::time_point now{};
    for (int frame{}; frame < 101; ++frame) {
        now += frame == 40 ? 20ms : 5ms;
        meter.record(now);
    }
    expect(meter.reading().has_value(), "a reading appears after half a second");
    const auto reading = *meter.reading();
    expect(reading.frames_per_second > 150.0 && reading.frames_per_second < 210.0 &&
               reading.worst_frame_ms == 20.0,
           "the counter reports the rate and the slowest frame of the window");
    expect(frontend::FrameRateMeter::text(reading).find("FPS") != std::string::npos,
           "the overlay text names its unit");
    now += 5s;
    meter.record(now);
    expect(meter.reading()->worst_frame_ms == 20.0,
           "a suspend is not a frame time and does not publish a bogus reading");
}

} // namespace

int main() {
    defaults_reproduce_the_old_renderer();
    retail_tier_keeps_its_look();
    presets_own_only_quality_fields();
    apply_plan_classifies_native_rows();
    frame_limiter_asks_for_whole_frames_per_tick();
    frame_rate_meter_reports_rate_and_worst_frame();
    if (failures != 0) {
        std::fprintf(stderr, "graphics options: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("graphics options: defaults, tiers, presets, plan, limiter and meter passed\n");
    return 0;
}
