#include "battlespades/frontend/scripted_weapon_images.hpp"
#include "battlespades/render/render_views.hpp"
#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <nlohmann/json.hpp>
#include <stb_image_write.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool value,const std::string& error){if(!value)throw std::runtime_error(error);}
}
int main(int argc,char** argv){
    using namespace battlespades;
    SDL_Window* window=nullptr;
    try{
        expect(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
        window=SDL_CreateWindow("Weapon sight regression",1024,768,SDL_WINDOW_HIDDEN);
        expect(window!=nullptr,SDL_GetError());
        render::BgfxUiRenderer renderer;
        render::BgfxUiRendererConfig config;
        config.native_window.window=SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr);
        config.asset_root=AOS_TEST_ASSET_ROOT;config.shader_root=AOS_SHADER_BIN_ROOT;
        config.drawable_extent={1024,768};config.backend=render::GraphicsBackend::direct3d11;config.vertical_sync=false;
        expect(renderer.initialize(config),std::string{renderer.last_error()});
        const auto supported=bgfx::getCaps()->supported;
        expect((supported&BGFX_CAPS_TEXTURE_READ_BACK)&&(supported&BGFX_CAPS_TEXTURE_BLIT),"GPU readback is required for this regression");
        const std::filesystem::path output=argc>1?argv[1]:"tmp/weapon-sight-images";
        std::filesystem::create_directories(output);
        const auto colour=bgfx::createTexture2D(1024,768,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_RT);
        const auto framebuffer=bgfx::createFrameBuffer(1,&colour,false);
        const auto readback=bgfx::createTexture2D(1024,768,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK);
        const std::array<std::uint8_t,4> white_pixel{255,255,255,255};
        const auto background=renderer.create_texture_rgba8(white_pixel,{1,1},render::TextureFilter::nearest);
        expect(background.has_value(),"Could not create scope test background");
        const auto capture=[&](frontend::ScriptedWeaponImages& images,const world::SkinFrame& frame,bool aim,bool assist=true,bool lens_test=false){
            expect(renderer.begin_frame(),std::string{renderer.last_error()});
            for(const auto view:{render::backdrop_clear_view_id,render::ui_window_view_id,render::ui_canvas_view_id})bgfx::setViewFrameBuffer(view,framebuffer);
            if(lens_test)expect(renderer.draw({background->texture,{0,0,1024,768},std::nullopt,std::nullopt,
                {65,80,70,255},render::UiDrawSpace::window_pixels}),"Scope background failed");
            expect(images.draw(renderer,frame,config.drawable_extent,aim?37.5F:75.F,aim&&assist),std::string{renderer.last_error()});
            constexpr std::uint16_t readback_view=render::ui_canvas_view_id+1U;
            bgfx::blit(readback_view,readback,0,0,colour,0,0,1024,768);
            std::vector<std::uint8_t> pixels(1024U*768U*4U);
            const auto ready=bgfx::readTexture(readback,pixels.data());
            expect(renderer.end_frame(),std::string{renderer.last_error()});
            while(bgfx::frame()<ready){}
            return pixels;
        };
        std::ifstream input(config.asset_root.parent_path()/"client/cosmetics/catalog.json");
        const auto catalog=nlohmann::json::parse(input);
        std::size_t packs=0,images_checked=0,original_crosshairs=0;
        for(const auto& item:catalog.at("items")){
            if(!item.value("enabled",false)||!item.contains("scripted_skin"))continue;
            const std::string id=item.at("id");
            world::ScriptedWeapon skin;std::string error;
            expect(skin.load(config.asset_root.parent_path()/item.at("scripted_skin").get<std::string>(),error),error);
            const auto initial_count=skin.resources().size();
            frontend::ScriptedWeaponImages images;
            expect(images.load(renderer,skin,error),id+": "+error);
            for(std::size_t resource=0;resource<skin.resources().size();++resource){
                const auto& r=skin.resources()[resource];if(r.kind!=1||r.path.empty())continue;
                expect(images.contains(resource),id+": image not resident: "+r.name);++images_checked;
            }
            for(int phase=0;phase<2;++phase){
                world::SkinInput state;state.aim=phase?1.F:0.F;state.screen_width=1024;state.screen_height=768;
                for(int n=0;n<40;++n){state.dt=1.F/60.F;expect(skin.update(state,error),id+": "+error);}
                // Draw2D must reuse already resident resource IDs, including scopes
                // first requested on entering ADS. No upload may occur mid-frame.
                expect(skin.resources().size()==initial_count,id+": resources appeared only after opening a frame");
                const auto pixels=capture(images,skin.frame(),phase!=0);
                if(!phase && std::ranges::count_if(skin.frame().sprites,[](const auto& sprite){return sprite.screen;})==1) {
                    const auto& sprite=*std::ranges::find_if(skin.frame().sprites,[](const auto& s){return s.screen;});
                    auto cursor_frame=skin.frame();
                    std::erase_if(cursor_frame.sprites,[](const auto& s){return !s.screen;});
                    const auto cursor_pixels=capture(images,cursor_frame,false,false);
                    const auto source=render::decode_png_rgba8(skin.resources()[sprite.resource].path);
                    expect(static_cast<bool>(source),"Original crosshair did not decode");
                    const auto& expected=source.texture->rgba8;
                    const auto w=source.texture->extent.width,h=source.texture->extent.height;
                    expect(sprite.radius==static_cast<float>(w),id+": crosshair is scaled away from its original size");
                    const int left=static_cast<int>(std::lround(sprite.position[0])),top=static_cast<int>(std::lround(sprite.position[1]));
                    int difference=0;
                    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
                        // Large authored sights are clipped by the viewport in OpenSpades too.
                        if(left+static_cast<int>(x)<0||left+static_cast<int>(x)>=1024||top+static_cast<int>(y)<0||top+static_cast<int>(y)>=768)continue;
                        const auto a=(y*w+x)*4U;
                        const auto b=((top+static_cast<int>(y))*1024+left+static_cast<int>(x))*4;
                        for(unsigned c=0;c<3;++c) {
                            const auto reference=static_cast<int>(std::lround(expected[a+c]*(expected[a+3]/255.F)*sprite.color[c]*sprite.color[3]));
                            difference=std::max(difference,std::abs(static_cast<int>(cursor_pixels.at(static_cast<std::size_t>(b)+c))-reference));
                        }
                    }
                    expect(difference<=3,id+": original crosshair pixels differ by "+std::to_string(difference));
                    ++original_crosshairs;
                }
                std::size_t visible=0,red=0;
                for(std::size_t y=368;y<400;++y)for(std::size_t x=496;x<528;++x){const auto p=(y*1024+x)*4;
                    if(pixels[p]>80||pixels[p+1]>80||pixels[p+2]>80)++visible;
                    if(pixels[p]>100&&pixels[p]>pixels[p+1]*2&&pixels[p]>pixels[p+2]*2)++red;
                }
                if(phase||images.has_screen_image(skin.frame()))expect(visible>0,id+": crosshair/aim mark is invisible at screen centre");
                if(phase&&!images.has_aim_image(skin.frame()))expect(red>=4,id+": iron sight fallback dot is missing");
                if(id=="community-mp5k-v2"&&phase)expect(images.has_aim_image(skin.frame())&&red>0,"MP5K must display its authored red dot");
                if(id=="community-bren-v2"||id=="community-mp5k-v2"||id=="community-kar98-v2"){
                    const auto file=output/(id+(phase?"-aim.png":"-hip.png"));
                    expect(stbi_write_png(file.string().c_str(),1024,768,4,pixels.data(),1024*4)!=0,"Could not save GPU evidence");
                }
                // An optic with its own reticle must look byte-identical with
                // the iron-sight assist enabled or disabled (no duplicate dot).
                if(phase&&images.has_aim_image(skin.frame())){
                    const auto without=capture(images,skin.frame(),true,false);
                    expect(pixels==without,id+": an optic received an extra centre dot");
                }
                std::cout<<id<<(phase?" aim":" hip")<<": centre="<<visible<<", red="<<red<<'\n';
            }
            images.clear(renderer);++packs;
        }
        expect(original_crosshairs==packs,"Not every scripted pack's original hip crosshair was compared");
        for(const auto& pack:{"pack-legacy-kar98-kar98-pak-unpacked-rifle","pack-legacy-mp5k-smg"}){
            const auto manifest=config.asset_root.parent_path()/"client/cosmetics/packs"/pack/"skin.json";
            const auto definition=world::load_skin_variants(manifest);
            for(const auto& choice:definition.options.front().choices){
                world::ScriptedWeapon skin;std::string error;
                expect(skin.load(manifest,error,{{"sight",choice.id}}),error);
                frontend::ScriptedWeaponImages images;expect(images.load(renderer,skin,error),error);
                world::SkinInput state;state.dt=1.F/60.F;state.aim=1;state.screen_width=1024;state.screen_height=768;
                for(int frame=0;frame<90;++frame)expect(skin.update(state,error),error);
                const auto pixels=capture(images,skin.frame(),true,true,true);
                const auto outer=(30U*1024U+30U)*4U,inner=(430U*1024U+560U)*4U;
                expect((pixels[outer]<5)==choice.scope,"Scope aperture must cover only scoped variants");
                expect(pixels[inner]>20,"The lens must leave the world visible");
                if(choice.scope){
                    expect(images.has_aim_image(skin.frame()),"Selected scope needs its authored reticle");
                    const auto without=capture(images,skin.frame(),true,false,true);
                    expect(pixels==without,"A selected scope must not acquire a duplicate aiming dot");
                }
                const auto file=output/(std::string{pack}+"-"+choice.id+".png");
                expect(stbi_write_png(file.string().c_str(),1024,768,4,pixels.data(),1024*4)!=0,"Could not save selected scope evidence");
                images.clear(renderer);
            }
        }
        static_cast<void>(renderer.release_texture(background->texture));
        bgfx::destroy(readback);bgfx::destroy(framebuffer);bgfx::destroy(colour);
        renderer.shutdown();SDL_DestroyWindow(window);window=nullptr;SDL_Quit();
        std::cout<<packs<<" packs, "<<images_checked<<" resident images and "<<original_crosshairs<<" original crosshairs verified through the live UI renderer\n";
        return 0;
    }catch(const std::exception& error){
        if(window)SDL_DestroyWindow(window);SDL_Quit();std::cerr<<error.what()<<'\n';return 1;
    }
}
