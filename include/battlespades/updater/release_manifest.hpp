#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::updater {

inline constexpr std::string_view release_manifest_asset_name = "battlespades-release.json";
inline constexpr std::string_view windows_platform_key = "windows-x64";

struct GitHubAsset {
    std::string name;
    std::string download_url;   ///< browser_download_url
    std::uint64_t size{};
};

struct GitHubRelease {
    std::string tag_name;
    std::string name;
    std::string html_url;
    bool draft{};
    bool prerelease{};
    std::vector<GitHubAsset> assets;

    [[nodiscard]] const GitHubAsset* asset(std::string_view asset_name) const noexcept;
};

/// GET /repos/{owner}/{repo}/releases/latest
[[nodiscard]] std::optional<GitHubRelease> parse_github_release(std::string_view json, std::string& error);
/// GET /repos/{owner}/{repo}/releases
[[nodiscard]] std::optional<std::vector<GitHubRelease>> parse_github_release_list(std::string_view json,
                                                                                  std::string& error);

struct PackageInfo {
    std::string name;            ///< release asset file name
    std::string url;             ///< explicit URL, else the asset's download URL
    std::uint64_t size{};        ///< 0 = unknown
    std::string sha256;          ///< lower-case hex
    std::string root;            ///< folder inside the zip that maps onto the install folder
};

/**
 * battlespades-release.json, schema 1. See docs/INSTALLER_AND_UPDATER.md.
 */
struct ReleaseManifest {
    int schema{1};
    std::string product;
    std::string version;
    std::string channel;
    std::string notes_url;
    std::optional<PackageInfo> windows_x64;
    std::vector<std::string> preserve;             ///< globs never overwritten when present
    std::vector<std::string> mirror_directories;   ///< folders made identical to the package
    std::vector<std::string> remove;               ///< files deleted from older installs
};

[[nodiscard]] std::optional<ReleaseManifest> parse_release_manifest(std::string_view json,
                                                                    std::string& error);

/// "<hex>  <name>", "<hex> *<name>" (sha256sum / CPack) or a bare digest.
[[nodiscard]] std::optional<std::string> parse_sha256_file(std::string_view text,
                                                           std::string_view expected_name);

/// BattleSpadesClient-<version>-Windows-AMD64.zip (the CPack ZIP name).
[[nodiscard]] const GitHubAsset* find_windows_package_asset(const GitHubRelease& release);

/// Defaults applied when a release carries no manifest (CPack ZIP + .sha256 only).
[[nodiscard]] ReleaseManifest default_manifest_for_checksum_release();

/// A concrete update: what to download, its digest and how to apply it.
struct ResolvedUpdate {
    std::string version;
    PackageInfo package;
    ReleaseManifest manifest;
};

enum class ResolveStage { need_manifest, need_checksum, resolved, unusable };

/**
 * Step one of resolving a release: decides which small asset must be
 * downloaded next. `asset_to_fetch` receives its URL.
 */
[[nodiscard]] ResolveStage plan_release(const GitHubRelease& release, std::string& asset_to_fetch,
                                        std::string& error);

[[nodiscard]] std::optional<ResolvedUpdate> resolve_with_manifest(const GitHubRelease& release,
                                                                  std::string_view manifest_json,
                                                                  std::string& error);
[[nodiscard]] std::optional<ResolvedUpdate> resolve_with_checksum(const GitHubRelease& release,
                                                                  std::string_view checksum_text,
                                                                  std::string& error);

/// Highest-versioned non-draft release (prereleases only when allowed).
[[nodiscard]] const GitHubRelease* pick_newest_release(const std::vector<GitHubRelease>& releases,
                                                       bool include_prereleases);

/// Tag without a leading 'v'.
[[nodiscard]] std::string version_from_tag(std::string_view tag);

} // namespace battlespades::updater
