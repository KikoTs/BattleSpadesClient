#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace battlespades::updater {

/**
 * updater.json beside the launcher. Every key is optional:
 *
 *   {
 *     "enabled": true,
 *     "manifest_url": "",                 // default https://www.aosplay.net/updates/<channel>.json
 *     "channel": "stable",                // e.g. "beta" -> .../updates/beta.json
 *     "check_timeout_ms": 6000,
 *     "large_update_bytes": 209715200,    // above this: "Update now / Later"
 *     "max_deferrals": 3,                 // Laters before a large update is mandatory
 *     "github_fallback": false,           // manifest unreachable -> old GitHub release check
 *     "repository": "KikoTs/BattleSpadesClient",
 *     "include_prereleases": false,
 *     "api_url": ""
 *   }
 */
struct UpdaterConfig {
    bool enabled{true};
    std::string manifest_url;
    std::string channel{"stable"};
    std::uint32_t check_timeout_ms{6000U};
    std::uint64_t large_update_bytes{200ULL * 1024ULL * 1024ULL};
    std::uint32_t max_deferrals{3U};
    bool github_fallback{};
    // Legacy GitHub release discovery (only used by the fallback).
    std::string repository{"KikoTs/BattleSpadesClient"};
    bool include_prereleases{};
    std::string api_url;
};

[[nodiscard]] UpdaterConfig parse_updater_config(std::string_view json, std::string& error);
[[nodiscard]] UpdaterConfig load_updater_config(const std::filesystem::path& file);
/// The stable.json URL: manifest_url, else the channel's file on aosplay.net.
[[nodiscard]] std::string manifest_endpoint(const UpdaterConfig& config);
/// The GitHub API URL of the legacy fallback.
[[nodiscard]] std::string release_endpoint(const UpdaterConfig& config);
/// "owner/name" with GitHub's allowed characters only.
[[nodiscard]] bool valid_repository(std::string_view repository) noexcept;
[[nodiscard]] bool valid_channel(std::string_view channel) noexcept;

} // namespace battlespades::updater
