#pragma once

#include "battlespades/assets/asset_install.hpp"
#include "battlespades/updater/update_manifest.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace battlespades::assets {

/**
 * "Download game assets" in BattleSpadesAssetInstaller, on every OS.
 *
 * The release manifest (stable.json, the schema the Windows launcher reads)
 * may publish an optional `retail_assets` component: a ZIP of the original
 * files hosted by the project owner. It is downloaded over HTTPS (resumable
 * through a ".partial" file and HTTP Range), checked for its exact size and
 * SHA-256, extracted with the updater's ZIP reader and then imported with the
 * very same find_asset_source + install_asset_tree_atomic path as a folder
 * the player selected, so both ways end with identical, verified files.
 */

inline constexpr std::string_view retail_download_page = "https://www.aosplay.net/download#game-files";

/** Shown instead of a download button when the manifest has no retail_assets. */
[[nodiscard]] std::string download_unavailable_message();
/** Shown when the manifest could not be fetched at all. */
[[nodiscard]] std::string download_offline_message(std::string_view reason);

struct RetailOffer final {
    bool manifest_reachable{};
    std::optional<updater::ComponentRelease> release{};
    std::string error{};   ///< why the manifest could not be read
    /** The offer comes from the saved copy because the server did not answer. */
    bool from_saved_copy{};
    /** The fetched stable.json text, for the saved copy. */
    std::string manifest_json{};

    /** True only when a "Download game assets" button may be shown. */
    [[nodiscard]] bool available() const noexcept { return manifest_reachable && release.has_value(); }
};

/** Decides from stable.json text whether the download can be offered. */
[[nodiscard]] RetailOffer retail_offer_from_manifest(std::string_view manifest_json);

/** One HTTP(S) transfer. `status` is the final HTTP status (0: no response). */
struct TransferResult final {
    long status{};
    bool cancelled{};
    std::string error{};
};

/**
 * Transport used by the download. The default is libcurl; tests replace it.
 *
 * fetch_text: GET into memory (at most `limit` bytes).
 * fetch_range: GET from byte `offset` (a Range request when non-zero). The
 *   sink receives the response status with every chunk and returns false to
 *   abort; `progress` gets (received in this transfer, expected in this
 *   transfer or 0) and returns false to cancel.
 */
struct RetailTransport final {
    std::function<TransferResult(const std::string& url, std::size_t limit, std::string& body)> fetch_text{};
    std::function<TransferResult(const std::string& url,
                                 std::uint64_t offset,
                                 const std::function<bool(long status, const char* data, std::size_t size)>& sink,
                                 const std::function<bool(std::uint64_t, std::uint64_t)>& progress)>
        fetch_range{};
};

/** libcurl transport (https; plain http only to 127.0.0.1/localhost). */
[[nodiscard]] RetailTransport curl_retail_transport(std::chrono::milliseconds connect_timeout =
                                                        std::chrono::seconds{15});

/** Fetches `manifest_url` and decides whether the download can be offered. */
[[nodiscard]] RetailOffer fetch_retail_offer(const RetailTransport& transport, const std::string& manifest_url);
/**
 * As above, and keeps `saved_copy` up to date: a successful fetch replaces it,
 * and when every location fails the offer is read from it instead.
 */
[[nodiscard]] RetailOffer fetch_retail_offer(const RetailTransport& transport, const std::string& manifest_url,
                                             const std::filesystem::path& saved_copy);

/**
 * The manifest URL: updater.json beside the installer when present
 * ("manifest_url" or "channel"), otherwise https://www.aosplay.net/updates/stable.json.
 */
[[nodiscard]] std::string default_release_manifest_url(const std::filesystem::path& executable_directory);

enum class RetailStage : std::uint8_t {
    downloading,
    verifying,
    extracting,
    importing,
};

/** (stage, done, total) for the progress UI; false cancels. */
using RetailProgress = std::function<bool(RetailStage stage, std::uint64_t done, std::uint64_t total)>;

enum class RetailInstallStatus : std::uint8_t {
    installed,
    cancelled,
    failed,
};

struct RetailInstallResult final {
    RetailInstallStatus status{RetailInstallStatus::failed};
    std::string error{};   ///< player-facing; empty on success

    [[nodiscard]] explicit operator bool() const noexcept { return status == RetailInstallStatus::installed; }
};

struct RetailInstallRequest final {
    updater::ComponentRelease release{};
    const AssetManifest* catalog{};             ///< asset-manifest.json (required files)
    std::filesystem::path destination{};        ///< e.g. <exe>/assets/original
    std::filesystem::path cache_directory{};    ///< download + extraction scratch (kept for resume)
    std::filesystem::path executable_directory{};   ///< for the optional Steam runtime import
};

/**
 * Downloads (mirror by mirror, resuming), verifies, extracts and imports the
 * retail_assets package. The verified archive and any partial download stay
 * in `cache_directory` on failure so a retry continues; everything there is
 * removed after a successful import.
 */
[[nodiscard]] RetailInstallResult download_and_install_retail_assets(const RetailInstallRequest& request,
                                                                     const RetailTransport& transport,
                                                                     const RetailProgress& progress = {});

} // namespace battlespades::assets
