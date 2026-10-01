#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::updater {

/**
 * Install-folder layout owned by the launcher (all under <install>/update so
 * every rename stays on one volume):
 *
 *   update/download/               partial and verified package archives
 *   update/staging/<comp>-<ver>/   extracted, verified trees waiting to be applied
 *   update/rollback/<comp>/        files the component's last update replaced + journal.json
 *   update/trash/                  files that could not be deleted yet (running images)
 *   update/state.json              skipped versions and "Later" counters
 *   update/installed.json          versions of components without their own version file
 */
struct UpdateLayout {
    std::filesystem::path install;
    [[nodiscard]] std::filesystem::path root() const { return install / "update"; }
    [[nodiscard]] std::filesystem::path download() const { return root() / "download"; }
    [[nodiscard]] std::filesystem::path staging() const { return root() / "staging"; }
    [[nodiscard]] std::filesystem::path rollback() const { return root() / "rollback"; }
    [[nodiscard]] std::filesystem::path rollback_dir(std::string_view component) const {
        return rollback() / std::filesystem::path{std::string{component}};
    }
    [[nodiscard]] std::filesystem::path trash() const { return root() / "trash"; }
    [[nodiscard]] std::filesystem::path state_file() const { return root() / "state.json"; }
    [[nodiscard]] std::filesystem::path installed_file() const { return root() / "installed.json"; }
};

struct ApplyOptions {
    std::string component{"client"};
    std::string target;                           ///< install-relative folder the stage maps onto
    std::vector<std::string> foreign_targets;     ///< folders owned by other components: never touched
    std::vector<std::string> preserve;            ///< globs (relative to target) kept when present
    std::vector<std::string> mirror_directories;  ///< folders (relative to target) whose extra files go
    std::vector<std::string> remove;              ///< explicit obsolete files (relative to target)
    std::string from_version;
    std::string to_version;
    std::string session;                          ///< groups components updated together
};

struct ApplyResult {
    bool ok{};
    std::string error;
    std::size_t replaced{};
    std::size_t added{};
    std::size_t removed{};
    std::size_t preserved{};
};

/**
 * Moves every file of `staged_root` into <install>/<target>. Each file it
 * replaces or removes is first moved to the component's rollback set, and a
 * journal records the plan before anything changes. Any failure undoes the
 * completed steps, so the component is either the old or the new version. The
 * component's previous rollback set is discarded: one prior version is kept
 * per component, and other components are never touched.
 *
 * A running executable (the launcher itself) can be renamed but not deleted,
 * which is why replacement is always rename-away, rename-in.
 */
[[nodiscard]] ApplyResult apply_staged_tree(const UpdateLayout& layout,
                                            const std::filesystem::path& staged_root,
                                            const ApplyOptions& options);

/// Restores the version recorded in update/rollback/<component>/journal.json.
[[nodiscard]] ApplyResult rollback_last_update(const UpdateLayout& layout, std::string_view component = "client");

/// Components whose last apply was interrupted (journal state "applying").
[[nodiscard]] std::vector<std::string> interrupted_components(const UpdateLayout& layout);
/// Components that have a rollback set.
[[nodiscard]] std::vector<std::string> rollback_components(const UpdateLayout& layout);

struct RollbackInfo {
    std::string component;
    std::string from_version;
    std::string to_version;
    bool applied{};
    std::string session;
};
[[nodiscard]] std::optional<RollbackInfo> read_rollback_info(const UpdateLayout& layout,
                                                             std::string_view component = "client");

/// Best-effort removal of update/trash (files parked by earlier runs).
void empty_trash(const UpdateLayout& layout);

/**
 * The folder inside an extracted archive that maps onto the target folder:
 * `hint` when given and present, otherwise the shallowest folder (depth <= 3)
 * that directly contains `marker_file` (any folder holding a file when the
 * marker is empty).
 */
[[nodiscard]] std::optional<std::filesystem::path>
find_package_root(const std::filesystem::path& extracted, std::string_view hint,
                  std::string_view marker_file);

/// '*' matches within one path segment, '**' across segments, '?' one char;
/// '/' and '\\' are equivalent; ASCII case-insensitive.
[[nodiscard]] bool glob_match(std::string_view pattern, std::string_view path) noexcept;

/**
 * update/state.json. A component version the player rolled back from is not
 * reinstalled automatically; "Later" answers count per component@version.
 */
struct UpdaterState {
    std::map<std::string, std::string> skipped_versions;   ///< component -> version
    std::map<std::string, std::uint32_t> deferrals;        ///< "component@version" -> count
};
[[nodiscard]] UpdaterState load_updater_state(const UpdateLayout& layout);
bool save_updater_state(const UpdateLayout& layout, const UpdaterState& state, std::string& error);

/**
 * Installed component versions:
 *   client  battlespades-version.json "version"
 *   server  server/VERSION (the server bundle's own file)
 *   assets  update/installed.json, else battlespades-version.json "components.assets"
 * update/installed.json records what the launcher applied for components
 * without a version file of their own.
 */
using InstalledVersions = std::map<std::string, std::string>;
[[nodiscard]] InstalledVersions read_installed_versions(const std::filesystem::path& install);
bool record_installed_version(const UpdateLayout& layout, std::string_view component, std::string_view version,
                              std::string& error);

/// The network protocol this client build speaks ("protocol" in battlespades-version.json).
[[nodiscard]] std::optional<std::uint32_t> read_client_protocol(const std::filesystem::path& install);

/// True when assets/original holds an imported or downloaded retail tree.
[[nodiscard]] bool retail_assets_present(const std::filesystem::path& install);

/// Reads {"version": "..."} from battlespades-version.json.
[[nodiscard]] std::optional<std::string> read_installed_version(const std::filesystem::path& install);

} // namespace battlespades::updater
