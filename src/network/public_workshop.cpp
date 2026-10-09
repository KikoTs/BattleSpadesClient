#include "battlespades/network/public_workshop.hpp"
#include "battlespades/network/workshop_preview.hpp"
#include "battlespades/platform/workshop_sync.hpp"
#include "battlespades/world/vxl_map.hpp"
#include <curl/curl.h>
#include <sodium.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <memory>
#include <mutex>
#include <regex>
#include <set>
#include <stdexcept>

namespace battlespades::network {
namespace {
using Json = nlohmann::json;
using Bytes = std::vector<unsigned char>;
constexpr std::size_t max_bytes = 64U * 1024U * 1024U;
void check_stop(std::stop_token stop) { if (stop.stop_requested()) throw std::runtime_error("Download cancelled."); }
std::uint64_t number(const Json& value) {
    if (value.is_number_unsigned()) return value.get<std::uint64_t>();
    if (value.is_number_integer()) return value.get<std::int64_t>() >= 0 ? value.get<std::uint64_t>() : 0;
    if (!value.is_string()) return 0;
    const auto text = value.get<std::string>();
    std::uint64_t result{};
    const auto parsed = std::from_chars(text.data(), text.data()+text.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == text.data()+text.size() ? result : 0;
}
std::string plain(std::string value, std::size_t limit) {
    std::erase_if(value, [](unsigned char c) { return c < 32U && c != '\n'; });
    if (value.size() > limit) value.resize(limit);
    return value;
}
std::string hash(std::span<const unsigned char> bytes) {
    std::array<unsigned char, crypto_hash_sha256_BYTES> digest{};
    crypto_hash_sha256(digest.data(), bytes.data(), static_cast<unsigned long long>(bytes.size()));
    std::array<char, crypto_hash_sha256_BYTES*2U+1U> hex{};
    sodium_bin2hex(hex.data(), hex.size(), digest.data(), digest.size());
    return hex.data();
}
struct Buffer { Bytes data; std::size_t limit{}; };
std::size_t write_data(char* data, std::size_t size, std::size_t count, void* userdata) {
    auto& buffer = *static_cast<Buffer*>(userdata);
    if (size && count > (buffer.limit-buffer.data.size())/size) return 0;
    const auto length=size*count;
    try { buffer.data.insert(buffer.data.end(), data, data+length); } catch (...) { return 0; }
    return length;
}
int cancelled(void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return static_cast<std::stop_token*>(userdata)->stop_requested() ? 1 : 0;
}
Bytes fetch(const std::string& url, std::size_t limit, std::stop_token stop, const std::string& post = {}) {
    check_stop(stop);
    static std::once_flag initialized;
    std::call_once(initialized, [] { if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) throw std::runtime_error("HTTP initialization failed."); });
    const std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle{curl_easy_init(), curl_easy_cleanup};
    if (!handle) throw std::runtime_error("HTTP initialization failed.");
    Buffer buffer{{}, limit};
    curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, "BattleSpadesWorkshop/1.0");
    curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT, limit > 8U*1024U*1024U ? 180L : 25L);
    curl_easy_setopt(handle.get(), CURLOPT_LOW_SPEED_LIMIT, 128L);
    curl_easy_setopt(handle.get(), CURLOPT_LOW_SPEED_TIME, 20L);
    curl_easy_setopt(handle.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(handle.get(), CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(handle.get(), CURLOPT_XFERINFOFUNCTION, cancelled);
    curl_easy_setopt(handle.get(), CURLOPT_XFERINFODATA, &stop);
    if (!post.empty()) {
        curl_easy_setopt(handle.get(), CURLOPT_POST, 1L);
        curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDS, post.c_str());
    }
    const auto code=curl_easy_perform(handle.get());
    check_stop(stop);
    long status{};
    curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status);
    if (code != CURLE_OK || status != 200)
        throw std::runtime_error("Workshop request failed (HTTP " + std::to_string(status) + "): " + curl_easy_strerror(code));
    return std::move(buffer.data);
}
Json json_fetch(const std::string& url, std::stop_token stop, const std::string& post = {}) {
    const auto bytes=fetch(url, 4U*1024U*1024U, stop, post);
    return Json::parse(bytes.begin(),bytes.end());
}
std::string encode(std::string_view text) {
    constexpr char digits[]="0123456789ABCDEF";
    std::string encoded;
    for (const auto ch:text) {
        const auto c=static_cast<unsigned char>(ch);
        if ((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_') encoded+=ch;
        else { encoded+='%'; encoded+=digits[c>>4U]; encoded+=digits[c&15U]; }
    }
    return encoded;
}
Bytes asset_bytes(const WorkshopAsset& asset, std::stop_token stop) {
    if (!PublicWorkshop::download_url_allowed(asset.url) || asset.bytes == 0 || asset.bytes > max_bytes)
        throw std::runtime_error("This item does not have a supported public download.");
    auto bytes=fetch(asset.url,static_cast<std::size_t>(asset.bytes),stop);
    if (bytes.size()!=asset.bytes || (!asset.sha256.empty() && hash(bytes)!=asset.sha256))
        throw std::runtime_error("Workshop file size or SHA-256 verification failed.");
    return bytes;
}
std::filesystem::path receipt_path(const std::filesystem::path& maps, const WorkshopReference& reference) {
    return maps / ("." + PublicWorkshop::installed_stem(reference) + ".json");
}
Json read_receipt(const std::filesystem::path& maps,const WorkshopReference& reference) {
    std::string error;
    const auto bytes=platform::read_workshop_file(receipt_path(maps,reference),8192,error);
    return bytes ? Json::parse(bytes->begin(),bytes->end(),nullptr,false) : Json{};
}
bool png(std::span<const unsigned char> data) {
    constexpr std::array<unsigned char,8> signature{137,80,78,71,13,10,26,10};
    if (data.size()<33U || !std::equal(signature.begin(),signature.end(),data.begin()) ||
        data[8]!=0 || data[9]!=0 || data[10]!=0 || data[11]!=13 ||
        data[12]!='I' || data[13]!='H' || data[14]!='D' || data[15]!='R') return false;
    const auto dimension=[&](std::size_t offset) {
        return (static_cast<std::uint32_t>(data[offset])<<24U) | (static_cast<std::uint32_t>(data[offset+1U])<<16U) |
            (static_cast<std::uint32_t>(data[offset+2U])<<8U) | data[offset+3U];
    };
    const auto width=dimension(16),height=dimension(20);
    return width && height && width<=4096U && height<=4096U && static_cast<std::uint64_t>(width)*height<=8U*1024U*1024U;
}
} // namespace

bool WorkshopReference::valid() const {
    if (source=="steam") return !id.empty() && PublicWorkshop::steam_id(id)==id;
    return source=="aosplay" && std::regex_match(id,std::regex{"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}"});
}
std::string PublicWorkshop::steam_id(std::string_view text) {
    std::string id{text};
    if (id.starts_with("https://steamcommunity.com/sharedfiles/filedetails/?") ||
        id.starts_with("https://steamcommunity.com/workshop/filedetails/?")) {
        std::smatch match;
        if (!std::regex_search(id,match,std::regex{"[?&]id=([0-9]+)(&|$)"})) return {};
        id=match[1].str();
    }
    if (id.empty() || id.size()>20U || id.front()=='0') return {};
    std::uint64_t value{};
    const auto parsed=std::from_chars(id.data(),id.data()+id.size(),value);
    return parsed.ec==std::errc{} && parsed.ptr==id.data()+id.size() && value ? id : std::string{};
}
bool PublicWorkshop::download_url_allowed(std::string_view url) {
    const std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> parsed{curl_url(),curl_url_cleanup};
    if (!parsed || curl_url_set(parsed.get(),CURLUPART_URL,std::string{url}.c_str(),0)!=CURLUE_OK) return false;
    const auto part=[&](CURLUPart which) {
        char* value{};
        if (curl_url_get(parsed.get(),which,&value,0)!=CURLUE_OK) return std::string{};
        std::string result{value}; curl_free(value); return result;
    };
    const auto host=part(CURLUPART_HOST),port=part(CURLUPART_PORT);
    return part(CURLUPART_SCHEME)=="https" && part(CURLUPART_USER).empty() && part(CURLUPART_PASSWORD).empty() &&
        part(CURLUPART_FRAGMENT).empty() && (port.empty() || port=="443") &&
        (host.ends_with(".steamusercontent.com") || host=="steamuserimages-a.akamaihd.net" ||
         host=="steamusercontent-a.akamaihd.net" || host.ends_with(".public.blob.vercel-storage.com"));
}
std::vector<std::string> PublicWorkshop::browse_ids(std::string_view html) {
    const std::string text{html};
    const std::regex link{"href=\"https://steamcommunity\\.com/sharedfiles/filedetails/\\?id=([0-9]+)\""};
    std::vector<std::string> ids;
    for (auto it=std::sregex_iterator(text.begin(),text.end(),link); it!=std::sregex_iterator{}; ++it) {
        auto id=steam_id((*it)[1].str());
        if (!id.empty() && std::ranges::find(ids,id)==ids.end()) ids.push_back(std::move(id));
        if (ids.size()==30U) break;
    }
    return ids;
}
PublicWorkshopItem PublicWorkshop::parse_steam(const Json& value) {
    PublicWorkshopItem item;
    item.reference={"steam",value.value("publishedfileid",std::string{})};
    const auto banned=value.value("banned",Json{});
    if (!item.reference.valid() || value.value("result",0)!=1 || value.value("consumer_app_id",0)!=224540 ||
        value.value("visibility",-1)!=0 || (banned.is_boolean()?banned.get<bool>():number(banned)!=0) || value.value("file_type",0)!=0)
        throw std::runtime_error("Choose a public Ace of Spades map (app 224540).");
    item.title=plain(value.value("title",std::string{}),160);
    item.author=value.value("creator",std::string{});
    item.description=plain(value.value("description",std::string{}),4096);
    item.version=std::to_string(number(value.value("time_updated",Json{})));
    item.created=number(value.value("time_created",Json{})); item.updated=number(value.value("time_updated",Json{}));
    item.subscribers=number(value.value("subscriptions",Json{})); item.favorites=number(value.value("favorited",Json{}));
    item.views=number(value.value("views",Json{}));
    item.preview_url=value.value("preview_url",std::string{});
    item.files.push_back({"container",value.value("file_url",std::string{}),{},number(value.value("file_size",Json{}))});
    if (value.contains("tags") && value["tags"].is_array()) for (const auto& tag:value["tags"])
        if (item.tags.size()<32U && tag.is_object()) item.tags.push_back(plain(tag.value("tag",std::string{}),32));
    return item;
}
PublicWorkshopItem PublicWorkshop::parse_archive(const Json& value) {
    PublicWorkshopItem item;
    item.reference={"aosplay",value.value("id",std::string{})};
    if (!item.reference.valid() || value.value("item_type",std::string{})!="map") throw std::runtime_error("Unsupported archive map.");
    item.title=plain(value.value("title",std::string{}),160);
    item.author=plain(value.value("uploader",std::string{}),128);
    item.description=plain(value.value("description",std::string{}),4096);
    if (value.contains("preview_url") && value["preview_url"].is_string()) item.preview_url=value["preview_url"].get<std::string>();
    if (value.contains("tags") && value["tags"].is_array()) for (const auto& tag:value["tags"])
        if (item.tags.size()<32U && tag.is_string()) item.tags.push_back(plain(tag.get<std::string>(),32));
    for (const auto& file:value.at("assets")) {
        if (item.files.size()>=12U) throw std::runtime_error("Too many Workshop files.");
        const auto name=file.value("filename",std::string{});
        const auto kind=name.ends_with(".vxl")?"vxl":name.ends_with(".ugc")?"ugc":name.ends_with(".txt")?"txt":name.ends_with(".png")?"png":"";
        if (!*kind) continue;
        const auto digest=file.value("sha256",std::string{});
        if (!std::regex_match(digest,std::regex{"[a-f0-9]{64}"})) throw std::runtime_error("Missing Workshop checksum.");
        auto url=file.value("download_url",std::string{});
        if (url.empty()) url=file.value("url",std::string{});
        item.files.push_back({kind,url,digest,number(file.value("size",Json{}))});
        item.version+=digest;
    }
    return item;
}
PublicWorkshopItem PublicWorkshop::steam_details(std::string_view text,std::stop_token stop) {
    const auto id=steam_id(text);
    if (id.empty()) throw std::runtime_error("Paste an Ace of Spades Workshop link or item ID.");
    const auto value=json_fetch("https://api.steampowered.com/ISteamRemoteStorage/GetPublishedFileDetails/v1/",stop,
        "itemcount=1&publishedfileids%5B0%5D="+id);
    auto item=parse_steam(value.at("response").at("publishedfiledetails").at(0));
    if (item.reference.id!=id) throw std::runtime_error("Steam returned a different item.");
    return item;
}
std::string PublicWorkshop::browse_url(std::string_view query,std::size_t page,const WorkshopBrowseOptions& options) {
    const std::array<std::string_view,5> sorts{"trend","toprated","totaluniquesubscribers","mostrecent","lastupdated"};
    const std::array<std::string_view,10> tags{"","CTF","dem","dia","MH","oc","TDM","TC","vip","zom"};
    if (std::ranges::find(sorts,options.sort)==sorts.end() || std::ranges::find(tags,options.tag)==tags.end() ||
        (options.days!=7 && options.days!=30 && options.days!=90 && options.days!=365 && options.days!=-1))
        throw std::runtime_error("Unsupported Workshop filter.");
    return "https://steamcommunity.com/workshop/browse/?appid=224540&browsesort="+options.sort+"&actualsort="+options.sort+
        "&section=readytouseitems&numperpage=30&p="+std::to_string((std::min)(page,std::size_t{833})+1U)+
        "&days="+std::to_string(options.days)+"&searchtext="+encode(query.substr(0,96))+
        (options.tag.empty()?std::string{}:"&requiredtags%5B%5D="+encode(options.tag));
}
WorkshopPage PublicWorkshop::browse_steam(std::string_view query,std::size_t page,std::stop_token stop,const WorkshopBrowseOptions& options) {
    WorkshopPage result;
    if (const auto id=steam_id(query); !id.empty()) { result.items.push_back(steam_details(id,stop)); return result; }
    const auto bytes=fetch(browse_url(query,page,options),4U*1024U*1024U,stop);
    const auto ids=browse_ids({reinterpret_cast<const char*>(bytes.data()),bytes.size()});
    if (ids.empty()) return result;
    std::string body="itemcount="+std::to_string(ids.size());
    for (std::size_t i=0;i<ids.size();++i) body+="&publishedfileids%5B"+std::to_string(i)+"%5D="+ids[i];
    const auto data=json_fetch("https://api.steampowered.com/ISteamRemoteStorage/GetPublishedFileDetails/v1/",stop,body);
    for (const auto& value:data.at("response").at("publishedfiledetails")) {
        try { auto item=parse_steam(value); if (std::ranges::find(ids,item.reference.id)!=ids.end()) result.items.push_back(std::move(item)); }
        catch (const std::exception&) { /* Deleted, private, wrong-app and collection entries cannot be installed. */ }
    }
    // Keep Steam's ranking; the details API need not return IDs in request order.
    std::ranges::sort(result.items,[&](const auto& a,const auto& b) {
        return std::ranges::find(ids,a.reference.id)<std::ranges::find(ids,b.reference.id);
    });
    result.more=ids.size()==30U && page<833U;
    return result;
}
std::vector<std::string> PublicWorkshop::parse_gallery(std::string_view html) {
    std::vector<std::string> urls;
    const auto start=html.find("var rgScreenshotURLs");
    if (start==std::string_view::npos) return urls;
    const auto end=html.find("};",start);
    if (end==std::string_view::npos || end-start>65536U) return urls;
    const std::string block{html.substr(start,end-start)};
    const std::regex entry{R"(['"][0-9]+['"]\s*:\s*['"](https://[^'"\s<>]+)['"])"};
    for (auto it=std::sregex_iterator(block.begin(),block.end(),entry);it!=std::sregex_iterator{} && urls.size()<10U;++it) {
        auto url=(*it)[1].str();
        for (std::size_t pos{};(pos=url.find("&amp;",pos))!=std::string::npos;) url.replace(pos,5,"&");
        if (download_url_allowed(url) && std::ranges::find(urls,url)==urls.end()) urls.push_back(std::move(url));
    }
    return urls;
}
std::vector<std::string> PublicWorkshop::steam_gallery(std::string_view id,std::stop_token stop) {
    if (steam_id(id)!=id || id.empty()) throw std::runtime_error("Invalid Workshop item.");
    const auto bytes=fetch("https://steamcommunity.com/sharedfiles/filedetails/?id="+std::string{id},4U*1024U*1024U,stop);
    return parse_gallery({reinterpret_cast<const char*>(bytes.data()),bytes.size()});
}
void PublicWorkshop::cache_preview(PublicWorkshopItem& item,const std::filesystem::path& cache,std::stop_token stop) {
    if (!download_url_allowed(item.preview_url)) return;
    const auto key=hash({reinterpret_cast<const unsigned char*>(item.preview_url.data()),item.preview_url.size()});
    const auto path=std::filesystem::absolute(cache)/("v2-"+key+".png");
    std::error_code ec;
    if (std::filesystem::is_regular_file(path,ec)) {
        std::string error;
        const auto cached=platform::read_workshop_file(path,2U*1024U*1024U,error);
        if (cached && png(*cached)) { item.preview_file=path.string(); return; }
    }
    try {
        const auto bytes=normalize_workshop_preview(fetch(item.preview_url,2U*1024U*1024U,stop));
        check_stop(stop);
        std::string error;
        std::filesystem::create_directories(cache);
        if (png(bytes) && platform::write_file_atomically(path,bytes,error)) item.preview_file=path.string();
    } catch (const std::exception&) { check_stop(stop); }
}
std::string PublicWorkshop::installed_stem(const WorkshopReference& item) {
    if (!item.valid()) throw std::runtime_error("Invalid Workshop item.");
    return "Subscribed_Web_"+item.source+"_"+item.id;
}
bool PublicWorkshop::installed(const PublicWorkshopItem& item,const std::filesystem::path& maps) {
    const auto receipt=read_receipt(maps,item.reference);
    if (!receipt.is_object() || receipt.value("version",std::string{})!=item.version) return false;
    const auto stem=installed_stem(item.reference);
    std::error_code ec;
    for (const auto* suffix:{".vxl",".ugc",".txt"}) {
        const auto path=maps/(stem+suffix);
        if (!std::filesystem::is_regular_file(path,ec) || std::filesystem::is_symlink(path,ec)) return false;
        if (!receipt.contains(suffix) || std::filesystem::file_size(path,ec)!=number(receipt[suffix]) || ec) return false;
    }
    return true;
}
void PublicWorkshop::install(const PublicWorkshopItem& item,const std::filesystem::path& maps,std::stop_token stop,const Progress& progress) {
    const auto stem=installed_stem(item.reference);
    if (progress) progress("Downloading "+item.title+"...");
    Bytes vxl,ugc,preview;
    std::string error;
    if (item.reference.source=="steam") {
        if (item.files.size()!=1U) throw std::runtime_error("Invalid Steam map manifest.");
        const auto bytes=asset_bytes(item.files.front(),stop);
        auto container=platform::parse_aos_container(bytes,error);
        if (!container) throw std::runtime_error(error);
        vxl=std::move(container->vxl); ugc=std::move(container->ugc);
    } else {
        for (const auto& file:item.files) {
            if (file.kind=="vxl") { if (!vxl.empty()) throw std::runtime_error("Choose an archive with one map."); vxl=asset_bytes(file,stop); }
            if (file.kind=="ugc") { if (!ugc.empty()) throw std::runtime_error("Choose an archive with one map sidecar."); ugc=asset_bytes(file,stop); }
        }
        if (ugc.empty()) for (const auto& file:item.files) if (file.kind=="txt") { ugc=asset_bytes(file,stop); break; }
    }
    if (ugc.empty() || ugc.size()>1024U*1024U || !Json::parse(ugc.begin(),ugc.end(),nullptr,false).is_object())
        throw std::runtime_error("The map has invalid JSON metadata.");
    if (progress) progress("Checking "+item.title+"...");
    const auto validation=world::VxlMap::load(std::as_bytes(std::span{vxl}));
    if (!validation) throw std::runtime_error("Invalid VXL: "+validation.error);
    check_stop(stop);
    if (!item.preview_file.empty()) {
        if (auto data=platform::read_workshop_file(item.preview_file,2U*1024U*1024U,error);data && png(*data)) preview=std::move(*data);
    }
    std::filesystem::create_directories(maps);
    const std::vector<std::pair<std::string,const Bytes*>> files{{".vxl",&vxl},{".txt",&ugc},{".png",&preview},{".ugc",&ugc}};
    std::vector<std::pair<std::filesystem::path,Bytes>> backups;
    // Validate every input before hiding the old sidecar. Restore the entire old
    // revision on a failed write; never leave a partially updated map visible.
    for (const auto& [suffix,bytes]:files) {
        static_cast<void>(bytes);
        const auto path=maps/(stem+suffix);
        if (std::filesystem::is_symlink(path)) throw std::runtime_error("Workshop destination is a symbolic link.");
        if (std::filesystem::exists(path)) {
            auto old=platform::read_workshop_file(path,max_bytes,error);
            if (!old) throw std::runtime_error(error);
            backups.emplace_back(path,std::move(*old));
        }
    }
    check_stop(stop);
    const auto receipt=receipt_path(maps,item.reference);
    if (std::filesystem::is_symlink(receipt)) throw std::runtime_error("Workshop receipt is a symbolic link.");
    std::filesystem::remove(maps/(stem+".ugc"));
    Json sizes{{"version",item.version},{"source",item.reference.source},{"item_id",item.reference.id}};
    try {
        for (const auto& [suffix,bytes]:files) {
            if (bytes->empty()) { std::filesystem::remove(maps/(stem+suffix)); continue; }
            if (!platform::write_file_atomically(maps/(stem+suffix),*bytes,error)) throw std::runtime_error(error);
            sizes[suffix]=bytes->size();
        }
        const auto text=sizes.dump();
        if (!platform::write_file_atomically(receipt,{reinterpret_cast<const unsigned char*>(text.data()),text.size()},error)) throw std::runtime_error(error);
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(maps/(stem+".ugc"),ec);
        for (const auto& [suffix,bytes]:files) { static_cast<void>(bytes); std::filesystem::remove(maps/(stem+suffix),ec); }
        // .ugc is last in backups, so rollback also preserves scanner ordering.
        for (const auto& [path,data]:backups) if (!platform::write_file_atomically(path,data,error)) break;
        throw;
    }
}
void PublicWorkshop::remove(const WorkshopReference& item,const std::filesystem::path& maps) {
    const auto receipt=read_receipt(maps,item);
    if (!receipt.is_object() || receipt.value("source",std::string{})!=item.source || receipt.value("item_id",std::string{})!=item.id) return;
    const auto stem=installed_stem(item);
    for (const auto* suffix:{".ugc",".vxl",".txt",".png"}) std::filesystem::remove(maps/(stem+suffix));
    std::filesystem::remove(receipt_path(maps,item));
}
} // namespace battlespades::network
