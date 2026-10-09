#include "battlespades/frontend/workshop_media.hpp"
#include <algorithm>

namespace battlespades::frontend {
using network::PublicWorkshop;
WorkshopMediaLoader::WorkshopMediaLoader(std::filesystem::path cache,Thumbnail thumbnail,Gallery gallery)
    :cache_(std::move(cache)),thumbnail_(std::move(thumbnail)),gallery_(std::move(gallery)) {
    if (!thumbnail_) thumbnail_=[](network::PublicWorkshopItem item,const std::filesystem::path& path,std::stop_token stop) {
        PublicWorkshop::cache_preview(item,path,stop); return item.preview_file;
    };
    if (!gallery_) gallery_=PublicWorkshop::steam_gallery;
    for (unsigned i=0;i<4;++i) workers_.emplace_back([this](std::stop_token stop) { run(stop); });
}
WorkshopMediaLoader::~WorkshopMediaLoader() {
    page_stop_.request_stop(); selection_stop_.request_stop();
    for (auto& worker:workers_) worker.request_stop();
    changed_.notify_all();
}
void WorkshopMediaLoader::page(const std::vector<network::PublicWorkshopItem>& items) {
    page_stop_.request_stop(); selection_stop_.request_stop();
    page_stop_=std::stop_source{}; selection_stop_=std::stop_source{}; selected_.clear();
    {
        std::lock_guard lock{mutex_}; ++generation_; jobs_.clear(); completed_.clear();
        for (const auto& item:items) if (item.preview_file.empty() && !item.preview_url.empty())
            jobs_.push_back({item,page_stop_.get_token(),generation_,false});
    }
    changed_.notify_all();
}
void WorkshopMediaLoader::select(const network::PublicWorkshopItem* item) {
    const auto key=item?item->reference.key():std::string{};
    if (key==selected_) return;
    selection_stop_.request_stop(); selection_stop_=std::stop_source{}; selected_=key;
    std::lock_guard lock{mutex_};
    std::erase_if(jobs_,[](const Job& job) { return job.gallery; });
    if (item && !item->gallery_loaded) {
        jobs_.push_front({*item,selection_stop_.get_token(),generation_,true});
        changed_.notify_one();
    }
}
void WorkshopMediaLoader::publish(Update update,std::stop_token stop) {
    std::lock_guard lock{mutex_};
    if (!stop.stop_requested() && update.generation==generation_) completed_.push_back(std::move(update));
}
std::vector<WorkshopMediaLoader::Update> WorkshopMediaLoader::poll() {
    std::vector<Update> result;
    std::lock_guard lock{mutex_};
    // Bound texture arrival on the render thread; no synchronous network waits.
    while (!completed_.empty() && result.size()<4U) {
        result.push_back(std::move(completed_.front())); completed_.pop_front();
    }
    return result;
}
void WorkshopMediaLoader::run(std::stop_token stop) {
    while (!stop.stop_requested()) {
        Job job;
        {
            std::unique_lock lock{mutex_};
            if (!changed_.wait(lock,stop,[&] { return !jobs_.empty(); })) return;
            job=std::move(jobs_.front()); jobs_.pop_front();
        }
        if (job.stop.stop_requested()) continue;
        const auto key=job.item.reference.key();
        try {
            if (!job.gallery) {
                publish({key,thumbnail_(job.item,cache_,job.stop),{}, {},0,false,true,job.generation},job.stop);
                continue;
            }
            std::vector<std::string> urls;
            if (!job.item.preview_url.empty()) urls.push_back(job.item.preview_url);
            std::string error;
            if (job.item.reference.source=="steam") {
                try {
                    std::vector<std::string> gallery;
                    bool cached{};
                    const auto cache_key=key+":"+job.item.version;
                    {
                        std::lock_guard lock{mutex_};
                        if (const auto found=galleries_.find(cache_key);found!=galleries_.end() && found->second.first>std::chrono::steady_clock::now()) {
                            gallery=found->second.second; cached=true;
                        }
                    }
                    if (!cached) {
                        gallery=gallery_(job.item.reference.id,job.stop);
                        if (!job.stop.stop_requested()) {
                            std::lock_guard lock{mutex_};
                            if (galleries_.size()>=64U) galleries_.erase(galleries_.begin());
                            galleries_[cache_key]={std::chrono::steady_clock::now()+std::chrono::minutes{5},gallery};
                        }
                    }
                    for (auto url:gallery)
                        if (std::ranges::find(urls,url)==urls.end()) urls.push_back(std::move(url));
                } catch (const std::exception&) { error="More screenshots are unavailable."; }
            }
            publish({key,{},error,urls,0,true,false,job.generation},job.stop);
            for (std::size_t i=0;i<urls.size() && !job.stop.stop_requested();++i) {
                auto image=job.item; image.preview_url=urls[i]; image.preview_file.clear();
                const auto file=i==0 && !job.item.preview_file.empty()?job.item.preview_file:thumbnail_(image,cache_,job.stop);
                publish({key,file,{}, {},i,true,false,job.generation},job.stop);
            }
            publish({key,{},error,{},0,true,true,job.generation},job.stop);
        } catch (const std::exception&) {
            publish({key,{},"Preview unavailable.",{},0,job.gallery,true,job.generation},job.stop);
        }
    }
}
}
