#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::updater {

inline constexpr std::uint32_t ace_of_spades_app_id = 224540U;
inline constexpr std::uint64_t steam_id64_base = 76561197960265728ULL;

struct SteamLibraryFolder {
    std::filesystem::path path;
    std::vector<std::uint32_t> apps;   ///< empty for the pre-2021 file format
};

/// Both the current ("0" { "path" ... "apps" {...} }) and legacy ("1" "D:\\Lib") formats.
[[nodiscard]] std::optional<std::vector<SteamLibraryFolder>>
parse_library_folders(std::string_view vdf_text, std::string& error);

/// The "installdir" of an appmanifest_<id>.acf.
[[nodiscard]] std::optional<std::string> parse_app_manifest_install_dir(std::string_view acf_text);

struct SteamGameLocation {
    std::filesystem::path library;
    std::filesystem::path install_dir;   ///< <library>/steamapps/common/<installdir>
};

/**
 * Finds which library holds `app_id`. Libraries whose "apps" list names the
 * app are tried first; a library counts only when its appmanifest exists and
 * names an install directory that exists.
 */
[[nodiscard]] std::optional<SteamGameLocation>
locate_steam_app(const std::filesystem::path& steam_root, std::uint32_t app_id, std::string& error);

struct SteamLoginUser {
    std::uint64_t steam_id{};
    std::string account_name;
    bool most_recent{};
    std::uint64_t timestamp{};
};

[[nodiscard]] std::vector<SteamLoginUser> parse_login_users(std::string_view vdf_text);
[[nodiscard]] std::uint32_t account_id_from_steam_id(std::uint64_t steam_id) noexcept;
/// Numeric directories under <steam>/userdata, excluding "0" (anonymous).
[[nodiscard]] std::vector<std::uint32_t> list_userdata_accounts(const std::filesystem::path& steam_root);

/**
 * The account a per-user Steam setting should be written for: the
 * loginusers.vdf "MostRecent" user, else the newest "Timestamp", else the
 * userdata account whose localconfig.vdf was written last. Only accounts
 * with a userdata directory qualify.
 */
[[nodiscard]] std::optional<std::uint32_t> pick_steam_account(const std::filesystem::path& steam_root);

[[nodiscard]] std::filesystem::path local_config_path(const std::filesystem::path& steam_root,
                                                      std::uint32_t account);
[[nodiscard]] std::filesystem::path shortcuts_path(const std::filesystem::path& steam_root,
                                                   std::uint32_t account);

/// {"UserLocalConfigStore","Software","Valve","Steam","apps","<id>","LaunchOptions"}
[[nodiscard]] std::vector<std::string> launch_options_key_path(std::uint32_t app_id);
/// "\"<launcher>\" %command%" — Steam substitutes %command% with the retail exe.
[[nodiscard]] std::string make_launch_options(const std::filesystem::path& launcher);

/// Steam's non-Steam shortcut id: crc32(exe + name) | 0x80000000.
[[nodiscard]] std::uint32_t shortcut_app_id(std::string_view quoted_exe, std::string_view app_name) noexcept;
[[nodiscard]] std::uint32_t crc32(std::string_view data) noexcept;

struct SteamShortcut {
    std::string app_name;
    std::filesystem::path exe;
    std::filesystem::path start_dir;
    std::filesystem::path icon;
    std::string launch_options;
};

/**
 * Adds (or refreshes) one shortcut in shortcuts.vdf bytes. Empty input is a
 * new file. Every other entry is preserved byte-for-byte; entries are
 * renumbered only on removal. Returns the new file bytes.
 */
[[nodiscard]] std::optional<std::vector<std::uint8_t>>
upsert_shortcut(const std::vector<std::uint8_t>& file, const SteamShortcut& shortcut,
                std::uint32_t& app_id, std::string& error);
/// Removes the entry whose appid matches. `removed` reports whether it existed.
[[nodiscard]] std::optional<std::vector<std::uint8_t>>
remove_shortcut(const std::vector<std::uint8_t>& file, std::uint32_t app_id, bool& removed,
                std::string& error);
/// Presence test used by tests and the helper's status output.
[[nodiscard]] bool has_shortcut(const std::vector<std::uint8_t>& file, std::uint32_t app_id);

} // namespace battlespades::updater
