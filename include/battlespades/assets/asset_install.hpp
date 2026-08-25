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
 * Selecting either the content directory itself or a parent containing `src`
 * is supported. Candidates are accepted only when the complete manifest fits.
 */
[[nodiscard]] std::optional<std::filesystem::path> find_asset_source(
    const std::filesystem::path& selected_directory,
    const AssetManifest& manifest,
    std::string& error) noexcept;

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
