#pragma once

#include "battlespades/platform/steam_networking.hpp"
#include "battlespades/platform/workshop_sync.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace battlespades::platform {

/** One Workshop subscription as Steam describes it. */
struct SteamWorkshopItem final {
    std::uint64_t published_file_id{};
    /** EItemState bits (subscribed 1, legacy 2, installed 4, needs update 8, downloading 16). */
    std::uint32_t state{};
    /** True once the details query answered for this item. */
    bool details{};
    /** EResult of the details query for this item (1 is OK). */
    int result{};
    std::string title;
    std::uint32_t time_updated{};
    std::uint32_t consumer_app_id{};
    /** EWorkshopFileType; 0 is a community item. */
    int file_type{};
    /** A legacy item's own file name, for example "Castle.aos"; untrusted. */
    std::string file_name;
    std::int32_t file_size{};
    std::uint64_t file_handle{};
    std::uint64_t preview_handle{};
    /** GetItemInstallInfo: a legacy item's path is its file, otherwise a folder. */
    std::string install_path;
    std::uint64_t size_on_disk{};
    std::uint32_t install_timestamp{};

    [[nodiscard]] bool legacy() const noexcept { return (state & 2U) != 0U; }
    [[nodiscard]] bool installed() const noexcept { return (state & 4U) != 0U; }
};

struct SteamWorkshopListing final {
    bool ok{};
    std::string error;
    std::vector<SteamWorkshopItem> items;
};

struct SteamWorkshopDownload final {
    std::vector<unsigned char> item;
    std::vector<unsigned char> preview;
    /** "ugc" (ISteamUGC::DownloadItem) or "remote" (ISteamRemoteStorage::UGCDownload). */
    std::string method;
    /** Where the item bytes came from, for diagnostics. */
    std::string source;
};

/**
 * Workshop calls over the flat Steamworks API of an attached runtime.
 *
 * Nothing here subscribes or unsubscribes: the account's subscriptions are
 * read, and the subscribed items are downloaded. Every call blocks while
 * Steam answers, so callers run it off the presentation thread.
 */
class SteamWorkshop final {
public:
    explicit SteamWorkshop(SteamNetworkingRuntime& runtime);

    /** False (with the reason) when Steam is not attached as Ace of Spades. */
    [[nodiscard]] bool available(std::string& reason) const;

    /** Subscriptions with details, state and install info. */
    [[nodiscard]] SteamWorkshopListing list_subscriptions(
        std::chrono::seconds timeout = std::chrono::seconds{30}) const;

    using Progress = std::function<void(std::uint64_t downloaded, std::uint64_t total)>;
    /**
     * The item's bytes (a retail .aos) and its preview.
     *
     * ISteamUGC::DownloadItem first, which installs into Steam's workshop
     * folder (a legacy item's install path is the file itself); then
     * ISteamRemoteStorage::UGCDownload of the item's file handle, which is how
     * retail fetched it in 2012. `prefer_remote` tries the latter first.
     */
    [[nodiscard]] std::optional<SteamWorkshopDownload> download(
        const SteamWorkshopItem& item, std::stop_token stop, std::string& error,
        const Progress& progress = {}, bool prefer_remote = false,
        std::chrono::seconds timeout = std::chrono::seconds{180}) const;

    struct Api;

private:
    SteamNetworkingRuntime& runtime_;
    std::shared_ptr<const Api> api_;
};

/** What the sync is doing, for the menu and the log. */
struct WorkshopSyncStatus final {
    enum class Phase { idle, listing, downloading, done, unavailable, failed };
    Phase phase{Phase::idle};
    std::size_t completed{};
    std::size_t total{};
    /** "Syncing Workshop maps 2/5", or why the sync did not run. */
    std::string message;
    /** Bumps every time a subscribed map was added, replaced or removed. */
    std::uint64_t generation{};
};

/**
 * Keeps `<maps_directory>/Subscribed_<id>.*` in step with the account's
 * Workshop subscriptions, on a worker thread of its own.
 *
 * request_sync() never blocks; requests while a sync runs fold into one more
 * pass. The runtime must outlive the service.
 */
class WorkshopSyncService final {
public:
    WorkshopSyncService(SteamNetworkingRuntime& runtime, std::filesystem::path maps_directory);
    ~WorkshopSyncService();

    WorkshopSyncService(const WorkshopSyncService&) = delete;
    WorkshopSyncService& operator=(const WorkshopSyncService&) = delete;

    void request_sync();
    [[nodiscard]] WorkshopSyncStatus status() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace battlespades::platform
