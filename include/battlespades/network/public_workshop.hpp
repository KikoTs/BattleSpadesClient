#pragma once

#include <filesystem>
#include <functional>
#include <nlohmann/json.hpp>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::network {
struct WorkshopReference final {
    std::string source, id;
    [[nodiscard]] std::string key() const { return source + ":" + id; }
    [[nodiscard]] bool valid() const;
};
struct WorkshopAsset final {
    std::string kind, url, sha256;
    std::uint64_t bytes{};
};
struct PublicWorkshopItem final {
    WorkshopReference reference;
    std::string title, author, description, version, preview_url, preview_file;
    std::vector<std::string> tags;
    std::vector<WorkshopAsset> files;
    std::vector<std::string> preview_urls, preview_files;
    std::uint64_t created{}, updated{}, subscribers{}, favorites{}, views{};
    bool gallery_loaded{}, preview_loaded{};
    std::string gallery_error;
};
struct WorkshopBrowseOptions final {
    std::string sort{"trend"}, tag;
    int days{7};
};
struct WorkshopPage final {
    std::vector<PublicWorkshopItem> items;
    bool more{};
};
enum class WorkshopRequestKind { catalog, details, subscriptions, subscribe, unsubscribe };
struct WorkshopRequest final {
    WorkshopRequestKind kind{WorkshopRequestKind::catalog};
    WorkshopReference reference;
    std::string query, expected_account;
    std::size_t offset{};
};
struct WorkshopResult final {
    nlohmann::json payload;
    std::string error;
    [[nodiscard]] explicit operator bool() const { return error.empty() && payload.is_object(); }
};
/** All network methods block and must run on a cancellable worker. No Steam login is used. */
class PublicWorkshop final {
public:
    using Progress = std::function<void(std::string)>;
    [[nodiscard]] static std::string steam_id(std::string_view text);
    [[nodiscard]] static std::vector<std::string> browse_ids(std::string_view html);
    [[nodiscard]] static PublicWorkshopItem parse_steam(const nlohmann::json& item);
    [[nodiscard]] static PublicWorkshopItem parse_archive(const nlohmann::json& item);
    [[nodiscard]] static PublicWorkshopItem steam_details(std::string_view id, std::stop_token stop = {});
    [[nodiscard]] static std::string browse_url(std::string_view query, std::size_t page, const WorkshopBrowseOptions& options = {});
    [[nodiscard]] static WorkshopPage browse_steam(std::string_view query, std::size_t page, std::stop_token stop = {}, const WorkshopBrowseOptions& options = {});
    [[nodiscard]] static std::vector<std::string> parse_gallery(std::string_view html);
    [[nodiscard]] static std::vector<std::string> steam_gallery(std::string_view id, std::stop_token stop = {});
    [[nodiscard]] static bool download_url_allowed(std::string_view url);
    static void cache_preview(PublicWorkshopItem& item, const std::filesystem::path& cache, std::stop_token stop);
    [[nodiscard]] static std::string installed_stem(const WorkshopReference& item);
    [[nodiscard]] static bool installed(const PublicWorkshopItem& item, const std::filesystem::path& maps);
    static void install(const PublicWorkshopItem& item, const std::filesystem::path& maps,
                        std::stop_token stop = {}, const Progress& progress = {});
    /** Removes only browser-owned files backed by this item's receipt. */
    static void remove(const WorkshopReference& item, const std::filesystem::path& maps);
};
} // namespace battlespades::network
