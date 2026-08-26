#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace battlespades::assets {

struct AssetManifestEntry final {
    std::filesystem::path relative_path{};
    std::uint64_t size{};
    std::string sha256{};
};

struct AssetManifest final {
    std::vector<AssetManifestEntry> files{};
    std::uint64_t total_bytes{};
};

struct AssetManifestLoadResult final {
    std::optional<AssetManifest> manifest{};
    std::string error{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return manifest.has_value();
    }
};

enum class AssetVerificationDepth : std::uint8_t {
    metadata,
    full_hash,
};

struct AssetTreeCheck final {
    bool valid{};
    std::size_t files_checked{};
    std::uint64_t bytes_checked{};
    std::string error{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return valid;
    }
};

struct AssetInstallProgress final {
    std::size_t files_completed{};
    std::size_t files_total{};
    std::uint64_t bytes_completed{};
    std::uint64_t bytes_total{};
};

using AssetProgressCallback = std::function<void(const AssetInstallProgress&)>;

struct AssetInstallResult final {
    bool installed{};
    std::string error{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return installed;
    }
};

struct NativeSteamImportResult final {
    bool imported{};
    std::string error{};

    /** A missing optional runtime is not an installation failure. */
    [[nodiscard]] explicit operator bool() const noexcept { return error.empty(); }
};

/** Loads and structurally validates the immutable retail-asset catalog. */
[[nodiscard]] AssetManifestLoadResult
load_asset_manifest(const std::filesystem::path& manifest_path) noexcept;

/**
 * Checks that every catalogued file is present and has the expected size.
 * Full-hash mode additionally verifies SHA-256 and is used by the installer;
 * the inexpensive metadata mode is safe to run at every client startup.
 */
[[nodiscard]] AssetTreeCheck verify_asset_tree(
    const std::filesystem::path& root,
    const AssetManifest& manifest,
    AssetVerificationDepth depth = AssetVerificationDepth::metadata) noexcept;

/**
 * Resolves a folder selected by the player to the actual retail content root.
 * Selecting either the Windows content directory itself or a parent containing
 * it is supported. Legacy macOS `.app` resources are rejected because that old
 * build does not share the recovered Windows asset contract. Candidates are
 * accepted only when the complete manifest fits.
 */
[[nodiscard]] std::optional<std::filesystem::path> find_asset_source(
    const std::filesystem::path& selected_directory,
    const AssetManifest& manifest,
    std::string& error) noexcept;

/** Returns the first conventional Steam installation folder present locally. */
[[nodiscard]] std::optional<std::filesystem::path>
default_asset_source_directory() noexcept;

/**
 * Copies and hashes every required asset into a sibling staging directory,
 * then atomically swaps the verified tree into place. A failed or cancelled
 * import never exposes a partial runtime asset directory.
 */
[[nodiscard]] AssetInstallResult install_asset_tree_atomic(
    const std::filesystem::path& source_root,
    const std::filesystem::path& destination_root,
    const AssetManifest& manifest,
    AssetProgressCallback progress = {}) noexcept;

/**
 * Imports the user's owned x86 Steamworks runtime without redistributing it.
 *
 * Windows accepts only an ordinary 32-bit PE `steam_api.dll`; redirected files
 * and `steamclient.dll` are never copied. Absence is a valid offline install.
 */
[[nodiscard]] NativeSteamImportResult import_native_steam_runtime(
    const std::filesystem::path& selected_directory,
    const std::filesystem::path& executable_directory) noexcept;

enum class AssetInstallerExit : std::uint8_t {
    installed,
    cancelled,
    failed,
};

/** Runs the packaged installer as a child process and waits for its result. */
[[nodiscard]] AssetInstallerExit run_asset_installer(
    const std::filesystem::path& installer_executable,
    const std::filesystem::path& manifest_path,
    const std::filesystem::path& destination_root,
    std::string& error) noexcept;

} // namespace battlespades::assets
