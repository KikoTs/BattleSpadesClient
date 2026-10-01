// Lists this Steam account's Ace of Spades Workshop subscriptions and their
// state, without the game and without downloading anything.
//
//   aos_workshop_sync_probe                     list subscriptions
//   aos_workshop_sync_probe --download <dir>    also download each one and
//                                               install it under <dir>/maps
//   options: --only <id>     just this subscription
//            --remote        try ISteamRemoteStorage::UGCDownload first
//            --sync <dir>    run the real WorkshopSyncService against <dir>
//
// Nothing here subscribes or unsubscribes. ISteamUGC::DownloadItem does let
// Steam install the item into its own steamapps/workshop folder, as the game
// would. Point --download/--sync at a scratch directory, not the game.

#include "battlespades/platform/steam_networking.hpp"
#include "battlespades/platform/steam_workshop.hpp"
#include "battlespades/platform/workshop_sync.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace {

using namespace battlespades::platform;

[[nodiscard]] std::string state_text(std::uint32_t state) {
    std::string text;
    const auto add = [&](std::uint32_t bit, std::string_view name) {
        if ((state & bit) == 0U) return;
        if (!text.empty()) text += '|';
        text += name;
    };
    add(1U, "subscribed");
    add(2U, "legacy");
    add(4U, "installed");
    add(8U, "needs-update");
    add(16U, "downloading");
    add(32U, "download-pending");
    return text.empty() ? std::string{"none"} : text;
}

[[nodiscard]] std::string title_from_sidecar(const std::vector<unsigned char>& ugc) {
    // A tiny reader for "title": "..." so the probe needs no JSON library.
    const std::string text{ugc.begin(), ugc.end()};
    const auto key = text.find("\"title\"");
    if (key == std::string::npos) return {};
    const auto open = text.find('"', text.find(':', key) + 1U);
    if (open == std::string::npos) return {};
    const auto close = text.find('"', open + 1U);
    return close == std::string::npos ? std::string{} : text.substr(open + 1U, close - open - 1U);
}

int usage() {
    std::cerr << "usage: aos_workshop_sync_probe [--only <id>] [--remote] "
                 "[--download <dir> | --sync <dir>]\n";
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    std::optional<std::filesystem::path> download_directory;
    std::optional<std::filesystem::path> sync_directory;
    std::uint64_t only{};
    bool prefer_remote{};
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--download" && index + 1 < argc) {
            download_directory = std::filesystem::absolute(argv[++index]);
        } else if (argument == "--sync" && index + 1 < argc) {
            sync_directory = std::filesystem::absolute(argv[++index]);
        } else if (argument == "--only" && index + 1 < argc) {
            only = std::strtoull(argv[++index], nullptr, 10);
        } else if (argument == "--remote") {
            prefer_remote = true;
        } else {
            return usage();
        }
    }

    SteamNetworkingRuntime runtime;
    SteamNetworkingRuntimeConfig config;
    config.app_id = workshop_app_id;
    // Spacewar has no Ace of Spades subscriptions; refuse it rather than
    // listing someone else's Workshop.
    config.fallback_app_id = 0U;
    config.relay_timeout = std::chrono::seconds{0};
    std::error_code ignored;
    config.search_directory = std::filesystem::absolute(argv[0], ignored).parent_path();
    std::string error;
    if (!runtime.start(config, error)) {
        std::cerr << "Steam unavailable as app " << workshop_app_id << ": " << error << '\n';
        return 1;
    }
    std::cout << "attached as app " << runtime.app_id() << ", Steam id " << runtime.steam_id()
              << " (" << runtime.persona_name() << ")\n";

    if (sync_directory.has_value()) {
        WorkshopSyncService service{runtime, *sync_directory / "maps"};
        service.request_sync();
        std::string last;
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds{200});
            const auto status = service.status();
            if (status.message != last) {
                last = status.message;
                std::cout << "[sync] " << status.message << '\n';
            }
            using Phase = WorkshopSyncStatus::Phase;
            if (status.phase == Phase::done || status.phase == Phase::failed ||
                status.phase == Phase::unavailable) {
                return status.phase == Phase::done ? 0 : 1;
            }
        }
    }

    SteamWorkshop workshop{runtime};
    const auto listing = workshop.list_subscriptions();
    if (!listing.ok) {
        std::cerr << "cannot list subscriptions: " << listing.error << '\n';
        return 1;
    }
    std::cout << listing.items.size() << " subscription(s)\n";
    int failures{};
    for (const auto& item : listing.items) {
        if (only != 0U && item.published_file_id != only) continue;
        std::cout << "\n" << item.published_file_id << "  \"" << item.title << "\"\n"
                  << "  state:        " << state_text(item.state) << " (" << item.state << ")\n"
                  << "  details:      " << (item.details ? "yes" : "no") << ", result "
                  << item.result << ", consumer app " << item.consumer_app_id << ", file type "
                  << item.file_type << "\n"
                  << "  time_updated: " << item.time_updated << "\n"
                  << "  file:         \"" << item.file_name << "\" (" << item.file_size
                  << " bytes, handle " << item.file_handle << ")\n"
                  << "  preview:      handle " << item.preview_handle << "\n"
                  << "  install:      "
                  << (item.install_path.empty() ? std::string{"(none)"} : item.install_path)
                  << " (" << item.size_on_disk << " bytes, stamp " << item.install_timestamp
                  << ")\n"
                  << "  installs as:  " << workshop_subscribed_stem(item.published_file_id)
                  << ".{vxl,txt,ugc,png}\n";
        if (!download_directory.has_value()) continue;

        const std::stop_source stop;
        auto last_report = std::chrono::steady_clock::now();
        auto download = workshop.download(
            item, stop.get_token(), error,
            [&](std::uint64_t done, std::uint64_t total) {
                const auto now = std::chrono::steady_clock::now();
                if (now - last_report < std::chrono::seconds{1}) return;
                last_report = now;
                std::cout << "  downloading:  " << done << "/" << total << '\n';
            },
            prefer_remote);
        if (!download.has_value()) {
            ++failures;
            std::cout << "  DOWNLOAD FAILED: " << error << '\n';
            continue;
        }
        std::cout << "  downloaded:   " << download->item.size() << " bytes via "
                  << download->method << " from " << download->source << "; preview "
                  << download->preview.size() << " bytes\n";
        std::filesystem::create_directories(*download_directory / "raw", ignored);
        const auto raw_name = std::to_string(item.published_file_id) + "_" +
                              (workshop_safe_file_name(item.file_name) ? item.file_name
                                                                        : std::string{"item.bin"});
        if (!write_file_atomically(*download_directory / "raw" / raw_name, download->item, error)) {
            std::cout << "  raw copy failed: " << error << '\n';
        }
        std::string parse_error;
        if (const auto container = parse_aos_container(download->item, parse_error);
            container.has_value()) {
            std::cout << "  container:    VXL " << container->vxl.size() << " bytes, UGC "
                      << container->ugc.size() << " bytes, sidecar title \""
                      << title_from_sidecar(container->ugc) << "\"\n";
        } else {
            std::cout << "  container:    NOT a retail .aos: " << parse_error << '\n';
        }
        const auto entry =
            install_workshop_map(*download_directory / "maps", item.published_file_id,
                                 item.time_updated, download->item, download->preview, error);
        if (!entry.has_value()) {
            ++failures;
            std::cout << "  INSTALL FAILED: " << error << '\n';
            continue;
        }
        std::cout << "  installed:   ";
        for (const auto& file : entry->files) std::cout << ' ' << file;
        std::cout << '\n';
    }
    return failures == 0 ? 0 : 1;
}
