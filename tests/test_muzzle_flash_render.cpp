#include "battlespades/frontend/scripted_weapon_images.hpp"
#include "battlespades/render/render_views.hpp"
#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <stb_image_write.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {void expect(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}}
int main(int argc,char** argv){
    using namespace battlespades;
    try{
        expect(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
        auto* window=SDL_CreateWindow("Muzzle depth and light regression",800,600,SDL_WINDOW_HIDDEN);expect(window!=nullptr,SDL_GetError());
        render::BgfxUiRenderer ui;render::BgfxUiRendererConfig config;
        config.native_window.window=SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr);
        config.asset_root=argc>1?argv[1]:AOS_TEST_ASSET_ROOT;config.shader_root=AOS_SHADER_BIN_ROOT;
        config.drawable_extent={800,600};config.backend=argc>3&&std::string_view{argv[3]}=="vulkan"?render::GraphicsBackend::vulkan:render::GraphicsBackend::direct3d11;
        config.vertical_sync=false;expect(ui.initialize(config),std::string{ui.last_error()});
        render::WorldRenderer scene;expect(scene.initialize(config.shader_root,config.asset_root),std::string{scene.last_error()});
        auto profile=render::profile_for(settings::ShaderQuality::low,settings::QualityLevel::high);profile.hdr_target=false;
        scene.set_quality_profile(profile);
        world::MapAtmosphere atmosphere;atmosphere.key_intensity=.12F;atmosphere.ambient_intensity=.22F;
        atmosphere.fog_color={16,20,26};scene.set_atmosphere(atmosphere);scene.set_fog_color({16,20,26});
        world::ChunkMesh wall;wall.minimum={48,48,48};wall.maximum={48,52,52};
        wall.vertices={{48,48,48,0x00505050,1},{48,52,48,0x00505050,1},{48,52,52,0x00505050,1},{48,48,52,0x00505050,1}};
        wall.indices={0,1,2,0,2,3};expect(scene.set_world_model_mesh(0,wall),std::string{scene.last_error()});
        const std::array world_draws{render::WorldModelDraw{0}};
        const auto colour=bgfx::createTexture2D(800,600,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_RT);
        const auto depth=bgfx::createTexture2D(800,600,false,1,bgfx::TextureFormat::D24S8,BGFX_TEXTURE_RT_WRITE_ONLY);
        const std::array attachments{colour,depth};const auto framebuffer=bgfx::createFrameBuffer(2,attachments.data(),false);
        const auto readback=bgfx::createTexture2D(800,600,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK);
        expect(bgfx::isValid(framebuffer)&&bgfx::isValid(readback),"GPU capture target failed");
        const std::array<std::uint8_t,4> white{255,255,255,255};const auto probe=ui.create_texture_rgba8(white,{1,1},render::TextureFilter::nearest);
        expect(probe.has_value(),"Occlusion probe texture failed");
        const std::filesystem::path output=argc>2?argv[2]:"tmp/honey-muzzle";std::filesystem::create_directories(output);
        std::size_t checked=0;
        for(const auto* pack:{"pack-legacy-honey-badger-smg-smg","pack-legacy-honey-badger-alternatehoneybadger-smg"}){
            world::ScriptedWeapon skin;std::string error;
            expect(skin.load(config.asset_root.parent_path()/"client/cosmetics/packs"/pack/"skin.json",error),error);
            frontend::ScriptedWeaponImages images;expect(images.load(ui,skin,error),error);expect(images.has_muzzle_images(),"Honey Badger flash resources are missing");
            scene.clear_view_model();
            for(std::size_t id=0;id<skin.resources().size();++id)if(skin.resources()[id].kind==0){
                const auto mesh=skin.model_mesh(id,{72,111,181,255});if(!mesh.empty())expect(scene.set_view_model_mesh(static_cast<std::uint32_t>(id),mesh),std::string{scene.last_error()});
            }
            for(int aimed=0;aimed<2;++aimed){
                render::WorldCamera camera;camera.eye={50,50,50};camera.fov_y_degrees=aimed?50.:75.;camera.fog_distance=64;
                world::SkinInput input;input.dt=1.F/60;input.muted=true;input.aim=static_cast<float>(aimed);input.screen_width=800;input.screen_height=600;
                for(int n=0;n<60;++n)expect(skin.update(input,error),error);
                expect(images.muzzle_lights(skin.frame(),camera).empty(),"Idle weapon emits a muzzle light");
                input.ready=0;input.fired=true;expect(skin.update(input,error),error);
                const auto shot=skin.frame();const auto lights=images.muzzle_lights(shot,camera);expect(!lights.empty(),"Fired weapon does not emit a muzzle light");
                std::vector<render::ViewModelDraw> models;for(const auto& draw:shot.models)models.push_back({static_cast<std::uint32_t>(draw.resource),draw.transform});
                const auto capture=[&](bool gun,bool flash,bool illumination,bool behind){
                    expect(ui.begin_frame(),std::string{ui.last_error()});
                    expect(scene.submit(camera,config.drawable_extent,gun?std::span<const render::ViewModelDraw>{models}:std::span<const render::ViewModelDraw>{},world_draws,{}, {},illumination?std::span<const world::DynamicLight>{lights}:std::span<const world::DynamicLight>{}),std::string{scene.last_error()});
                    for(const auto view:{render::backdrop_clear_view_id,render::world_view_id,render::view_model_view_id,render::ui_window_view_id,render::ui_canvas_view_id})bgfx::setViewFrameBuffer(view,framebuffer);
                    auto frame=shot;
                    if(!flash)std::erase_if(frame.sprites,[&](const auto& sprite){return skin.resources()[sprite.resource].name.starts_with("Gfx/Flash/");});
                    expect(images.draw(ui,frame,config.drawable_extent,static_cast<float>(camera.fov_y_degrees)),std::string{ui.last_error()});
                    if(behind)expect(ui.draw(render::ViewModelSprite{probe->texture,{0,0,-4},8,0,{255,80,20,255},true}),std::string{ui.last_error()});
                    bgfx::blit(render::ui_canvas_view_id+1,readback,0,0,colour,0,0,800,600);
                    std::vector<std::uint8_t> pixels(800*600*4);const auto ready=bgfx::readTexture(readback,pixels.data());
                    expect(ui.end_frame(),std::string{ui.last_error()});while(bgfx::frame()<ready){}return pixels;
                };
                const auto background=capture(false,false,false,false),base=capture(true,false,false,false);
                const auto occluded=capture(true,false,false,true),flash=capture(true,true,false,false),lit=capture(true,true,true,false);
                std::size_t weapon_pixels=0,leaked=0,bright_weapon=0,bright_wall=0,flash_pixels=0;
                for(std::size_t p=0;p<base.size();p+=4){
                    int mask=0,leak=0,illumination=0,effect=0;
                    for(unsigned c=0;c<3;++c){mask+=std::abs(base[p+c]-background[p+c]);leak+=std::abs(base[p+c]-occluded[p+c]);
                        illumination+=static_cast<int>(lit[p+c])-flash[p+c];effect+=std::abs(base[p+c]-flash[p+c]);}
                    if(mask>12){++weapon_pixels;if(leak>4)++leaked;if(illumination>8)++bright_weapon;}
                    else if(illumination>8)++bright_wall;
                    if(effect>8)++flash_pixels;
                }
                expect(weapon_pixels>1000,"Weapon capture is missing");
                expect(leaked<weapon_pixels/100,"Depth-tested muzzle sprite leaks through the weapon");
                expect(bright_weapon>100&&bright_wall>100,"Muzzle light must illuminate the weapon and nearby geometry");
                expect(flash_pixels>50,"Authored muzzle flash is invisible");
                const auto stem=std::string{pack}+(aimed?"-aim":"-hip");
                for(const auto& [suffix,pixels]:std::array<std::pair<const char*,const std::vector<std::uint8_t>*>,3>{{{"-idle",&base},{"-flash",&lit},{"-depth-probe",&occluded}}})
                    expect(stbi_write_png((output/(stem+suffix+".png")).string().c_str(),800,600,4,pixels->data(),800*4)!=0,"Capture write failed");
                std::cout<<stem<<": receiver pixels="<<weapon_pixels<<", leaked="<<leaked<<", lit gun="<<bright_weapon<<", lit wall="<<bright_wall<<", flash="<<flash_pixels<<'\n';
                input.fired=false;input.ready=1;expect(skin.update(input,error),error);
                expect(images.muzzle_lights(skin.frame(),camera).empty(),"Muzzle light persists after the flash");++checked;
            }
            images.clear(ui);
        }
        static_cast<void>(ui.release_texture(probe->texture));scene.shutdown();
        bgfx::destroy(readback);bgfx::destroy(framebuffer);bgfx::destroy(depth);bgfx::destroy(colour);
        ui.shutdown();SDL_DestroyWindow(window);SDL_Quit();std::cout<<checked<<" Honey Badger muzzle depth/lighting cases passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
