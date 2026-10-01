#include "battlespades/updater/update_apply.hpp"

#include "battlespades/updater/file_util.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <deque>
#include <set>

namespace battlespades::updater {
namespace {

namespace fs = std::filesystem;

enum class OpKind { replace, add, remove };

struct Operation {
    OpKind kind{};
    std::string rel;      ///< install-relative
    std::string staged;   ///< stage-relative (replace/add)
    int progress{};       ///< 0 = nothing done, 1 = original moved away, 2 = complete
};

[[nodiscard]] std::string lower(std::string text) {
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

[[nodiscard]] fs::path rel_path(std::string_view rel) { return path_from_utf8(rel); }

[[nodiscard]] std::string join(std::string_view folder, std::string_view rel) {
    if (folder.empty()) return std::string{rel};
    if (rel.empty()) return std::string{folder};
    return std::string{folder} + "/" + std::string{rel};
}

[[nodiscard]] std::string normalize(std::string text) {
    for (auto& c : text) {
        if (c == '\\') c = '/';
    }
    while (!text.empty() && text.back() == '/') text.pop_back();
    return text;
}

/// `path` equals `folder` or lies below it (ASCII case-insensitive).
[[nodiscard]] bool within(std::string_view path, std::string_view folder) {
    if (folder.empty()) return true;
    const auto p = lower(std::string{path});
    const auto f = lower(std::string{folder});
    return p == f || (p.size() > f.size() && p.starts_with(f) && p[f.size()] == '/');
}

[[nodiscard]] fs::path journal_file(const fs::path& rollback) { return rollback / "journal.json"; }

[[nodiscard]] fs::path backup_of(const fs::path& rollback, std::string_view rel) {
    return rollback / "files" / rel_path(rel);
}

bool move_file(const fs::path& from, const fs::path& to, std::string& error) {
    std::error_code code;
    fs::create_directories(to.parent_path(), code);
    code.clear();
    fs::rename(from, to, code);
    if (code) {
        error = "cannot move " + path_to_utf8(from) + " to " + path_to_utf8(to) + ": " + code.message();
        return false;
    }
    return true;
}

/// Deletes a file, or parks it in update/trash when it is a running image.
bool park(const UpdateLayout& layout, const fs::path& file, std::string& error) {
    std::error_code code;
    if (!fs::exists(file, code)) return true;
    if (fs::remove(file, code) && !code) return true;
    for (int attempt = 0; attempt < 10000; ++attempt) {
        const auto target = layout.trash() / (std::to_string(attempt) + "-" + path_to_utf8(file.filename()));
        if (fs::exists(target, code)) continue;
        return move_file(file, target, error);
    }
    error = "cannot park " + path_to_utf8(file);
    return false;
}

bool write_journal(const fs::path& rollback, const ApplyOptions& options, const std::vector<Operation>& operations,
                   std::string_view state, std::string& error) {
    nlohmann::json json;
    json["schema"] = 2;
    json["state"] = std::string{state};
    json["component"] = options.component;
    json["target"] = options.target;
    json["from"] = options.from_version;
    json["to"] = options.to_version;
    json["session"] = options.session;
    json["replaced"] = nlohmann::json::array();
    json["added"] = nlohmann::json::array();
    json["removed"] = nlohmann::json::array();
    for (const auto& op : operations) {
        const char* key = op.kind == OpKind::replace ? "replaced" : (op.kind == OpKind::add ? "added" : "removed");
        json[key].push_back(op.rel);
    }
    std::error_code code;
    fs::create_directories(rollback, code);
    return write_file_atomic(journal_file(rollback), json.dump(1), error);
}

[[nodiscard]] bool matches_any(const std::vector<std::string>& patterns, std::string_view rel) {
    return std::ranges::any_of(patterns, [&](const std::string& p) { return glob_match(p, rel); });
}

[[nodiscard]] bool is_update_folder(std::string_view rel) { return within(rel, "update"); }

[[nodiscard]] bool safe_rel(std::string_view rel, bool allow_empty = false) {
    if (rel.empty()) return allow_empty;
    if (rel.front() == '/' || rel.find(':') != std::string_view::npos) return false;
    std::size_t start{};
    while (start <= rel.size()) {
        auto end = rel.find('/', start);
        if (end == std::string_view::npos) end = rel.size();
        const auto part = rel.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") return false;
        start = end + 1U;
    }
    return true;
}

bool list_files(const fs::path& root, std::vector<std::string>& out, std::string& error) {
    std::error_code code;
    for (fs::recursive_directory_iterator it{root, code}, end; !code && it != end; it.increment(code)) {
        if (it->is_directory(code)) continue;
        if (!it->is_regular_file(code)) {
            error = "unsupported file type in package: " + path_to_utf8(it->path());
            return false;
        }
        out.push_back(generic_utf8(it->path().lexically_relative(root)));
    }
    if (code) {
        error = "cannot enumerate " + path_to_utf8(root) + ": " + code.message();
        return false;
    }
    return true;
}

/// Reverses completed steps. Returns false if anything could not be put back;
/// the journal and backups must then survive for the next launch to retry.
bool undo(const UpdateLayout& layout, const fs::path& rollback, const fs::path& staged_root,
          std::vector<Operation>& operations) {
    std::string ignored;
    bool ok = true;
    for (auto it = operations.rbegin(); it != operations.rend(); ++it) {
        const auto target = layout.install / rel_path(it->rel);
        const auto backup = backup_of(rollback, it->rel);
        const auto staged = staged_root / rel_path(it->staged);
        switch (it->kind) {
        case OpKind::replace:
            if (it->progress == 2) ok = move_file(target, staged, ignored) && ok;
            if (it->progress >= 1) ok = move_file(backup, target, ignored) && ok;
            break;
        case OpKind::add:
            if (it->progress == 2) ok = move_file(target, staged, ignored) && ok;
            break;
        case OpKind::remove:
            if (it->progress >= 1) ok = move_file(backup, target, ignored) && ok;
            break;
        }
        it->progress = 0;
    }
    return ok;
}

[[nodiscard]] bool segment_match(std::string_view pattern, std::string_view text) noexcept {
    // Classic wildcard match within one segment ('*', '?').
    std::size_t p{}, t{}, star = std::string_view::npos, mark{};
    while (t < text.size()) {
        if (p < pattern.size() &&
            (pattern[p] == '?' ||
             std::tolower(static_cast<unsigned char>(pattern[p])) == std::tolower(static_cast<unsigned char>(text[t])))) {
            ++p;
            ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            mark = t;
        } else if (star != std::string_view::npos) {
            p = star + 1U;
            t = ++mark;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

[[nodiscard]] std::vector<std::string_view> segments(std::string_view text) {
    std::vector<std::string_view> parts;
    std::size_t start{};
    while (start <= text.size()) {
        auto end = text.find_first_of("/\\", start);
        if (end == std::string_view::npos) end = text.size();
        if (end > start) parts.push_back(text.substr(start, end - start));
        start = end + 1U;
    }
    return parts;
}

[[nodiscard]] bool match_segments(const std::vector<std::string_view>& pattern, std::size_t p,
                                  const std::vector<std::string_view>& path, std::size_t t) noexcept {
    if (p == pattern.size()) return t == path.size();
    if (pattern[p] == "**") {
        for (std::size_t skip = t; skip <= path.size(); ++skip) {
            if (match_segments(pattern, p + 1U, path, skip)) return true;
        }
        return false;
    }
    if (t == path.size()) return false;
    return segment_match(pattern[p], path[t]) && match_segments(pattern, p + 1U, path, t + 1U);
}

[[nodiscard]] std::optional<nlohmann::json> read_json(const fs::path& file) {
    std::string error;
    const auto text = read_text_file(file, error);
    if (!text.has_value()) return std::nullopt;
    try {
        return nlohmann::json::parse(*text);
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] std::vector<std::string> subfolders(const fs::path& folder) {
    std::vector<std::string> names;
    std::error_code code;
    for (fs::directory_iterator it{folder, code}, end; !code && it != end; it.increment(code)) {
        if (it->is_directory(code)) names.push_back(path_to_utf8(it->path().filename()));
    }
    std::ranges::sort(names);
    return names;
}

} // namespace

bool glob_match(std::string_view pattern, std::string_view path) noexcept {
    return match_segments(segments(pattern), 0U, segments(path), 0U);
}

ApplyResult apply_staged_tree(const UpdateLayout& layout, const fs::path& staged_root, const ApplyOptions& options) {
    ApplyResult result;
    std::error_code code;
    const auto target = normalize(options.target);
    if (!safe_rel(target, true) || (!target.empty() && is_update_folder(target))) {
        result.error = "unsafe component target: " + options.target;
        return result;
    }
    if (options.component.empty() || options.component.find_first_of("/\\.:") != std::string::npos) {
        result.error = "invalid component name: " + options.component;
        return result;
    }
    if (!fs::is_directory(staged_root, code)) {
        result.error = "staged update is missing: " + path_to_utf8(staged_root);
        return result;
    }
    const auto rollback = layout.rollback_dir(options.component);
    fs::remove_all(rollback, code);
    if (code) {
        result.error = "cannot clear the previous rollback set: " + code.message();
        return result;
    }
    std::vector<std::string> foreign;
    for (const auto& folder : options.foreign_targets) {
        const auto normalized = normalize(folder);
        // The client owns the install root; a nested foreign folder is excluded from it.
        if (!normalized.empty() && normalized != target) foreign.push_back(normalized);
    }
    const auto is_foreign = [&](std::string_view rel) {
        return std::ranges::any_of(foreign, [&](const std::string& f) { return within(rel, f) && !within(target, f); });
    };

    std::vector<std::string> staged;
    if (!list_files(staged_root, staged, result.error)) return result;
    std::set<std::string> staged_keys;   // install-relative, lower case
    for (const auto& rel : staged) {
        const auto installed = join(target, rel);
        if (!safe_rel(rel) || is_update_folder(installed)) {
            result.error = "package contains an unsafe path: " + rel;
            return result;
        }
        if (is_foreign(installed)) {
            result.error = "the " + options.component + " package contains files of another component: " + rel;
            return result;
        }
        staged_keys.insert(lower(installed));
    }

    std::vector<Operation> operations;
    for (const auto& rel : staged) {
        const auto installed = join(target, rel);
        const auto destination = layout.install / rel_path(installed);
        if (fs::is_directory(destination, code)) {
            result.error = "a folder is in the way of " + installed;
            return result;
        }
        if (fs::exists(destination, code)) {
            if (matches_any(options.preserve, rel)) {
                ++result.preserved;
                continue;
            }
            operations.push_back({OpKind::replace, installed, rel, 0});
        } else {
            operations.push_back({OpKind::add, installed, rel, 0});
        }
    }
    std::set<std::string> removals;
    for (const auto& mirror_raw : options.mirror_directories) {
        const auto mirror = normalize(mirror_raw);
        if (!safe_rel(mirror) || !fs::is_directory(staged_root / rel_path(mirror), code)) continue;   // package lacks it
        const auto installed_dir = layout.install / rel_path(join(target, mirror));
        if (!fs::is_directory(installed_dir, code)) continue;
        std::vector<std::string> installed;
        if (!list_files(installed_dir, installed, result.error)) return result;
        for (const auto& inner : installed) {
            const auto relative_to_target = join(mirror, inner);
            const auto rel = join(target, relative_to_target);
            if (!staged_keys.contains(lower(rel)) && !matches_any(options.preserve, relative_to_target) &&
                !is_foreign(rel) && !is_update_folder(rel)) {
                removals.insert(rel);
            }
        }
    }
    for (const auto& raw : options.remove) {
        const auto relative_to_target = normalize(raw);
        if (!safe_rel(relative_to_target)) continue;
        const auto rel = join(target, relative_to_target);
        if (is_update_folder(rel) || is_foreign(rel) || staged_keys.contains(lower(rel))) continue;
        if (fs::is_regular_file(layout.install / rel_path(rel), code)) removals.insert(rel);
    }
    for (const auto& rel : removals) operations.push_back({OpKind::remove, rel, {}, 0});

    if (!write_journal(rollback, options, operations, "applying", result.error)) return result;

    for (auto& op : operations) {
        const auto destination = layout.install / rel_path(op.rel);
        const auto source = staged_root / rel_path(op.staged);
        const auto backup = backup_of(rollback, op.rel);
        bool ok = true;
        if (op.kind == OpKind::replace || op.kind == OpKind::remove) {
            ok = move_file(destination, backup, result.error);
            if (ok) op.progress = 1;
        }
        if (ok && op.kind != OpKind::remove) ok = move_file(source, destination, result.error);
        if (!ok) {
            if (undo(layout, rollback, staged_root, operations)) {
                fs::remove_all(rollback, code);
            } else {
                result.error += " (restoring the previous files also failed; the next launch retries)";
            }
            result.replaced = result.added = result.removed = 0U;
            return result;
        }
        op.progress = 2;
        if (op.kind == OpKind::replace) ++result.replaced;
        if (op.kind == OpKind::add) ++result.added;
        if (op.kind == OpKind::remove) ++result.removed;
    }

    if (!write_journal(rollback, options, operations, "applied", result.error)) {
        // The files are in place; only the journal marker failed. Keep going.
        result.error.clear();
    }
    result.ok = true;
    return result;
}

std::optional<RollbackInfo> read_rollback_info(const UpdateLayout& layout, std::string_view component) {
    const auto json = read_json(journal_file(layout.rollback_dir(component)));
    if (!json.has_value()) return std::nullopt;
    try {
        return RollbackInfo{std::string{component}, json->value("from", std::string{}), json->value("to", std::string{}),
                            json->value("state", std::string{}) == "applied", json->value("session", std::string{})};
    } catch (...) {
        return std::nullopt;
    }
}

std::vector<std::string> rollback_components(const UpdateLayout& layout) {
    std::vector<std::string> names;
    std::error_code code;
    for (const auto& name : subfolders(layout.rollback())) {
        if (fs::is_regular_file(journal_file(layout.rollback_dir(name)), code)) names.push_back(name);
    }
    return names;
}

std::vector<std::string> interrupted_components(const UpdateLayout& layout) {
    std::vector<std::string> names;
    for (const auto& name : rollback_components(layout)) {
        const auto info = read_rollback_info(layout, name);
        if (info.has_value() && !info->applied) names.push_back(name);
    }
    return names;
}

ApplyResult rollback_last_update(const UpdateLayout& layout, std::string_view component) {
    ApplyResult result;
    const auto rollback = layout.rollback_dir(component);
    const auto loaded = read_json(journal_file(rollback));
    if (!loaded.has_value()) {
        result.error = "there is no previous " + std::string{component} + " version to roll back to";
        return result;
    }
    const auto& json = *loaded;
    std::vector<std::string> failures;
    std::string error;
    const auto list = [&](const char* key) {
        std::vector<std::string> values;
        try {
            if (json.contains(key)) {
                for (const auto& item : json.at(key)) values.push_back(item.get<std::string>());
            }
        } catch (...) {
        }
        return values;
    };
    std::error_code code;
    for (const auto& rel : list("replaced")) {
        if (!safe_rel(rel) || is_update_folder(rel)) continue;
        const auto destination = layout.install / rel_path(rel);
        const auto backup = backup_of(rollback, rel);
        if (!fs::exists(backup, code)) continue;   // never moved away: the old file is still in place
        if (!park(layout, destination, error) || !move_file(backup, destination, error)) {
            failures.push_back(error);
            continue;
        }
        ++result.replaced;
    }
    for (const auto& rel : list("added")) {
        if (!safe_rel(rel) || is_update_folder(rel)) continue;
        const auto destination = layout.install / rel_path(rel);
        if (!fs::exists(destination, code)) continue;
        if (!park(layout, destination, error)) {
            failures.push_back(error);
            continue;
        }
        ++result.added;
    }
    for (const auto& rel : list("removed")) {
        if (!safe_rel(rel) || is_update_folder(rel)) continue;
        const auto destination = layout.install / rel_path(rel);
        const auto backup = backup_of(rollback, rel);
        if (!fs::exists(backup, code) || fs::exists(destination, code)) continue;
        if (!move_file(backup, destination, error)) {
            failures.push_back(error);
            continue;
        }
        ++result.removed;
    }
    if (!failures.empty()) {
        result.error = failures.front();
        if (failures.size() > 1U) result.error += " (and " + std::to_string(failures.size() - 1U) + " more)";
        return result;
    }
    fs::remove_all(rollback, code);
    result.ok = true;
    return result;
}

void empty_trash(const UpdateLayout& layout) {
    std::error_code code;
    fs::remove_all(layout.trash(), code);
}

std::optional<fs::path> find_package_root(const fs::path& extracted, std::string_view hint, std::string_view marker_file) {
    std::error_code code;
    const auto marker = path_from_utf8(marker_file);
    const auto qualifies = [&](const fs::path& directory) {
        if (!marker_file.empty()) return fs::is_regular_file(directory / marker, code);
        for (fs::directory_iterator it{directory, code}, end; !code && it != end; it.increment(code)) {
            if (it->is_regular_file(code)) return true;
        }
        return false;
    };
    if (!hint.empty()) {
        const auto candidate = extracted / path_from_utf8(hint);
        if (fs::is_directory(candidate, code) && (marker_file.empty() || qualifies(candidate))) return candidate;
    }
    std::deque<std::pair<fs::path, int>> queue{{extracted, 0}};
    while (!queue.empty()) {
        auto [directory, depth] = queue.front();
        queue.pop_front();
        if (qualifies(directory)) return directory;
        if (depth >= 3) continue;
        std::vector<fs::path> children;
        for (fs::directory_iterator it{directory, code}, end; !code && it != end; it.increment(code)) {
            if (it->is_directory(code)) children.push_back(it->path());
        }
        std::ranges::sort(children);
        for (auto& child : children) queue.emplace_back(std::move(child), depth + 1);
    }
    return std::nullopt;
}

UpdaterState load_updater_state(const UpdateLayout& layout) {
    UpdaterState state;
    const auto json = read_json(layout.state_file());
    if (!json.has_value()) return state;
    try {
        if (json->contains("skipped_versions")) {
            for (const auto& [component, version] : json->at("skipped_versions").items()) {
                state.skipped_versions[component] = version.get<std::string>();
            }
        }
        // Schema-1 files recorded only the client.
        if (const auto legacy = json->value("skipped_version", std::string{}); !legacy.empty()) {
            state.skipped_versions.emplace("client", legacy);
        }
        if (json->contains("deferrals")) {
            for (const auto& [key, count] : json->at("deferrals").items()) {
                state.deferrals[key] = count.get<std::uint32_t>();
            }
        }
    } catch (...) {
    }
    return state;
}

bool save_updater_state(const UpdateLayout& layout, const UpdaterState& state, std::string& error) {
    std::error_code code;
    fs::create_directories(layout.root(), code);
    nlohmann::json json;
    json["skipped_versions"] = state.skipped_versions;
    json["deferrals"] = state.deferrals;
    return write_file_atomic(layout.state_file(), json.dump(2), error);
}

std::optional<std::string> read_installed_version(const fs::path& install) {
    const auto json = read_json(install / "battlespades-version.json");
    if (!json.has_value()) return std::nullopt;
    try {
        auto version = json->value("version", std::string{});
        if (version.empty()) return std::nullopt;
        return version;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::uint32_t> read_client_protocol(const fs::path& install) {
    const auto json = read_json(install / "battlespades-version.json");
    if (!json.has_value() || !json->contains("protocol") || !json->at("protocol").is_number_unsigned()) {
        return std::nullopt;
    }
    return json->at("protocol").get<std::uint32_t>();
}

bool retail_assets_present(const fs::path& install) {
    // The importer activates assets/original atomically, so a non-empty
    // folder is a complete import; full verification stays in the client.
    std::error_code code;
    const auto root = install / "assets" / "original";
    if (!fs::is_directory(root, code)) return false;
    return fs::directory_iterator{root, code} != fs::directory_iterator{};
}

InstalledVersions read_installed_versions(const fs::path& install) {
    InstalledVersions versions;
    const UpdateLayout layout{install};
    if (const auto recorded = read_json(layout.installed_file()); recorded.has_value() && recorded->is_object()) {
        for (const auto& [component, version] : recorded->items()) {
            if (version.is_string()) versions[component] = version.get<std::string>();
        }
    }
    if (const auto build = read_json(install / "battlespades-version.json"); build.has_value()) {
        try {
            if (const auto client = build->value("version", std::string{}); !client.empty()) versions["client"] = client;
            if (build->contains("components") && build->at("components").is_object()) {
                for (const auto& [component, version] : build->at("components").items()) {
                    if (component != "client" && version.is_string()) versions.emplace(component, version.get<std::string>());
                }
            }
        } catch (...) {
        }
    }
    std::string error;
    if (auto server = read_text_file(install / "server" / "VERSION", error); server.has_value()) {
        while (!server->empty() && (server->back() == '\n' || server->back() == '\r' || server->back() == ' ')) {
            server->pop_back();
        }
        if (!server->empty()) versions["server"] = *server;
    }
    return versions;
}

bool record_installed_version(const UpdateLayout& layout, std::string_view component, std::string_view version,
                              std::string& error) {
    nlohmann::json json = nlohmann::json::object();
    if (const auto existing = read_json(layout.installed_file()); existing.has_value() && existing->is_object()) {
        json = *existing;
    }
    json[std::string{component}] = std::string{version};
    std::error_code code;
    fs::create_directories(layout.root(), code);
    return write_file_atomic(layout.installed_file(), json.dump(2), error);
}

} // namespace battlespades::updater
