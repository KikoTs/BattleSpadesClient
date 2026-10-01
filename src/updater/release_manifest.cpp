#include "battlespades/updater/release_manifest.hpp"

#include "battlespades/updater/semver.hpp"
#include "battlespades/updater/sha256.hpp"

#include <nlohmann/json.hpp>

#include <cctype>

namespace battlespades::updater {
namespace {

[[nodiscard]] bool iequals(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(left[i])) !=
            std::tolower(static_cast<unsigned char>(right[i]))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ends_with_icase(std::string_view text, std::string_view suffix) noexcept {
    return text.size() >= suffix.size() && iequals(text.substr(text.size() - suffix.size()), suffix);
}

[[nodiscard]] bool starts_with_icase(std::string_view text, std::string_view prefix) noexcept {
    return text.size() >= prefix.size() && iequals(text.substr(0U, prefix.size()), prefix);
}

/// Package bytes come from GitHub over TLS; plain http is allowed only for a
/// local test mirror, never for anything reachable over the network.
[[nodiscard]] bool acceptable_download_url(std::string_view url) noexcept {
    return starts_with_icase(url, "https://") || starts_with_icase(url, "http://127.0.0.1") ||
           starts_with_icase(url, "http://localhost");
}

/// A relative path that cannot escape its base ("a/b", never "../x" or "C:\").
[[nodiscard]] bool safe_relative_path(std::string_view path) {
    if (path.empty()) return true;
    if (path.front() == '/' || path.front() == '\\' || path.find(':') != std::string_view::npos) {
        return false;
    }
    std::size_t start{};
    while (start <= path.size()) {
        auto end = path.find_first_of("/\\", start);
        if (end == std::string_view::npos) end = path.size();
        const auto part = path.substr(start, end - start);
        if (part == "..") return false;
        start = end + 1U;
    }
    return true;
}

[[nodiscard]] std::vector<std::string> string_list(const nlohmann::json& json, const char* key,
                                                   bool paths, std::string& error) {
    std::vector<std::string> values;
    if (!json.contains(key)) return values;
    for (const auto& item : json.at(key)) {
        auto value = item.get<std::string>();
        if (paths && !safe_relative_path(value)) {
            error = std::string{key} + " entry is not a safe relative path: " + value;
            return {};
        }
        values.push_back(std::move(value));
    }
    return values;
}

[[nodiscard]] GitHubRelease release_from_json(const nlohmann::json& json) {
    GitHubRelease release;
    release.tag_name = json.value("tag_name", std::string{});
    if (json.contains("name") && json.at("name").is_string()) release.name = json.at("name").get<std::string>();
    release.html_url = json.value("html_url", std::string{});
    release.draft = json.value("draft", false);
    release.prerelease = json.value("prerelease", false);
    if (json.contains("assets") && json.at("assets").is_array()) {
        for (const auto& item : json.at("assets")) {
            GitHubAsset asset;
            asset.name = item.value("name", std::string{});
            asset.download_url = item.value("browser_download_url", std::string{});
            asset.size = item.value("size", std::uint64_t{0});
            if (!asset.name.empty()) release.assets.push_back(std::move(asset));
        }
    }
    return release;
}

} // namespace

const GitHubAsset* GitHubRelease::asset(std::string_view asset_name) const noexcept {
    for (const auto& item : assets) {
        if (iequals(item.name, asset_name)) return &item;
    }
    return nullptr;
}

std::optional<GitHubRelease> parse_github_release(std::string_view json, std::string& error) {
    try {
        const auto parsed = nlohmann::json::parse(json);
        if (!parsed.is_object() || !parsed.contains("tag_name")) {
            error = parsed.is_object() && parsed.contains("message")
                        ? "GitHub: " + parsed.at("message").get<std::string>()
                        : "GitHub response is not a release";
            return std::nullopt;
        }
        return release_from_json(parsed);
    } catch (const std::exception& exception) {
        error = std::string{"GitHub release JSON: "} + exception.what();
        return std::nullopt;
    }
}

std::optional<std::vector<GitHubRelease>> parse_github_release_list(std::string_view json,
                                                                    std::string& error) {
    try {
        const auto parsed = nlohmann::json::parse(json);
        if (!parsed.is_array()) {
            error = "GitHub response is not a release list";
            return std::nullopt;
        }
        std::vector<GitHubRelease> releases;
        for (const auto& item : parsed) {
            if (item.is_object() && item.contains("tag_name")) releases.push_back(release_from_json(item));
        }
        return releases;
    } catch (const std::exception& exception) {
        error = std::string{"GitHub release list JSON: "} + exception.what();
        return std::nullopt;
    }
}

std::optional<ReleaseManifest> parse_release_manifest(std::string_view json, std::string& error) {
    try {
        const auto parsed = nlohmann::json::parse(json);
        ReleaseManifest manifest;
        manifest.schema = parsed.at("schema").get<int>();
        if (manifest.schema != 1) {
            error = "unsupported release manifest schema " + std::to_string(manifest.schema);
            return std::nullopt;
        }
        manifest.product = parsed.at("product").get<std::string>();
        if (manifest.product != "BattleSpadesClient") {
            error = "release manifest is for '" + manifest.product + "', not BattleSpadesClient";
            return std::nullopt;
        }
        manifest.version = parsed.at("version").get<std::string>();
        if (!parse_version(manifest.version).has_value()) {
            error = "release manifest version is not semver: " + manifest.version;
            return std::nullopt;
        }
        manifest.channel = parsed.value("channel", std::string{});
        manifest.notes_url = parsed.value("notes_url", std::string{});
        if (parsed.contains("platforms")) {
            const auto& platforms = parsed.at("platforms");
            if (platforms.contains(windows_platform_key)) {
                const auto& entry = platforms.at(windows_platform_key);
                PackageInfo package;
                package.name = entry.at("package").get<std::string>();
                package.url = entry.value("url", std::string{});
                package.size = entry.value("size", std::uint64_t{0});
                package.sha256 = entry.at("sha256").get<std::string>();
                package.root = entry.value("root", std::string{});
                if (package.name.empty() || package.name.find_first_of("/\\") != std::string::npos) {
                    error = "package must be a plain release asset file name";
                    return std::nullopt;
                }
                if (!is_sha256_hex(package.sha256)) {
                    error = "package sha256 must be 64 hex digits";
                    return std::nullopt;
                }
                for (auto& c : package.sha256) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (!package.url.empty() && !starts_with_icase(package.url, "https://")) {
                    error = "package url must use https";
                    return std::nullopt;
                }
                if (!safe_relative_path(package.root)) {
                    error = "package root must be a relative path inside the archive";
                    return std::nullopt;
                }
                manifest.windows_x64 = std::move(package);
            }
        }
        manifest.preserve = string_list(parsed, "preserve", true, error);
        if (!error.empty()) return std::nullopt;
        manifest.mirror_directories = string_list(parsed, "mirror_directories", true, error);
        if (!error.empty()) return std::nullopt;
        manifest.remove = string_list(parsed, "remove", true, error);
        if (!error.empty()) return std::nullopt;
        return manifest;
    } catch (const std::exception& exception) {
        error = std::string{"release manifest JSON: "} + exception.what();
        return std::nullopt;
    }
}

std::optional<std::string> parse_sha256_file(std::string_view text, std::string_view expected_name) {
    std::size_t start{};
    while (start < text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        auto line = text.substr(start, end - start);
        start = end + 1U;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.remove_suffix(1U);
        }
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1U);
        if (line.size() < 64U) continue;
        const auto digest = line.substr(0U, 64U);
        if (!is_sha256_hex(digest)) continue;
        auto rest = line.substr(64U);
        while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) rest.remove_prefix(1U);
        if (!rest.empty() && rest.front() == '*') rest.remove_prefix(1U);
        // Bare digests are accepted; named lines must name our package.
        if (rest.empty() || expected_name.empty() || iequals(rest, expected_name)) {
            std::string value{digest};
            for (auto& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return value;
        }
    }
    return std::nullopt;
}

const GitHubAsset* find_windows_package_asset(const GitHubRelease& release) {
    for (const auto& asset : release.assets) {
        if (starts_with_icase(asset.name, "BattleSpadesClient-") &&
            (ends_with_icase(asset.name, "-Windows-AMD64.zip") ||
             ends_with_icase(asset.name, "-Windows-x64.zip"))) {
            return &asset;
        }
    }
    return nullptr;
}

ReleaseManifest default_manifest_for_checksum_release() {
    ReleaseManifest manifest;
    manifest.product = "BattleSpadesClient";
    manifest.preserve = {"ui-layout.json"};
    manifest.mirror_directories = {"shaders", "server/_internal"};
    return manifest;
}

std::string version_from_tag(std::string_view tag) {
    if (!tag.empty() && (tag.front() == 'v' || tag.front() == 'V')) tag.remove_prefix(1U);
    return std::string{tag};
}

ResolveStage plan_release(const GitHubRelease& release, std::string& asset_to_fetch, std::string& error) {
    if (const auto* manifest = release.asset(release_manifest_asset_name); manifest != nullptr) {
        asset_to_fetch = manifest->download_url;
        return ResolveStage::need_manifest;
    }
    const auto* package = find_windows_package_asset(release);
    if (package == nullptr) {
        error = "release " + release.tag_name + " has no Windows package";
        return ResolveStage::unusable;
    }
    const auto* checksum = release.asset(package->name + ".sha256");
    if (checksum == nullptr) {
        // Never install bytes we cannot verify.
        error = "release " + release.tag_name + " publishes no checksum for " + package->name;
        return ResolveStage::unusable;
    }
    asset_to_fetch = checksum->download_url;
    return ResolveStage::need_checksum;
}

std::optional<ResolvedUpdate> resolve_with_manifest(const GitHubRelease& release,
                                                    std::string_view manifest_json, std::string& error) {
    auto manifest = parse_release_manifest(manifest_json, error);
    if (!manifest.has_value()) return std::nullopt;
    if (!manifest->windows_x64.has_value()) {
        error = "release manifest has no " + std::string{windows_platform_key} + " package";
        return std::nullopt;
    }
    ResolvedUpdate update;
    update.version = manifest->version;
    update.package = *manifest->windows_x64;
    if (update.package.url.empty()) {
        const auto* asset = release.asset(update.package.name);
        if (asset == nullptr) {
            error = "release " + release.tag_name + " has no asset named " + update.package.name;
            return std::nullopt;
        }
        update.package.url = asset->download_url;
        if (update.package.size != 0U && asset->size != 0U && asset->size != update.package.size) {
            error = "manifest size of " + update.package.name + " does not match the release asset";
            return std::nullopt;
        }
        if (update.package.size == 0U) update.package.size = asset->size;
    }
    if (!acceptable_download_url(update.package.url)) {
        error = "package URL must use https: " + update.package.url;
        return std::nullopt;
    }
    update.manifest = std::move(*manifest);
    return update;
}

std::optional<ResolvedUpdate> resolve_with_checksum(const GitHubRelease& release,
                                                    std::string_view checksum_text, std::string& error) {
    const auto* package = find_windows_package_asset(release);
    if (package == nullptr) {
        error = "release " + release.tag_name + " has no Windows package";
        return std::nullopt;
    }
    const auto digest = parse_sha256_file(checksum_text, package->name);
    if (!digest.has_value()) {
        error = "checksum file does not contain a SHA-256 for " + package->name;
        return std::nullopt;
    }
    ResolvedUpdate update;
    update.version = version_from_tag(release.tag_name);
    if (!parse_version(update.version).has_value()) {
        error = "release tag is not a semver version: " + release.tag_name;
        return std::nullopt;
    }
    if (!acceptable_download_url(package->download_url)) {
        error = "package URL must use https: " + package->download_url;
        return std::nullopt;
    }
    update.package = PackageInfo{package->name, package->download_url, package->size, *digest, {}};
    update.manifest = default_manifest_for_checksum_release();
    update.manifest.version = update.version;
    return update;
}

const GitHubRelease* pick_newest_release(const std::vector<GitHubRelease>& releases,
                                         bool include_prereleases) {
    const GitHubRelease* best = nullptr;
    std::optional<Version> best_version;
    for (const auto& release : releases) {
        if (release.draft || (release.prerelease && !include_prereleases)) continue;
        const auto version = parse_version(release.tag_name);
        if (!version.has_value()) continue;
        if (best == nullptr || compare_versions(*version, *best_version) > 0) {
            best = &release;
            best_version = version;
        }
    }
    return best;
}

} // namespace battlespades::updater
