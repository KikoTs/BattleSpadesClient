#include "battlespades/frontend/inventory_view.hpp"
#include "battlespades/frontend/inventory_session.hpp"
#include "battlespades/render/render_views.hpp"
#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <stb_image_write.h>
#include <filesystem>
#include <iostream>
#include <thread>

namespace { void expect(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);} }
int main(int argc,char** argv){
    using namespace battlespades;
    std::string stage="initialize renderer";
    try {
        expect(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
        auto* window=SDL_CreateWindow("Inventory render checks",800,600,SDL_WINDOW_HIDDEN);
        expect(window!=nullptr,SDL_GetError());
        render::BgfxUiRenderer renderer;
        render::BgfxUiRendererConfig config;
        config.native_window.window=SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr);
        config.asset_root=AOS_TEST_ASSET_ROOT;config.shader_root=AOS_SHADER_BIN_ROOT;
        if(argc>1)config.asset_root=argv[1];
        config.drawable_extent={800,600};config.backend=render::GraphicsBackend::direct3d11;config.vertical_sync=false;
        expect(renderer.initialize(config),std::string{renderer.last_error()});
        const auto colour=bgfx::createTexture2D(800,600,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_RT);
        const auto framebuffer=bgfx::createFrameBuffer(1,&colour,false);
        const auto readback=bgfx::createTexture2D(800,600,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK);
        std::filesystem::create_directories("tmp/inventory-ui");
        {
            stage="create inventory view";
            world::SkinVariantPreferences variants{"tmp/inventory-ui/skin-variants.json"};
            frontend::InventoryView view{renderer,config.asset_root,&variants};
            expect(view.ready(),std::string{view.error()});
            frontend::InventoryMenuModel model;model.complete(frontend::inventory_preview_fixture());
            double longest_prepare=0;
            std::string slowest_prepare;
            auto capture=[&](const std::string& name){
                stage="capture "+name;
                const auto revision=variants.revision();
                for(int i=0;i<1200 && (i<120 || !view.previews_ready());++i){
                    stage="prepare "+name+" frame "+std::to_string(i);
                    const auto start=std::chrono::steady_clock::now();view.prepare(model,true);
                    const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
                    if(elapsed>longest_prepare){longest_prepare=elapsed;slowest_prepare=stage;}
                    expect(view.error().empty(),std::string{view.error()});
                    stage="begin "+name+" frame "+std::to_string(i);
                    const bool began=renderer.begin_frame();expect(began,std::string{renderer.last_error()});
                    for(const auto v:{render::backdrop_clear_view_id,render::ui_window_view_id,render::ui_canvas_view_id})bgfx::setViewFrameBuffer(v,framebuffer);
                    stage="draw "+name+" frame "+std::to_string(i);
                    const bool drawn=view.draw();expect(drawn,std::string{renderer.last_error()});
                    stage="end "+name+" frame "+std::to_string(i);
                    const bool ended=renderer.end_frame();expect(ended,std::string{renderer.last_error()});
                    std::this_thread::sleep_for(std::chrono::milliseconds{5});
                }
                expect(variants.revision()==revision,"Rendering selectors changed saved preferences");
                expect(view.previews_ready(),"Model preview worker did not finish: "+name);
                constexpr std::uint16_t copy_view=render::ui_canvas_view_id+1U;
                bgfx::blit(copy_view,readback,0,0,colour,0,0,800,600);
                std::vector<std::uint8_t> pixels(800*600*4);
                const auto ready=bgfx::readTexture(readback,pixels.data());while(bgfx::frame()<ready){}
                expect(stbi_write_png(("tmp/inventory-ui/"+name+".png").c_str(),800,600,4,pixels.data(),800*4)!=0,"Screenshot failed");
                return pixels;
            };
            const auto click_element=[&](Rml::Element* element){
                expect(element!=nullptr,"Missing menu control");
                const auto offset=element->GetAbsoluteOffset(Rml::BoxArea::Border);
                const auto size=element->GetBox().GetSize(Rml::BoxArea::Border);
                view.pointer_move({static_cast<std::int32_t>(offset.x+size.x*.5F),static_cast<std::int32_t>(offset.y+size.y*.5F)});
                view.pointer_button(true);view.pointer_button(false);
            };
            if(!(argc>2&&std::string{argv[2]}=="--variants-only")){
            const auto all=model.filtered_items();
            for(std::size_t page=0;page*6<all.size();++page){
                model.page=page;model.selected=page*6;
                const auto pixels=capture("page-"+std::to_string(page));
                for(std::size_t tile=0;tile<6 && page*6+tile<all.size();++tile){
                    const auto x0=52+static_cast<int>(tile%3)*124,y0=246+static_cast<int>(tile/3)*126;
                    std::size_t details=0;
                    const auto background=static_cast<std::size_t>((y0*800+x0)*4);
                    for(int y=y0;y<y0+46;++y)for(int x=x0;x<x0+101;++x){
                        const auto p=static_cast<std::size_t>((y*800+x)*4);
                        if(std::abs(pixels[p]-pixels[background])+std::abs(pixels[p+1]-pixels[background+1])+std::abs(pixels[p+2]-pixels[background+2])>24)++details;
                    }
                    expect(details>30,"Empty rendered icon: "+model.data.items[all[page*6+tile]].id);
                }
            }
            model.page=model.selected=0;capture("weapons");
            model.kind_filter=2;model.selected=model.page=0;capture("characters");
            auto* class_select=dynamic_cast<Rml::ElementFormControlSelect*>(Rml::GetContext("inventory")->GetDocument(0)->QuerySelector("select[action='choose-class']"));
            click_element(class_select);
            capture("class-picker");
            expect(class_select->IsSelectBoxVisible(),"The class selector did not open");
            click_element(class_select->GetOption(1));
            expect(model.slot_index==1U,"The named class picker did not choose Scout");
            capture("class-selected");
            model.kind_filter=4;model.selected=model.page=0;capture("entities");
            model.data.crates.push_back({"supply-preview","12","","supply-v4"});
            model.section=frontend::InventorySection::crates;model.selected=model.page=0;capture("crates");
            expect(longest_prepare<200,"Selecting a model stalls the menu for over 200 ms");
            std::cout<<all.size()<<" rendered skin icons, class picker and four crate families; longest menu preparation "<<longest_prepare<<" ms\n";
            }
            const auto select_skin=[&](const std::string& name){
                model.section=frontend::InventorySection::collection;model.kind_filter=model.rarity_filter=0;model.owned_only=false;
                const auto items=model.filtered_items();bool found=false;
                for(std::size_t i=0;i<items.size();++i)if(model.data.items[items[i]].name==name){model.selected=i;model.page=i/6;found=true;break;}
                expect(found,"Missing skin "+name);capture("variants-"+name);
            };
            const auto choose=[&](const std::string& option,const std::string& value){
                const auto revision=variants.revision();
                auto* document=Rml::GetContext("inventory")->GetDocument(0);
                document->GetElementById("inventory-inspector")->SetScrollTop(10000.F);
                view.prepare(model,true);
                auto* select=dynamic_cast<Rml::ElementFormControlSelect*>(document->GetElementById("variant-"+option));
                expect(select!=nullptr,"Missing variant selector "+option);
                click_element(select);view.prepare(model,true);
                expect(select->IsSelectBoxVisible(),"Variant dropdown did not open");
                Rml::Element* choice{};
                for(int i=0;i<select->GetNumOptions();++i)
                    if(select->GetOption(i)->GetAttribute<Rml::String>("value","")==value)choice=select->GetOption(i);
                click_element(choice);
                view.prepare(model,true);
                expect(variants.selection(model.selected_item()->id).at(option)==value,"Variant event did not persist the selected choice");
                expect(variants.revision()==revision+1,"One choice must save exactly once");
                document->GetElementById("inventory-inspector")->SetScrollTop(10000.F);
            };
            select_skin("Kar98");
            expect(Rml::GetContext("inventory")->GetDocument(0)->GetElementById("hero")!=nullptr,
                   "Initial item preview is absent");
            const auto selected_before_empty=model.selected;
            model.selected=model.filtered_items().size();
            view.prepare(model,true);
            model.selected=selected_before_empty;
            capture("preview-return-from-empty");
            expect(Rml::GetContext("inventory")->GetDocument(0)->GetElementById("hero")!=nullptr,
                   "Returning from an empty selection permanently lost the model preview");
            choose("sight","iron");capture("variants-kar98-iron");
            choose("sight","scope-duplex");capture("variants-kar98-scope");
            expect(Rml::GetContext("inventory")->GetDocument(0)->GetElementById("variant-zoom")==nullptr,
                   "Manual aim zoom is still exposed in the inventory");
            select_skin("MP5K");choose("muzzle","standard");choose("sight","scope-2");capture("variants-mp5k");
            Rml::GetContext("inventory")->GetDocument(0)->GetElementById("inventory-inspector")->SetScrollTop(0.F);
            capture("variants-mp5k-scope-model");
            world::SkinVariantPreferences restored{"tmp/inventory-ui/skin-variants.json"};std::string error;
            expect(restored.load(error),error);
            expect(restored.selection(model.selected_item()->id).at("muzzle")=="standard","Menu choice did not survive preference reload");
            expect(longest_prepare<200,"Variant selection blocks the menu: "+std::to_string(longest_prepare)+" ms at "+slowest_prepare);
            std::cout<<"Variant selectors, live preview and persistence verified; longest prepare "<<longest_prepare<<" ms\n";
        }
        bgfx::destroy(readback);bgfx::destroy(framebuffer);bgfx::destroy(colour);
        renderer.shutdown();SDL_DestroyWindow(window);SDL_Quit();
        std::cout<<"Inventory render captures completed\n";return 0;
    }catch(const std::exception& e){std::cerr<<stage<<": "<<e.what()<<'\n';return 1;}
}
