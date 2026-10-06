#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
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

/** Where a directory offered to find_asset_source() came from. */
enum class AssetSourceOrigin : std::uint8_t {
    /** A folder the player chose, or one found on their disk. */
    player_folder,
    /**
     * Our own SHA-256 verified download, unpacked in the installer's cache.
     * On macOS that cache is inside BattleSpadesClient.app, so the path it
     * sits at says nothing about what the package holds.
     */
    verified_package,
};

/**
 * Resolves a folder selected by the player to the actual retail content root.
 * Selecting either the Windows content directory itself or a parent containing
 * it is supported. Legacy macOS `.app` resources are rejected because that old
 * build does not share the recovered Windows asset contract; the rule is about
 * what a player picked, so it does not apply to a verified package. Candidates
 * are accepted only when the complete manifest fits.
 */
[[nodiscard]] std::optional<std::filesystem::path> find_asset_source(
    const std::filesystem::path& selected_directory,
    const AssetManifest& manifest,
    std::string& error,
    AssetSourceOrigin origin = AssetSourceOrigin::player_folder) noexcept;

// ---------------------------------------------------------------------------
// Finding the player's Ace of Spades installation.
//
// Ace of Spades (Steam app 224540) is a Windows game. On Windows it lives in a
// Steam library; on Linux it is usually installed through Steam Play
// (Proton) into the native Steam library; on macOS it can only come from a
// Windows Steam inside CrossOver/Whisky/Wine (or a copied Windows folder).
// Every Steam root is resolved through libraryfolders.vdf and
// appmanifest_224540.acf "installdir", never by guessing <library>/common.
// ---------------------------------------------------------------------------

enum class HostPlatform : std::uint8_t { windows, macos, linux_desktop };

/** Inputs of the Steam search, explicit so every platform layout is testable. */
struct SteamSearchEnvironment final {
    HostPlatform platform{};
    std::optional<std::filesystem::path> home{};
    std::optional<std::filesystem::path> xdg_data_home{};
    /** Windows: %ProgramFiles(x86)% and %ProgramFiles%. */
    std::vector<std::filesystem::path> program_files{};
    /** Windows: HKCU SteamPath / HKLM InstallPath. */
    std::vector<std::filesystem::path> registry_steam_roots{};
};

[[nodiscard]] SteamSearchEnvironment current_steam_search_environment() noexcept;

/**
 * Steam installations that may exist on this platform, most likely first:
 * native Steam, Flatpak/Snap Steam, and Windows Steam inside Wine prefixes,
 * CrossOver bottles and Whisky bottles. Not filtered by existence.
 */
[[nodiscard]] std::vector<std::filesystem::path>
steam_root_candidates(const SteamSearchEnvironment& environment);

/**
 * Ace of Spades install folders of one Steam root or library: every library
 * listed in its libraryfolders.vdf (Windows drive paths are mapped into the
 * surrounding Wine prefix) whose appmanifest_224540.acf names an existing
 * <library>/steamapps/common/<installdir>. Conventional folder names are used
 * only when a library has no app manifest at all.
 */
[[nodiscard]] std::vector<std::filesystem::path>
steam_game_directories(const std::filesystem::path& steam_root_or_library);

/**
 * Maps a Windows path read from a Steam file inside a Wine prefix
 * ("C:\Program Files (x86)\Steam", "D:\Games") onto the host
 * (<prefix>/drive_c/..., <prefix>/dosdevices/d:/...). `inside_prefix` is any
 * path within the prefix. nullopt when the text is not a drive path or no
 * prefix surrounds `inside_prefix`.
 */
[[nodiscard]] std::optional<std::filesystem::path>
map_wine_drive_path(const std::filesystem::path& inside_prefix, std::string_view windows_path);

struct DetectedAssetSource final {
    /** The Ace of Spades folder that was found (verified or not). */
    std::optional<std::filesystem::path> folder{};
    /** Set when `folder` holds the complete required asset set. */
    std::optional<std::filesystem::path> verified_root{};
    /** Why `folder` does not verify; empty when it does. */
    std::string error{};

    [[nodiscard]] bool found() const noexcept { return folder.has_value(); }
};

/**
 * Searches every Steam installation for Ace of Spades. A folder is proposed
 * only when it really contains the game: the first one that verifies wins;
 * otherwise the first one holding any of the catalogued files is returned
 * with the reason it failed. Nothing found is not an error.
 */
[[nodiscard]] DetectedAssetSource detect_asset_source(const AssetManifest& manifest,
                                                      const SteamSearchEnvironment& environment);

/**
 * Where the folder picker should open: a detected Ace of Spades folder, else
 * an existing Steam "common" folder, else nothing (the OS default).
 */
[[nodiscard]] std::optional<std::filesystem::path>
default_asset_source_directory() noexcept;

// ---------------------------------------------------------------------------
// Where the imported files live.
//
// Normally <executable>/assets/original. When the executable folder is not
// writable (a macOS app in /Applications without permission, a translocated
// quarantined app, a read-only install), the user data folder is used
// instead: <data>/assets/original, with <data>/assets/client linked to the
// packaged <executable>/assets/client so that every "<assets>/client" and
// "<assets>/original" sibling lookup keeps working.
// ---------------------------------------------------------------------------

/**
 * %LOCALAPPDATA%\BattleSpades, ~/Library/Application Support/BattleSpades,
 * or $XDG_DATA_HOME/BattleSpades (~/.local/share/BattleSpades).
 */
[[nodiscard]] std::optional<std::filesystem::path> user_data_directory() noexcept;

struct AssetRootLocations final {
    std::filesystem::path packaged{};               ///< <executable>/assets/original
    std::optional<std::filesystem::path> user{};    ///< <data>/assets/original
};

[[nodiscard]] AssetRootLocations asset_root_locations(const std::filesystem::path& executable_directory);

/** Creates `directory` when needed and proves a file can be written in it. */
[[nodiscard]] bool directory_is_writable(const std::filesystem::path& directory) noexcept;

/**
 * Makes <user assets>/client point at <packaged assets>/client (symbolic link,
 * a junction on Windows without symlink rights, a copy as the last resort).
 * Refreshed on every start because a moved or translocated app changes the
 * packaged path.
 */
[[nodiscard]] bool link_packaged_client_assets(const std::filesystem::path& user_assets_directory,
                                               const std::filesystem::path& packaged_assets_directory,
                                               std::string& error) noexcept;

/**
 * The import destination: the packaged root when its folder is writable,
 * otherwise the prepared user data root. nullopt (with `error`) when neither
 * can be written.
 */
[[nodiscard]] std::optional<std::filesystem::path>
choose_asset_destination(const std::filesystem::path& executable_directory, std::string& error) noexcept;

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

/**
 * Turns an importer error (from find_asset_source / install_asset_tree_atomic)
 * into a message a player can act on: the original reason first, followed by
 * what to do about it (verify the Steam files, free the destination, ...).
 * "Download game assets" is suggested only when `download_offered` says that
 * choice is actually on screen.
 */
[[nodiscard]] std::string explain_asset_install_error(std::string_view error,
                                                      bool download_offered = false);

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
