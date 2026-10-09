#include "battlespades/network/public_workshop.hpp"
#include "battlespades/platform/workshop_sync.hpp"
#include <iostream>
#include <stdexcept>

using namespace battlespades::network;
void expect(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
template<class F> void rejects(F&& work) { bool failed{}; try { work(); } catch (const std::exception&) { failed=true; } expect(failed,"Invalid item was accepted"); }
int main(int argc,char** argv) {
    try {
        expect(PublicWorkshop::steam_id("18446744073709551615")=="18446744073709551615","64-bit IDs lost precision");
        for (const auto* id:{"0","01","-1","18446744073709551616","123x","../../123","https://evil.test/?id=123"})
            expect(PublicWorkshop::steam_id(id).empty(),"Invalid ID accepted");
        expect(PublicWorkshop::steam_id("https://steamcommunity.com/sharedfiles/filedetails/?id=185279489&searchtext=")=="185279489","Workshop URL not parsed");
        expect(PublicWorkshop::download_url_allowed("https://cdn.steamusercontent.com/ugc/123/map"),"Steam CDN rejected");
        expect(PublicWorkshop::download_url_allowed("https://store.public.blob.vercel-storage.com/map.vxl"),"Archive storage rejected");
        for (const auto* url:{"http://cdn.steamusercontent.com/a","file:///etc/passwd","https://127.0.0.1/a",
            "https://cdn.steamusercontent.com.evil.test/a","https://evil.test/a","https://user@cdn.steamusercontent.com/a","https://cdn.steamusercontent.com:444/a"})
            expect(!PublicWorkshop::download_url_allowed(url),"Unsafe download URL accepted");
        const auto ids=PublicWorkshop::browse_ids("<a href=\"https://steamcommunity.com/sharedfiles/filedetails/?id=123\">Map</a><a href=\"https://steamcommunity.com/sharedfiles/filedetails/?id=123\">duplicate</a><a href=\"https://steamcommunity.com/sharedfiles/filedetails/?id=456\">Map</a>");
        expect(ids==std::vector<std::string>{"123","456"},"Browse links not deduplicated");
        const auto url=PublicWorkshop::browse_url("snow & city",2,{"mostrecent","CTF",30});
        expect(url.find("browsesort=mostrecent")!=std::string::npos && url.find("requiredtags%5B%5D=CTF")!=std::string::npos &&
            url.find("days=30")!=std::string::npos && url.find("&p=3")!=std::string::npos && url.find("snow%20%26%20city")!=std::string::npos &&
            url.find("numperpage=30")!=std::string::npos,"Steam filters/pagination are wrong");
        rejects([] { static_cast<void>(PublicWorkshop::browse_url("",0,{"bad","",7})); });
        const auto gallery=PublicWorkshop::parse_gallery(R"(var rgScreenshotURLs = {'123':'https://images.steamusercontent.com/ugc/a?x=1&amp;y=2','124':'https://127.0.0.1/private','125':'https://images.steamusercontent.com/ugc/b'}; <img src="https://images.steamusercontent.com/avatar">)");
        expect(gallery==std::vector<std::string>{"https://images.steamusercontent.com/ugc/a?x=1&y=2","https://images.steamusercontent.com/ugc/b"},"Unrelated or unsafe gallery URLs accepted");
        nlohmann::json data{{"publishedfileid","185279489"},{"result",1},{"consumer_app_id",224540},{"visibility",0},
            {"title","Paintball"},{"file_size","2839047"},{"file_url","https://cdn.steamusercontent.com/ugc/file"},{"time_updated",123},{"banned",0}};
        auto item=PublicWorkshop::parse_steam(data);
        expect(item.files.front().bytes==2839047U,"Steam string size not parsed");
        data["consumer_app_id"]=480; rejects([&] { static_cast<void>(PublicWorkshop::parse_steam(data)); });
        data["consumer_app_id"]=224540; data["visibility"]=1; rejects([&] { static_cast<void>(PublicWorkshop::parse_steam(data)); });
        data["visibility"]=0; data["banned"]=true; rejects([&] { static_cast<void>(PublicWorkshop::parse_steam(data)); });
        expect(PublicWorkshop::installed_stem(item.reference)=="Subscribed_Web_steam_185279489","Unexpected install namespace");
        expect(!battlespades::platform::parse_workshop_subscribed_stem(PublicWorkshop::installed_stem(item.reference)),"Steam sync could take ownership of browser files");
        rejects([] { static_cast<void>(PublicWorkshop::installed_stem({"aosplay","../../map"})); });
        if (argc==4 && std::string_view{argv[1]}=="--live") {
            const std::filesystem::path root{argv[3]};
            item=PublicWorkshop::steam_details(argv[2]);
            PublicWorkshop::cache_preview(item,root/"previews",{});
            PublicWorkshop::install(item,root/"maps",{},[](std::string message){std::cout<<message<<'\n';});
            expect(PublicWorkshop::installed(item,root/"maps"),"Downloaded map did not install");
            const auto page=PublicWorkshop::browse_steam("",0);
            expect(!page.items.empty(),"Live Steam catalog is empty");
            auto gallery_item=PublicWorkshop::steam_details("197458861");
            const auto screenshots=PublicWorkshop::steam_gallery(gallery_item.reference.id);
            expect(screenshots.size()>1U,"Live map gallery has no additional screenshots");
            for (const auto& screenshot:screenshots) {
                gallery_item.preview_url=screenshot; gallery_item.preview_file.clear();
                PublicWorkshop::cache_preview(gallery_item,root/"gallery",{});
                expect(!gallery_item.preview_file.empty(),"Live gallery image could not be decoded");
            }
            std::cout<<"Installed "<<item.title<<"; catalog returned "<<page.items.size()<<" maps\n";
        }
        std::cout<<"Public Workshop checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
