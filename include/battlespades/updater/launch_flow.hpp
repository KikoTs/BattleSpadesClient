#pragma once

#include "battlespades/updater/steam_registration.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::updater {

// ---------------------------------------------------------------------------
// Steam Play chooser.
//
// With the Ace of Spades launch options set to `"<launcher>" %command%`, Steam
// starts the launcher with the ORIGINAL game's command line (retail aos.exe
// plus Steam's arguments). The launcher then offers "Play BattleSpades"
// (default) or "Play Ace of Spades (original)", which runs that command
// unchanged. Started without %command% (Start menu, desktop, non-Steam
// shortcut) there is nothing to choose: BattleSpades starts.
// ---------------------------------------------------------------------------

enum class LaunchTarget { battlespades, original };
enum class LaunchDecision { battlespades, original, ask };

struct LaunchChoiceInputs {
    bool has_original_command{};             ///< Steam passed the retail exe (%command%)
    std::optional<LaunchTarget> remembered;  ///< "Remember my choice"
    bool shift_held{};                       ///< Shift held while starting: ask again
    bool force_chooser{};                    ///< --choose
};

[[nodiscard]] LaunchDecision decide_launch(const LaunchChoiceInputs& inputs) noexcept;

/// update\launch-choice.json. A missing or unreadable file is "not remembered".
[[nodiscard]] std::optional<LaunchTarget> load_launch_choice(const std::filesystem::path& file);
/// nullopt clears the remembered choice (removes the file).
bool save_launch_choice(const std::filesystem::path& file, std::optional<LaunchTarget> choice, std::string& error);

[[nodiscard]] const char* to_string(LaunchTarget target) noexcept;

/**
 * The launch options the player had for Ace of Spades before BattleSpades
 * replaced them (recorded by BattleSpadesSetupHelper in
 * steam-registration.json), so "Play Ace of Spades (original)" behaves as
 * Steam would have without BattleSpades. Empty when there were none, or when
 * the recorded value is itself a BattleSpades launcher line.
 */
[[nodiscard]] std::string previous_launch_options(const RegistrationState& state, std::uint32_t app_id);

/**
 * The command line that starts the original game: Steam's %command%
 * (`original_command[0]` is the retail exe, the rest Steam's arguments, all
 * unchanged) combined with `previous_launch_options` the way Steam combines
 * launch options: a value containing %command% is a template (%command% is
 * replaced), any other value is appended.
 */
[[nodiscard]] std::string build_original_command_line(const std::vector<std::string>& original_command,
                                                      std::string_view previous_launch_options);

// ---------------------------------------------------------------------------
// First-run screen ("Get the original game files"): which choices to show, in
// which order, and which one is preselected. One screen, one click.
// ---------------------------------------------------------------------------

inline constexpr std::string_view retail_download_page_url = "https://www.aosplay.net/download#game-files";

enum class FirstRunAction {
    import_detected,     ///< "Use my Ace of Spades folder" (the Steam copy found automatically)
    choose_folder,       ///< "Select my Ace of Spades folder" (importer's folder picker)
    download,            ///< "Download game assets" (manifest component retail_assets)
    open_download_page,  ///< "Open aosplay.net/download" (no retail_assets published)
};

enum class FirstRunNote {
    none,
    download_unavailable,  ///< the manifest has no retail_assets yet
    offline,               ///< the manifest could not be reached
};

struct FirstRunInputs {
    bool game_folder_found{};   ///< Steam's Ace of Spades folder was located
    bool import_failed{};       ///< the last import attempt failed
    bool download_failed{};     ///< the last download attempt failed or was interrupted
    bool download_offered{};    ///< the manifest publishes retail_assets
    bool manifest_reachable{};  ///< stable.json was fetched
};

struct FirstRunScreen {
    std::vector<FirstRunAction> actions;   ///< in display order
    FirstRunAction preselected{FirstRunAction::choose_folder};
    FirstRunNote note{FirstRunNote::none};
    bool retry_download{};                 ///< label the download "Retry download"
};

[[nodiscard]] FirstRunScreen plan_first_run(const FirstRunInputs& inputs);

} // namespace battlespades::updater
