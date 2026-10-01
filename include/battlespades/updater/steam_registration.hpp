#pragma once

#include "battlespades/updater/steam_library.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::updater {

/**
 * What the installer changed in Steam, so uninstall can put back exactly the
 * previous value and nothing else. Stored as JSON outside the install folder
 * (%LOCALAPPDATA%\BattleSpades\steam-registration.json) together with a full
 * copy of every Steam file taken before its first modification.
 */
struct LaunchOptionRecord {
    std::uint32_t account{};
    std::uint32_t app_id{ace_of_spades_app_id};
    std::filesystem::path config_file;
    bool had_previous{};
    std::string previous;
    std::string applied;
    std::filesystem::path backup;
};

struct ShortcutRecord {
    std::uint32_t account{};
    std::filesystem::path shortcuts_file;
    std::uint32_t app_id{};
    bool file_existed{};
    std::filesystem::path backup;
};

struct RegistrationState {
    std::vector<LaunchOptionRecord> launch_options;
    std::vector<ShortcutRecord> shortcuts;
};

/// A missing file is an empty state.
[[nodiscard]] std::optional<RegistrationState> load_registration_state(const std::filesystem::path& file,
                                                                       std::string& error);
bool save_registration_state(const std::filesystem::path& file, const RegistrationState& state,
                             std::string& error);

enum class StepResult {
    changed,
    unchanged,
    user_modified,   ///< restore skipped: the value is no longer ours
    not_found,
    failed,
};

[[nodiscard]] const char* describe(StepResult result) noexcept;

/// Sets UserLocalConfigStore/Software/Valve/Steam/apps/<app>/LaunchOptions.
[[nodiscard]] StepResult apply_launch_options(const std::filesystem::path& local_config,
                                              std::uint32_t account, std::uint32_t app_id,
                                              std::string_view value,
                                              const std::filesystem::path& backup_dir,
                                              std::string_view backup_label,
                                              RegistrationState& state, std::string& error);

/// Puts back the previous value (or removes the key) only while it is still ours.
[[nodiscard]] StepResult restore_launch_options(const LaunchOptionRecord& record,
                                                const std::filesystem::path& backup_dir,
                                                std::string_view backup_label, std::string& error);

[[nodiscard]] StepResult apply_shortcut(const std::filesystem::path& shortcuts_file,
                                        std::uint32_t account, const SteamShortcut& shortcut,
                                        const std::filesystem::path& backup_dir,
                                        std::string_view backup_label, RegistrationState& state,
                                        std::string& error);

[[nodiscard]] StepResult remove_registered_shortcut(const ShortcutRecord& record,
                                                    const std::filesystem::path& backup_dir,
                                                    std::string_view backup_label,
                                                    std::string& error);

} // namespace battlespades::updater
