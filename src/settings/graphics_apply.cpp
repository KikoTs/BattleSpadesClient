#include "battlespades/settings/graphics_apply.hpp"

namespace battlespades::settings {

GraphicsApplyPlan plan_graphics_apply(const ClientSettings& active,
                                      const ClientSettings& requested,
                                      bool multisample_live) noexcept {
    const auto& before = active.graphics;
    const auto& after = requested.graphics;
    GraphicsApplyPlan plan;
    plan.display = before.window_mode != after.window_mode ||
                   before.resolution != after.resolution;
    plan.vertical_sync = before.vsync != after.vsync;
    plan.multisample = before.antialiasing != after.antialiasing;
    plan.multisample_deferred = plan.multisample && !multisample_live;
    plan.shader_profile = before.shader_quality != after.shader_quality ||
                          before.effect_quality != after.effect_quality;
    plan.terrain_remesh = before.compatibility_shader() != after.compatibility_shader();
    plan.per_frame = before.draw_distance != after.draw_distance ||
                     before.render_interpolation != after.render_interpolation ||
                     before.hud_scale != after.hud_scale ||
                     before.field_of_view != after.field_of_view ||
                     before.frame_limit != after.frame_limit ||
                     before.frame_rate_cap != after.frame_rate_cap ||
                     before.show_fps != after.show_fps ||
                     before.render_scale != after.render_scale ||
                     before.upscale != after.upscale || before.sharpness != after.sharpness ||
                     before.anisotropic_filtering != after.anisotropic_filtering ||
                     before.smooth_textures != after.smooth_textures ||
                     before.shadow_quality != after.shadow_quality ||
                     before.shadow_distance != after.shadow_distance ||
                     before.ambient_occlusion != after.ambient_occlusion ||
                     before.bloom != after.bloom || before.motion_blur != after.motion_blur ||
                     before.brightness != after.brightness || before.gamma != after.gamma ||
                     before.color_vision != after.color_vision;
    plan.restart_only = before.graphics_api != after.graphics_api ||
                        before.low_latency != after.low_latency ||
                        before.texture_quality != after.texture_quality ||
                        before.model_quality != after.model_quality;
    return plan;
}

} // namespace battlespades::settings
