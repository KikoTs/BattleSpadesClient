#include "battlespades/platform/steam_workshop.hpp"

#include "battlespades/core/diagnostics.hpp"

#include <steam/steam_api.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>

namespace battlespades::platform {

/** Flat-API entry points the workshop needs, looked up in the runtime's library. */
struct SteamWorkshop::Api final {
    ISteamUGC*(S_CALLTYPE* ugc)(){};
    uint32(S_CALLTYPE* num_subscribed)(ISteamUGC*, bool){};
    uint32(S_CALLTYPE* subscribed_items)(ISteamUGC*, PublishedFileId_t*, uint32, bool){};
    uint32(S_CALLTYPE* item_state)(ISteamUGC*, PublishedFileId_t){};
    bool(S_CALLTYPE* install_info)(ISteamUGC*, PublishedFileId_t, uint64*, char*, uint32,
                                   uint32*){};
    bool(S_CALLTYPE* download_info)(ISteamUGC*, PublishedFileId_t, uint64*, uint64*){};
    bool(S_CALLTYPE* download_item)(ISteamUGC*, PublishedFileId_t, bool){};
    UGCQueryHandle_t(S_CALLTYPE* create_details_query)(ISteamUGC*, PublishedFileId_t*, uint32){};
    SteamAPICall_t(S_CALLTYPE* send_query)(ISteamUGC*, UGCQueryHandle_t){};
    bool(S_CALLTYPE* query_result)(ISteamUGC*, UGCQueryHandle_t, uint32, SteamUGCDetails_t*){};
    bool(S_CALLTYPE* release_query)(ISteamUGC*, UGCQueryHandle_t){};
    ISteamRemoteStorage*(S_CALLTYPE* remote_storage)(){};
    SteamAPICall_t(S_CALLTYPE* ugc_download)(ISteamRemoteStorage*, UGCHandle_t, uint32){};
    int32(S_CALLTYPE* ugc_read)(ISteamRemoteStorage*, UGCHandle_t, void*, int32, uint32,
                                EUGCReadAction){};
    HSteamPipe(S_CALLTYPE* steam_pipe)(){};
    bool(S_CALLTYPE* call_result)(HSteamPipe, SteamAPICall_t, void*, int, int, bool*){};
    ISteamUtils*(S_CALLTYPE* steam_utils)(){};
    bool(S_CALLTYPE* call_completed)(ISteamUtils*, SteamAPICall_t, bool*){};

    /** True when every ISteamUGC entry the sync needs was found. */
    bool ugc_complete{};
    /** True when the legacy ISteamRemoteStorage download path was found. */
    bool remote_complete{};
};

namespace {

using Api = SteamWorkshop::Api;

template <typename Function>
void bind(const SteamNetworkingRuntime& runtime, Function& target,
          std::initializer_list<const char*> names) {
    // Later names are newer interface versions; keep the newest exported.
    for (const auto* const name : names) {
        if (auto* const symbol = runtime.steamworks_symbol(name); symbol != nullptr) {
            target = reinterpret_cast<Function>(symbol);
        }
    }
}

[[nodiscard]] std::shared_ptr<const Api> bind_api(const SteamNetworkingRuntime& runtime) {
    auto api = std::make_shared<Api>();
    bind(runtime, api->ugc,
         {"SteamAPI_SteamUGC_v016", "SteamAPI_SteamUGC_v017", "SteamAPI_SteamUGC_v018",
          "SteamAPI_SteamUGC_v020", "SteamAPI_SteamUGC_v021"});
    bind(runtime, api->num_subscribed, {"SteamAPI_ISteamUGC_GetNumSubscribedItems"});
    bind(runtime, api->subscribed_items, {"SteamAPI_ISteamUGC_GetSubscribedItems"});
    bind(runtime, api->item_state, {"SteamAPI_ISteamUGC_GetItemState"});
    bind(runtime, api->install_info, {"SteamAPI_ISteamUGC_GetItemInstallInfo"});
    bind(runtime, api->download_info, {"SteamAPI_ISteamUGC_GetItemDownloadInfo"});
    bind(runtime, api->download_item, {"SteamAPI_ISteamUGC_DownloadItem"});
    bind(runtime, api->create_details_query, {"SteamAPI_ISteamUGC_CreateQueryUGCDetailsRequest"});
    bind(runtime, api->send_query, {"SteamAPI_ISteamUGC_SendQueryUGCRequest"});
    bind(runtime, api->query_result, {"SteamAPI_ISteamUGC_GetQueryUGCResult"});
    bind(runtime, api->release_query, {"SteamAPI_ISteamUGC_ReleaseQueryUGCRequest"});
    bind(runtime, api->remote_storage,
         {"SteamAPI_SteamRemoteStorage_v014", "SteamAPI_SteamRemoteStorage_v016"});
    bind(runtime, api->ugc_download, {"SteamAPI_ISteamRemoteStorage_UGCDownload"});
    bind(runtime, api->ugc_read, {"SteamAPI_ISteamRemoteStorage_UGCRead"});
    bind(runtime, api->steam_pipe, {"SteamAPI_GetHSteamPipe"});
    bind(runtime, api->call_result, {"SteamAPI_ManualDispatch_GetAPICallResult"});
    bind(runtime, api->steam_utils, {"SteamAPI_SteamUtils_v010", "SteamAPI_SteamUtils_v011"});
    bind(runtime, api->call_completed, {"SteamAPI_ISteamUtils_IsAPICallCompleted"});
    api->ugc_complete = api->ugc != nullptr && api->num_subscribed != nullptr &&
                        api->subscribed_items != nullptr && api->item_state != nullptr &&
                        api->install_info != nullptr && api->download_info != nullptr &&
                        api->download_item != nullptr && api->create_details_query != nullptr &&
                        api->send_query != nullptr && api->query_result != nullptr &&
                        api->release_query != nullptr && api->steam_pipe != nullptr &&
                        api->call_result != nullptr;
    api->remote_complete = api->remote_storage != nullptr && api->ugc_download != nullptr &&
                           api->ugc_read != nullptr && api->steam_pipe != nullptr &&
                           api->call_result != nullptr;
    return api;
}

/**
 * Waits for an asynchronous call and copies its result.
 *
 * The runtime's pump only collects results someone registered, so this one
 * is still held by Steam and is read here directly once it has completed.
 */
template <typename Result>
[[nodiscard]] std::optional<Result> await_result(const Api& api, SteamAPICall_t call,
                                                 std::chrono::steady_clock::duration timeout,
                                                 std::stop_token stop) {
    if (call == k_uAPICallInvalid) return std::nullopt;
    const auto pipe = api.steam_pipe();
    auto* const utils = api.steam_utils != nullptr ? api.steam_utils() : nullptr;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!stop.stop_requested() && std::chrono::steady_clock::now() < deadline) {
        bool failed{};
        const bool completed = utils == nullptr || api.call_completed == nullptr ||
                               api.call_completed(utils, call, &failed);
        if (completed) {
            Result result{};
            failed = false;
            if (api.call_result(pipe, call, &result, static_cast<int>(sizeof(Result)),
                                Result::k_iCallback, &failed)) {
                if (failed) return std::nullopt;
                return result;
            }
            if (utils != nullptr && api.call_completed != nullptr) return std::nullopt;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }
    return std::nullopt;
}

[[nodiscard]] std::string bounded(const char* text, std::size_t capacity) {
    return std::string{text, strnlen(text, capacity)};
}

/** Reads a remote-storage UGC handle whole (the legacy, 2012 download path). */
[[nodiscard]] std::optional<std::vector<unsigned char>> remote_download(
    const Api& api, UGCHandle_t handle, std::chrono::seconds timeout, std::stop_token stop,
    std::string& error) {
    if (!api.remote_complete) {
        error = "the Steamworks library has no ISteamRemoteStorage download";
        return std::nullopt;
    }
    if (handle == k_UGCHandleInvalid || handle == 0U) {
        error = "the item has no file handle";
        return std::nullopt;
    }
    auto* const storage = api.remote_storage();
    if (storage == nullptr) {
        error = "ISteamRemoteStorage is unavailable";
        return std::nullopt;
    }
    const auto result = await_result<RemoteStorageDownloadUGCResult_t>(
        api, api.ugc_download(storage, handle, 0U), timeout, stop);
    if (!result.has_value()) {
        error = "UGCDownload did not complete";
        return std::nullopt;
    }
    if (result->m_eResult != k_EResultOK) {
        error = "UGCDownload answered EResult " + std::to_string(result->m_eResult);
        return std::nullopt;
    }
    if (result->m_nSizeInBytes <= 0 ||
        static_cast<std::uintmax_t>(result->m_nSizeInBytes) > maximum_workshop_item_bytes) {
        error = "UGCDownload reported " + std::to_string(result->m_nSizeInBytes) + " bytes";
        return std::nullopt;
    }
    std::vector<unsigned char> bytes(static_cast<std::size_t>(result->m_nSizeInBytes));
    const auto read = api.ugc_read(storage, handle, bytes.data(), result->m_nSizeInBytes, 0U,
                                   k_EUGCRead_Close);
    if (read != result->m_nSizeInBytes) {
        error = "UGCRead returned " + std::to_string(read) + " of " +
                std::to_string(result->m_nSizeInBytes) + " bytes";
        return std::nullopt;
    }
    return bytes;
}

/** Steam reports paths in UTF-8. */
[[nodiscard]] std::filesystem::path utf8_path(const std::string& text) {
    return std::filesystem::path{std::u8string{text.begin(), text.end()}};
}

/** The item file inside an ISteamUGC install path (a file for legacy items). */
[[nodiscard]] std::optional<std::filesystem::path> locate_item_file(const std::string& install_path,
                                                                    const std::string& file_name) {
    if (install_path.empty()) return std::nullopt;
    const auto root = utf8_path(install_path);
    std::error_code code;
    if (std::filesystem::is_regular_file(root, code) && !code) return root;
    if (!std::filesystem::is_directory(root, code) || code) return std::nullopt;
    if (workshop_safe_file_name(file_name)) {
        const auto named = root / utf8_path(file_name);
        if (std::filesystem::is_regular_file(named, code) && !code) return named;
    }
    // Otherwise the folder's only .aos, or its only file.
    std::vector<std::filesystem::path> files;
    std::vector<std::filesystem::path> containers;
    for (std::filesystem::directory_iterator iterator{root, code}, end;
         !code && iterator != end && files.size() < 64U; iterator.increment(code)) {
        if (iterator->is_symlink(code) || !iterator->is_regular_file(code)) continue;
        files.push_back(iterator->path());
        if (iterator->path().extension() == ".aos") containers.push_back(iterator->path());
    }
    if (containers.size() == 1U) return containers.front();
    if (files.size() == 1U) return files.front();
    return std::nullopt;
}

[[nodiscard]] std::optional<std::vector<unsigned char>> ugc_download(
    const Api& api, const SteamWorkshopItem& item, std::chrono::seconds timeout,
    std::stop_token stop, const SteamWorkshop::Progress& progress, std::string& source,
    std::string& error) {
    if (!api.ugc_complete) {
        error = "the Steamworks library has no ISteamUGC download";
        return std::nullopt;
    }
    auto* const ugc = api.ugc();
    if (ugc == nullptr) {
        error = "ISteamUGC is unavailable";
        return std::nullopt;
    }
    const auto id = item.published_file_id;
    const auto settled = [&] {
        const auto state = api.item_state(ugc, id);
        return (state & k_EItemStateInstalled) != 0U &&
               (state & (k_EItemStateNeedsUpdate | k_EItemStateDownloading |
                         k_EItemStateDownloadPending)) == 0U;
    };
    if (!settled() && !api.download_item(ugc, id, true)) {
        error = "DownloadItem was refused (not subscribed, or Steam is offline)";
        return std::nullopt;
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!settled()) {
        if (stop.stop_requested() || std::chrono::steady_clock::now() >= deadline) {
            error = "DownloadItem did not finish in time";
            return std::nullopt;
        }
        uint64 downloaded{};
        uint64 total{};
        if (progress && api.download_info(ugc, id, &downloaded, &total)) progress(downloaded, total);
        std::this_thread::sleep_for(std::chrono::milliseconds{250});
    }
    std::array<char, 1'024U> folder{};
    uint64 size{};
    uint32 timestamp{};
    if (!api.install_info(ugc, id, &size, folder.data(), static_cast<uint32>(folder.size()),
                          &timestamp)) {
        error = "GetItemInstallInfo has no install for the item";
        return std::nullopt;
    }
    const auto file = locate_item_file(bounded(folder.data(), folder.size()), item.file_name);
    if (!file.has_value()) {
        error = "the installed item holds no recognisable map file at " +
                bounded(folder.data(), folder.size());
        return std::nullopt;
    }
    source = file->string();
    return read_workshop_file(*file, maximum_workshop_item_bytes, error);
}

} // namespace

SteamWorkshop::SteamWorkshop(SteamNetworkingRuntime& runtime)
    : runtime_{runtime}, api_{bind_api(runtime)} {}

bool SteamWorkshop::available(std::string& reason) const {
    reason.clear();
    if (!runtime_.ready()) {
        reason = "Steam is not running";
        return false;
    }
    if (runtime_.app_id() != workshop_app_id) {
        reason = "attached to Steam as app " + std::to_string(runtime_.app_id()) +
                 ", not Ace of Spades (" + std::to_string(workshop_app_id) +
                 "); this account cannot read its Workshop subscriptions";
        return false;
    }
    if (!api_->ugc_complete) {
        reason = "the Steamworks library lacks the ISteamUGC calls the Workshop sync needs";
        return false;
    }
    return true;
}

SteamWorkshopListing SteamWorkshop::list_subscriptions(std::chrono::seconds timeout) const {
    SteamWorkshopListing listing;
    if (!available(listing.error)) return listing;
    const auto& api = *api_;
    auto* const ugc = api.ugc();
    if (ugc == nullptr) {
        listing.error = "ISteamUGC is unavailable";
        return listing;
    }
    const auto count = std::min<uint32>(api.num_subscribed(ugc, false), 4'096U);
    std::vector<PublishedFileId_t> ids(count);
    if (count != 0U) {
        ids.resize(api.subscribed_items(ugc, ids.data(), count, false));
    }
    for (const auto id : ids) {
        SteamWorkshopItem item;
        item.published_file_id = id;
        item.state = api.item_state(ugc, id);
        std::array<char, 1'024U> folder{};
        uint64 size{};
        uint32 timestamp{};
        if (api.install_info(ugc, id, &size, folder.data(), static_cast<uint32>(folder.size()),
                             &timestamp)) {
            item.install_path = bounded(folder.data(), folder.size());
            item.size_on_disk = size;
            item.install_timestamp = timestamp;
        }
        listing.items.push_back(std::move(item));
    }
    // Details come fifty at a time (kNumUGCResultsPerPage).
    const std::stop_source never;
    for (std::size_t first = 0U; first < ids.size(); first += kNumUGCResultsPerPage) {
        const auto page = std::min<std::size_t>(kNumUGCResultsPerPage, ids.size() - first);
        const auto query =
            api.create_details_query(ugc, ids.data() + first, static_cast<uint32>(page));
        if (query == k_UGCQueryHandleInvalid) {
            listing.error = "CreateQueryUGCDetailsRequest was refused";
            return listing;
        }
        const auto completed = await_result<SteamUGCQueryCompleted_t>(
            api, api.send_query(ugc, query), timeout, never.get_token());
        if (!completed.has_value() || completed->m_eResult != k_EResultOK) {
            api.release_query(ugc, query);
            listing.error = completed.has_value()
                                ? "the Workshop details query answered EResult " +
                                      std::to_string(completed->m_eResult)
                                : "the Workshop details query did not complete (offline?)";
            return listing;
        }
        for (uint32 index = 0U; index < completed->m_unNumResultsReturned; ++index) {
            SteamUGCDetails_t details{};
            if (!api.query_result(ugc, query, index, &details)) continue;
            const auto found = std::ranges::find(listing.items, details.m_nPublishedFileId,
                                                 &SteamWorkshopItem::published_file_id);
            if (found == listing.items.end()) continue;
            found->details = true;
            found->result = details.m_eResult;
            found->title = bounded(details.m_rgchTitle, sizeof(details.m_rgchTitle));
            found->time_updated = details.m_rtimeUpdated;
            found->consumer_app_id = details.m_nConsumerAppID;
            found->file_type = details.m_eFileType;
            found->file_name = bounded(details.m_pchFileName, sizeof(details.m_pchFileName));
            found->file_size = details.m_nFileSize;
            found->file_handle = details.m_hFile;
            found->preview_handle = details.m_hPreviewFile;
        }
        api.release_query(ugc, query);
    }
    listing.ok = true;
    return listing;
}

std::optional<SteamWorkshopDownload> SteamWorkshop::download(const SteamWorkshopItem& item,
                                                             std::stop_token stop,
                                                             std::string& error,
                                                             const Progress& progress,
                                                             bool prefer_remote,
                                                             std::chrono::seconds timeout) const {
    error.clear();
    if (!available(error)) return std::nullopt;
    const auto& api = *api_;
    SteamWorkshopDownload output;
    std::string ugc_error;
    std::string remote_error;
    const auto try_ugc = [&] {
        std::string source;
        auto bytes = ugc_download(api, item, timeout, stop, progress, source, ugc_error);
        if (!bytes.has_value()) return false;
        output.item = std::move(*bytes);
        output.method = "ugc";
        output.source = std::move(source);
        return true;
    };
    const auto try_remote = [&] {
        auto bytes = remote_download(api, item.file_handle, timeout, stop, remote_error);
        if (!bytes.has_value()) return false;
        output.item = std::move(*bytes);
        output.method = "remote";
        output.source = "UGCDownload " + std::to_string(item.file_handle);
        return true;
    };
    const bool fetched = prefer_remote ? (try_remote() || try_ugc()) : (try_ugc() || try_remote());
    if (!fetched) {
        error = "ISteamUGC: " + (ugc_error.empty() ? std::string{"not tried"} : ugc_error) +
                "; ISteamRemoteStorage: " +
                (remote_error.empty() ? std::string{"not tried"} : remote_error);
        return std::nullopt;
    }
    // Retail fetched the preview with its own UGCDownload; a missing preview
    // only leaves the map without a thumbnail.
    if (item.preview_handle != 0U && item.preview_handle != k_UGCHandleInvalid) {
        std::string preview_error;
        if (auto preview = remote_download(api, item.preview_handle, std::chrono::seconds{60},
                                           stop, preview_error);
            preview.has_value()) {
            output.preview = std::move(*preview);
        } else {
            core::diagnostic("workshop", "no preview for " +
                                             std::to_string(item.published_file_id) + ": " +
                                             preview_error);
        }
    }
    return output;
}

// ---------------------------------------------------------------------------
// WorkshopSyncService
// ---------------------------------------------------------------------------

struct WorkshopSyncService::Impl final {
    SteamNetworkingRuntime& runtime;
    std::filesystem::path maps_directory;
    mutable std::mutex mutex;
    std::condition_variable_any wake;
    bool requested{};
    WorkshopSyncStatus current;
    std::jthread worker;

    Impl(SteamNetworkingRuntime& owner, std::filesystem::path directory)
        : runtime{owner}, maps_directory{std::move(directory)} {}

    void set(WorkshopSyncStatus::Phase phase, std::size_t completed, std::size_t total,
             std::string message, bool changed = false) {
        const std::scoped_lock lock{mutex};
        current.phase = phase;
        current.completed = completed;
        current.total = total;
        current.message = std::move(message);
        if (changed) ++current.generation;
    }

    void run(std::stop_token stop) {
        while (!stop.stop_requested()) {
            {
                std::unique_lock lock{mutex};
                if (!wake.wait(lock, stop, [this] { return requested; })) return;
                requested = false;
            }
            sync_once(stop);
        }
    }

    /**
     * A subscribed map already on disk that this sync did not write (a retail
     * download): left alone while it is at least as new as Steam's version,
     * which is retail's own freshness test (sub_10001D20 compares the .ugc
     * and .vxl modification times with m_rtimeUpdated).
     */
    [[nodiscard]] bool unmanaged_copy_current(std::uint64_t id, std::uint32_t time_updated) const {
        const auto stem = workshop_subscribed_stem(id);
        for (const auto* const extension : {".ugc", ".vxl"}) {
            std::error_code code;
            const auto path = maps_directory / (stem + extension);
            if (!std::filesystem::is_regular_file(path, code) || code) return false;
            const auto written = std::filesystem::last_write_time(path, code);
            if (code) return false;
            const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::clock_cast<std::chrono::system_clock>(written).time_since_epoch());
            if (seconds.count() < static_cast<long long>(time_updated)) return false;
        }
        return true;
    }

    void sync_once(std::stop_token stop) {
        using Phase = WorkshopSyncStatus::Phase;
        SteamWorkshop workshop{runtime};
        std::string reason;
        if (!workshop.available(reason)) {
            core::diagnostic("workshop", "sync skipped: " + reason);
            set(Phase::unavailable, 0U, 0U, reason);
            return;
        }
        set(Phase::listing, 0U, 0U, "Checking Workshop subscriptions");
        auto listing = workshop.list_subscriptions();
        if (!listing.ok) {
            core::diagnostic("workshop", "cannot list subscriptions: " + listing.error);
            set(Phase::failed, 0U, 0U, listing.error);
            return;
        }
        std::vector<WorkshopSubscribedItem> subscribed;
        for (const auto& item : listing.items) {
            // Only items the details query vouched for as this game's.
            if (!item.details || item.result != k_EResultOK ||
                (item.consumer_app_id != 0U && item.consumer_app_id != workshop_app_id)) {
                core::diagnostic("workshop", "ignoring subscription " +
                                                 std::to_string(item.published_file_id) +
                                                 " (no usable details)");
                continue;
            }
            subscribed.push_back({item.published_file_id, item.time_updated});
        }
        std::error_code code;
        std::filesystem::create_directories(maps_directory, code);
        const auto index_path = maps_directory / std::string{workshop_index_file_name};
        auto index = load_workshop_index(index_path);
        auto plan = plan_workshop_sync(index, subscribed, [&](std::uint64_t id) {
            return workshop_entry_files_present(maps_directory, index.items.at(id));
        });
        bool changed{};
        std::string error;
        for (const auto id : plan.remove) {
            const auto removed = remove_workshop_map(maps_directory, id, index.items.at(id));
            core::diagnostic("workshop", "unsubscribed " + std::to_string(id) + ": removed " +
                                             std::to_string(removed) + " file(s)");
            index.items.erase(id);
            changed = true;
        }
        if (changed && !save_workshop_index(index_path, index, error)) {
            core::diagnostic("workshop", "cannot save the index: " + error);
        }
        std::vector<std::uint64_t> work;
        for (const auto id : plan.install) {
            const auto found = std::ranges::find(subscribed, id,
                                                 &WorkshopSubscribedItem::published_file_id);
            if (unmanaged_copy_current(id, found->time_updated)) {
                core::diagnostic("workshop", workshop_subscribed_stem(id) +
                                                 " is already installed (not by this sync); "
                                                 "leaving it as it is");
                continue;
            }
            work.push_back(id);
        }
        work.insert(work.end(), plan.update.begin(), plan.update.end());
        std::size_t done{};
        std::size_t failed{};
        const auto total = work.size();
        for (const auto id : work) {
            if (stop.stop_requested()) return;
            const auto item = std::ranges::find(listing.items, id,
                                                &SteamWorkshopItem::published_file_id);
            set(Phase::downloading, done, total,
                "Syncing Workshop maps " + std::to_string(done + 1U) + "/" +
                    std::to_string(total),
                std::exchange(changed, false));
            auto download = workshop.download(*item, stop, error);
            std::optional<WorkshopIndexEntry> entry;
            if (download.has_value()) {
                entry = install_workshop_map(maps_directory, id, item->time_updated,
                                             download->item, download->preview, error);
            }
            if (!entry.has_value()) {
                ++failed;
                core::diagnostic("workshop", "could not install \"" + item->title + "\" (" +
                                                 std::to_string(id) + "): " + error);
            } else {
                core::diagnostic("workshop", "installed \"" + item->title + "\" as " +
                                                 workshop_subscribed_stem(id) + " via " +
                                                 download->method + " from " + download->source);
                index.items[id] = std::move(*entry);
                if (!save_workshop_index(index_path, index, error)) {
                    core::diagnostic("workshop", "cannot save the index: " + error);
                }
                changed = true;
            }
            ++done;
        }
        auto message = total == 0U ? std::string{"Workshop maps are up to date"}
                       : failed == 0U
                           ? "Synced " + std::to_string(total) + " Workshop map(s)"
                           : std::to_string(failed) + " of " + std::to_string(total) +
                                 " Workshop map(s) failed to download";
        core::diagnostic("workshop", message + " (" + std::to_string(subscribed.size()) +
                                         " subscribed)");
        set(failed == 0U ? Phase::done : Phase::failed, done, total, std::move(message),
            changed);
    }
};

WorkshopSyncService::WorkshopSyncService(SteamNetworkingRuntime& runtime,
                                         std::filesystem::path maps_directory)
    : impl_{std::make_unique<Impl>(runtime, std::move(maps_directory))} {
    impl_->worker = std::jthread{[raw = impl_.get()](std::stop_token stop) { raw->run(stop); }};
}

WorkshopSyncService::~WorkshopSyncService() {
    impl_->worker.request_stop();
    if (impl_->worker.joinable()) impl_->worker.join();
}

void WorkshopSyncService::request_sync() {
    {
        const std::scoped_lock lock{impl_->mutex};
        impl_->requested = true;
    }
    impl_->wake.notify_all();
}

WorkshopSyncStatus WorkshopSyncService::status() const {
    const std::scoped_lock lock{impl_->mutex};
    return impl_->current;
}

} // namespace battlespades::platform
