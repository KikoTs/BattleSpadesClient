#include "battlespades/assets/retail_download.hpp"

#include "battlespades/updater/file_util.hpp"
#include "battlespades/updater/update_apply.hpp"
#include "battlespades/updater/update_plan.hpp"
#include "battlespades/updater/updater_config.hpp"
#include "battlespades/updater/zip_extract.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <mutex>
#include <sstream>
#include <utility>

namespace battlespades::assets {
namespace {

namespace fs = std::filesystem;

constexpr std::size_t maximum_manifest_bytes{1024U * 1024U};
constexpr int download_rounds{2};

[[nodiscard]] bool curl_ready() {
    static std::once_flag once;
    static CURLcode initialization{CURLE_FAILED_INIT};
    std::call_once(once, [] { initialization = curl_global_init(CURL_GLOBAL_DEFAULT); });
    return initialization == CURLE_OK;
}

struct CurlHandle final {
    CURL* value{curl_easy_init()};
    CurlHandle() = default;
    CurlHandle(const CurlHandle&) = delete;
    CurlHandle& operator=(const CurlHandle&) = delete;
    ~CurlHandle() {
        if (value != nullptr) curl_easy_cleanup(value);
    }
};

#if !defined(_WIN32)
/// A statically linked OpenSSL looks for CA certificates where it was built
/// (the CI machine). Point it at the system bundle instead.
[[nodiscard]] const char* system_ca_bundle() {
    static const std::string bundle = [] {
        const auto* info = curl_version_info(CURLVERSION_NOW);
        const std::string ssl = info != nullptr && info->ssl_version != nullptr ? info->ssl_version : "";
        if (ssl.find("OpenSSL") == std::string::npos && ssl.find("LibreSSL") == std::string::npos &&
            ssl.find("BoringSSL") == std::string::npos && ssl.find("quictls") == std::string::npos) {
            return std::string{};
        }
        constexpr std::array candidates{
            "/etc/ssl/certs/ca-certificates.crt",   // Debian, Ubuntu, Arch, Gentoo
            "/etc/pki/tls/certs/ca-bundle.crt",     // Fedora, RHEL
            "/etc/ssl/ca-bundle.pem",               // openSUSE
            "/etc/pki/tls/cacert.pem",              // OpenELEC
            "/etc/ssl/cert.pem",                    // macOS, Alpine
            "/usr/local/etc/openssl/cert.pem",      // Homebrew
        };
        for (const auto* candidate : candidates) {
            std::error_code code;
            if (fs::is_regular_file(candidate, code) && !code) return std::string{candidate};
        }
        return std::string{};
    }();
    return bundle.empty() ? nullptr : bundle.c_str();
}
#endif

void configure_common(CURL* handle, const std::string& url, std::chrono::milliseconds connect_timeout) {
    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
    // URLs were checked with updater::acceptable_url (https, or http to
    // 127.0.0.1/localhost for local test mirrors). Redirects: https only.
    curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle, CURLOPT_USERAGENT, "BattleSpadesAssetInstaller/" AOS_VERSION_STRING);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(connect_timeout.count()));
    // A stalled transfer (no byte for a minute) fails; a slow one does not.
    curl_easy_setopt(handle, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(handle, CURLOPT_LOW_SPEED_TIME, 60L);
#if defined(_WIN32)
    curl_easy_setopt(handle, CURLOPT_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_NATIVE_CA));
#else
    if (const auto* bundle = system_ca_bundle(); bundle != nullptr) {
        curl_easy_setopt(handle, CURLOPT_CAINFO, bundle);
    }
#endif
}

struct TextSink final {
    std::string* body{};
    std::size_t limit{};
    bool overflow{};
};

std::size_t write_text(char* data, std::size_t size, std::size_t count, void* user) {
    auto& sink = *static_cast<TextSink*>(user);
    const auto bytes = size * count;
    if (sink.body->size() + bytes > sink.limit) {
        sink.overflow = true;
        return 0U;
    }
    sink.body->append(data, bytes);
    return bytes;
}

struct RangeContext final {
    CURL* handle{};
    const std::function<bool(long, const char*, std::size_t)>* sink{};
    const std::function<bool(std::uint64_t, std::uint64_t)>* progress{};
    bool sink_aborted{};
    bool progress_cancelled{};
};

std::size_t write_range(char* data, std::size_t size, std::size_t count, void* user) {
    auto& context = *static_cast<RangeContext*>(user);
    const auto bytes = size * count;
    long status{};
    curl_easy_getinfo(context.handle, CURLINFO_RESPONSE_CODE, &status);
    if (!(*context.sink)(status, data, bytes)) {
        context.sink_aborted = true;
        return 0U;
    }
    return bytes;
}

int transfer_progress(void* user, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
    auto& context = *static_cast<RangeContext*>(user);
    if (context.progress != nullptr && *context.progress &&
        !(*context.progress)(static_cast<std::uint64_t>(now < 0 ? 0 : now),
                             static_cast<std::uint64_t>(total < 0 ? 0 : total))) {
        context.progress_cancelled = true;
        return 1;
    }
    return 0;
}

[[nodiscard]] std::string http_problem(const TransferResult& result) {
    if (!result.error.empty()) return result.error;
    return "HTTP " + std::to_string(result.status);
}

enum class FetchOutcome { success, failed, cancelled };

/// One mirror: download (resuming the partial file), then size + SHA-256.
FetchOutcome fetch_package(const std::string& url,
                           const fs::path& archive,
                           const updater::ComponentRelease& release,
                           const RetailTransport& transport,
                           const RetailProgress& progress,
                           bool& made_progress,
                           std::string& error) {
    auto partial = archive;
    partial += ".partial";
    std::error_code code;
    std::uint64_t offset{};
    if (fs::is_regular_file(partial, code)) {
        offset = fs::file_size(partial, code);
        if (code || offset > release.size) {
            fs::remove(partial, code);
            offset = 0U;
        }
    }

    if (offset < release.size) {
        std::ofstream output(partial, std::ios::binary | (offset > 0U ? std::ios::app : std::ios::trunc));
        if (!output) {
            error = "cannot write " + updater::path_to_utf8(partial);
            return FetchOutcome::failed;
        }
        std::uint64_t written = offset;
        std::uint64_t base = offset;
        long first_status{};
        bool bad_status{};
        bool too_large{};
        bool write_failed{};
        const std::function<bool(long, const char*, std::size_t)> sink =
            [&](long status, const char* data, std::size_t size) {
                if (first_status == 0) {
                    first_status = status;
                    if (status == 200 && offset > 0U) {
                        // The server ignored the Range request: start over.
                        output.close();
                        output.open(partial, std::ios::binary | std::ios::trunc);
                        written = 0U;
                        base = 0U;
                    } else if (status != 200 && status != 206) {
                        bad_status = true;
                        return false;
                    }
                }
                if (written + size > release.size) {
                    too_large = true;
                    return false;
                }
                output.write(data, static_cast<std::streamsize>(size));
                if (!output) {
                    write_failed = true;
                    return false;
                }
                written += size;
                made_progress = true;
                return true;
            };
        const std::function<bool(std::uint64_t, std::uint64_t)> on_progress = [&](std::uint64_t now, std::uint64_t) {
            return !progress || progress(RetailStage::downloading, (std::min)(base + now, release.size), release.size);
        };
        const auto result = transport.fetch_range(url, offset, sink, on_progress);
        output.close();
        if (result.cancelled) {
            return FetchOutcome::cancelled;
        }
        if (result.status == 416) {
            // Range not satisfiable: the partial file is not a prefix of the
            // package on this mirror. Start over on the next attempt.
            fs::remove(partial, code);
            error = "the server could not resume the download (HTTP 416)";
            return FetchOutcome::failed;
        }
        if (bad_status || (result.error.empty() && result.status != 200 && result.status != 206)) {
            error = "HTTP " + std::to_string(first_status != 0 ? first_status : result.status);
            return FetchOutcome::failed;
        }
        if (too_large) {
            fs::remove(partial, code);
            error = "the server sent more than the expected " + std::to_string(release.size) + " bytes";
            return FetchOutcome::failed;
        }
        if (write_failed) {
            error = "cannot write " + updater::path_to_utf8(partial) + " (is the disk full?)";
            return FetchOutcome::failed;
        }
        if (!result.error.empty()) {
            // Interrupted: keep the partial file, the next attempt resumes it.
            error = result.error;
            return FetchOutcome::failed;
        }
    }

    if (progress && !progress(RetailStage::verifying, 0U, release.size)) return FetchOutcome::cancelled;
    if (!updater::verify_package_file(partial, release.size, release.sha256, error)) {
        fs::remove(partial, code);
        return FetchOutcome::failed;
    }
    fs::remove(archive, code);
    fs::rename(partial, archive, code);
    if (code) {
        error = "cannot finish the download: " + code.message();
        return FetchOutcome::failed;
    }
    return FetchOutcome::success;
}

[[nodiscard]] RetailInstallResult failed(std::string message) {
    return {RetailInstallStatus::failed, std::move(message)};
}

} // namespace

std::string download_unavailable_message() {
    return std::string{"Automatic download isn't available yet \xE2\x80\x94 choose your Ace of Spades folder.\n\n"
                       "How to get the game files: "} +
           std::string{retail_download_page};
}

std::string download_offline_message(std::string_view reason) {
    std::string message =
        "The BattleSpades download server could not be reached, so the game files cannot be downloaded "
        "right now. Some internet providers block it (for example in Russia): turn on a VPN and try again, "
        "or choose your Ace of Spades folder.\n\nHelp: " +
        std::string{retail_download_page};
    if (!reason.empty()) {
        message += "\n\n(";
        message += reason;
        message += ')';
    }
    return message;
}

RetailOffer retail_offer_from_manifest(std::string_view manifest_json) {
    RetailOffer offer;
    offer.manifest_reachable = true;
    std::string error;
    const auto manifest = updater::parse_update_manifest(manifest_json, error);
    if (!manifest.has_value()) {
        offer.error = "invalid release manifest: " + error;
        return offer;
    }
    if (const auto* release = manifest->component(updater::component_retail_assets); release != nullptr) {
        offer.release = *release;
    }
    return offer;
}

RetailTransport curl_retail_transport(std::chrono::milliseconds connect_timeout) {
    RetailTransport transport;
    transport.fetch_text = [connect_timeout](const std::string& url, std::size_t limit, std::string& body) {
        TransferResult result;
        body.clear();
        if (!curl_ready()) {
            result.error = "the HTTP library could not be initialised";
            return result;
        }
        CurlHandle handle;
        if (handle.value == nullptr) {
            result.error = "cannot create an HTTP request";
            return result;
        }
        TextSink sink{&body, limit, false};
        configure_common(handle.value, url, connect_timeout);
        curl_easy_setopt(handle.value, CURLOPT_TIMEOUT_MS, 30000L);
        curl_slist* headers = curl_slist_append(nullptr, "Cache-Control: no-cache");
        curl_easy_setopt(handle.value, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(handle.value, CURLOPT_WRITEFUNCTION, &write_text);
        curl_easy_setopt(handle.value, CURLOPT_WRITEDATA, &sink);
        const auto performed = curl_easy_perform(handle.value);
        curl_easy_getinfo(handle.value, CURLINFO_RESPONSE_CODE, &result.status);
        curl_slist_free_all(headers);
        if (performed != CURLE_OK) {
            result.error = sink.overflow ? std::string{"the response is too large"}
                                         : std::string{curl_easy_strerror(performed)};
        }
        return result;
    };
    transport.fetch_range = [connect_timeout](const std::string& url,
                                              std::uint64_t offset,
                                              const std::function<bool(long, const char*, std::size_t)>& sink,
                                              const std::function<bool(std::uint64_t, std::uint64_t)>& progress) {
        TransferResult result;
        if (!curl_ready()) {
            result.error = "the HTTP library could not be initialised";
            return result;
        }
        CurlHandle handle;
        if (handle.value == nullptr) {
            result.error = "cannot create an HTTP request";
            return result;
        }
        RangeContext context{handle.value, &sink, &progress, false, false};
        configure_common(handle.value, url, connect_timeout);
        // A manual Range (not CURLOPT_RESUME_FROM): a server that ignores it
        // answers 200 and the sink restarts the file instead of failing.
        const auto range = std::to_string(offset) + "-";
        if (offset > 0U) curl_easy_setopt(handle.value, CURLOPT_RANGE, range.c_str());
        curl_easy_setopt(handle.value, CURLOPT_WRITEFUNCTION, &write_range);
        curl_easy_setopt(handle.value, CURLOPT_WRITEDATA, &context);
        curl_easy_setopt(handle.value, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(handle.value, CURLOPT_XFERINFOFUNCTION, &transfer_progress);
        curl_easy_setopt(handle.value, CURLOPT_XFERINFODATA, &context);
        const auto performed = curl_easy_perform(handle.value);
        curl_easy_getinfo(handle.value, CURLINFO_RESPONSE_CODE, &result.status);
        if (context.progress_cancelled) {
            result.cancelled = true;
        } else if (performed != CURLE_OK && !context.sink_aborted) {
            result.error = curl_easy_strerror(performed);
        }
        return result;
    };
    return transport;
}

RetailOffer fetch_retail_offer(const RetailTransport& transport, const std::string& manifest_url) {
    RetailOffer offer;
    if (!updater::acceptable_url(manifest_url)) {
        offer.error = "the release manifest URL must use https: " + manifest_url;
        return offer;
    }
    if (!transport.fetch_text) {
        offer.error = "no HTTP transport";
        return offer;
    }
    // aosplay.net first, then the GitHub copy: in some countries the site's
    // host is blocked while GitHub still answers.
    for (const auto& location : updater::manifest_locations(manifest_url)) {
        std::string body;
        const auto result = transport.fetch_text(location, maximum_manifest_bytes, body);
        if (result.error.empty() && result.status == 200) {
            auto fetched = retail_offer_from_manifest(body);
            fetched.manifest_json = std::move(body);
            return fetched;
        }
        offer.error += (offer.error.empty() ? "" : "; ") + location + ": " + http_problem(result);
    }
    return offer;
}

RetailOffer fetch_retail_offer(const RetailTransport& transport, const std::string& manifest_url,
                               const std::filesystem::path& saved_copy) {
    auto offer = fetch_retail_offer(transport, manifest_url);
    std::string ignored;
    if (offer.manifest_reachable) {
        if (offer.release.has_value() && !offer.manifest_json.empty()) {
            static_cast<void>(updater::write_file_atomic(saved_copy, offer.manifest_json, ignored));
        }
        return offer;
    }
    const auto saved = updater::read_text_file(saved_copy, ignored);
    if (!saved.has_value()) return offer;
    auto fallback = retail_offer_from_manifest(*saved);
    if (!fallback.available()) return offer;
    fallback.from_saved_copy = true;
    fallback.error = offer.error;
    return fallback;
}

std::string default_release_manifest_url(const std::filesystem::path& executable_directory) {
    std::error_code code;
    const auto config_file = executable_directory / "updater.json";
    if (fs::is_regular_file(config_file, code) && !code) {
        return updater::manifest_endpoint(updater::load_updater_config(config_file));
    }
    return std::string{updater::default_manifest_url};
}

RetailInstallResult download_and_install_retail_assets(const RetailInstallRequest& request,
                                                       const RetailTransport& transport,
                                                       const RetailProgress& progress) {
    try {
        const auto& release = request.release;
        if (request.catalog == nullptr || release.urls.empty() || release.size == 0U || release.package.empty() ||
            request.destination.empty() || request.cache_directory.empty() || !transport.fetch_range) {
            return failed("the game-file download is not configured correctly");
        }
        std::error_code code;
        fs::create_directories(request.cache_directory, code);
        if (code) {
            return failed("cannot create " + updater::path_to_utf8(request.cache_directory) + ": " + code.message() +
                          "\n\nMake sure the BattleSpades folder is writable and the drive has free space.");
        }
        const auto archive = request.cache_directory / updater::path_from_utf8(release.package);

        // 1. Download (or reuse an already verified archive).
        std::string ignored;
        if (!updater::verify_package_file(archive, release.size, release.sha256, ignored)) {
            fs::remove(archive, code);
            updater::MirrorResult mirrors;
            for (int round = 0; round < download_rounds; ++round) {
                bool made_progress{};
                mirrors = updater::try_mirrors(release.urls, [&](const std::string& url, std::string& error) {
                    if (!updater::acceptable_url(url)) {
                        error = "not an https URL";
                        return updater::MirrorOutcome::failed;
                    }
                    switch (fetch_package(url, archive, release, transport, progress, made_progress, error)) {
                    case FetchOutcome::success:
                        return updater::MirrorOutcome::success;
                    case FetchOutcome::cancelled:
                        return updater::MirrorOutcome::cancelled;
                    case FetchOutcome::failed:
                        break;
                    }
                    return updater::MirrorOutcome::failed;
                });
                // An interrupted transfer that got somewhere is resumed once
                // more right away; anything else is the player's call.
                if (mirrors.outcome != updater::MirrorOutcome::failed || !made_progress) break;
            }
            if (mirrors.outcome == updater::MirrorOutcome::cancelled) {
                return {RetailInstallStatus::cancelled,
                        "The download was stopped. It continues where it stopped when you try again."};
            }
            if (mirrors.outcome != updater::MirrorOutcome::success) {
                std::string message = "The game files could not be downloaded:";
                for (const auto& problem : mirrors.errors) message += "\n  " + problem;
                message +=
                    "\n\nCheck your internet connection and try again; the download continues where it stopped.";
                return failed(std::move(message));
            }
        }

        // 2. Extract next to the cache (same volume as the destination).
        const auto extracted = request.cache_directory / "extracted";
        fs::remove_all(extracted, code);
        std::string extract_error;
        const bool unpacked = updater::extract_zip_archive(
            archive, extracted, extract_error, [&](std::uint64_t done, std::uint64_t total) {
                return !progress || progress(RetailStage::extracting, done, total);
            });
        if (!unpacked) {
            fs::remove_all(extracted, code);
            if (extract_error == "cancelled") {
                return {RetailInstallStatus::cancelled, "The import was stopped. Try again to continue."};
            }
            return failed("The downloaded game files could not be unpacked: " + extract_error +
                          "\n\nMake sure the drive has about 1 GB free, then try again.");
        }
        const auto root = updater::find_package_root(extracted, release.root, "");
        if (!root.has_value()) {
            fs::remove_all(request.cache_directory, code);
            return failed("The downloaded package contains no game files. Please report this at " +
                          std::string{retail_download_page} + ".");
        }

        // 3. The same validation and import as a folder chosen by the player.
        if (progress && !progress(RetailStage::importing, 0U, request.catalog->total_bytes)) {
            fs::remove_all(extracted, code);
            return {RetailInstallStatus::cancelled, "The import was stopped. Try again to continue."};
        }
        std::string discovery_error;
        const auto source =
            find_asset_source(*root, *request.catalog, discovery_error, AssetSourceOrigin::verified_package);
        if (!source.has_value()) {
            // A package that does not match this client will not get better
            // by downloading it again.
            fs::remove_all(request.cache_directory, code);
            return failed("The downloaded game files did not pass the asset check: " + discovery_error +
                          "\n\nThis package does not match this BattleSpades version. Update BattleSpades, or "
                          "choose your Ace of Spades folder instead.");
        }
        const auto installed = install_asset_tree_atomic(
            *source, request.destination, *request.catalog, [&](const AssetInstallProgress& step) {
                if (progress) static_cast<void>(progress(RetailStage::importing, step.bytes_completed, step.bytes_total));
            });
        if (!installed) {
            fs::remove_all(extracted, code);
            return failed(explain_asset_install_error(installed.error));
        }
        if (!request.executable_directory.empty()) {
            const auto steam = import_native_steam_runtime(*source, request.executable_directory);
            if (!steam) {
                fs::remove_all(extracted, code);
                return failed(explain_asset_install_error(steam.error));
            }
        }
        fs::remove_all(request.cache_directory, code);
        return {RetailInstallStatus::installed, {}};
    } catch (const std::exception& exception) {
        return failed(std::string{"The game-file download failed: "} + exception.what());
    } catch (...) {
        return failed("The game-file download failed.");
    }
}

} // namespace battlespades::assets
