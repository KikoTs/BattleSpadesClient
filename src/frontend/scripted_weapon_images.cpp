#include "battlespades/frontend/scripted_weapon_images.hpp"
#include "battlespades/render/camera_basis.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace battlespades::frontend {
bool ScriptedWeaponImages::load(render::BgfxUiRenderer& renderer,
                                const world::ScriptedWeapon& skin,std::string& error) {
    clear(renderer);error.clear();
    for(std::size_t id=0;id<skin.resources().size();++id){
        const auto& resource=skin.resources()[id];
        if(resource.kind!=1||resource.path.empty())continue;
        // load_texture() accepts retail-root relative paths. Pack images live
        // beside that root, and are already confined by the skin host.
        const auto decoded=render::decode_png_rgba8(resource.path);
        if(!decoded){if(error.empty())error=resource.name+": "+decoded.error;continue;}
        const auto image=renderer.create_texture_rgba8(decoded.texture->rgba8,
            decoded.texture->extent,render::TextureFilter::linear);
        if(image){
            images_.emplace(id,*image);
            const auto& pixels=decoded.texture->rgba8;
            for(std::size_t p=3;p<pixels.size();p+=4)if(pixels[p]){visible_images_.insert(id);break;}
            if(resource.name.starts_with("Gfx/Flash/"))muzzle_images_.insert(id);
            else if(resource.name=="Gfx/Sight.tga"||resource.name=="Gfx/Sight.png")hip_crosshairs_.insert(id);
            // Only images actually emitted this frame can replace the iron
            // sight dot. Loaded-but-inactive scopes, muzzle flashes and shell
            // sprites do not count as aiming marks.
            else if(!resource.name.starts_with("Gfx/Flash/")&&!resource.name.starts_with("Gfx/Bullet/")&&
                    !resource.name.starts_with("Gfx/Killfeed/"))aim_images_.insert(id);
        }
        else if(error.empty())error=resource.name+": "+std::string{renderer.last_error()};
    }
    const std::array<std::uint8_t,4> white{255,255,255,255};
    dot_=renderer.create_texture_rgba8(white,{1U,1U},render::TextureFilter::nearest);
    // A lens aperture around the pack's own reticle, independent of aspect ratio.
    constexpr std::uint32_t side=512U;
    std::vector<std::uint8_t> mask(side*side*4U,0U);
    for(std::uint32_t y=0;y<side;++y)for(std::uint32_t x=0;x<side;++x){
        const float distance=std::hypot(static_cast<float>(x)+.5F-side*.5F,static_cast<float>(y)+.5F-side*.5F);
        mask[(y*side+x)*4U+3U]=static_cast<std::uint8_t>(std::clamp(distance-side*.46F,0.F,1.F)*255.F);
    }
    scope_mask_=renderer.create_texture_rgba8(mask,{side,side},render::TextureFilter::linear);
    if(!scope_mask_&&error.empty())error=std::string{renderer.last_error()};
    if(!dot_&&error.empty())error=std::string{renderer.last_error()};
    return error.empty();
}
void ScriptedWeaponImages::clear(render::BgfxUiRenderer& renderer){
    if(scope_mask_)static_cast<void>(renderer.release_texture(scope_mask_->texture));scope_mask_.reset();
    for(const auto& [id,image]:images_){static_cast<void>(id);static_cast<void>(renderer.release_texture(image.texture));}
    if(dot_)static_cast<void>(renderer.release_texture(dot_->texture));
    images_.clear();visible_images_.clear();aim_images_.clear();hip_crosshairs_.clear();muzzle_images_.clear();dot_.reset();
}
bool ScriptedWeaponImages::has_screen_image(const world::SkinFrame& frame)const{
    return std::ranges::any_of(frame.sprites,[this](const auto& s){return s.screen&&s.color[3]>0&&visible_images_.contains(s.resource);});
}
bool ScriptedWeaponImages::has_aim_image(const world::SkinFrame& frame)const{
    return std::ranges::any_of(frame.sprites,[this](const auto& s){
        return aim_images_.contains(s.resource)&&visible_images_.contains(s.resource)&&
               (s.screen?s.color[3]>0:s.position[1]>.01F);
    });
}
std::vector<world::DynamicLight> ScriptedWeaponImages::muzzle_lights(const world::SkinFrame& frame,
                                                                  const render::WorldCamera& camera)const{
    std::vector<world::DynamicLight> result;
    const auto basis=render::world_camera_basis(camera.yaw_degrees,camera.pitch_degrees);
    for(const auto& sprite:frame.sprites){
        if(sprite.screen||!muzzle_images_.contains(sprite.resource)||sprite.position[1]<=.01F||
           !visible_images_.contains(sprite.resource))continue;
        const float brightness=std::max({sprite.color[0],sprite.color[1],sprite.color[2]});if(brightness<=0)continue;
        world::DynamicLight light;
        for(std::size_t k=0;k<3;++k)light.position[k]=static_cast<float>(camera.eye[k]-basis.right[k]*sprite.position[0]+
            basis.forward[k]*sprite.position[1]-basis.up[k]*sprite.position[2]);
        light.color={1.F,.72F,.38F};light.radius=2.4F;light.intensity=1.2F*std::min(brightness,1.F);
        result.push_back(light);if(result.size()==4)break;
    }
    return result;
}
bool ScriptedWeaponImages::draw(render::BgfxUiRenderer& renderer,const world::SkinFrame& frame,
                                render::UiExtent extent,float fov_y_degrees,bool iron_sight_dot)const{
    const float width=static_cast<float>(extent.width),height=static_cast<float>(extent.height);
    const float focal=height*.5F/std::tan(fov_y_degrees*std::numbers::pi_v<float>/360.F);
    const bool add_dot=iron_sight_dot&&!has_aim_image(frame)&&dot_.has_value();
    for(const auto& s:frame.sprites){
        if(add_dot&&hip_crosshairs_.contains(s.resource))continue;
        const auto image=images_.find(s.resource);if(image==images_.end())continue;
        const auto channel=[](float v){return static_cast<std::uint8_t>(std::clamp(v,0.F,1.F)*255.F);};
        const bool additive=!s.screen&&s.color[3]==0.F;
        if(!s.screen&&muzzle_images_.contains(s.resource)){
            if(!renderer.draw(render::ViewModelSprite{image->second.texture,{-s.position[0],-s.position[2],-s.position[1]},
                s.radius,-s.rotation,{channel(s.color[0]),channel(s.color[1]),channel(s.color[2]),additive?std::uint8_t{255}:channel(s.color[3])},additive}))return false;
            continue;
        }
        render::UiRect rect;
        if(s.screen)rect={s.position[0],s.position[1],s.radius,s.position[2]>0?s.position[2]:static_cast<float>(image->second.extent.height)};
        else{
            if(s.position[1]<=.01F)continue;
            const float radius=s.radius*focal/s.position[1];
            rect={width*.5F-s.position[0]*focal/s.position[1]-radius,
                  height*.5F+s.position[2]*focal/s.position[1]-radius,radius*2,radius*2};
        }
        if(!renderer.draw({image->second.texture,rect,std::nullopt,std::nullopt,
            {channel(s.color[0]),channel(s.color[1]),channel(s.color[2]),additive?std::uint8_t{255}:channel(s.color[3])},
            render::UiDrawSpace::window_pixels,s.rotation*180.F/std::numbers::pi_v<float>,additive}))return false;
    }
    if(frame.scope_opacity>0.F&&scope_mask_&&dot_){
        const float side=std::min(width,height),left=(width-side)*.5F,top=(height-side)*.5F;
        const auto alpha=static_cast<std::uint8_t>(std::clamp(frame.scope_opacity,0.F,1.F)*255.F);
        const auto black=[&](render::UiRect rect){return rect.width<=0||rect.height<=0||renderer.draw({dot_->texture,rect,
            std::nullopt,std::nullopt,{0,0,0,alpha},render::UiDrawSpace::window_pixels});};
        if(!black({0,0,left,height})||!black({left+side,0,left,height})||!black({0,0,width,top})||!black({0,top+side,width,top})||
           !renderer.draw({scope_mask_->texture,{left,top,side,side},std::nullopt,std::nullopt,
                {255,255,255,alpha},render::UiDrawSpace::window_pixels}))return false;
    }
    if(add_dot){
        const auto square=[&](float size,render::UiColor color){
            return renderer.draw({dot_->texture,{(width-size)*.5F,(height-size)*.5F,size,size},
                std::nullopt,std::nullopt,color,render::UiDrawSpace::window_pixels});
        };
        if(!square(5.F,{0,0,0,190})||!square(3.F,{255,48,48,255}))return false;
    }
    return true;
}
}
