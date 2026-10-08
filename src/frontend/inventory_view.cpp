#include "battlespades/frontend/inventory_view.hpp"
#include "battlespades/frontend/inventory_session.hpp"
#include "battlespades/world/cosmetic_preview.hpp"
#include "battlespades/world/class_catalog.hpp"
#include "battlespades/world/scripted_weapon.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <fstream>
#include <future>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <iostream>
#include <map>
#include <set>
#include <random>
#include <sstream>
#include <utility>
#include <limits>

namespace battlespades::frontend {
namespace {
using Clock = std::chrono::steady_clock;
std::string escape(std::string_view value) {
    std::string result;
    for (const auto c : value) {
        if (c == '&') result += "&amp;";
        else if (c == '<') result += "&lt;";
        else if (c == '>') result += "&gt;";
        else if (c == '"') result += "&quot;";
        else result += c;
    }
    return result;
}
std::string weapon_name(int tool) {
    switch (tool) {
    case 6: return "Rifle"; case 7: return "SMG"; case 8: return "Minigun";
    case 9: return "Shotgun"; case 10: return "Double-barrel shotgun";
    case 18: return "Sniper rifle"; case 19: return "Semi-auto sniper";
    case 35: return "Tommy gun"; case 37: return "Classic shotgun";
    case 38: return "Classic SMG"; case 60: return "Assault rifle";
    case 61: return "Medic LMG";
    default: return "Equipment " + std::to_string(tool);
    }
}
int slot_tool(std::string_view slot) {
    if (!slot.starts_with("weapon:")) return -1;
    try { return std::stoi(std::string{slot.substr(7)}); } catch (...) { return -1; }
}
std::string slot_name(std::string_view slot) {
    for (const auto& definition : world::class_catalog())
        if (slot.starts_with("class:"+std::to_string(definition.class_id)+":"))
            return std::string{definition.display_name};
    const auto tool=slot_tool(slot);
    return tool>=0?weapon_name(tool):slot=="tombstone"?"Death model":slot.starts_with("entity:")?"World object":"Appearance";
}
std::string replacements(const InventoryCosmetic& item) {
    std::string result;
    std::vector<int> tools;
    for (const auto& slot : item.slots) {
        const auto tool = slot_tool(slot);
        if (tool < 0 || std::ranges::find(tools, tool) != tools.end()) continue;
        tools.push_back(tool);
        if (!result.empty()) result += " / ";
        result += weapon_name(tool);
    }
    return result.empty() ? (item.kind == "hat" ? "Headwear" : item.kind == "character_skin" ?
        "Character appearance" : item.kind == "tombstone" ? "Tombstone" : item.kind=="prop_model" ? "World object" : "Profile badge") : result;
}
std::string classes(const InventoryCosmetic& item) {
    std::string result;
    for (const auto& definition : world::class_catalog()) {
        bool matches{};
        for (const auto& slot : item.slots) {
            const auto tool = slot_tool(slot);
            for (const auto group : definition.item_groups)
                if (tool >= 0 && std::ranges::find(group, static_cast<std::uint16_t>(tool)) != group.end())
                    matches = true;
            if (slot.starts_with("class:" + std::to_string(definition.class_id) + ":")) matches = true;
        }
        if (matches) {
            if (!result.empty()) result += " / ";
            result += definition.display_name;
        }
    }
    return result.empty() ? "All classes" : result;
}
std::string crate_title(std::string_view version) {
    return version.starts_with("weapons-") ? "WEAPON CRATE" : version.starts_with("characters-") ?
        "CHARACTER CRATE" : version.starts_with("cosmetics-") ? "COSMETIC CRATE" : "SUPPLY CRATE";
}
float preview_yaw(const InventoryCosmetic& item) {
    return item.kind=="character_skin" || item.kind=="hat" ? 3.5F : 1.15F;
}
std::vector<std::uint8_t> cached_thumbnail(const InventoryCosmetic& item,const std::filesystem::path& root) {
    std::string key="model-icon-v4-assembled:"+item.sha256;
    for(const auto& [role,part]:item.character_parts)key+=':'+role+':'+part.sha256;
    if(!item.scripted_skin.empty()) {
        std::error_code ec;
        const auto manifest=root.parent_path()/item.scripted_skin;
        const auto stamp=std::filesystem::last_write_time(manifest,ec);
        key+=':'+item.scripted_skin+':'+(ec?"missing":std::to_string(static_cast<long long>(stamp.time_since_epoch().count())));
    }
    const auto path=network::default_revival_state_path().parent_path()/"model-icons"/(item.id+".rgba");
    const auto size=world::cosmetic_preview_width*world::cosmetic_preview_height*4U;
    std::error_code error;
    if(std::filesystem::file_size(path,error)==key.size()+1U+size&&!error) {
        std::ifstream input{path,std::ios::binary};std::string stored;std::getline(input,stored);
        if(stored==key){std::vector<std::uint8_t> pixels(size);input.read(reinterpret_cast<char*>(pixels.data()),size);if(input)return pixels;}
    }
    const auto angle=item.kind=="character_skin"||item.kind=="hat"?.35:1.15;
    auto pixels=build_inventory_preview(item,root,false,angle,1.0);
    if(pixels.size()==size) {
        std::filesystem::create_directories(path.parent_path(),error);
        auto temporary=path;temporary+=".tmp";
        std::ofstream output{temporary,std::ios::binary|std::ios::trunc};output<<key<<'\n';
        output.write(reinterpret_cast<const char*>(pixels.data()),static_cast<std::streamsize>(pixels.size()));output.close();
        if(output){std::filesystem::remove(path,error);std::filesystem::rename(temporary,path,error);}
    }
    return pixels;
}
std::string button(std::string_view action, std::string_view title, bool enabled = true,
                   std::string_view css = {}) {
    return "<button action=\"" + escape(action) + "\" class=\"" + std::string{css} + "\"" +
        (enabled ? "" : " disabled") + ">" + escape(title) + "</button>";
}
std::string card(const InventoryCosmetic& item, std::string_view action, bool selected = false) {
    return "<button action=\"" + escape(action) + "\" class=\"item " + escape(item.rarity) + " " + escape(item.kind) +
        (selected ? " selected" : "") + "\"><div class=\"card-preview\"><img src=\"inventory:thumb/" + escape(item.id) +
        "\"/></div><div class=\"item-name\">" + escape(item.name) + "</div><div class=\"item-parent\">" +
        escape(replacements(item)) + "</div><div class=\"item-state\">" +
        (item.equipped ? "EQUIPPED" : item.owned ? "OWNED" : "UNOWNED") + "</div></button>";
}
}

struct InventoryView::Impl final : Rml::RenderInterface, Rml::SystemInterface, Rml::EventListener {
    struct Geometry { std::shared_ptr<render::UiGeometryData> data; };
    struct Texture { render::UiTextureInfo info; bool owned{true}; };
    render::BgfxUiRenderer& renderer;
    std::filesystem::path root;
    Rml::Context* context{};
    Rml::ElementDocument* document{};
    InventoryMenuModel* model{};
    InventoryAction action{InventoryAction::none};
    world::SkinVariantPreferences* variant_preferences{};
    std::map<std::string,world::SkinVariantDefinition> variant_definitions;
    std::map<std::string,world::SkinVariantSelection> variant_selections;
    std::uint64_t variant_revision{};
    std::string variant_message;
    int navigation{-1}, sound{};
    std::string failure, content_key, hero_key, receipt_id, opening_version, pool_version{"weapons-v4"};
    std::string viewport_key;
    std::vector<render::UiGeometry> draws;
    std::vector<render::UiTexture> retired_textures;
    std::optional<render::UiRect> scissor;
    std::array<float,16U> transform{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    bool clipping{}, initialized{}, fixture{}, dragging{}, spinning{}, auto_rotate{true}, preparing{};
    ui::Point pointer{};
    float yaw{1.15F}, pitch{-0.22F}, zoom{1.0F}, pixel_scale{1.0F};
    Clock::time_point start{Clock::now()}, previous{start}, spin_start{start};
    double spin_position{};
    int tick_index{-1};
    render::UiTextureInfo white{}, hero{};
    std::map<std::string, render::UiTextureInfo> thumbnail_textures;
    std::set<std::string, std::less<>> thumbnail_requested;
    std::set<std::string, std::less<>> thumbnail_failed;
    std::size_t thumbnails_pending{};
    struct ThumbnailQueue {
        std::mutex mutex;
        std::condition_variable_any changed;
        std::deque<std::string> pending;
        std::map<std::string,std::vector<std::uint8_t>> completed;
    };
    std::shared_ptr<ThumbnailQueue> thumbnail_queue{std::make_shared<ThumbnailQueue>()};
    std::jthread thumbnail_worker;
    std::future<std::optional<world::ChunkMesh>> hero_worker;
    std::string hero_pending;
    std::vector<const InventoryCosmetic*> reel;

    Impl(render::BgfxUiRenderer& value, std::filesystem::path path,world::SkinVariantPreferences* preferences)
        : renderer(value), root(std::filesystem::absolute(path).lexically_normal()),variant_preferences(preferences) {
        const std::array<std::uint8_t,4U> pixel{255,255,255,255};
        const auto texture = renderer.create_texture_rgba8(pixel, {1,1}, render::TextureFilter::nearest);
        if (!texture) { failure = "Inventory renderer could not create its texture."; return; }
        white = *texture;
        thumbnail_worker=std::jthread([assets=root,queue=thumbnail_queue](std::stop_token stop) {
            while (!stop.stop_requested()) {
                std::string id;
                {
                    std::unique_lock lock{queue->mutex};
                    if (!queue->changed.wait(lock,stop,[&]{return !queue->pending.empty();})) return;
                    id=std::move(queue->pending.front()); queue->pending.pop_front();
                }
                std::vector<std::uint8_t> pixels;
                if (const auto* item=find_inventory_cosmetic(id)) {
                    try {
                        // The software thumbnail camera faces +Z; the GPU viewport faces -Z.
                        pixels=cached_thumbnail(*item,assets);
                    }
                    catch (const std::exception& error) { std::cerr<<"Inventory thumbnail "<<id<<": "<<error.what()<<'\n'; }
                }
                else if(id.starts_with("crate:")) {
                    std::string error;
                    const auto model=world::Kv6Model::load_file(assets.parent_path()/"client/ui/models"/(id.substr(6)+"-crate.kv6"),&error);
                    if(model)pixels=world::cosmetic_preview(*model,std::nullopt,false,0.65,1.0);
                }
                std::lock_guard lock{queue->mutex};
                queue->completed.emplace(std::move(id),std::move(pixels));
            }
        });
        Rml::SetRenderInterface(this);
        Rml::SetSystemInterface(this);
        initialized = Rml::Initialise();
        if (!initialized) { failure = "Inventory layout engine could not start."; return; }
        Rml::LoadFontFace((root/"fonts/A750-Sans-Medium.ttf").string(), "GameSans", Rml::Style::FontStyle::Normal,
                          Rml::Style::FontWeight::Normal, true);
        Rml::LoadFontFace((root/"fonts/Edo.ttf").string(), "GameTitle", Rml::Style::FontStyle::Normal);
        Rml::LoadFontFace((root/"fonts/Spades.ttf").string(), "GameHeading", Rml::Style::FontStyle::Normal);
        context = Rml::CreateContext("inventory", {800,600});
        const auto path_to_document = root.parent_path()/"client/ui/inventory.rml";
        document = context ? context->LoadDocument(path_to_document.string()) : nullptr;
        if (!document) { failure = "Inventory layout could not be loaded: " + path_to_document.string(); return; }
        for (const auto* id:{"level","xp","crate-count","xp-fill","section0","section1","section2","section3","content","status","reveal"}) {
            if (!document->GetElementById(id)) { failure="Inventory layout is incomplete."; document=nullptr; return; }
        }
        document->AddEventListener("click", this);
        document->AddEventListener("change", this);
        document->Show();
    }
    ~Impl() override {
        thumbnail_worker.request_stop(); thumbnail_queue->changed.notify_all();
        if (initialized) Rml::Shutdown();
        for(const auto texture:retired_textures)static_cast<void>(renderer.release_texture(texture));
        for (const auto& [id,texture]:thumbnail_textures) static_cast<void>(renderer.release_texture(texture.texture));
        if (white.texture.is_valid()) static_cast<void>(renderer.release_texture(white.texture));
    }
    double GetElapsedTime() override { return std::chrono::duration<double>(Clock::now()-start).count(); }
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        if (type <= Rml::Log::LT_WARNING) std::cerr << "Inventory UI: " << message << '\n';
        if(message.starts_with("Could not load texture:"))failure=message;
        return true;
    }
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                 Rml::Span<const int> indices) override {
        auto geometry = std::make_unique<Geometry>();
        geometry->data = std::make_shared<render::UiGeometryData>();
        auto& out = *geometry->data;
        out.vertices.reserve(vertices.size());
        for (const auto& v : vertices) {
            const auto& c = v.colour;
            const auto color = static_cast<std::uint32_t>(c.red) | (static_cast<std::uint32_t>(c.green)<<8U) |
                (static_cast<std::uint32_t>(c.blue)<<16U) | (static_cast<std::uint32_t>(c.alpha)<<24U);
            out.vertices.push_back({v.position.x,v.position.y,0,v.tex_coord.x,v.tex_coord.y,color});
        }
        for (const auto index : indices) {
            if (index < 0 || static_cast<std::size_t>(index)>=vertices.size()) return 0;
            out.indices.push_back(static_cast<std::uint32_t>(index));
        }
        return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry.release());
    }
    void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation,
                         Rml::TextureHandle texture) override {
        const auto* geometry = reinterpret_cast<Geometry*>(handle);
        const auto image = texture ? reinterpret_cast<Texture*>(texture)->info : white;
        auto scaled=transform;
        for (const auto index:{0U,4U,8U,12U,1U,5U,9U,13U}) scaled[index]/=pixel_scale;
        draws.push_back({geometry->data,image.texture,translation.x,translation.y,scaled,
                        clipping ? scissor : std::nullopt});
    }
    void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override { delete reinterpret_cast<Geometry*>(handle); }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> pixels, Rml::Vector2i size) override {
        if (size.x<=0 || size.y<=0) return 0;
        const auto texture = renderer.create_texture_rgba8({pixels.data(),pixels.size()},
            {static_cast<std::uint32_t>(size.x),static_cast<std::uint32_t>(size.y)}, render::TextureFilter::linear);
        return texture ? reinterpret_cast<Rml::TextureHandle>(new Texture{*texture}) : 0;
    }
    Rml::TextureHandle LoadTexture(Rml::Vector2i& size, const Rml::String& source) override {
        if(const auto marker=source.find("inventory:crate/");marker!=std::string::npos) {
            const auto family=source.substr(marker+16U);
            if(family!="weapons"&&family!="characters"&&family!="cosmetics"&&family!="supply")return 0;
            return thumbnail("crate:"+family,size);
        }
        if (source.find("inventory:hero") != std::string::npos) {
            if (!hero.texture.is_valid()) return 0;
            size = {static_cast<int>(hero.extent.width),static_cast<int>(hero.extent.height)};
            return reinterpret_cast<Rml::TextureHandle>(new Texture{hero,false});
        }
        if (const auto marker = source.find("inventory:thumb/"); marker != std::string::npos) {
            const auto id = source.substr(marker+16U);
            const auto* item = find_inventory_cosmetic(id);
            if (!item) return 0;
            if (item->kind != "profile_badge") {
                return thumbnail(id,size);
            }
            return load_png(root/item->asset,size);
        }
        const auto game = source.find("game:");
        if (game != std::string::npos) return load_png(root/source.substr(game+5U),size);
        return load_png(source,size);
    }
    Rml::TextureHandle thumbnail(const std::string& id,Rml::Vector2i& size) {
                size = {static_cast<int>(world::cosmetic_preview_width),static_cast<int>(world::cosmetic_preview_height)};
                auto texture=thumbnail_textures.find(id);
                if (texture==thumbnail_textures.end()) {
                    const std::vector<std::uint8_t> empty(world::cosmetic_preview_width*world::cosmetic_preview_height*4U,0U);
                    const auto created=renderer.create_texture_rgba8(empty,
                        {world::cosmetic_preview_width,world::cosmetic_preview_height},render::TextureFilter::linear);
                    if (!created) return 0;
                    texture=thumbnail_textures.emplace(id,*created).first;
                    queue_thumbnail(id, true);
                }
                return reinterpret_cast<Rml::TextureHandle>(new Texture{texture->second,false});
    }
    void queue_thumbnail(const std::string& id, bool visible) {
        std::lock_guard lock{thumbnail_queue->mutex};
        if (thumbnail_requested.contains(id)) {
            // Fast navigation must not sit behind the previous page's work.
            const auto queued = std::ranges::find(thumbnail_queue->pending, id);
            if (visible && queued != thumbnail_queue->pending.end()) {
                thumbnail_queue->pending.erase(queued);
                thumbnail_queue->pending.push_front(id);
            }
            return;
        }
        if (thumbnail_requested.size() >= 256U) return;
        thumbnail_requested.insert(id);
        if (visible) thumbnail_queue->pending.push_front(id);
        else thumbnail_queue->pending.push_back(id);
        ++thumbnails_pending;
        thumbnail_queue->changed.notify_one();
    }
    void preload_nearby() {
        if (model->section != InventorySection::collection) return;
        const auto items = model->filtered_items();
        const auto first = std::min(model->page * 6U, items.size());
        // Warm the current and next page, without making the whole catalogue
        // a prerequisite for opening the screen or creating GPU textures.
        for (auto index = first; index < std::min(items.size(), first + 12U); ++index) {
            const auto& item = model->data.items[items[index]];
            if (item.kind != "profile_badge") queue_thumbnail(item.id, index < first + 6U);
        }
    }
    Rml::TextureHandle load_png(const std::filesystem::path& path, Rml::Vector2i& size) {
        auto decoded = render::decode_png_rgba8(path);
        if (!decoded) return 0;
        auto& pixels = decoded.texture->rgba8;
        for (std::size_t i{}; i<pixels.size(); i+=4U) for (std::size_t c{}; c<3U; ++c)
            pixels[i+c] = static_cast<std::uint8_t>(static_cast<unsigned>(pixels[i+c])*pixels[i+3U]/255U);
        size = {static_cast<int>(decoded.texture->extent.width),static_cast<int>(decoded.texture->extent.height)};
        return GenerateTexture({pixels.data(),pixels.size()},size);
    }
    void ReleaseTexture(Rml::TextureHandle texture) override {
        const std::unique_ptr<Texture> image{reinterpret_cast<Texture*>(texture)};
        // A font atlas can grow while RmlUi is still emitting this frame's
        // geometry. Keep its earlier draws alive through end_frame.
        if (image->owned) retired_textures.push_back(image->info.texture);
    }
    void EnableScissorRegion(bool enabled) override { clipping=enabled; }
    void SetScissorRegion(Rml::Rectanglei region) override {
        scissor=render::UiRect{static_cast<float>(region.Left())/pixel_scale,static_cast<float>(region.Top())/pixel_scale,
                              static_cast<float>(region.Width())/pixel_scale,static_cast<float>(region.Height())/pixel_scale};
    }
    void SetTransform(const Rml::Matrix4f* matrix) override {
        if (matrix) std::copy_n(matrix->data(),16,transform.begin());
        else transform={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    }
    const InventoryCosmetic* award() const {
        if (!model || !model->reveal) return nullptr;
        for (const auto& item : model->data.items)
            if ((!model->reveal->item_id.empty() && item.id==model->reveal->item_id) ||
                (model->reveal->item_id.empty() && item.name==model->reveal->name)) return &item;
        return nullptr;
    }
    void ProcessEvent(Rml::Event& event) override {
        // Building select elements emits change events too. Only user changes
        // should write preferences or replace an in-flight model preview.
        if (!model || preparing) return;
        auto* target=event.GetTargetElement();
        while (target && !target->HasAttribute("action")) target=target->GetParentNode();
        if (!target || target->HasAttribute("disabled")) return;
        const auto command=target->GetAttribute<Rml::String>("action","");
        if(command=="choose-variant"){
            if(event.GetType()=="change")if(auto* select=dynamic_cast<Rml::ElementFormControlSelect*>(target)){
                if(const auto* item=model->selected_item()){
                    const auto option=target->GetAttribute<Rml::String>("option","");
                    auto values=variant_selection(*item);
                    for(const auto& definition:variant_definition(*item).options)
                        if(definition.id==option && definition.selected(values).id==select->GetValue())return;
                    values[option]=select->GetValue();
                    save_variant(*item,values);
                }
            }
            return;
        }
        if (command=="choose-class") {
            if(event.GetType()=="change") if(auto* select=dynamic_cast<Rml::ElementFormControlSelect*>(target)) {
                const auto index=static_cast<std::size_t>(std::stoul(select->GetValue()));
                if(const auto* item=model->selected_item();item&&index<item->slots.size())model->slot_index=index;
            }
            return;
        }
        if(event.GetType()!="click")return;
        if (model->reveal && command!="skip" && command!="continue" && command!="rotate" &&
            command!="team" && command!="reset-view") return;
        sound=3;
        if(command=="reset-variants"){if(const auto* item=model->selected_item())save_variant(*item,{});}
        else if (command=="skip") { spinning=false; spin_position=32.0; sound=2; }
        else if (command=="continue") { model->reveal.reset(); spinning=false; content_key.clear(); }
        else if (command=="back") navigation=0;
        else if (command.starts_with("profile:")) navigation=std::stoi(command.substr(8))+1;
        else if (command=="refresh") action=InventoryAction::refresh;
        else if (command=="open") {
            if (const auto* crate=model->selected_crate()) opening_version=crate->catalog_version;
            if (fixture) {
                for (const auto& item:model->data.items) if (std::ranges::find(item.crate_versions,opening_version)!=item.crate_versions.end()) {
                    model->reveal=InventoryOpening{"preview-"+std::to_string(GetElapsedTime()),item.name,item.rarity,"","","",false,item.id};
                    break;
                }
            } else if (!model->busy) action=InventoryAction::open;
        }
        else if (command=="creators") action=InventoryAction::creators;
        else if (command=="equip") action=InventoryAction::equip;
        else if (command=="unequip") action=InventoryAction::unequip;
        else if (command=="equip-weapon") action=InventoryAction::equip_weapon;
        else if (command=="unequip-weapon") action=InventoryAction::unequip_weapon;
        else if (command=="rotate") auto_rotate=!auto_rotate;
        else if (command=="team") { model->blue_team=!model->blue_team; hero_key.clear(); }
        else if (command=="reset-view") {
            const auto* item=model->reveal?award():model->selected_item();
            yaw=item?preview_yaw(*item):1.15F; pitch=-0.22F; zoom=1.0F; auto_rotate=true;
        }
        else if (command=="owned") { model->owned_only=!model->owned_only; model->selected=model->page=0; }
        else if (command.starts_with("pool:")) pool_version=command.substr(5);
        else if (command=="kind") { model->kind_filter=(model->kind_filter+1U)%5U; model->selected=model->page=0; }
        else if (command=="rarity") { model->rarity_filter=(model->rarity_filter+1U)%6U; model->selected=model->page=0; }
        else if (command.starts_with("section:")) {
            model->section=static_cast<InventorySection>(std::stoi(command.substr(8)));
            model->selected=model->page=model->slot_index=0;
        }
        else if (command.starts_with("item:")) { model->selected=static_cast<std::size_t>(std::stoul(command.substr(5))); model->slot_index=0; }
        else if (command.starts_with("crate:")) {
            model->selected=static_cast<std::size_t>(std::stoul(command.substr(6)));
            if (const auto* crate=model->selected_crate()) pool_version=crate->catalog_version;
        }
        else if (command.starts_with("slot:")) model->slot_index=static_cast<std::size_t>(std::stoul(command.substr(5)));
        else if (command.starts_with("inspect:")) {
            const auto id=command.substr(8);
            model->kind_filter=model->rarity_filter=0; model->owned_only=false;
            model->section=InventorySection::collection;
            const auto items=model->filtered_items();
            for (std::size_t i{}; i<items.size(); ++i) if (model->data.items[items[i]].id==id) model->selected=i;
            model->page=model->selected/6U;
        }
        else if (command=="next" || command=="prev") {
            const bool next=command=="next";
            if (next) {
                const auto count=model->section==InventorySection::collection?model->filtered_items().size():
                    model->section==InventorySection::crates?model->data.crates.size():model->data.history.size();
                if ((model->page+1U)*6U<count) { ++model->page; model->selected=model->page*6U; }
                else if (!model->busy && model->section==InventorySection::crates && !model->data.next_crates.empty()) action=InventoryAction::more_crates;
                else if (!model->busy && model->section==InventorySection::history && !model->data.next_history.empty()) action=InventoryAction::more_history;
            } else if (model->page) { --model->page; model->selected=model->page*6U; }
            else if (!model->busy && model->section==InventorySection::crates && model->data.crate_cursors.size()>1U) action=InventoryAction::previous_crates;
            else if (!model->busy && model->section==InventorySection::history && model->data.history_cursors.size()>1U) action=InventoryAction::previous_history;
        }
        content_key.clear();
        if (model->reveal && (command=="rotate" || command=="team" || command=="reset-view")) {
            // Update these labels without rebuilding the button that is handling this event.
            if (auto* control=document->GetElementById("reward-rotate")) control->SetInnerRML(auto_rotate?"PAUSE":"ROTATE");
            if (auto* control=document->GetElementById("reward-team")) control->SetInnerRML(model->blue_team?"BLUE":"GREEN");
        }
    }
    const world::SkinVariantDefinition& variant_definition(const InventoryCosmetic& item){
        auto [entry,inserted]=variant_definitions.try_emplace(item.id);
        if(inserted&&!item.scripted_skin.empty())entry->second=world::load_skin_variants(root.parent_path()/item.scripted_skin);
        return entry->second;
    }
    world::SkinVariantSelection variant_selection(const InventoryCosmetic& item){
        auto [entry,inserted]=variant_selections.try_emplace(item.id);
        if(inserted&&variant_preferences)entry->second=variant_preferences->selection(item.id);
        return entry->second;
    }
    void save_variant(const InventoryCosmetic& item,const world::SkinVariantSelection& values){
        const auto& definition=variant_definition(item);
        if(definition.options.empty())return;
        if(variant_preferences){
            std::string error;
            if(!variant_preferences->set(item.id,definition,values,error)){variant_message=error;content_key.clear();return;}
            variant_selections[item.id]=variant_preferences->selection(item.id);
            variant_message="Saved for this skin";
        }else{variant_selections[item.id]=values;variant_message="Preview selection";}
        ++variant_revision;content_key.clear();sound=3;
    }
    std::string variant_controls(const InventoryCosmetic& item){
        const auto& definition=variant_definition(item);
        if(definition.options.empty())return {};
        const auto values=variant_selection(item);
        std::string html="<div class=\"variant-options\"><div class=\"eyebrow\">VARIANTS &amp; SIGHTS</div>";
        for(const auto& option:definition.options){
            const auto& chosen=option.selected(values);
            html+="<div class=\"variant-row\"><label>"+escape(option.label)+"</label><select id=\"variant-"+escape(option.id)+
                "\" action=\"choose-variant\" option=\""+escape(option.id)+"\">";
            for(const auto& choice:option.choices)html+="<option value=\""+escape(choice.id)+"\""+(choice.id==chosen.id?" selected":"")+">"+escape(choice.label)+"</option>";
            html+="</select></div>";
        }
        if(definition.options.size()==1U)html+="<div class=\"hint\">This pack has one authored sight setup.</div>";
        return html+"<div class=\"variant-footer\">"+button("reset-variants","RESET VARIANTS")+"<span>"+
            escape(variant_message.empty()?"Saved separately for each skin":variant_message)+"</span></div></div>";
    }
    std::string inspector(const InventoryCosmetic& item, bool reward=false) {
        std::string html="<div id=\""+std::string{reward?"reward-inspector":"inventory-inspector"}+"\" class=\"inspector "+escape(item.rarity)+" "+escape(item.kind)+"\"><div class=\"eyebrow\">"+
            escape(item.rarity)+(item.kind=="weapon_model"?" WEAPON MODEL":" COSMETIC")+"</div><h2>"+escape(item.name)+"</h2>";
        if (item.kind=="profile_badge" || hero.texture.is_valid()) {
            html += "<img id=\""+std::string{reward?"reward-hero":"hero"}+"\" class=\"hero\" src=\"" +
                (item.kind=="profile_badge"?"inventory:thumb/"+escape(item.id):"inventory:hero")+"\"/>";
        } else html += std::string{"<div class=\"preview-unavailable\">"} +
            (hero_worker.valid() ? "Loading model preview..." : "Model preview unavailable") + "</div>";
        if (item.kind!="profile_badge") {
            html += "<div class=\"preview-tools\"><button action=\"rotate\" id=\""+std::string{reward?"reward-rotate":"preview-rotate"}+
                "\">"+(auto_rotate?"PAUSE":"ROTATE")+"</button><button action=\"team\" id=\""+
                std::string{reward?"reward-team":"preview-team"}+"\">"+(model->blue_team?"BLUE":"GREEN")+"</button>"+
                button("reset-view","RESET")+"</div>";
            html += "<div class=\"hint\">Drag to rotate · Scroll to zoom</div>";
        }
        html += "<div class=\"spec\"><span>REPLACES</span>"+escape(replacements(item))+"</div>";
        if(item.kind!="character_skin"&&item.kind!="hat")
            html += "<div class=\"spec compatible-classes\"><span>CLASSES</span>"+escape(classes(item))+"</div>";
        html += "<div class=\"author\">By "+escape(item.author)+"</div>";
        if(!reward)html+=variant_controls(item);
        if (!reward) {
            const bool character=item.kind=="character_skin" || item.kind=="hat";
            html += character?"<div class=\"class-picker\"><label>PLAYER CLASS</label><select action=\"choose-class\">":"<div class=\"slots\">";
            for (std::size_t i{}; i<item.slots.size(); ++i) {
                const auto tool=slot_tool(item.slots[i]);
                if (tool>=0 && item.slots[i].ends_with(":world")) continue;
                const auto label=slot_name(item.slots[i]);
                if(character) {
                    const auto* assigned=model->equipped_item(item.slots[i]);
                    html+="<option value=\""+std::to_string(i)+"\""+(i==model->slot_index?" selected":"")+">"+
                        escape(label)+(assigned==&item?" · EQUIPPED":"")+"</option>";
                } else html += button("slot:"+std::to_string(i),label,true,i==model->slot_index?"active":"");
            }
            html += character?"</select></div>":"</div>";
            const auto slot=item.slots.empty()?std::string{}:item.slots[std::min(model->slot_index,item.slots.size()-1U)];
            const bool weapon=slot.starts_with("weapon:");
            const auto other=weapon?slot.substr(0,slot.rfind(':')+1U)+"world":slot;
            const bool equipped=model->equipped_item(slot)==&item && model->equipped_item(other)==&item;
            html += button(weapon?(equipped?"unequip-weapon":"equip-weapon"):(equipped?"unequip":"equip"),
                           equipped?"UNEQUIP "+slot_name(slot):item.owned?(weapon?"EQUIP WEAPON":character?"EQUIP "+slot_name(slot):"EQUIP APPEARANCE"):"EARN FROM CRATES",
                           !fixture && model->online && !model->busy && item.owned && model->data.equip_enabled,"primary");
        }
        return html+"</div>";
    }
    std::string collection() {
        const auto filtered=model->filtered_items();
        constexpr std::array kinds{"ALL TYPES","WEAPON MODELS","CHARACTERS","DEATH MODELS","WORLD OBJECTS"};
        constexpr std::array rarities{"ALL RARITIES","COMMON","UNCOMMON","RARE","EPIC","LEGENDARY"};
        std::string html="<div class=\"browser\"><div class=\"filters\">"+
            button("kind",kinds[model->kind_filter])+button("rarity",rarities[model->rarity_filter])+
            button("owned",model->owned_only?"OWNED":"ALL ITEMS")+"</div><div class=\"grid\">";
        for (std::size_t index=model->page*6U; index<std::min(filtered.size(),(model->page+1U)*6U); ++index)
            html += card(model->data.items[filtered[index]],"item:"+std::to_string(index),index==model->selected);
        if (filtered.empty()) html += "<div class=\"empty\">"+std::string{model->busy?"Loading your collection...":"No items match these filters."}+"</div>";
        html += "</div>"+pagination(std::to_string(filtered.size())+" ITEMS · PAGE "+std::to_string(model->page+1U))+"</div>";
        if (const auto* item=model->selected_item()) html+=inspector(*item);
        return html;
    }
    std::string pagination(std::string_view label={}) const {
        const auto count=model->section==InventorySection::collection?model->filtered_items().size():
            model->section==InventorySection::crates?model->data.crates.size():model->data.history.size();
        const bool remote_previous=!model->busy &&
            ((model->section==InventorySection::crates && model->data.crate_cursors.size()>1U) ||
             (model->section==InventorySection::history && model->data.history_cursors.size()>1U));
        const bool remote_next=!model->busy &&
            ((model->section==InventorySection::crates && !model->data.next_crates.empty()) ||
             (model->section==InventorySection::history && !model->data.next_history.empty()));
        return "<div class=\"pagination\">"+button("prev","PREV",model->page>0U || remote_previous)+
            "<span>"+escape(label)+"</span>"+button("next","NEXT",(model->page+1U)*6U<count || remote_next)+"</div>";
    }
    std::string crates() {
        std::string html="<div class=\"crate-browser\"><h2>YOUR CRATES</h2><p>Earned through play · Free to open</p><div class=\"crate-grid\">";
        for (std::size_t i=model->page*6U; i<std::min(model->data.crates.size(),(model->page+1U)*6U); ++i) {
            const auto& crate=model->data.crates[i];
            const auto family=crate.catalog_version.substr(0,crate.catalog_version.find('-'));
            html+="<button action=\"crate:"+std::to_string(i)+"\" class=\"crate-card"+(i==model->selected?std::string{" selected"}:std::string{})+"\">"+
                "<img src=\"inventory:crate/"+escape(family)+"\"/><div>"+crate_title(crate.catalog_version)+"</div><p>LEVEL "+escape(crate.level)+"</p></button>";
        }
        if (model->data.crates.empty()) html+="<p class=\"empty\">No unopened crates. Keep playing to earn your next level.</p>";
        const auto* crate=model->selected_crate();
        const auto& version=pool_version;
        html+="</div>"+pagination();
        html+=button("open",model->busy?"OPENING...":fixture?"PREVIEW OPENING":
                     "OPEN "+crate_title(crate?crate->catalog_version:version)+" · FREE",
                     crate && !model->busy && (fixture || (model->online && model->data.opening_enabled)),"primary")+"</div>";
        html+="<div class=\"crate-contents\"><div class=\"pool-tabs\">"+
            button("pool:weapons-v4","WEAPONS",true,version=="weapons-v4"?"active":"")+
            button("pool:characters-v4","CHARACTERS",true,version=="characters-v4"?"active":"")+
            button("pool:cosmetics-v4","COSMETICS",true,version=="cosmetics-v4"?"active":"")+
            "</div><h2>"+crate_title(version)+" CONTENTS</h2><p>Inspect every possible reward. Duplicate protection.</p>";
        const auto odds=inventory_effective_odds(model->data,version);
        constexpr std::array names{"Common","Uncommon","Rare","Epic","Legendary"};
        html+="<div class=\"odds\">";
        for (std::size_t i{}; i<5U; ++i) {
            std::ostringstream percent; percent<<std::fixed<<std::setprecision(1)<<odds[i]*100.0;
            html+="<span>"+std::string{names[i]}+" "+percent.str()+"%</span>";
        }
        const auto pool=inventory_crate_pool(model->data,version);
        html+="</div><div class=\"guarantees\">Guaranteed within: Rare+ "+std::to_string(10U-std::min(9U,pool.pity[0]))+
            " / Epic+ "+std::to_string(40U-std::min(39U,pool.pity[1]))+" / Legendary "+
            std::to_string(100U-std::min(99U,pool.pity[2]))+" openings, while qualifying rewards remain unowned.</div><div id=\"reward-pool\" class=\"pool\">";
        for (const auto& item:model->data.items)
            if (item.enabled && std::ranges::find(item.crate_versions,version)!=item.crate_versions.end()) html+=card(item,"inspect:"+item.id);
        return html+"</div></div>";
    }
    std::string history() {
        std::string html="<div class=\"history\"><h2>OPENING HISTORY</h2><p>Your saved rewards and original creators.</p>";
        for (std::size_t i=model->page*6U; i<std::min(model->data.history.size(),(model->page+1U)*6U); ++i) {
            const auto& record=model->data.history[i];
            const InventoryCosmetic* item{};
            for (const auto& candidate:model->data.items) if (candidate.id==record.item_id || candidate.name==record.name) { item=&candidate; break; }
            html+="<div class=\"history-row "+escape(record.rarity)+(item?" "+escape(item->kind):"")+"\">";
            if (item) html+="<img src=\"inventory:thumb/"+escape(item->id)+"\"/>";
            html+="<div><h3>"+escape(record.name)+"</h3><p>"+escape(record.rarity)+" · "+escape(record.date.substr(0,10))+
                (item?" · By "+escape(item->author):"")+"</p></div>";
            if (item) html+=button("inspect:"+item->id,"INSPECT");
            html+="</div>";
        }
        if (model->data.history.empty()) html+="<p class=\"empty\">Your opened crates will appear here.</p>";
        return html+pagination("PAGE "+std::to_string(model->page+1U))+"</div>";
    }
    void begin_reveal() {
        const auto* winner=award();
        if (!winner || !model->reveal) return;
        if (opening_version.empty() && !winner->crate_versions.empty()) opening_version=winner->crate_versions.back();
        receipt_id=model->reveal->id;
        std::vector<const InventoryCosmetic*> pool;
        for (const auto& item:model->data.items)
            if (opening_version.empty() || std::ranges::find(item.crate_versions,opening_version)!=item.crate_versions.end()) pool.push_back(&item);
        if (pool.empty()) pool.push_back(winner);
        std::seed_seq seed(receipt_id.begin(),receipt_id.end());
        std::mt19937 random(seed);
        reel.clear();
        for (std::size_t i{}; i<38U; ++i) reel.push_back(i==32U?winner:pool[random()%pool.size()]);
        spin_start=Clock::now(); spinning=true; spin_position=0; tick_index=-1; yaw=preview_yaw(*winner); pitch=-0.22F; zoom=1.0F;
        std::string html="<div class=\"reveal-panel\"><div class=\"eyebrow\">SUPPLY DELIVERY</div><h1 id=\"reveal-title\">OPENING YOUR CRATE</h1>";
        html+="<div id=\"reel-window\"><div class=\"reel-marker\"></div><div id=\"reel\">";
        for (const auto* item:reel) html+=card(*item,"");
        html+="</div></div><div id=\"reward-details\">"+inspector(*winner,true)+"</div>";
        html+="<p id=\"reward-status\">"+std::string{fixture?"Preview animation. No crate is spent and no item is awarded.":
            "Your awarded item is already saved to your inventory."}+"</p><div class=\"reveal-buttons\">"+
            button("skip","SKIP ANIMATION",true,"skip")+button("continue","CONTINUE",true,"continue primary")+"</div></div>";
        document->GetElementById("reveal")->SetInnerRML(html);
    }
    void update_hero(const InventoryCosmetic* item) {
        const auto key=item && item->kind!="profile_badge"
            ? item->id+(model->blue_team?":blue":":green")+":"+std::to_string(variant_revision) : std::string{};
        if (key != hero_key) {
            hero_key=key; hero={}; content_key.clear();
            // A completed attempt is not a retained preview after the selection
            // is cleared (for example by an empty filter or an account reset).
            // Keep an active job's key so its result can still be validated.
            if (!hero_worker.valid()) hero_pending.clear();
            yaw=item?preview_yaw(*item):1.15F; pitch=-0.22F; zoom=1.0F;
        }
        if(hero_worker.valid()) {
            if(hero_worker.wait_for(std::chrono::seconds{0})!=std::future_status::ready)return;
            const auto mesh=hero_worker.get();
            if(hero_pending==key && item) {
                if(mesh)if(const auto texture=renderer.set_model_preview(*mesh))hero=*texture;
                content_key.clear();
            }
        }
        if (key.empty() || key==hero_pending) return;
        hero_pending=key;
        const auto values=variant_selection(*item);
        const bool configurable=!item->scripted_skin.empty();
        hero_worker=std::async(std::launch::async,[cosmetic=*item,assets=root,blue=model->blue_team,values,configurable]() -> std::optional<world::ChunkMesh>{
            try {
                if(!configurable)return inventory_preview_mesh(cosmetic,assets,blue);
                return inventory_weapon_preview_mesh(cosmetic,assets,blue,values);
            } catch (const std::exception& error) {
                std::cerr << "Inventory preview " << cosmetic.id << ": " << error.what() << '\n';
                return std::nullopt;
            }
        });
    }
    void prepare(InventoryMenuModel& value, bool is_fixture, float scale) {
        model=&value; fixture=is_fixture;
        for(const auto texture:retired_textures)static_cast<void>(renderer.release_texture(texture));
        retired_textures.clear();
        if (!document) return;
        struct PrepareGuard { bool& flag; explicit PrepareGuard(bool& value):flag(value){flag=true;} ~PrepareGuard(){flag=false;} } guard{preparing};
        scale=std::clamp(scale,0.25F,8.0F);
        if (pixel_scale!=scale) {
            pixel_scale=scale;
            context->SetDensityIndependentPixelRatio(scale);
            context->SetDimensions({static_cast<int>(std::lround(800*scale)),static_cast<int>(std::lround(600*scale))});
        }
        std::map<std::string,std::vector<std::uint8_t>> completed;
        {
            std::lock_guard lock{thumbnail_queue->mutex};
            // A background worker may finish many icons while this route is
            // hidden. Keep render-thread uploads within a two-image budget.
            for (unsigned i{}; i < 2U && !thumbnail_queue->completed.empty(); ++i)
                completed.insert(thumbnail_queue->completed.extract(thumbnail_queue->completed.begin()));
        }
        for (const auto& [id,pixels]:completed) {
            if(thumbnails_pending) --thumbnails_pending;
            if(pixels.empty()){thumbnail_failed.insert(id);continue;}
            if (const auto texture=thumbnail_textures.find(id); texture!=thumbnail_textures.end()) {
                const auto uploaded=renderer.update_texture_rgba8(texture->second.texture,pixels);
                if (!uploaded) failure="A model preview could not be uploaded.";
            } else if (const auto created=renderer.create_texture_rgba8(pixels,
                    {world::cosmetic_preview_width,world::cosmetic_preview_height},render::TextureFilter::linear)) {
                thumbnail_textures.emplace(id,*created);
            }
        }
        preload_nearby();
        const auto now=Clock::now();
        const auto elapsed=std::clamp(std::chrono::duration<float>(now-previous).count(),0.0F,0.1F);
        previous=now;
        if (auto_rotate && !dragging && !spinning) yaw+=elapsed*0.12F;
        update_hero(model->reveal?award():model->selected_item());
        if (model->reveal && receipt_id!=model->reveal->id) begin_reveal();
        auto* overlay=document->GetElementById("reveal");
        overlay->SetProperty("display",model->reveal?"block":"none");
        if (model->reveal) {
            if (spinning) {
                const auto t=std::clamp(std::chrono::duration<double>(now-spin_start).count()/5.6,0.0,1.0);
                spin_position=32.0*(1.0-std::pow(1.0-t,4.0));
                const auto tick=static_cast<int>(std::floor(spin_position+0.5));
                if (tick!=tick_index) { sound=1; tick_index=tick; }
                if (t>=1.0) { spinning=false; sound=2; }
            }
            overlay->SetClass("finished",!spinning);
            if (auto* strip=document->GetElementById("reel")) strip->SetProperty("left",std::to_string(264.0-spin_position*144.0)+"dp");
            if (auto* title=document->GetElementById("reveal-title")) {
                const std::string text=spinning?"OPENING YOUR CRATE":"NEW ITEM ACQUIRED";
                if (title->GetInnerRML()!=text) title->SetInnerRML(text);
            }
        }
        std::ostringstream signature;
        signature<<model->data.revision<<':'<<static_cast<int>(model->section)<<':'<<model->selected<<':'<<model->page<<':'
            <<model->kind_filter<<':'<<model->rarity_filter<<':'<<model->owned_only<<':'<<model->slot_index<<':'<<model->busy<<':'
            <<model->online<<':'<<model->error<<':'<<model->data.items.size()<<':'<<model->data.crates.size()<<':'<<auto_rotate<<':'<<model->blue_team<<':'<<pool_version<<':'<<variant_revision;
        if (signature.str()!=content_key) {
            const auto viewport=std::to_string(static_cast<int>(model->section))+":"+std::to_string(model->selected)+":"+
                std::to_string(model->page)+":"+std::to_string(model->kind_filter)+":"+std::to_string(model->rarity_filter)+":"+pool_version;
            std::map<std::string,float> scroll;
            if(viewport==viewport_key)for(const auto* id:{"inventory-inspector","reward-pool"})
                if(auto* element=document->GetElementById(id))scroll.emplace(id,element->GetScrollTop());
            viewport_key=viewport;
            content_key=signature.str();
            document->GetElementById("level")->SetInnerRML("ACCOUNT LEVEL "+escape(model->data.level));
            document->GetElementById("xp")->SetInnerRML(escape(model->data.level_xp)+" / "+escape(model->data.next_xp)+" XP");
            document->GetElementById("crate-count")->SetInnerRML(escape(model->data.crate_count)+" CRATES");
            try {
                const double next=std::stod(model->data.next_xp), progress=next>0?std::stod(model->data.level_xp)/next:0;
                document->GetElementById("xp-fill")->SetProperty("width",std::to_string(std::clamp(progress,0.0,1.0)*100.0)+"%");
            } catch (...) { document->GetElementById("xp-fill")->SetProperty("width","0%"); }
            for (int i{}; i<4; ++i) document->GetElementById("section"+std::to_string(i))->SetClass("active",static_cast<int>(model->section)==i);
            const auto html=model->section==InventorySection::collection?collection():model->section==InventorySection::crates?crates():
                model->section==InventorySection::history?history():"<div class=\"creator\"><h1>THE CREATOR WORKSHOP</h1><p>Build a voxel skin in the KV6 editor, preview it and submit it for review.</p><p>Community models keep their original authors' credits and use the same weapon gameplay.</p>"+button("creators","OPEN CREATOR PAGES",true,"primary")+"</div>";
            document->GetElementById("content")->SetInnerRML(html);
            if(!scroll.empty()) {
                context->Update();
                for(const auto& [id,top]:scroll)if(auto* element=document->GetElementById(id))element->SetScrollTop(top);
            }
            document->GetElementById("status")->SetInnerRML(escape(model->error.empty()?"Earned through play · Free opening · Cosmetic changes only":model->error));
        }
        draws.clear();
        context->Update();
        context->Render();
        if (hero.texture.is_valid()) renderer.render_model_preview(yaw,pitch,zoom);
    }
};

InventoryView::InventoryView(render::BgfxUiRenderer& renderer,std::filesystem::path root,world::SkinVariantPreferences* variants)
    : impl_(std::make_unique<Impl>(renderer,std::move(root),variants)) {}
InventoryView::~InventoryView() = default;
void InventoryView::prepare(InventoryMenuModel& model,bool fixture,float scale) { impl_->prepare(model,fixture,scale); }
bool InventoryView::draw() { for (const auto& draw:impl_->draws) if (!impl_->renderer.draw(draw)) return false; return true; }
void InventoryView::pointer_move(ui::Point point) {
    if (!impl_->context) return;
    if (impl_->dragging) {
        impl_->yaw+=static_cast<float>(point.x-impl_->pointer.x)*0.012F;
        impl_->pitch=std::clamp(impl_->pitch+static_cast<float>(point.y-impl_->pointer.y)*0.012F,-1.2F,1.2F);
    }
    impl_->pointer=point;
    impl_->context->ProcessMouseMove(static_cast<int>(std::lround(static_cast<float>(point.x)*impl_->pixel_scale)),
                                    static_cast<int>(std::lround(static_cast<float>(point.y)*impl_->pixel_scale)),0);
}
void InventoryView::pointer_button(bool down) {
    if (!impl_->context) return;
    if (down) {
        const auto* hovered=impl_->context->GetHoverElement();
        impl_->dragging=hovered && hovered->IsClassSet("hero");
        if (impl_->dragging) impl_->auto_rotate=false;
        impl_->context->ProcessMouseButtonDown(0,0);
    } else { impl_->dragging=false; impl_->context->ProcessMouseButtonUp(0,0); }
}
void InventoryView::cancel_pointer_capture() {
    if (!impl_->context) return;
    impl_->dragging=false;
    impl_->pointer={-1,-1};
    impl_->context->ProcessMouseMove(-1,-1,0);
    impl_->context->ProcessMouseButtonUp(0,0);
}
void InventoryView::wheel(int steps) {
    if (!impl_->context) return;
    const auto* hovered=impl_->context->GetHoverElement();
    if (hovered && hovered->IsClassSet("hero")) impl_->zoom=std::clamp(impl_->zoom+static_cast<float>(steps)*0.1F,0.65F,1.6F);
    else impl_->context->ProcessMouseWheel(static_cast<float>(-steps),0);
}
void InventoryView::input(ui::InputEvent event) {
    if (!impl_->context) return;
    auto key=Rml::Input::KI_UNKNOWN;
    switch (event.action) {
    case ui::InputAction::focus_next: case ui::InputAction::focus_previous: key=Rml::Input::KI_TAB; break;
    case ui::InputAction::navigate_up: key=Rml::Input::KI_UP; break;
    case ui::InputAction::navigate_down: key=Rml::Input::KI_DOWN; break;
    case ui::InputAction::navigate_left: key=Rml::Input::KI_LEFT; break;
    case ui::InputAction::navigate_right: key=Rml::Input::KI_RIGHT; break;
    case ui::InputAction::activate: key=Rml::Input::KI_RETURN; break;
    case ui::InputAction::cancel:
        if (event.triggers_action()) {
            if (impl_->model && impl_->model->reveal) {
                if (impl_->spinning) { impl_->spinning=false; impl_->spin_position=32; impl_->sound=2; }
                else impl_->model->reveal.reset();
            } else impl_->navigation=0;
        }
        return;
    }
    const auto modifier=event.action==ui::InputAction::focus_previous?Rml::Input::KM_SHIFT:0;
    if (event.phase==ui::InputPhase::released) impl_->context->ProcessKeyUp(key,modifier);
    else impl_->context->ProcessKeyDown(key,modifier);
}
InventoryAction InventoryView::take_action() { return std::exchange(impl_->action,InventoryAction::none); }
int InventoryView::take_navigation() { return std::exchange(impl_->navigation,-1); }
int InventoryView::take_sound() { return std::exchange(impl_->sound,0); }
std::string_view InventoryView::error() const { return impl_->failure; }
bool InventoryView::ready() const { return impl_->document!=nullptr; }
bool InventoryView::previews_ready() const { return !impl_->thumbnails_pending && !impl_->hero_worker.valid(); }
} // namespace battlespades::frontend
