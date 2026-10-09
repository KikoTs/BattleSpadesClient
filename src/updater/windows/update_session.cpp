#include "update_session.hpp"

#include "win_util.hpp"

#include "battlespades/updater/file_util.hpp"
#include "battlespades/updater/launcher_args.hpp"
#include "battlespades/updater/release_manifest.hpp"
#include "battlespades/updater/semver.hpp"
#include "battlespades/updater/sha256.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <ctime>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace battlespades::updater::win {
namespace {

namespace fs = std::filesystem;

constexpr std::size_t maximum_manifest_body = 1024U * 1024U;
constexpr std::size_t maximum_api_body = 4U * 1024U * 1024U;
constexpr std::size_t maximum_small_asset = 256U * 1024U;

void report(const SessionCallbacks& callbacks, std::string_view text) {
    if (callbacks.status) callbacks.status(text);
    if (callbacks.log) callbacks.log(text);
}

void log(const SessionCallbacks& callbacks, std::string_view text) {
    if (callbacks.log) callbacks.log(text);
}

[[nodiscard]] HttpOptions small_options(const UpdaterConfig& config, bool github) {
    HttpOptions options;
    options.timeout_ms = config.check_timeout_ms;
    options.user_agent = std::string{"BattleSpadesLauncher/"} + AOS_VERSION_STRING;
    if (github) {
        options.headers = {"Accept: application/vnd.github+json", "X-GitHub-Api-Version: 2022-11-28"};
    } else {
        options.headers = {"Accept: application/json", "Cache-Control: no-cache"};
    }
    return options;
}

[[nodiscard]] HttpOptions download_options(const UpdaterConfig& config) {
    HttpOptions options;
    options.timeout_ms = std::max<std::uint32_t>(config.check_timeout_ms, 30000U);
    options.user_agent = std::string{"BattleSpadesLauncher/"} + AOS_VERSION_STRING;
    options.headers = {"Accept: application/octet-stream"};
    return options;
}

[[nodiscard]] std::string session_id() {
    const auto now = std::time(nullptr);
    std::tm utc{};
    gmtime_s(&utc, &now);
    char text[32]{};
    std::strftime(text, sizeof(text), "%Y%m%dT%H%M%SZ", &utc);
    return text;
}

[[nodiscard]] fs::path stage_dir(const UpdateLayout& layout, const ComponentRelease& release) {
    return layout.staging() / path_from_utf8(release.name + "-" + release.version);
}

/// The old GitHub latest-release check, expressed as a one-component manifest.
[[nodiscard]] std::optional<UpdateManifest> github_manifest(const UpdaterConfig& config, const SessionCallbacks& callbacks,
                                                            std::string& error) {
    const auto endpoint = release_endpoint(config);
    log(callbacks, "GitHub fallback: " + endpoint);
    const auto response = http_get(endpoint, small_options(config, true), maximum_api_body);
    if (response.status != 200U) {
        error = response.status == 0U ? response.error : "GitHub answered HTTP " + std::to_string(response.status);
        return std::nullopt;
    }
    GitHubRelease release;
    const auto first = response.body.find_first_not_of(" \t\r\n");
    if (first != std::string::npos && response.body[first] == '[') {
        auto releases = parse_github_release_list(response.body, error);
        if (!releases.has_value()) return std::nullopt;
        const auto* newest = pick_newest_release(*releases, config.include_prereleases);
        if (newest == nullptr) {
            error = "no usable GitHub release";
            return std::nullopt;
        }
        release = *newest;
    } else {
        auto parsed = parse_github_release(response.body, error);
        if (!parsed.has_value()) return std::nullopt;
        release = std::move(*parsed);
    }
    std::string asset_url;
    const auto stage = plan_release(release, asset_url, error);
    if (stage == ResolveStage::unusable) return std::nullopt;
    const auto asset = http_get(asset_url, download_options(config), maximum_small_asset);
    if (asset.status != 200U) {
        error = "cannot fetch release metadata: " + (asset.status == 0U ? asset.error : "HTTP " + std::to_string(asset.status));
        return std::nullopt;
    }
    auto resolved = stage == ResolveStage::need_manifest ? resolve_with_manifest(release, asset.body, error)
                                                         : resolve_with_checksum(release, asset.body, error);
    if (!resolved.has_value()) return std::nullopt;
    if (resolved->package.size == 0U) {
        error = "GitHub release does not state the package size";
        return std::nullopt;
    }
    UpdateManifest manifest;
    manifest.product = "BattleSpades";
    manifest.channel = "github";
    ComponentRelease client;
    client.name = std::string{component_client};
    client.version = resolved->version;
    client.package = resolved->package.name;
    client.urls = {resolved->package.url};
    client.size = resolved->package.size;
    client.sha256 = resolved->package.sha256;
    client.root = resolved->package.root;
    client.preserve = resolved->manifest.preserve;
    client.mirror_directories = resolved->manifest.mirror_directories;
    client.remove = resolved->manifest.remove;
    manifest.components.push_back(std::move(client));
    return manifest;
}

/// One mirror: download, then check status, size and SHA-256.
MirrorOutcome fetch_from_mirror(const std::string& url, const fs::path& archive, const ComponentRelease& release,
                                const UpdaterConfig& config, const SessionCallbacks& callbacks, std::string& error) {
    auto partial = archive;
    partial += ".partial";
    std::error_code code;
    const auto existing = prepare_package_partial(partial, release.size, release.sha256, error);
    if (!existing.has_value()) return MirrorOutcome::failed;
    if (*existing < release.size) {
        const auto response = http_download(url, partial, download_options(config), callbacks.progress, release.size);
        // Interrupted or refused downloads keep the partial file: the next mirror
        // or the next launch resumes it (Range request) and the hash decides.
        if (response.error == "cancelled") return MirrorOutcome::cancelled;
        if (response.status != 200U) {
            error = response.status == 0U ? response.error : "HTTP " + std::to_string(response.status);
            return MirrorOutcome::failed;
        }
        if (!verify_package_file(partial, release.size, release.sha256, error)) {
            fs::remove(partial, code);
            return MirrorOutcome::failed;
        }
    }
    // Complete partials were already verified locally: do not ask for an
    // empty range and then download the entire archive again after HTTP 416.
    fs::remove(archive, code);
    fs::rename(partial, archive, code);
    if (code) {
        error = "cannot finalise the download: " + code.message();
        return MirrorOutcome::failed;
    }
    auto identity = partial;
    identity += ".identity";
    fs::remove(identity, code);
    return MirrorOutcome::success;
}

[[nodiscard]] bool verified_archive(const fs::path& archive, const ComponentRelease& release) {
    std::string ignored;
    return verify_package_file(archive, release.size, release.sha256, ignored);
}

/// Stage one verified archive as update/staging/<component>-<version>.
bool stage_archive(const UpdateLayout& layout, const fs::path& archive, const ComponentRelease& release,
                   std::string& error) {
    std::error_code code;
    const auto final_stage = stage_dir(layout, release);
    auto extracting = final_stage;
    extracting += ".extracting";
    fs::remove_all(extracting, code);
    const auto payload = extracting / "payload";
    if (!extract_zip(archive, payload, error)) {
        fs::remove_all(extracting, code);
        return false;
    }
    const std::string marker = release.name == component_client ? "BattleSpadesClient.exe"
                               : release.name == component_server ? "BattleSpades.exe"
                                                                  : "";
    const auto root = find_package_root(payload, release.root, marker);
    if (!root.has_value()) {
        fs::remove_all(extracting, code);
        error = release.name + " package has no " + (marker.empty() ? std::string{"files"} : marker);
        return false;
    }
    nlohmann::json staged;
    staged["schema"] = 2;
    staged["component"] = release.name;
    staged["version"] = release.version;
    staged["sha256"] = release.sha256;
    staged["root"] = generic_utf8(root->lexically_relative(payload));
    if (!write_file_atomic(extracting / "staged.json", staged.dump(2), error)) {
        fs::remove_all(extracting, code);
        return false;
    }
    fs::remove_all(final_stage, code);
    fs::rename(extracting, final_stage, code);
    if (code) {
        error = "cannot finalise the staged update: " + code.message();
        return false;
    }
    return true;
}

[[nodiscard]] std::optional<fs::path> staged_root(const UpdateLayout& layout, const ComponentRelease& release) {
    const auto stage = stage_dir(layout, release);
    std::string error;
    const auto text = read_text_file(stage / "staged.json", error);
    if (!text.has_value()) return std::nullopt;
    try {
        const auto json = nlohmann::json::parse(*text);
        if (json.value("component", std::string{}) != release.name || json.value("version", std::string{}) != release.version ||
            !same_sha256(json.value("sha256", std::string{}), release.sha256)) {
            return std::nullopt;
        }
        return stage / "payload" / path_from_utf8(json.value("root", std::string{}));
    } catch (...) {
        return std::nullopt;
    }
}

/// Stages that the manifest no longer asks for are stale.
void prune_stages(const UpdateLayout& layout, const std::vector<PlannedUpdate>& updates) {
    std::error_code code;
    for (fs::directory_iterator it{layout.staging(), code}, end; !code && it != end; it.increment(code)) {
        const auto name = path_to_utf8(it->path().filename());
        const bool wanted = std::ranges::any_of(updates, [&](const PlannedUpdate& update) {
            return name == update.release.name + "-" + update.release.version;
        });
        if (!wanted) {
            std::error_code ignored;
            fs::remove_all(it->path(), ignored);
        }
    }
}

} // namespace

Discovery discover_updates(const UpdaterConfig& config, const SessionCallbacks& callbacks) {
    Discovery discovery;
    for (const auto& endpoint : manifest_locations(manifest_endpoint(config))) {
        log(callbacks, "checking " + endpoint);
        const auto response = http_get(endpoint, small_options(config, false), maximum_manifest_body);
        if (response.status == 200U) {
            std::string parse_error;
            auto manifest = parse_update_manifest(response.body, parse_error);
            if (manifest.has_value()) {
                discovery.source = DiscoverySource::manifest;
                discovery.manifest = std::move(manifest);
                discovery.error.clear();
                discovery.body = response.body;
                return discovery;
            }
            // A broken manifest is a publishing error, not an outage: no fallback.
            discovery.error = "invalid update manifest: " + parse_error;
            return discovery;
        }
        const auto problem = response.status == 0U
                                 ? endpoint + ": " + response.error
                                 : endpoint + " answered HTTP " + std::to_string(response.status);
        log(callbacks, "unreachable: " + problem);
        discovery.error += (discovery.error.empty() ? "" : "; ") + problem;
    }
    if (!config.github_fallback) return discovery;
    log(callbacks, "manifest unreachable (" + discovery.error + ")");
    std::string error;
    if (auto manifest = github_manifest(config, callbacks, error); manifest.has_value()) {
        discovery.source = DiscoverySource::github_fallback;
        discovery.manifest = std::move(manifest);
        discovery.error.clear();
    } else {
        discovery.error += "; GitHub fallback: " + error;
    }
    return discovery;
}

namespace {

/// retail_assets goes through BattleSpadesAssetInstaller, which validates the
/// files against asset-manifest.json and installs them atomically into
/// assets\original, exactly like "Use my Ace of Spades folder".
bool import_retail_assets(const UpdateLayout& layout, const fs::path& root, std::string& error) {
    const auto imported = run_asset_import(layout, root);
    if (!imported.ok()) {
        error = "the downloaded game files did not pass the asset check: " + imported.message;
        return false;
    }
    return true;
}

} // namespace

AssetImportResult run_asset_import(const UpdateLayout& layout, const std::optional<fs::path>& source) {
    AssetImportResult result;
    const auto importer = layout.install / L"BattleSpadesAssetInstaller.exe";
    std::error_code code;
    if (!fs::is_regular_file(importer, code)) {
        result.message = "BattleSpadesAssetInstaller.exe is missing from " + path_to_utf8(layout.install) +
                         ". Reinstall BattleSpades.";
        return result;
    }
    const auto report_file = layout.root() / "import-report.txt";
    fs::create_directories(layout.root(), code);
    fs::remove(report_file, code);
    std::vector<std::string> arguments;
    if (source.has_value()) {
        arguments.insert(arguments.end(), {"--source", path_to_utf8(*source)});
    } else {
        // The launcher's first-run screen already offers every choice
        // (including the download): the importer only shows its folder picker.
        arguments.emplace_back("--choose-folder");
    }
    arguments.insert(arguments.end(), {"--destination", path_to_utf8(layout.install / "assets" / "original"),
                                       "--report", path_to_utf8(report_file)});
    const auto line = widen(build_windows_command_line(path_to_utf8(importer), arguments));
    if (source.has_value()) {
        result.exit = run_hidden(importer.wstring(), line, 30U * 60U * 1000U);
    } else {
        // Interactive: the importer's own window and folder picker.
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        std::wstring mutable_line = line;
        if (CreateProcessW(importer.c_str(), mutable_line.data(), nullptr, nullptr, FALSE, 0U, nullptr,
                           layout.install.c_str(), &startup, &process)) {
            CloseHandle(process.hThread);
            WaitForSingleObject(process.hProcess, INFINITE);
            DWORD exit_code{1U};
            if (GetExitCodeProcess(process.hProcess, &exit_code)) result.exit = exit_code;
            CloseHandle(process.hProcess);
        }
    }
    std::string ignored;
    auto report = read_text_file(report_file, ignored).value_or(std::string{});
    fs::remove(report_file, code);
    if (result.ok()) return result;
    if (report == "ok") report.clear();
    if (!report.empty()) {
        result.message = std::move(report);
    } else if (!result.exit.has_value()) {
        result.message = "the asset importer could not be started or did not finish in time";
    } else if (result.cancelled()) {
        result.message = "the import was cancelled";
    } else {
        result.message = "the asset importer failed with exit code " + std::to_string(*result.exit);
    }
    return result;
}

SessionResult run_update_session(const UpdateLayout& layout, const UpdateManifest& manifest,
                                 const std::vector<PlannedUpdate>& updates, const UpdaterConfig& config,
                                 const SessionCallbacks& callbacks) {
    SessionResult result;
    if (updates.empty()) return result;
    std::error_code code;
    fs::create_directories(layout.download(), code);
    fs::create_directories(layout.staging(), code);
    prune_stages(layout, updates);

    // Components are independent: one failing never blocks or rolls back
    // another. Each is downloaded (resumable, mirror by mirror, verified),
    // staged, then applied on its own.
    const auto session = session_id();
    std::vector<std::string> foreign;
    for (const auto& component : manifest.components) foreign.push_back(component.target);
    std::size_t index{};
    for (const auto& update : updates) {
        ++index;
        const auto& release = update.release;
        const auto fail = [&](const std::string& why) {
            log(callbacks, release.name + " " + release.version + " failed: " + why);
            if (result.error.empty()) result.error = release.name + ": " + why;
            result.failed.push_back(release.name);
        };
        const auto archive = layout.download() / path_from_utf8(release.package);
        if (!staged_root(layout, release).has_value()) {
            if (!verified_archive(archive, release)) {
                fs::remove(archive, code);
                report(callbacks, "Downloading " + release.name + " " + release.version + " (" + std::to_string(index) +
                                      " of " + std::to_string(updates.size()) + ")...");
                const auto mirrors = try_mirrors(release.urls, [&](const std::string& url, std::string& error) {
                    log(callbacks, "  " + release.name + " from " + url);
                    return fetch_from_mirror(url, archive, release, config, callbacks, error);
                });
                for (const auto& failure : mirrors.errors) log(callbacks, "  mirror failed: " + failure);
                if (mirrors.outcome == MirrorOutcome::cancelled) {
                    result.outcome = SessionOutcome::cancelled;
                    return result;
                }
                if (mirrors.outcome != MirrorOutcome::success) {
                    fail("every mirror failed");
                    continue;
                }
            }
            report(callbacks, "Unpacking " + release.name + " " + release.version + "...");
            std::string error;
            if (!stage_archive(layout, archive, release, error)) {
                fail(error);
                continue;
            }
        }
        const auto root = staged_root(layout, release);
        if (!root.has_value()) {
            fail("the staged files disappeared");
            continue;
        }
        report(callbacks, "Installing " + release.name + " " + release.version + "...");
        if (release.name == component_retail_assets) {
            std::string error;
            const bool imported = import_retail_assets(layout, *root, error);
            fs::remove_all(stage_dir(layout, release), code);
            if (!imported) {
                fail(error);
                continue;
            }
        } else {
            ApplyOptions options;
            options.component = release.name;
            options.target = release.target;
            options.foreign_targets = foreign;
            options.preserve = release.preserve;
            options.mirror_directories = release.mirror_directories;
            options.remove = release.remove;
            options.from_version = update.installed;
            options.to_version = release.version;
            options.session = session;
            const auto outcome = apply_staged_tree(layout, *root, options);
            // Applying moves files out of the stage; never keep a half-consumed one.
            fs::remove_all(stage_dir(layout, release), code);
            if (!outcome.ok) {
                fail(outcome.error);   // apply_staged_tree already restored this component
                continue;
            }
            log(callbacks, "applied " + release.name + " " + release.version + ": " + std::to_string(outcome.replaced) +
                               " replaced, " + std::to_string(outcome.added) + " added, " +
                               std::to_string(outcome.removed) + " removed, " + std::to_string(outcome.preserved) +
                               " preserved");
        }
        std::string ignored;
        static_cast<void>(record_installed_version(layout, release.name, release.version, ignored));
        fs::remove(archive, code);
        result.applied.push_back(release.name + " " + release.version);
    }
    result.outcome = result.failed.empty() ? SessionOutcome::applied
                     : result.applied.empty() ? SessionOutcome::failed
                                              : SessionOutcome::partial;
    return result;
}

SessionResult rollback_last_session(const UpdateLayout& layout) {
    SessionResult result;
    std::string latest;
    std::vector<RollbackInfo> infos;
    for (const auto& component : rollback_components(layout)) {
        if (auto info = read_rollback_info(layout, component); info.has_value() && info->applied) {
            latest = std::max(latest, info->session);
            infos.push_back(std::move(*info));
        }
    }
    if (infos.empty()) {
        result.outcome = SessionOutcome::nothing_to_do;
        result.error = "there is no previous version to restore";
        return result;
    }
    auto state = load_updater_state(layout);
    for (const auto& info : infos) {
        if (info.session != latest) continue;
        const auto restored = rollback_last_update(layout, info.component);
        if (!restored.ok) {
            result.outcome = SessionOutcome::failed;
            result.error = info.component + ": " + restored.error;
            return result;
        }
        std::string ignored;
        if (!info.from_version.empty()) {
            static_cast<void>(record_installed_version(layout, info.component, info.from_version, ignored));
        }
        state.skipped_versions[info.component] = info.to_version;
        result.applied.push_back(info.component + " " + info.from_version);
    }
    std::string ignored;
    static_cast<void>(save_updater_state(layout, state, ignored));
    result.outcome = SessionOutcome::applied;
    return result;
}

} // namespace battlespades::updater::win
