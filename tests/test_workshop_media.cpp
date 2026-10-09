#include "battlespades/frontend/workshop_media.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace battlespades;
using namespace std::chrono_literals;
void expect(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
    try {
        std::atomic<unsigned> active{},peak{},started{};
        std::atomic<bool> release{};
        frontend::WorkshopMediaLoader loader{"unused",[&](auto item,const auto&,std::stop_token stop) {
            const auto count=++active; auto previous=peak.load();
            while (previous<count && !peak.compare_exchange_weak(previous,count)) {}
            ++started;
            while (!release.load() && !stop.stop_requested()) std::this_thread::sleep_for(1ms);
            --active;
            return item.reference.id+".png";
        },[](auto,std::stop_token) { return std::vector<std::string>{"https://images.steamusercontent.com/a","https://images.steamusercontent.com/b"}; }};
        std::vector<network::PublicWorkshopItem> old(30);
        for (std::size_t i=0;i<old.size();++i) { old[i].reference={"steam",std::to_string(i+1U)}; old[i].preview_url="https://images.steamusercontent.com/preview"; }
        loader.page(old);
        const auto deadline=std::chrono::steady_clock::now()+3s;
        while (started.load()<4U && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(1ms);
        expect(started.load()==4U,"Worker count is not bounded");
        expect(loader.poll().empty(),"Unfinished images should not be published");
        auto current=old.front(); current.reference.id="100";
        const auto begin=std::chrono::steady_clock::now();
        loader.page({current}); loader.select(&current);
        expect(std::chrono::steady_clock::now()-begin<100ms,"Changing pages waited for obsolete media");
        release=true;
        bool thumbnail{},gallery_done{}; unsigned images{};
        while (std::chrono::steady_clock::now()<deadline && !(thumbnail && gallery_done)) {
            for (const auto& update:loader.poll()) {
                expect(update.key=="steam:100","Cancelled page replaced the current previews");
                if (!update.gallery) thumbnail=true;
                if (update.gallery && !update.file.empty()) ++images;
                if (update.gallery && update.done) gallery_done=true;
            }
            std::this_thread::sleep_for(1ms);
        }
        expect(thumbnail && gallery_done && images==3U,"Gallery did not stream every image");
        expect(peak.load()<=4U,"Media requests exceeded concurrency limit");
        std::cout<<"Bounded media loading, immediate page changes, progressive gallery and stale result checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
