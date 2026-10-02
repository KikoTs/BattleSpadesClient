#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace battlespades::updater {

/**
 * BattleSpadesLauncher.exe command line.
 *
 * Steam runs launch options `"<launcher>" %command%` as
 * `"<launcher>" "<...>\aceofspades\aos.exe" [steam args]`. The retail
 * executable path is dropped (the first bare argument ending in ".exe");
 * everything else that is not a launcher switch is forwarded unchanged to
 * BattleSpadesClient.exe, so +connect / +connect_lobby invites keep working.
 * The retail exe and every argument after it are also kept verbatim in
 * `original_command`, so "Play Ace of Spades (original)" can run Steam's
 * %command% unchanged.
 */
struct LauncherArguments {
    bool no_update{};      ///< --no-update: start the installed version as-is
    bool rollback{};       ///< --rollback: restore the previous version and exit
    bool update_only{};    ///< --update-only: check/apply, never start the game
    bool choose{};         ///< --choose: show the Steam Play chooser even if a choice is remembered
    bool reset_launch_choice{};   ///< --reset-launch-choice: forget "Remember my choice", then exit
    std::vector<std::string> install_components;   ///< --install-component NAME: opt-in install, then exit
    std::string manifest_url;   ///< --update-manifest URL: override stable.json (testing)
    std::string api_url;        ///< --update-api URL: override the GitHub fallback endpoint (testing)
    std::vector<std::string> forwarded;
    std::string dropped_command;   ///< the retail exe Steam passed, if any
    std::vector<std::string> original_command;   ///< retail exe + Steam's arguments, verbatim (empty without %command%)
    std::string error;
};

[[nodiscard]] LauncherArguments parse_launcher_arguments(const std::vector<std::string>& arguments);

/// Quotes one argument by the CommandLineToArgvW / MSVC CRT rules.
[[nodiscard]] std::string quote_windows_argument(std::string_view argument);
[[nodiscard]] std::string build_windows_command_line(std::string_view executable,
                                                     const std::vector<std::string>& arguments);

/// True for a bare (non-switch) argument that names a Windows executable.
[[nodiscard]] bool looks_like_steam_command(std::string_view argument) noexcept;

} // namespace battlespades::updater
