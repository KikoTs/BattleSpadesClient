#include "battlespades/frontend/cosmetic_icons.hpp"
#include "battlespades/frontend/class_selection_presentation.hpp"
#include "battlespades/world/cosmetic_preview.hpp"
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <cmath>

namespace battlespades::frontend {
std::string cosmetic_icon_key(const CosmeticIconRequest& r){
    std::string key="runtime/cosmetic/"+r.item.id+"/"+r.item.sha256+"/"+std::to_string(static_cast<int>(r.kind))+"/"+
        std::to_string(r.class_id)+"/"+std::to_string(r.blue_team);
    if(r.team_color)key+="/rgb:"+std::to_string(r.team_color->red)+","+std::to_string(r.team_color->green)+","+std::to_string(r.team_color->blue);
    for(const auto& [name,part]:r.item.character_parts)key+="/"+name+":"+part.sha256;
    if(r.hat)key+="/hat:"+r.hat->id+":"+r.hat->sha256;
    for(const auto& [option,value]:r.variants)if(option!="zoom")key+="/"+option+":"+value;
    return key;
}

std::vector<std::uint8_t> render_cosmetic_icon(const CosmeticIconRequest& r,const std::filesystem::path& root){
    const auto mesh=r.kind==CosmeticIconKind::weapon?inventory_weapon_preview_mesh(r.item,root,r.blue_team,r.variants):
        inventory_preview_mesh(r.item,root,r.blue_team,r.class_id,r.hat?&*r.hat:nullptr,r.kind==CosmeticIconKind::class_head,r.team_color);
    if(!mesh||mesh->empty())return {};
    world::CosmeticPreviewStyle style;
    style.width=style.height=cosmetic_icon_size;
    style.padding=r.kind==CosmeticIconKind::class_head?12.:9.;
    style.outline=r.kind==CosmeticIconKind::class_head?5.:3.;
    if(r.kind==CosmeticIconKind::class_body)style.fit_width=ClassSelectionAppearance::portrait_source.width;
    // A slight diagonal gives long guns more room in square loadout slots.
    if(r.kind==CosmeticIconKind::weapon){style.roll=-.38;style.outline=4.;}
    else {
        // The original class art faces forward with the camera near eye level.
        // Keep a bold silhouette; outlining each helmet step adds noise in the HUD.
        style.pitch=r.kind==CosmeticIconKind::class_head?-.04:.12;
        if(r.kind==CosmeticIconKind::class_head)style.roll=-.08;
        style.samples=4;
        style.internal_contours=false;
        style.boost_colors=false;
    }
    return world::cosmetic_preview(*mesh,r.kind==CosmeticIconKind::weapon?3.141592653589793-1.15:-.20,1.0,style);
}
struct CosmeticIconCache::Impl {
    struct Entry {std::optional<render::UiTextureInfo> image;std::uint64_t used{};bool pending{true};};
    struct Job {std::string key;CosmeticIconRequest request;};
    struct Result {std::string key;std::vector<std::uint8_t> pixels;};
    std::filesystem::path root;
    std::map<std::string,Entry,std::less<>> entries;
    std::mutex mutex;std::condition_variable changed;std::deque<Job> jobs;std::deque<Result> completed;
    std::uint64_t frame{};std::size_t rendered{};
    std::jthread worker;
    explicit Impl(std::filesystem::path path):root(std::move(path)),worker([this](std::stop_token stop){
        while(!stop.stop_requested()){
            Job job;
            {std::unique_lock lock{mutex};changed.wait(lock,[&]{return stop.stop_requested()||!jobs.empty();});
                if(stop.stop_requested()){return;}job=std::move(jobs.front());jobs.pop_front();}
            Result result;result.key=std::move(job.key);
            try{result.pixels=render_cosmetic_icon(job.request,root);}catch(const std::exception&){}
            {std::lock_guard lock{mutex};completed.push_back(std::move(result));}
        }
    }){}
    ~Impl(){worker.request_stop();changed.notify_all();}
};
CosmeticIconCache::CosmeticIconCache(std::filesystem::path root):impl_(std::make_unique<Impl>(std::move(root))){}
CosmeticIconCache::~CosmeticIconCache()=default;
std::string CosmeticIconCache::request(const CosmeticIconRequest& request,std::string_view fallback){
    auto& p=*impl_;const auto key=cosmetic_icon_key(request);
    if(auto found=p.entries.find(key);found!=p.entries.end()){
        found->second.used=p.frame;return found->second.image?key:std::string{fallback};
    }
    if(p.entries.size()>=128U)return std::string{fallback};
    p.entries.emplace(key,Impl::Entry{std::nullopt,p.frame,true});
    {std::lock_guard lock{p.mutex};p.jobs.push_back({key,request});}p.changed.notify_one();
    return std::string{fallback};
}
void CosmeticIconCache::pump(render::BgfxUiRenderer& renderer){
    auto& p=*impl_;++p.frame;
    for(unsigned n=0;n<2;++n){
        Impl::Result result;
        {std::lock_guard lock{p.mutex};if(p.completed.empty())break;result=std::move(p.completed.front());p.completed.pop_front();}
        const auto found=p.entries.find(result.key);if(found==p.entries.end())continue;
        found->second.pending=false;++p.rendered;
        if(!result.pixels.empty())found->second.image=renderer.create_texture_rgba8(result.pixels,
            {cosmetic_icon_size,cosmetic_icon_size},render::TextureFilter::linear,true);
    }
    // Retire only icons unused for several frames, before drawing starts.
    while(p.entries.size()>96U){
        auto oldest=p.entries.end();
        for(auto it=p.entries.begin();it!=p.entries.end();++it)
            if(!it->second.pending&&it->second.used+120<p.frame&&(oldest==p.entries.end()||it->second.used<oldest->second.used))oldest=it;
        if(oldest==p.entries.end())break;
        if(oldest->second.image){static_cast<void>(renderer.release_texture(oldest->second.image->texture));}p.entries.erase(oldest);
    }
}
std::optional<render::UiTextureInfo> CosmeticIconCache::texture(std::string_view asset)const{
    const auto it=impl_->entries.find(asset);if(it==impl_->entries.end())return std::nullopt;
    it->second.used=impl_->frame;return it->second.image;
}
void CosmeticIconCache::clear(render::BgfxUiRenderer& renderer){
    impl_->worker.request_stop();impl_->changed.notify_all();if(impl_->worker.joinable())impl_->worker.join();
    for(const auto& [key,entry]:impl_->entries)if(entry.image)static_cast<void>(renderer.release_texture(entry.image->texture));
    impl_->entries.clear();
}
std::size_t CosmeticIconCache::rendered_count()const{return impl_->rendered;}
}
