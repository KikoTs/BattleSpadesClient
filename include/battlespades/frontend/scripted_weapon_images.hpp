#pragma once
#include "battlespades/render/bgfx_ui_renderer.hpp"
#include "battlespades/world/scripted_weapon.hpp"
#include "battlespades/render/world_renderer.hpp"
#include <map>
#include <set>

namespace battlespades::frontend {

/** Pack paths are confined by ScriptedWeapon, independently of the retail UI root. */
class ScriptedWeaponImages final {
public:
    /** Decode and upload all pack images before begin_frame(). */
    [[nodiscard]] bool load(render::BgfxUiRenderer& renderer,
                            const world::ScriptedWeapon& skin,std::string& error);
    void clear(render::BgfxUiRenderer& renderer);
    [[nodiscard]] bool contains(std::size_t resource) const { return images_.contains(resource); }
    [[nodiscard]] bool has_screen_image(const world::SkinFrame& frame) const;
    [[nodiscard]] bool has_aim_image(const world::SkinFrame& frame) const;
    [[nodiscard]] bool has_muzzle_images() const { return !muzzle_images_.empty(); }
    /** Flash lights follow the same authored attachment as their depth-tested image. */
    [[nodiscard]] std::vector<world::DynamicLight> muzzle_lights(const world::SkinFrame& frame,
                                                               const render::WorldCamera& camera) const;
    /** Submits resident images only; never reads files or creates textures mid-frame. */
    [[nodiscard]] bool draw(render::BgfxUiRenderer& renderer,const world::SkinFrame& frame,
                            render::UiExtent extent,float fov_y_degrees,bool iron_sight_dot=false) const;
private:
    std::map<std::size_t,render::UiTextureInfo> images_;
    std::set<std::size_t> aim_images_, visible_images_, hip_crosshairs_, muzzle_images_;
    std::optional<render::UiTextureInfo> dot_;
    std::optional<render::UiTextureInfo> scope_mask_;
};
}
