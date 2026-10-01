// BattleSpadesSetupHelper.exe: the Steam detection and registration steps of
// the installer, kept in tested C++ instead of installer script.
//
//   detect     [--steam-root DIR] [--out FILE]
//   register   --launcher EXE [--launch-options] [--shortcut] [--steam-root DIR]
//              [--account ID | --all-accounts] [--state FILE] [--backup-dir DIR] [--out FILE]
//   unregister [--state FILE] [--backup-dir DIR] [--out FILE]
//
// Exit codes: 0 success, 1 error, 2 Steam is running, 3 Steam/game/account not found.
// Steam rewrites localconfig.vdf and shortcuts.vdf when it exits, so a file
// under the live Steam installation is never written while steam.exe runs.

#include "win_util.hpp"

#include "battlespades/updater/file_util.hpp"
#include "battlespades/updater/steam_library.hpp"
#include "battlespades/updater/steam_registration.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <iostream>
#include <map>
#include <sstream>

namespace {

namespace fs = std::filesystem;
namespace up = battlespades::updater;
namespace win = battlespades::updater::win;

constexpr int exit_ok = 0;
constexpr int exit_error = 1;
constexpr int exit_steam_running = 2;
constexpr int exit_not_found = 3;

struct Options {
    std::string command;
    std::optional<fs::path> steam_root;
    std::optional<fs::path> launcher;
    std::optional<fs::path> out;
    std::optional<fs::path> state;
    std::optional<fs::path> backup_dir;
    std::optional<std::uint32_t> account;
    bool all_accounts{};
    bool launch_options{};
    bool shortcut{};
    std::string error;
};

class Output {
public:
    void set(const std::string& key, const std::string& value) {
        lines_ << key << '=' << value << "\n";
    }
    void flush(const std::optional<fs::path>& file) {
        const auto text = lines_.str();
        std::fwrite(text.data(), 1U, text.size(), stdout);
        if (file.has_value()) {
            std::string ignored;
            static_cast<void>(up::write_file_atomic(*file, text, ignored));
        }
    }

private:
    std::ostringstream lines_;
};

Options parse(const std::vector<std::string>& arguments) {
    Options options;
    if (arguments.empty()) {
        options.error = "missing command (detect, register, unregister)";
        return options;
    }
    options.command = arguments.front();
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        const auto& argument = arguments[index];
        const auto value = [&]() -> std::optional<std::string> {
            if (index + 1U >= arguments.size()) {
                options.error = argument + " requires a value";
                return std::nullopt;
            }
            return arguments[++index];
        };
        if (argument == "--launch-options") {
            options.launch_options = true;
        } else if (argument == "--shortcut") {
            options.shortcut = true;
        } else if (argument == "--all-accounts") {
            options.all_accounts = true;
        } else if (argument == "--steam-root" || argument == "--launcher" || argument == "--out" ||
                   argument == "--state" || argument == "--backup-dir" || argument == "--account") {
            const auto text = value();
            if (!text.has_value()) return options;
            if (argument == "--account") {
                try {
                    options.account = static_cast<std::uint32_t>(std::stoul(*text));
                } catch (...) {
                    options.error = "--account needs a numeric Steam account id";
                    return options;
                }
                continue;
            }
            const auto path = up::path_from_utf8(*text);
            if (argument == "--steam-root") options.steam_root = path;
            if (argument == "--launcher") options.launcher = path;
            if (argument == "--out") options.out = path;
            if (argument == "--state") options.state = path;
            if (argument == "--backup-dir") options.backup_dir = path;
        } else {
            options.error = "unknown argument: " + argument;
            return options;
        }
    }
    return options;
}

std::string lower_path(const fs::path& path) {
    std::error_code code;
    auto absolute = fs::weakly_canonical(path, code);
    if (code) absolute = path;
    auto text = up::generic_utf8(absolute.lexically_normal());
    for (auto& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

/// A file belongs to the live Steam installation (the one steam.exe may rewrite).
bool is_live_steam_file(const fs::path& file) {
    const auto live = win::registry_steam_root();
    if (!live.has_value()) return false;
    auto root = lower_path(*live);
    if (!root.empty() && root.back() != '/') root.push_back('/');
    return lower_path(file).starts_with(root);
}

bool steam_blocks(const std::vector<fs::path>& files, Output& output) {
    bool live = false;
    for (const auto& file : files) live = live || is_live_steam_file(file);
    if (live && win::is_steam_running()) {
        output.set("error", "Steam is running; exit Steam completely and retry");
        return true;
    }
    return false;
}

std::string timestamp_label() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    char text[32]{};
    std::snprintf(text, sizeof(text), "%04u%02u%02u-%02u%02u%02u", now.wYear, now.wMonth, now.wDay, now.wHour,
                  now.wMinute, now.wSecond);
    return text;
}

fs::path default_data_dir() { return win::local_app_data() / L"BattleSpades"; }

int detect(const Options& options, Output& output) {
    const auto root = options.steam_root.has_value() ? options.steam_root : win::registry_steam_root();
    output.set("steam_running", win::is_steam_running() ? "1" : "0");
    if (!root.has_value()) {
        output.set("error", "Steam is not installed");
        return exit_not_found;
    }
    output.set("steam_root", up::path_to_utf8(root->lexically_normal().make_preferred()));
    std::string error;
    const auto game = up::locate_steam_app(*root, up::ace_of_spades_app_id, error);
    if (game.has_value()) {
        output.set("library", up::path_to_utf8(game->library.lexically_normal().make_preferred()));
        output.set("game_dir", up::path_to_utf8(game->install_dir.lexically_normal().make_preferred()));
    } else {
        output.set("game_dir", "");
    }
    const auto account = up::pick_steam_account(*root);
    output.set("account", account.has_value() ? std::to_string(*account) : "");
    std::string accounts;
    for (const auto id : up::list_userdata_accounts(*root)) {
        if (!accounts.empty()) accounts += ',';
        accounts += std::to_string(id);
    }
    output.set("accounts", accounts);
    return game.has_value() ? exit_ok : exit_not_found;
}

int do_register(const Options& options, Output& output) {
    if (!options.launcher.has_value()) {
        output.set("error", "--launcher is required");
        return exit_error;
    }
    if (!options.launch_options && !options.shortcut) {
        output.set("error", "choose --launch-options and/or --shortcut");
        return exit_error;
    }
    const auto root = options.steam_root.has_value() ? options.steam_root : win::registry_steam_root();
    if (!root.has_value()) {
        output.set("error", "Steam is not installed");
        return exit_not_found;
    }
    std::vector<std::uint32_t> accounts;
    if (options.account.has_value()) {
        accounts.push_back(*options.account);
    } else if (options.all_accounts) {
        for (const auto id : up::list_userdata_accounts(*root)) {
            std::error_code code;
            if (fs::is_regular_file(up::local_config_path(*root, id), code)) accounts.push_back(id);
        }
    } else if (const auto picked = up::pick_steam_account(*root); picked.has_value()) {
        accounts.push_back(*picked);
    }
    if (accounts.empty()) {
        output.set("error", "no Steam account has signed in on this computer");
        return exit_not_found;
    }

    std::vector<fs::path> touched;
    for (const auto id : accounts) {
        if (options.launch_options) touched.push_back(up::local_config_path(*root, id));
        if (options.shortcut) touched.push_back(up::shortcuts_path(*root, id));
    }
    if (steam_blocks(touched, output)) return exit_steam_running;

    const auto state_file = options.state.value_or(default_data_dir() / L"steam-registration.json");
    const auto backup_dir = options.backup_dir.value_or(default_data_dir() / L"steam-backup");
    std::string error;
    auto state = up::load_registration_state(state_file, error);
    if (!state.has_value()) {
        output.set("error", error);
        return exit_error;
    }
    const auto label = timestamp_label();
    const auto launcher = fs::absolute(*options.launcher);
    int status = exit_ok;
    for (const auto id : accounts) {
        const auto prefix = "account." + std::to_string(id);
        if (options.launch_options) {
            const auto value = up::make_launch_options(launcher);
            const auto result = up::apply_launch_options(up::local_config_path(*root, id), id, up::ace_of_spades_app_id,
                                                         value, backup_dir, label, *state, error);
            output.set(prefix + ".launch_options", up::describe(result));
            if (result == up::StepResult::failed || result == up::StepResult::not_found) {
                output.set(prefix + ".launch_options_error", error);
                status = result == up::StepResult::not_found ? exit_not_found : exit_error;
            }
        }
        if (options.shortcut) {
            up::SteamShortcut shortcut;
            shortcut.app_name = "BattleSpades";
            shortcut.exe = launcher;
            shortcut.start_dir = launcher.parent_path();
            shortcut.icon = launcher;
            const auto result = up::apply_shortcut(up::shortcuts_path(*root, id), id, shortcut, backup_dir, label,
                                                   *state, error);
            output.set(prefix + ".shortcut", up::describe(result));
            if (result == up::StepResult::failed) {
                output.set(prefix + ".shortcut_error", error);
                status = exit_error;
            }
        }
    }
    if (!up::save_registration_state(state_file, *state, error)) {
        output.set("error", "cannot save " + up::path_to_utf8(state_file) + ": " + error);
        return exit_error;
    }
    output.set("state", up::path_to_utf8(state_file));
    output.set("backup_dir", up::path_to_utf8(backup_dir));
    return status;
}

int do_unregister(const Options& options, Output& output) {
    const auto state_file = options.state.value_or(default_data_dir() / L"steam-registration.json");
    const auto backup_dir = options.backup_dir.value_or(default_data_dir() / L"steam-backup");
    std::string error;
    auto state = up::load_registration_state(state_file, error);
    if (!state.has_value()) {
        output.set("error", error);
        return exit_error;
    }
    std::vector<fs::path> touched;
    for (const auto& record : state->launch_options) touched.push_back(record.config_file);
    for (const auto& record : state->shortcuts) touched.push_back(record.shortcuts_file);
    if (touched.empty()) {
        output.set("result", "nothing registered");
        return exit_ok;
    }
    if (steam_blocks(touched, output)) return exit_steam_running;

    const auto label = "uninstall-" + timestamp_label();
    up::RegistrationState remaining;
    int status = exit_ok;
    for (const auto& record : state->launch_options) {
        const auto result = up::restore_launch_options(record, backup_dir, label, error);
        output.set("account." + std::to_string(record.account) + ".launch_options", up::describe(result));
        if (result == up::StepResult::failed) {
            output.set("account." + std::to_string(record.account) + ".launch_options_error", error);
            remaining.launch_options.push_back(record);
            status = exit_error;
        }
    }
    for (const auto& record : state->shortcuts) {
        const auto result = up::remove_registered_shortcut(record, backup_dir, label, error);
        output.set("account." + std::to_string(record.account) + ".shortcut", up::describe(result));
        if (result == up::StepResult::failed) {
            output.set("account." + std::to_string(record.account) + ".shortcut_error", error);
            remaining.shortcuts.push_back(record);
            status = exit_error;
        }
    }
    if (remaining.launch_options.empty() && remaining.shortcuts.empty()) {
        std::error_code code;
        fs::remove(state_file, code);
    } else if (!up::save_registration_state(state_file, remaining, error)) {
        output.set("error", error);
        status = exit_error;
    }
    return status;
}

std::vector<std::string> command_line_arguments() {
    int count{};
    LPWSTR* values = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::string> arguments;
    for (int index = 1; index < count; ++index) arguments.push_back(win::narrow(values[index]));
    LocalFree(values);
    return arguments;
}

} // namespace

int main() {
    const auto options = parse(command_line_arguments());
    Output output;
    int status = exit_error;
    try {
        if (!options.error.empty()) {
            output.set("error", options.error);
        } else if (options.command == "detect") {
            status = detect(options, output);
        } else if (options.command == "register") {
            status = do_register(options, output);
        } else if (options.command == "unregister") {
            status = do_unregister(options, output);
        } else {
            output.set("error", "unknown command: " + options.command);
        }
    } catch (const std::exception& exception) {
        output.set("error", exception.what());
        status = exit_error;
    }
    output.set("exit", std::to_string(status));
    output.flush(options.out);
    return status;
}
