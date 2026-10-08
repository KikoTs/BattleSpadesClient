#include "battlespades/settings/graphics_apply.hpp"

namespace battlespades::settings {

GraphicsApplyPlan plan_graphics_apply(const ClientSettings& active,
                                      const ClientSettings& requested,
                                      bool multisample_live) noexcept {
    const auto& before = active.graphics;
    const auto& after = requested.graphics;
    GraphicsApplyPlan plan;
    plan.display = active.main.fullscreen != requested.main.fullscreen ||
                   before.resolution != after.resolution ||
                   before.borderless_fullscreen != after.borderless_fullscreen;
    plan.vertical_sync = before.vsync != after.vsync;
    plan.multisample = before.antialiasing != after.antialiasing;
    plan.multisample_deferred = plan.multisample && !multisample_live;
    plan.shader_profile = before.shader_quality != after.shader_quality ||
                          before.effect_quality != after.effect_quality;
    plan.terrain_remesh = before.compatibility_shader() != after.compatibility_shader();
    plan.per_frame = before.draw_distance != after.draw_distance ||
                     before.render_interpolation != after.render_interpolation ||
                     before.hud_scale != after.hud_scale;
    plan.restart_only = before.graphics_api != after.graphics_api ||
                        before.texture_quality != after.texture_quality ||
                        before.model_quality != after.model_quality;
    return plan;
}

} // namespace battlespades::settings
