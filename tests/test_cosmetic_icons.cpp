#include "battlespades/frontend/cosmetic_icons.hpp"
#include "battlespades/frontend/class_selection_presentation.hpp"
#include "battlespades/frontend/game_hud.hpp"
#include "battlespades/render/render_views.hpp"
#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <stb_image_write.h>
#include <chrono>
#include <iostream>
#include <thread>

namespace {void expect(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}}
int main(int argc,char** argv){
    using namespace battlespades;
    try{
        expect(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
        auto* window=SDL_CreateWindow("Cosmetic icon render checks",800,600,SDL_WINDOW_HIDDEN);expect(window!=nullptr,SDL_GetError());
        render::BgfxUiRenderer renderer;render::BgfxUiRendererConfig config;
        config.native_window.window=SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr);
        config.asset_root=argc>1?argv[1]:AOS_TEST_ASSET_ROOT;config.shader_root=AOS_SHADER_BIN_ROOT;
        config.drawable_extent={800,600};config.backend=render::GraphicsBackend::direct3d11;config.vertical_sync=false;
        expect(renderer.initialize(config),std::string{renderer.last_error()});
        frontend::CosmeticIconCache cache{config.asset_root};
        const auto* mp5=frontend::find_inventory_cosmetic("community-mp5k-v2");
        const auto* character=frontend::find_inventory_cosmetic("community-169749-winter-warfare-bonus-boris-v3");
        expect(mp5&&character,"Missing icon fixtures");
        frontend::CosmeticIconRequest red;red.item=*mp5;red.variants={{"sight","red-dot"},{"muzzle","standard"}};
        auto scope=red;scope.variants["sight"]="scope-2";
        auto iron=red;iron.variants["sight"]="iron";
        frontend::CosmeticIconRequest head;head.item=*character;head.kind=frontend::CosmeticIconKind::class_head;
        auto green=head;green.blue_team=false;
        auto body=head;body.kind=frontend::CosmeticIconKind::class_body;
        const std::vector requests{red,scope,iron,head,green,body};
        const auto zoom_key=frontend::cosmetic_icon_key(red);auto zoom=red;zoom.variants["zoom"]="sniper";
        expect(frontend::cosmetic_icon_key(zoom)==zoom_key,"Zoom-only changes must reuse the same model icon");
        for(const auto& request:requests)expect(cache.request(request,"stock")=="stock","Pending icons must retain the stock icon");
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{30};
        while(cache.rendered_count()<requests.size()&&std::chrono::steady_clock::now()<deadline){
            cache.pump(renderer);expect(renderer.begin_frame(),std::string{renderer.last_error()});expect(renderer.end_frame(),std::string{renderer.last_error()});
            for(const auto& request:requests)static_cast<void>(cache.request(request,"stock"));
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
        expect(cache.rendered_count()==requests.size(),"On-demand icon worker did not finish");
        std::filesystem::create_directories("tmp/cosmetic-icons");
        std::vector<std::vector<std::uint8_t>> pixels;
        for(std::size_t i=0;i<requests.size();++i){
            const auto asset=cache.request(requests[i],"stock");expect(asset!="stock"&&cache.texture(asset).has_value(),"Rendered icon did not replace stock");
            pixels.push_back(frontend::render_cosmetic_icon(requests[i],config.asset_root));
            std::size_t visible=0;for(std::size_t p=3;p<pixels.back().size();p+=4)if(pixels.back()[p])++visible;
            expect(visible>200,"Equipped skin icon is empty");
            expect(stbi_write_png(("tmp/cosmetic-icons/icon-"+std::to_string(i)+".png").c_str(),256,256,4,pixels.back().data(),256*4)!=0,"Icon PNG failed");
        }
        expect(pixels[0]!=pixels[1]&&pixels[0]!=pixels[2],"Attachment changes must change the weapon thumbnail");
        expect(pixels[3]!=pixels[5],"Class portrait must have a distinct head crop");
        for(int frame=0;frame<30;++frame){for(const auto& request:requests)static_cast<void>(cache.request(request,"stock"));cache.pump(renderer);}
        expect(cache.rendered_count()==requests.size(),"Unchanged icons must not render again");
        auto missing=red;missing.item.id="missing-model";missing.item.scripted_skin.clear();
        expect(cache.request(missing,"stock")=="stock","Unavailable models must use stock icons");
        const auto failure_deadline=std::chrono::steady_clock::now()+std::chrono::seconds{5};
        while(cache.rendered_count()==requests.size()&&std::chrono::steady_clock::now()<failure_deadline){cache.pump(renderer);std::this_thread::sleep_for(std::chrono::milliseconds{5});}
        expect(cache.rendered_count()==requests.size()+1,"Missing model request did not complete");
        for(int n=0;n<20;++n){expect(cache.request(missing,"stock")=="stock","Missing model removed its stock fallback");cache.pump(renderer);}
        expect(cache.rendered_count()==requests.size()+1,"Missing models must not retry every frame");
        frontend::GameHudModel hud;hud.set_player_score(0,true);
        const auto head_asset=cache.request(head,"stock"),weapon_asset=cache.request(scope,"stock");
        hud.set_class_portrait(head_asset,false);hud.set_ammo_state(weapon_asset,30,90,true);hud.set_inventory_state({{weapon_asset,"1"},{"png/ui/weapons/spade.png","2"}},0U,true);// retail hides a strip of <= 1 entries
        const auto draw=frontend::GameHudPresentation{}.build(hud,{{800,600},1000U});
        std::size_t heads=0,weapons=0;
        for(const auto& command:draw.commands())if(const auto* sprite=std::get_if<ui::SpriteDrawCommand>(&command)){
            heads+=sprite->asset_id==head_asset;weapons+=sprite->asset_id==weapon_asset;
        }
        expect(heads==1&&weapons>=2,"Gameplay HUD must draw the skin portrait, toolbar and ammo icons");
        if(argc>2&&std::string_view{argv[2]}=="--all"){
            std::size_t count=0;
            for(const auto& item:frontend::inventory_preview_fixture().items){
                if(!item.enabled||(item.kind!="weapon_model"&&item.kind!="character_skin"))continue;
                frontend::CosmeticIconRequest request;request.item=item;
                if(item.kind=="character_skin"){
                    request.kind=frontend::CosmeticIconKind::class_head;
                    for(const auto& slot:item.slots)if(slot.starts_with("class:")){request.class_id=static_cast<std::uint8_t>(std::stoi(slot.substr(6)));break;}
                }
                const auto image=frontend::render_cosmetic_icon(request,config.asset_root);
                std::size_t coverage=0;for(std::size_t p=3;p<image.size();p+=4)coverage+=image[p]>0;
                if(coverage<=200){
                    const auto mesh=item.kind=="weapon_model"?frontend::inventory_weapon_preview_mesh(item,config.asset_root,true,request.variants):frontend::inventory_preview_mesh(item,config.asset_root,true,request.class_id,nullptr,true);
                    std::cerr<<item.id<<": pixels="<<coverage<<", vertices="<<(mesh?mesh->vertices.size():0)<<", indices="<<(mesh?mesh->indices.size():0)<<'\n';
                }
                expect(coverage>200,"Missing gameplay icon: "+item.id);
                expect(stbi_write_png(("tmp/cosmetic-icons/"+item.id+".png").c_str(),256,256,4,image.data(),256*4)!=0,"Catalogue icon capture failed");
                ++count;
            }
            std::cout<<count<<" equipped weapon and class skin icons rendered\n";
        }
        const auto colour=bgfx::createTexture2D(800,600,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_RT);
        const auto framebuffer=bgfx::createFrameBuffer(1,&colour,false);
        const auto readback=bgfx::createTexture2D(800,600,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK);
        const auto backdrop=renderer.load_texture("png/ui/in_game_menus/select_class/loadout_background.png",render::TextureFilter::linear);
        expect(backdrop.has_value(),"Could not load the original menu tile background");
        std::vector<render::UiTextureInfo> small_icons;
        for(const auto* id:{"community-bren-v2","community-pack-awp-78cfd-rifle-v3"}){
            const auto* item=frontend::find_inventory_cosmetic(id);expect(item!=nullptr,"Missing small-slot fixture");
            frontend::CosmeticIconRequest request;request.item=*item;
            const auto data=frontend::render_cosmetic_icon(request,config.asset_root);
            const auto icon=renderer.create_texture_rgba8(data,{256,256},render::TextureFilter::linear);
            expect(icon.has_value(),"Small-slot texture failed");small_icons.push_back(*icon);
        }
        for(const auto* asset:{"png/ui/weapons/smg.png","png/ui/weapons/spade.png"}){
            const auto icon=renderer.load_texture(asset,render::TextureFilter::linear);
            expect(icon.has_value(),"Original comparison icon failed");small_icons.push_back(*icon);
        }
        expect(renderer.begin_frame(),std::string{renderer.last_error()});
        for(const auto view:{render::backdrop_clear_view_id,render::ui_window_view_id,render::ui_canvas_view_id})bgfx::setViewFrameBuffer(view,framebuffer);
        for(std::size_t i=0;i<requests.size();++i){const auto image=cache.texture(cache.request(requests[i],"stock"));
            expect(renderer.draw({backdrop->texture,{static_cast<float>(16+(i%3)*264),static_cast<float>(20+(i/3)*278),240,240},std::nullopt,std::nullopt,{255,255,255,255},render::UiDrawSpace::window_pixels}),"Menu background draw failed");
            expect(renderer.draw({image->texture,{static_cast<float>(16+(i%3)*264),static_cast<float>(20+(i/3)*278),240,240},std::nullopt,std::nullopt,{255,255,255,255},render::UiDrawSpace::window_pixels}),"Cached texture draw failed");}
        // Match the actual 42-pixel class-menu slots: generated icons use the
        // enlarged 39-pixel inset, while existing stock art keeps its 33-pixel inset.
        std::vector<render::UiTextureInfo> strip{*cache.texture(cache.request(red,"stock"))};
        strip.insert(strip.end(),small_icons.begin(),small_icons.end());
        for(std::size_t i=0;i<strip.size();++i){
            const float x=16.F+static_cast<float>(i)*64.F,inset=i<3?1.5F:4.5F;
            expect(renderer.draw({backdrop->texture,{x,550,42,42},std::nullopt,std::nullopt,{255,255,255,255},render::UiDrawSpace::window_pixels}),"Small slot background failed");
            expect(renderer.draw({strip[i].texture,{x+inset,550+inset,42-2*inset,42-2*inset},std::nullopt,std::nullopt,{255,255,255,255},render::UiDrawSpace::window_pixels}),"Small slot icon failed");
        }
        bgfx::blit(render::ui_canvas_view_id+1U,readback,0,0,colour,0,0,800,600);
        std::vector<std::uint8_t> contact(800*600*4);const auto ready=bgfx::readTexture(readback,contact.data());
        expect(renderer.end_frame(),std::string{renderer.last_error()});while(bgfx::frame()<ready){}
        expect(stbi_write_png("tmp/cosmetic-icons/rendered-grid.png",800,600,4,contact.data(),800*4)!=0,"GPU capture failed");
        static_cast<void>(renderer.release_texture(backdrop->texture));
        for(const auto& icon:small_icons)static_cast<void>(renderer.release_texture(icon.texture));
        bgfx::destroy(readback);bgfx::destroy(framebuffer);bgfx::destroy(colour);
        cache.clear(renderer);renderer.shutdown();SDL_DestroyWindow(window);SDL_Quit();
        std::cout<<"Cosmetic icons: variants, class portraits, team keys, on-demand reuse and HUD routing passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
