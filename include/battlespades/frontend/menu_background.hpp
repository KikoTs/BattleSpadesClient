#pragma once

#include "battlespades/settings/menu_backgrounds.hpp"
#include "battlespades/ui/draw_list.hpp"

#include <algorithm>
#include <cmath>
#include <span>

namespace battlespades::frontend {

struct MenuBackgroundLayer final {
    std::string_view asset;
    float parallax;
    bool cutout;
    // Register an extracted sprite to the source composition without resampling its pixels.
    double scale{1.0}, offset_x{}, offset_y{};
};

// Separate full-canvas RGBA cutouts over an inpainted background plate.
// The original image remains the static view and the fallback for missing assets.
inline constexpr std::array miner_background_layers{
    MenuBackgroundLayer{"png/ui/menu_backgrounds/layers/miner-and-zombies/scenery.png", 0.55F, false},
    MenuBackgroundLayer{"png/ui/menu_backgrounds/layers/miner-and-zombies/zombie.png", 1.15F, true},
    MenuBackgroundLayer{"png/ui/menu_backgrounds/layers/miner-and-zombies/foreground.png", 2.10F, true},
};

inline constexpr std::array battle_background_layers{
    MenuBackgroundLayer{"png/ui/menu_backgrounds/layers/original-battle-artwork/scenery.png", 0.55F, false},
    MenuBackgroundLayer{"png/ui/menu_backgrounds/layers/original-battle-artwork/jetpacker.png", 0.95F, true, 0.74, 0.0, 0.01},
    MenuBackgroundLayer{"png/ui/menu_backgrounds/layers/original-battle-artwork/rifleman.png", 1.35F, true},
    MenuBackgroundLayer{"png/ui/menu_backgrounds/layers/original-battle-artwork/miner.png", 2.10F, true, 0.735, 0.0, 0.20},
};
inline constexpr std::array builder_background_layers{
    MenuBackgroundLayer{"png/ui/menu_backgrounds/layers/battle-builder-construction/scenery.png", 0.55F, false},
    MenuBackgroundLayer{"png/ui/menu_backgrounds/layers/battle-builder-construction/builder.png", 2.10F, true, 0.92, 0.0, 0.04},
};
inline constexpr std::array mafia_background_layers{
    MenuBackgroundLayer{"png/ui/menu_backgrounds/layers/valentines-mafia-warehouse/scenery.png", 0.55F, false},
    MenuBackgroundLayer{"png/ui/menu_backgrounds/layers/valentines-mafia-warehouse/right-group.png", 1.35F, true, 0.75, 0.25, 0.125},
    MenuBackgroundLayer{"png/ui/menu_backgrounds/layers/valentines-mafia-warehouse/left-gangster.png", 2.10F, true, 0.63, 0.0, 0.29},
};
inline constexpr std::array matchmaking_background_layers{
    MenuBackgroundLayer{"png/ui/menu_backgrounds/layers/matchmaking-battle/scenery.png", 0.55F, false},
    MenuBackgroundLayer{"png/ui/menu_backgrounds/layers/matchmaking-battle/rifleman.png", 1.25F, true},
    MenuBackgroundLayer{"png/ui/menu_backgrounds/layers/matchmaking-battle/rocketeer.png", 2.10F, true, 0.81, 0.113, 0.14},
};
inline constexpr std::array blueprint_background_layers{
    MenuBackgroundLayer{"png/ui/menu_backgrounds/layers/tutorial-blueprints/scenery.png", 0.55F, false},
    MenuBackgroundLayer{"png/ui/menu_backgrounds/layers/tutorial-blueprints/miner.png", 1.65F, true, 1.0, 0.0, -0.036},
};

struct MenuBackgroundScene final {
    std::string_view id;
    double width, height;
    std::span<const MenuBackgroundLayer> layers;
};

inline constexpr std::array menu_background_scenes{
    MenuBackgroundScene{"miner-and-zombies", 1448.0, 1086.0, miner_background_layers},
    MenuBackgroundScene{"original-battle-artwork", 1465.0, 1073.0, battle_background_layers},
    MenuBackgroundScene{"battle-builder-construction", 1448.0, 1086.0, builder_background_layers},
    MenuBackgroundScene{"valentines-mafia-warehouse", 1448.0, 1086.0, mafia_background_layers},
    MenuBackgroundScene{"matchmaking-battle", 1774.0, 887.0, matchmaking_background_layers},
    MenuBackgroundScene{"tutorial-blueprints", 1774.0, 887.0, blueprint_background_layers},
};

[[nodiscard]] constexpr const MenuBackgroundScene* menu_background_scene(std::string_view id) noexcept {
    for (const auto& scene : menu_background_scenes) if (scene.id == id) return &scene;
    return nullptr;
}

[[nodiscard]] inline const MenuBackgroundLayer* menu_background_layer(std::string_view asset) {
    for (const auto& scene : menu_background_scenes)
        for (const auto& layer : scene.layers) if (layer.asset == asset) return &layer;
    return nullptr;
}

/** Frame-rate-independent pointer easing; disabling motion restores a still image. */
struct MenuBackgroundMotion final {
    double x{}, y{};
    double velocity_x{}, velocity_y{}, time{};
    double blur_x{}, blur_y{}, target_x{}, target_y{};
    bool tracking{};

    void update(bool enabled, bool pointer_active, double pointer_x, double pointer_y,
                double width, double height, double seconds) noexcept {
        if (!enabled) { *this = {}; return; }
        const bool valid = pointer_active && width > 0.0 && height > 0.0 &&
                           std::isfinite(pointer_x) && std::isfinite(pointer_y);
        const double tx = valid ? std::clamp(pointer_x * 2.0 / width - 1.0, -1.0, 1.0) : 0.0;
        const double ty = valid ? std::clamp(pointer_y * 2.0 / height - 1.0, -1.0, 1.0) : 0.0;
        const double dt = std::isfinite(seconds) ? std::clamp(seconds, 0.0, 0.1) : 0.0;
        // Exact critically damped spring integration: smooth velocity even
        // when the cursor reverses direction, with no oscillation or overshoot.
        const auto spring = [dt](double target, double& position, double& velocity) {
            constexpr double frequency = 32.0;
            const double displacement = position - target;
            const double travel = (velocity + frequency * displacement) * dt;
            const double decay = std::exp(-frequency * dt);
            position = target + (displacement + travel) * decay;
            velocity = (velocity - frequency * travel) * decay;
        };
        spring(tx, x, velocity_x);
        spring(ty, y, velocity_y);
        // A short shutter follows actual pointer input, independently of camera settling.
        // No blur spike on focus gain/loss, and no lingering smear when the cursor stops.
        if (dt > 0.0 && valid && tracking) {
            const double mix = -std::expm1(-dt / 0.022);
            blur_x += (std::clamp((tx - target_x) / dt, -8.0, 8.0) - blur_x) * mix;
            blur_y += (std::clamp((ty - target_y) / dt, -8.0, 8.0) - blur_y) * mix;
        } else if (!valid || !tracking) {
            blur_x = blur_y = 0.0;
        }
        target_x = tx;
        target_y = ty;
        tracking = valid;
        time = std::fmod(time + dt, 4096.0);
    }
};

/** Cover without stretching; a small overscan leaves room for gentle camera travel. */
inline void style_menu_background(ui::SpriteDrawCommand& sprite,
                                  const settings::MenuBackgroundOption& option,
                                  ui::PixelExtent window, bool motion,
                                  const MenuBackgroundMotion& pointer) {
    const double width = static_cast<double>(window.width);
    const double height = static_cast<double>(window.height);
    const double inset = option.map_scene ? 14.0 : 0.0;
    const bool cropped = option.crop_width > 0.0;
    const double source_width = cropped ? option.crop_width : option.width - inset * 2.0;
    const double source_height = cropped ? option.crop_height : option.height - inset * 2.0;
    const double scale = std::max(width / source_width, height / source_height) *
                         (motion ? 1.03 : 1.0);
    const double w = source_width * scale, h = source_height * scale;
    sprite.destination = {(width - w) * 0.5, (height - h) * 0.5, w, h};
    sprite.source_pixels = ui::DrawRect{cropped ? option.crop_x : inset,
                                        cropped ? option.crop_y : inset,
                                        source_width, source_height};
    sprite.background_motion.reset();
    sprite.background_time = 0.0F;
    if (motion) {
        sprite.background_motion = std::array<float, 4U>{
            static_cast<float>(std::clamp(pointer.x, -1.0, 1.0)),
            static_cast<float>(std::clamp(pointer.y, -1.0, 1.0)),
            static_cast<float>(std::clamp(pointer.blur_x, -8.0, 8.0)),
            static_cast<float>(std::clamp(pointer.blur_y, -8.0, 8.0))};
        sprite.background_time = static_cast<float>(pointer.time);
    }
}

[[nodiscard]] inline bool is_menu_background(const ui::DrawCommand& command) {
    const auto* sprite = std::get_if<ui::SpriteDrawCommand>(&command);
    if (sprite == nullptr || sprite->space != ui::DrawSpace::window_pixels) return false;
    return std::ranges::any_of(settings::menu_background_options, [sprite](const auto& option) {
        return sprite->asset_id == option.asset ||
               (!option.fallback.empty() && sprite->asset_id == option.fallback);
    });
}

// Full tick frames and cached render-only frames must apply the same setting.
// Otherwise static mode alternates between two crops at the render/tick rates.
inline void style_menu_background_frame(ui::DrawList& list,
                                       const settings::MenuBackgroundOption& option,
                                       std::string_view asset, ui::PixelExtent window,
                                       bool motion, const MenuBackgroundMotion& pointer) {
    for (auto& command : list.mutable_commands()) {
        if (!is_menu_background(command)) continue;
        auto& sprite = std::get<ui::SpriteDrawCommand>(command);
        sprite.asset_id = asset;
        style_menu_background(sprite, option, window, motion, pointer);
    }
}

inline void style_menu_background_layer(ui::SpriteDrawCommand& sprite, ui::PixelExtent window,
                                        const MenuBackgroundMotion& pointer, const MenuBackgroundScene& scene,
                                        const MenuBackgroundLayer& layer) {
    auto option = *settings::menu_background_option(scene.id);
    option.crop_x *= scene.width / option.width;
    option.crop_y *= scene.height / option.height;
    option.crop_width *= scene.width / option.width;
    option.crop_height *= scene.height / option.height;
    option.width = scene.width;
    option.height = scene.height;
    sprite.asset_id = layer.asset;
    sprite.sampling = ui::TextureFilter::linear;
    style_menu_background(sprite, option, window, true, pointer);
    // Expand the banner crop back to its full canvas before placing the cutout.
    // All layers share one camera/framing; the viewport clips the unused margins.
    const double pixel_scale = sprite.destination.width / sprite.source_pixels->width;
    const double full_width = scene.width * pixel_scale;
    const double full_height = scene.height * pixel_scale;
    sprite.destination = {
        sprite.destination.x - option.crop_x * pixel_scale + full_width * layer.offset_x,
        sprite.destination.y - option.crop_y * pixel_scale + full_height * layer.offset_y,
        full_width * layer.scale, full_height * layer.scale};
    sprite.source_pixels = ui::DrawRect{0.0, 0.0, scene.width, scene.height};
}

} // namespace battlespades::frontend
