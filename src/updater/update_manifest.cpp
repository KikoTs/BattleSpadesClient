#include "battlespades/updater/update_manifest.hpp"

#include "battlespades/updater/semver.hpp"
#include "battlespades/updater/sha256.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

namespace battlespades::updater {
namespace {

[[nodiscard]] bool starts_with_icase(std::string_view text, std::string_view prefix) noexcept {
    if (text.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(text[i])) != std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

/// Relative, inside its base, never the updater's own folder.
[[nodiscard]] bool safe_relative(std::string_view path, bool allow_empty) {
    if (path.empty()) return allow_empty;
    if (path.front() == '/' || path.front() == '\\' || path.find(':') != std::string_view::npos) return false;
    std::size_t start{};
    bool first = true;
    while (start <= path.size()) {
        auto end = path.find_first_of("/\\", start);
        if (end == std::string_view::npos) end = path.size();
        const auto part = path.substr(start, end - start);
        if (part == ".." || part == ".") return false;
        if (first) {
            std::string lower{part};
            for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lower == "update") return false;
            first = false;
        }
        start = end + 1U;
    }
    return true;
}

/// A path inside a ZIP: relative, no "..", no drive.
[[nodiscard]] bool safe_archive_path(std::string_view path) {
    if (path.empty()) return true;
    if (path.front() == '/' || path.front() == '\\' || path.find(':') != std::string_view::npos) return false;
    std::size_t start{};
    while (start <= path.size()) {
        auto end = path.find_first_of("/\\", start);
        if (end == std::string_view::npos) end = path.size();
        if (path.substr(start, end - start) == "..") return false;
        start = end + 1U;
    }
    return true;
}

/// `path` equals `folder` or lies below it (case-insensitive, '/' separated).
[[nodiscard]] bool within(std::string_view path, std::string_view folder) {
    if (folder.empty() || path.size() < folder.size()) return false;
    for (std::size_t i = 0; i < folder.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(path[i])) != std::tolower(static_cast<unsigned char>(folder[i]))) {
            return false;
        }
    }
    return path.size() == folder.size() || path[folder.size()] == '/';
}

[[nodiscard]] bool valid_component_name(std::string_view name) {
    return !name.empty() && name.size() <= 32U && std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

[[nodiscard]] std::vector<std::string> path_list(const nlohmann::json& json, const char* key, std::string& error) {
    std::vector<std::string> values;
    if (!json.contains(key)) return values;
    for (const auto& item : json.at(key)) {
        auto value = item.get<std::string>();
        if (!safe_relative(value, false)) {
            error = std::string{key} + " entry is not a safe relative path: " + value;
            return {};
        }
        values.push_back(std::move(value));
    }
    return values;
}

struct Comparator {
    int op{};   // 0 '=', 1 '>', 2 '>=', 3 '<', 4 '<='
    Version version;
};

[[nodiscard]] std::optional<std::vector<Comparator>> parse_comparators(std::string_view constraint) {
    std::vector<Comparator> comparators;
    std::size_t start{};
    while (start < constraint.size()) {
        while (start < constraint.size() && (constraint[start] == ' ' || constraint[start] == ',')) ++start;
        if (start >= constraint.size()) break;
        auto end = constraint.find_first_of(" ,", start);
        if (end == std::string_view::npos) end = constraint.size();
        auto token = constraint.substr(start, end - start);
        start = end;
        Comparator comparator;
        if (token.starts_with(">=")) { comparator.op = 2; token.remove_prefix(2U); }
        else if (token.starts_with("<=")) { comparator.op = 4; token.remove_prefix(2U); }
        else if (token.starts_with("==")) { comparator.op = 0; token.remove_prefix(2U); }
        else if (token.starts_with(">")) { comparator.op = 1; token.remove_prefix(1U); }
        else if (token.starts_with("<")) { comparator.op = 3; token.remove_prefix(1U); }
        else if (token.starts_with("=")) { comparator.op = 0; token.remove_prefix(1U); }
        const auto version = parse_version(token);
        if (!version.has_value()) return std::nullopt;
        comparator.version = *version;
        comparators.push_back(std::move(comparator));
    }
    return comparators;
}

} // namespace

const ComponentRelease* UpdateManifest::component(std::string_view name) const noexcept {
    for (const auto& item : components) {
        if (item.name == name) return &item;
    }
    return nullptr;
}

bool acceptable_url(std::string_view url) noexcept {
    if (starts_with_icase(url, "https://")) return true;
    if (!starts_with_icase(url, "http://")) return false;
    const auto authority = url.substr(7U, url.find_first_of("/?#", 7U) - 7U);
    const auto colon = authority.find(':');
    const auto host = authority.substr(0U, colon);
    // Match the complete authority: localhost.evil.example and
    // 127.0.0.1@evil.example are not loopback HTTP test mirrors.
    if (host != "127.0.0.1" && !(host.size() == 9U && starts_with_icase(host, "localhost"))) return false;
    if (colon == std::string_view::npos) return true;
    const auto port = authority.substr(colon + 1U);
    if (port.empty() || port.size() > 5U) return false;
    unsigned number{};
    for (const char digit : port) {
        if (digit < '0' || digit > '9') return false;
        number = number * 10U + static_cast<unsigned>(digit - '0');
    }
    return number > 0U && number <= 65535U;
}

std::string default_component_target(std::string_view component) {
    if (component == component_server) return "server";
    if (component == component_retail_assets) return "assets/original";
    if (component == component_assets) return "assets/client";
    return {};
}

bool valid_constraint(std::string_view constraint) { return parse_comparators(constraint).has_value(); }

bool satisfies_constraint(std::string_view version, std::string_view constraint) {
    const auto comparators = parse_comparators(constraint);
    const auto parsed = parse_version(version);
    if (!comparators.has_value() || !parsed.has_value()) return false;
    for (const auto& comparator : *comparators) {
        const int order = compare_versions(*parsed, comparator.version);
        const bool ok = comparator.op == 0   ? order == 0
                        : comparator.op == 1 ? order > 0
                        : comparator.op == 2 ? order >= 0
                        : comparator.op == 3 ? order < 0
                                             : order <= 0;
        if (!ok) return false;
    }
    return true;
}

std::optional<UpdateManifest> parse_update_manifest(std::string_view json, std::string& error) {
    try {
        const auto parsed = nlohmann::json::parse(json);
        UpdateManifest manifest;
        manifest.schema = parsed.at("schema").get<int>();
        if (manifest.schema != 2) {
            error = "unsupported update manifest schema " + std::to_string(manifest.schema);
            return std::nullopt;
        }
        manifest.product = parsed.at("product").get<std::string>();
        if (manifest.product != "BattleSpades") {
            error = "update manifest is for '" + manifest.product + "', not BattleSpades";
            return std::nullopt;
        }
        manifest.channel = parsed.value("channel", std::string{});
        manifest.published = parsed.value("published", std::string{});
        manifest.notes_url = parsed.value("notes_url", std::string{});
        manifest.required = parsed.value("required", false);
        const auto& components = parsed.at("components");
        if (!components.is_object()) {
            error = "components must be an object keyed by component name";
            return std::nullopt;
        }
        for (const auto& [name, entry] : components.items()) {
            if (!valid_component_name(name)) {
                error = "invalid component name '" + name + "'";
                return std::nullopt;
            }
            ComponentRelease release;
            release.name = name;
            release.version = entry.at("version").get<std::string>();
            if (!parse_version(release.version).has_value()) {
                error = name + ": version is not semver: " + release.version;
                return std::nullopt;
            }
            release.package = entry.at("package").get<std::string>();
            if (release.package.empty() || release.package.find_first_of("/\\:") != std::string::npos) {
                error = name + ": package must be a plain file name";
                return std::nullopt;
            }
            for (const auto& url : entry.at("urls")) release.urls.push_back(url.get<std::string>());
            if (release.urls.empty()) {
                error = name + ": urls must list at least one mirror";
                return std::nullopt;
            }
            for (const auto& url : release.urls) {
                if (!acceptable_url(url)) {
                    error = name + ": mirror URL must use https: " + url;
                    return std::nullopt;
                }
            }
            release.size = entry.at("size").get<std::uint64_t>();
            if (release.size == 0U) {
                error = name + ": size must be the package size in bytes";
                return std::nullopt;
            }
            release.sha256 = entry.at("sha256").get<std::string>();
            if (!is_sha256_hex(release.sha256)) {
                error = name + ": sha256 must be 64 hex digits";
                return std::nullopt;
            }
            for (auto& c : release.sha256) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            release.root = entry.value("root", std::string{});
            if (!safe_archive_path(release.root)) {
                error = name + ": root must be a relative path inside the archive";
                return std::nullopt;
            }
            release.target = entry.contains("target") ? entry.at("target").get<std::string>()
                                                      : default_component_target(name);
            if (!safe_relative(release.target, true)) {
                error = name + ": target must be a relative folder outside update/";
                return std::nullopt;
            }
            release.preserve = path_list(entry, "preserve", error);
            if (!error.empty()) return std::nullopt;
            release.mirror_directories = path_list(entry, "mirror_directories", error);
            if (!error.empty()) return std::nullopt;
            release.remove = path_list(entry, "remove", error);
            if (!error.empty()) return std::nullopt;
            release.required = entry.value("required", false);
            if (entry.contains("protocol")) release.protocol = entry.at("protocol").get<std::uint32_t>();
            // Components roll out independently: the only pairing is the
            // minimum server a client needs for hosting. Legacy schema-2
            // files spelled it "requires": {"server": ...}.
            std::string hosting = entry.value("hosting_server", std::string{});
            if (hosting.empty() && entry.contains("requires") && entry.at("requires").contains("server")) {
                hosting = entry.at("requires").at("server").get<std::string>();
            }
            if (!hosting.empty() && !valid_constraint(hosting)) {
                error = name + ": invalid hosting_server constraint " + hosting;
                return std::nullopt;
            }
            release.hosting_server = hosting;
            manifest.components.push_back(std::move(release));
        }
        if (manifest.components.empty()) {
            error = "update manifest lists no components";
            return std::nullopt;
        }
        // Two components may not own the same folder, and a component may not
        // mirror (and so prune) a folder that belongs to another one.
        for (const auto& a : manifest.components) {
            for (const auto& b : manifest.components) {
                if (&a == &b) continue;
                if (a.target == b.target || (!a.target.empty() && within(b.target, a.target))) {
                    error = "components '" + a.name + "' and '" + b.name + "' overlap at '" + b.target + "'";
                    return std::nullopt;
                }
                for (const auto& mirror : a.mirror_directories) {
                    const auto mirrored = a.target.empty() ? mirror : a.target + "/" + mirror;
                    if (within(b.target, mirrored)) {
                        error = a.name + " mirrors '" + mirrored + "', which contains component " + b.name;
                        return std::nullopt;
                    }
                }
            }
        }
        return manifest;
    } catch (const std::exception& exception) {
        error = std::string{"update manifest JSON: "} + exception.what();
        return std::nullopt;
    }
}

} // namespace battlespades::updater
