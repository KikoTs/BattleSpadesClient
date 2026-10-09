#include "battlespades/frontend/inventory_view.hpp"
#include "battlespades/render/render_views.hpp"
#include "battlespades/network/workshop_preview.hpp"
#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <stb_image_write.h>
#include <iostream>
#include <fstream>

namespace { void expect(bool ok,const std::string& message) { if (!ok) throw std::runtime_error(message); } }
int main(int argc,char** argv) {
    using namespace battlespades;
    try {
        expect(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
        auto* window=SDL_CreateWindow("Workshop render checks",800,600,SDL_WINDOW_HIDDEN);
        expect(window!=nullptr,SDL_GetError());
        render::BgfxUiRenderer renderer;
        render::BgfxUiRendererConfig config;
        config.native_window.window=SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr);
        config.asset_root=AOS_TEST_ASSET_ROOT; config.shader_root=AOS_SHADER_BIN_ROOT;
        config.drawable_extent={800,600}; config.backend=render::GraphicsBackend::direct3d11; config.vertical_sync=false;
        expect(renderer.initialize(config),std::string{renderer.last_error()});
        const auto backdrop=renderer.load_texture("png/ui/ugc_splash.png",render::TextureFilter::linear);
        expect(backdrop.has_value(),"Native menu backdrop missing");
        const auto colour=bgfx::createTexture2D(800,600,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_RT);
        const auto framebuffer=bgfx::createFrameBuffer(1,&colour,false);
        const auto readback=bgfx::createTexture2D(800,600,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK);
        std::filesystem::create_directories("tmp/workshop-ui");
        // Exercise JPEG decoding and aspect ratio with real pixels, independently
        // of network availability or any previously downloaded map previews.
        std::vector<std::uint8_t> portrait(90U*160U*3U,0);
        for (std::size_t i=0;i<portrait.size();i+=3U) portrait[i]=255;
        std::vector<std::uint8_t> jpeg;
        const auto write=[](void* context,void* data,int size) {
            auto& output=*static_cast<std::vector<std::uint8_t>*>(context);
            const auto* first=static_cast<const std::uint8_t*>(data);
            output.insert(output.end(),first,first+size);
        };
        expect(stbi_write_jpg_to_func(write,&jpeg,90,160,3,portrait.data(),90)!=0,"JPEG fixture failed");
        const auto normalized=network::normalize_workshop_preview(jpeg);
        expect(!normalized.empty(),"JPEG Workshop preview rejected");
        const auto fixture=std::filesystem::absolute("tmp/workshop-ui/portrait.png");
        { std::ofstream output{fixture,std::ios::binary}; output.write(reinterpret_cast<const char*>(normalized.data()),static_cast<std::streamsize>(normalized.size())); }
        const auto decoded=render::decode_png_rgba8(fixture);
        expect(decoded.texture && decoded.texture->extent.width==640U && decoded.texture->extent.height==360U,"Preview dimensions changed");
        expect(decoded.texture->rgba8[0]==25U && decoded.texture->rgba8[(180U*640U+320U)*4U]>250U &&
            decoded.texture->rgba8[(180U*640U+270U)*4U]==25U,"Portrait preview was stretched");
        expect(network::normalize_workshop_preview(std::vector<std::uint8_t>(2U*1024U*1024U+1U)).empty(),"Oversized preview accepted");
        expect(network::normalize_workshop_preview(std::array<std::uint8_t,3>{1,2,3}).empty(),"Invalid preview accepted");
        {
            frontend::InventoryView view{renderer,config.asset_root};
            expect(view.ready(),std::string{view.error()});
            frontend::WorkshopMenuModel model;
            model.signed_in=true; model.more=true;
            const std::array<const char*,6> titles{"Paintball","Battlefield 4 - Siege of Shanghai","Post Apocalyptia","de_dust2","Urban-1","A very long community map title that needs several lines"};
            for (std::size_t i=0;i<12U;++i) {
                network::PublicWorkshopItem item;
                item.reference={"steam",std::to_string(185279489U+i)};
                item.title=titles[i%6U]; item.description="A community map for Ace of Spades. Supports CTF and Team Deathmatch.\nDownloaded maps appear in Create Match > Subscribed Maps.";
                item.files.push_back({"container","https://cdn.steamusercontent.com/map",{},100});
                item.preview_file=fixture.string();
                if (i!=5U) {
                    const auto preview=std::filesystem::absolute("tmp/workshop-live/previews");
                    if (std::filesystem::is_directory(preview)) for (const auto& file:std::filesystem::directory_iterator(preview)) {
                        if (file.path().extension()==".png") { item.preview_file=file.path().string(); break; }
                    }
                }
                model.items.push_back(std::move(item));
            }
            model.subscriptions.insert(model.items[0].reference.key()); model.installed=model.subscriptions;
            for (auto& item:model.items) item.gallery_loaded=true;
            const auto capture=[&](const char* name) {
                for (int i=0;i<4;++i) {
                    view.prepare_workshop(model);
                    expect(view.error().empty(),std::string{view.error()});
                    expect(renderer.begin_frame(),std::string{renderer.last_error()});
                    for (const auto id:{render::backdrop_clear_view_id,render::ui_window_view_id,render::ui_canvas_view_id}) bgfx::setViewFrameBuffer(id,framebuffer);
                    render::UiSprite background; background.texture=backdrop->texture; background.destination={0,0,800,600};
                    background.space=render::UiDrawSpace::window_pixels;
                    expect(renderer.draw(background),"Menu background failed");
                    expect(view.draw(),std::string{renderer.last_error()});
                    expect(renderer.end_frame(),std::string{renderer.last_error()});
                }
                bgfx::blit(render::ui_canvas_view_id+1U,readback,0,0,colour,0,0,800,600);
                std::vector<std::uint8_t> pixels(800U*600U*4U);
                const auto frame=bgfx::readTexture(readback,pixels.data()); while (bgfx::frame()<frame) {}
                expect(stbi_write_png((std::string{"tmp/workshop-ui/"}+name+".png").c_str(),800,600,4,pixels.data(),800*4)!=0,"Screenshot failed");
            };
            capture("browse");
            auto* context=Rml::GetContext("inventory");
            auto* doc=context->GetDocument(1);
            auto* inventory_doc=context->GetDocument(0);
            expect(doc && doc->IsVisible() && !inventory_doc->IsVisible(),"Inventory and Workshop overlap");
            Rml::ElementList cards;
            doc->QuerySelectorAll(cards,".map");
            expect(cards.size()==12 && cards[1]->GetAbsoluteOffset().x>cards[0]->GetAbsoluteOffset().x,"Map grid must show two columns");
            expect(cards[0]->QuerySelector("img.map-image")!=nullptr,"Map cards have no thumbnails");
            const auto extent=cards[0]->QuerySelector("img.map-image")->GetBox().GetSize();
            expect(std::abs(extent.x/extent.y-16.0F/9.0F)<0.02F,"Map thumbnail aspect ratio changed");
            auto* first_card=cards.front();
            model.message="Loading one thumbnail..."; ++model.revision; view.prepare_workshop(model);
            expect(doc->QuerySelector(".map")==first_card,"Status update rebuilt the map grid");
            auto* sort=dynamic_cast<Rml::ElementFormControlSelect*>(doc->GetElementById("sort"));
            expect(sort!=nullptr,"Native sort dropdown is missing");
            sort->SetValue("mostrecent"); view.prepare_workshop(model);
            expect(model.filters.sort=="mostrecent" && view.take_workshop_action()==frontend::WorkshopAction::browse,"Sort change did not request a catalog search");
            model.items[0].preview_urls={"first","second"};
            model.items[0].preview_files={model.items[0].preview_file,fixture.string()}; ++model.revision; view.prepare_workshop(model);
            doc->QuerySelector("[action='image-next']")->Click(); view.prepare_workshop(model);
            expect(model.image==1 && doc->QuerySelector(".preview")->GetAttribute<Rml::String>("src","")==fixture.generic_string(),"Gallery next image failed");
            doc->QuerySelector("[action='show-details']")->Click(); capture("details");
            expect(model.details && doc->QuerySelector(".description")!=nullptr,"Inline map details failed");
            doc->QuerySelector("[action='show-images']")->Click(); view.prepare_workshop(model);
            expect(!model.details,"Could not return to screenshots");
            auto* search=dynamic_cast<Rml::ElementFormControlInput*>(doc->GetElementById("search"));
            expect(search!=nullptr,"Search input missing");
            search->Focus(); view.text_input("185279489"); view.prepare_workshop(model);
            expect(model.query=="185279489","Search text was not routed");
            view.input({ui::InputAction::activate,ui::InputPhase::pressed});
            expect(view.take_workshop_action()==frontend::WorkshopAction::browse,"Enter did not search");
            view.edit_key(true); view.prepare_workshop(model);
            expect(model.query=="18527948","Backspace failed");
            view.select_search_text(); view.text_input("https://steamcommunity.com/sharedfiles/filedetails/?id=185279489");
            expect(model.query=="https://steamcommunity.com/sharedfiles/filedetails/?id=185279489","Select-all and pasted search text failed");
            model.busy=true; model.message="Downloading Paintball..."; ++model.revision; capture("downloading");
            model.busy=false; model.selected=5; ++model.revision; capture("long-title");
            model.items[5].preview_file.clear(); ++model.revision; capture("missing-preview");
            expect(doc->QuerySelector(".details .no-preview")!=nullptr,"Missing preview has no fallback");
            model.items[5].title="<button action='remove'>Untrusted title</button>"; ++model.revision; view.prepare_workshop(model);
            expect(doc->QuerySelector(".detail-title button")==nullptr,"Metadata became executable markup");
            frontend::InventoryMenuModel inventory;
            view.prepare(inventory,true);
            expect(inventory_doc->IsVisible() && !doc->IsVisible(),"Inventory did not return");
            view.prepare_workshop(model);
            view.input({ui::InputAction::cancel,ui::InputPhase::pressed});
            expect(view.take_navigation()==0,"Escape did not close Workshop");
            model.items.clear(); model.selected=0; model.message="No maps found. Try another search."; ++model.revision; capture("empty");
            if (argc==2 && std::string_view{argv[1]}=="--live") {
                model.items=network::PublicWorkshop::browse_steam("",0).items;
                for (auto& item:model.items) network::PublicWorkshop::cache_preview(item,"tmp/workshop-live/previews",{});
                model.selected=0; model.details=false; model.image=0;
                model.query.clear(); model.filters={}; search->SetValue("");
                for (std::size_t i=0;i<model.items.size();++i) if (model.items[i].reference.id=="197458861") model.selected=i;
                auto& selected=model.items[model.selected]; selected.preview_urls={selected.preview_url}; selected.preview_files={selected.preview_file};
                for (const auto& url:network::PublicWorkshop::steam_gallery(selected.reference.id)) {
                    auto screenshot=selected; screenshot.preview_url=url; screenshot.preview_file.clear();
                    network::PublicWorkshop::cache_preview(screenshot,"tmp/workshop-live/gallery",{});
                    selected.preview_urls.push_back(url); selected.preview_files.push_back(screenshot.preview_file);
                }
                selected.gallery_loaded=true; model.message="Public Steam Workshop · Live catalog";
                ++model.revision; capture("live-browser");
                model.details=true; ++model.revision; capture("live-details");
            }
        }
        bgfx::destroy(readback); bgfx::destroy(framebuffer); bgfx::destroy(colour);
        static_cast<void>(renderer.release_texture(backdrop->texture));
        renderer.shutdown(); SDL_DestroyWindow(window); SDL_Quit();
        std::cout<<"Workshop layout, text input, escaping and shared-context navigation passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
