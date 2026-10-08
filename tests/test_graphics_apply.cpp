#include "battlespades/settings/graphics_apply.hpp"

#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using battlespades::settings::Antialiasing;
using battlespades::settings::ClientSettings;
using battlespades::settings::DrawDistance;
using battlespades::settings::GraphicsApi;
using battlespades::settings::GraphicsApplyPlan;
using battlespades::settings::QualityLevel;
using battlespades::settings::ShaderQuality;
using battlespades::settings::plan_graphics_apply;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] GraphicsApplyPlan plan(const std::function<void(ClientSettings&)>& change,
                                     bool multisample_live = true) {
    const ClientSettings before{};
    auto after = before;
    change(after);
    return plan_graphics_apply(before, after, multisample_live);
}

void unchanged_settings_need_nothing() {
    const auto result = plan([](ClientSettings&) {});
    expect(result == GraphicsApplyPlan{}, "identical settings must plan no work");
    expect(!result.live_work() && !result.restart_required(), "nothing to do, nothing to restart");
}

void kiril_crash_change_is_deferred_on_direct3d() {
    // The committed settings that ended the 2026-10-01 session: Antialiasing
    // moved to 4x on Direct3D 11.
    const auto direct3d = plan([](ClientSettings& s) { s.graphics.antialiasing = Antialiasing::samples_4; },
                               false);
    expect(direct3d.multisample && direct3d.multisample_deferred,
           "a Direct3D MSAA change must wait for a restart");
    expect(direct3d.restart_required() && !direct3d.live_work(),
           "a deferred MSAA change asks for a restart and does no live work");

    const auto vulkan = plan([](ClientSettings& s) { s.graphics.antialiasing = Antialiasing::samples_2; },
                             true);
    expect(vulkan.multisample && !vulkan.multisample_deferred && vulkan.live_work() &&
               !vulkan.restart_required(),
           "a backend that resets MSAA in place applies it live");
}

void live_rows_classify_by_mechanism() {
    expect(plan([](ClientSettings& s) { s.graphics.resolution = {1'280U, 720U}; }).display,
           "resolution is a display change");
    expect(plan([](ClientSettings& s) { s.main.fullscreen = !s.main.fullscreen; }).display,
           "fullscreen is a display change");
    expect(plan([](ClientSettings& s) {
               s.graphics.borderless_fullscreen = !s.graphics.borderless_fullscreen;
           }).display,
           "fullscreen kind is a display change");
    expect(plan([](ClientSettings& s) { s.graphics.vsync = !s.graphics.vsync; }).vertical_sync,
           "VSync is a presentation reset");
    const auto tier = plan([](ClientSettings& s) { s.graphics.shader_quality = ShaderQuality::ultra; });
    expect(tier.shader_profile && !tier.terrain_remesh && !tier.restart_required(),
           "an enhanced-to-enhanced tier switch is a per-frame profile");
    expect(plan([](ClientSettings& s) { s.graphics.effect_quality = QualityLevel::low; })
               .shader_profile,
           "effect quality is part of the profile");
    const auto legacy = plan([](ClientSettings& s) {
        s.graphics.shader_quality = ShaderQuality::compatibility;
    });
    expect(legacy.shader_profile && legacy.terrain_remesh && !legacy.restart_required(),
           "crossing into Legacy re-meshes terrain through the bounded lane");
    for (const auto& change : std::vector<std::function<void(ClientSettings&)>>{
             [](ClientSettings& s) { s.graphics.draw_distance = DrawDistance::low; },
             [](ClientSettings& s) {
                 s.graphics.render_interpolation = !s.graphics.render_interpolation;
             },
             [](ClientSettings& s) { s.graphics.hud_scale = 2.0; },
         }) {
        const auto result = plan(change);
        expect(result.per_frame && result.live_work() && !result.restart_required(),
               "draw distance, interpolation and HUD scale are read every frame");
    }
}

void startup_resources_require_a_restart() {
    for (const auto& change : std::vector<std::function<void(ClientSettings&)>>{
             [](ClientSettings& s) { s.graphics.graphics_api = GraphicsApi::vulkan; },
             [](ClientSettings& s) { s.graphics.texture_quality = QualityLevel::high; },
             [](ClientSettings& s) { s.graphics.model_quality = QualityLevel::low; },
         }) {
        const auto result = plan(change);
        expect(result.restart_only && result.restart_required() && !result.live_work(),
               "API, texture and model quality only apply on the next launch");
    }
}

void mixed_commit_does_both() {
    const auto result = plan(
        [](ClientSettings& s) {
            s.graphics.antialiasing = Antialiasing::samples_4;
            s.graphics.vsync = !s.graphics.vsync;
            s.graphics.texture_quality = QualityLevel::high;
        },
        false);
    expect(result.vertical_sync && result.live_work(), "the live part still applies now");
    expect(result.multisample_deferred && result.restart_only && result.restart_required(),
           "and the rest is announced for the restart");
}

} // namespace

int main() {
    const std::vector<std::pair<std::string_view, void (*)()>> tests{
        {"unchanged_settings_need_nothing", unchanged_settings_need_nothing},
        {"kiril_crash_change_is_deferred_on_direct3d", kiril_crash_change_is_deferred_on_direct3d},
        {"live_rows_classify_by_mechanism", live_rows_classify_by_mechanism},
        {"startup_resources_require_a_restart", startup_resources_require_a_restart},
        {"mixed_commit_does_both", mixed_commit_does_both},
    };
    for (const auto& [name, test] : tests) {
        try {
            test();
        } catch (const std::exception& error) {
            std::cerr << name << " failed: " << error.what() << '\n';
            return 1;
        }
    }
    std::cout << "graphics apply tests passed\n";
    return 0;
}
