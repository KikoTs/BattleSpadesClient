#include "battlespades/updater/updater_config.hpp"

#include "battlespades/updater/file_util.hpp"
#include "battlespades/updater/update_manifest.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace battlespades::updater {

bool valid_repository(std::string_view repository) noexcept {
    const auto slash = repository.find('/');
    if (slash == std::string_view::npos || slash == 0U || slash + 1U >= repository.size() ||
        repository.find('/', slash + 1U) != std::string_view::npos || repository.find("..") != std::string_view::npos) {
        return false;
    }
    return std::ranges::all_of(repository, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
               c == '.' || c == '/';
    });
}

bool valid_channel(std::string_view channel) noexcept {
    return !channel.empty() && channel.size() <= 32U && std::ranges::all_of(channel, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

UpdaterConfig parse_updater_config(std::string_view json, std::string& error) {
    UpdaterConfig config;
    try {
        const auto parsed = nlohmann::json::parse(json);
        config.enabled = parsed.value("enabled", config.enabled);
        const auto manifest_url = parsed.value("manifest_url", std::string{});
        if (manifest_url.empty() || acceptable_url(manifest_url)) {
            config.manifest_url = manifest_url;
        } else {
            error = "ignoring manifest_url: only https (or a localhost test server) is allowed";
        }
        const auto channel = parsed.value("channel", config.channel);
        if (valid_channel(channel)) {
            config.channel = channel;
        } else {
            error = "ignoring invalid channel '" + channel + "'";
        }
        const auto timeout = parsed.value("check_timeout_ms", config.check_timeout_ms);
        config.check_timeout_ms = std::clamp<std::uint32_t>(timeout, 1000U, 60000U);
        config.large_update_bytes = parsed.value("large_update_bytes", config.large_update_bytes);
        config.max_deferrals = std::min<std::uint32_t>(parsed.value("max_deferrals", config.max_deferrals), 100U);
        config.github_fallback = parsed.value("github_fallback", config.github_fallback);
        const auto repository = parsed.value("repository", config.repository);
        if (valid_repository(repository)) {
            config.repository = repository;
        } else {
            error = "ignoring invalid repository '" + repository + "'";
        }
        config.include_prereleases = parsed.value("include_prereleases", config.include_prereleases);
        const auto api_url = parsed.value("api_url", std::string{});
        if (api_url.empty() || acceptable_url(api_url)) {
            config.api_url = api_url;
        } else {
            error = "ignoring api_url: only https (or a localhost test server) is allowed";
        }
    } catch (const std::exception& exception) {
        error = std::string{"updater.json: "} + exception.what();
        return UpdaterConfig{};
    }
    return config;
}

UpdaterConfig load_updater_config(const std::filesystem::path& file) {
    std::string error;
    const auto text = read_text_file(file, error);
    if (!text.has_value()) return UpdaterConfig{};
    return parse_updater_config(*text, error);
}

std::string manifest_endpoint(const UpdaterConfig& config) {
    if (!config.manifest_url.empty()) return config.manifest_url;
    if (config.channel.empty() || config.channel == "stable") return std::string{default_manifest_url};
    return "https://www.aosplay.net/updates/" + config.channel + ".json";
}

std::vector<std::string> manifest_locations(const std::string& primary) {
    std::vector<std::string> locations{primary};
    if (primary == default_manifest_url) locations.emplace_back(default_manifest_mirror_url);
    return locations;
}

std::string release_endpoint(const UpdaterConfig& config) {
    if (!config.api_url.empty()) return config.api_url;
    const std::string base = "https://api.github.com/repos/" + config.repository + "/releases";
    return config.include_prereleases ? base + "?per_page=20" : base + "/latest";
}

} // namespace battlespades::updater
