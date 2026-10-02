#include "battlespades/updater/launch_flow.hpp"

#include "battlespades/updater/file_util.hpp"
#include "battlespades/updater/launcher_args.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace battlespades::updater {

namespace fs = std::filesystem;

LaunchDecision decide_launch(const LaunchChoiceInputs& inputs) noexcept {
    // Without Steam's %command% there is no original game to offer.
    if (!inputs.has_original_command) return LaunchDecision::battlespades;
    if (inputs.force_chooser || inputs.shift_held || !inputs.remembered.has_value()) return LaunchDecision::ask;
    return *inputs.remembered == LaunchTarget::original ? LaunchDecision::original : LaunchDecision::battlespades;
}

const char* to_string(LaunchTarget target) noexcept {
    return target == LaunchTarget::original ? "original" : "battlespades";
}

std::optional<LaunchTarget> load_launch_choice(const fs::path& file) {
    std::string error;
    const auto text = read_text_file(file, error);
    if (!text.has_value()) return std::nullopt;
    try {
        const auto json = nlohmann::json::parse(*text);
        const auto value = json.value("remember", std::string{});
        if (value == "battlespades") return LaunchTarget::battlespades;
        if (value == "original") return LaunchTarget::original;
    } catch (...) {
    }
    return std::nullopt;
}

bool save_launch_choice(const fs::path& file, std::optional<LaunchTarget> choice, std::string& error) {
    std::error_code code;
    if (!choice.has_value()) {
        fs::remove(file, code);
        if (code) {
            error = code.message();
            return false;
        }
        return true;
    }
    fs::create_directories(file.parent_path(), code);
    nlohmann::json json;
    json["schema"] = 1;
    json["remember"] = to_string(*choice);
    return write_file_atomic(file, json.dump(2) + "\n", error);
}

namespace {

[[nodiscard]] bool contains_ascii_case_insensitive(std::string_view text, std::string_view needle) {
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
    return std::ranges::search(text, needle, [&](char a, char b) { return lower(a) == lower(b); }).begin() !=
           text.end();
}

[[nodiscard]] std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1U);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) {
        text.remove_suffix(1U);
    }
    return text;
}

} // namespace

std::string previous_launch_options(const RegistrationState& state, std::uint32_t app_id) {
    for (const auto& record : state.launch_options) {
        if (record.app_id != app_id || !record.had_previous) continue;
        const auto value = trim(record.previous);
        if (value.empty() || contains_ascii_case_insensitive(value, "BattleSpadesLauncher")) continue;
        return std::string{value};
    }
    return {};
}

std::string build_original_command_line(const std::vector<std::string>& original_command,
                                        std::string_view previous_launch_options) {
    if (original_command.empty()) return {};
    const std::vector<std::string> arguments(original_command.begin() + 1, original_command.end());
    const auto command = build_windows_command_line(original_command.front(), arguments);
    const auto previous = trim(previous_launch_options);
    if (previous.empty()) return command;
    constexpr std::string_view placeholder = "%command%";
    if (const auto at = previous.find(placeholder); at != std::string_view::npos) {
        // A wrapper template: Steam substitutes %command% textually.
        std::string line{previous};
        for (std::size_t pos = line.find(placeholder); pos != std::string::npos;
             pos = line.find(placeholder, pos + command.size())) {
            line.replace(pos, placeholder.size(), command);
        }
        return line;
    }
    return command + " " + std::string{previous};
}

FirstRunScreen plan_first_run(const FirstRunInputs& inputs) {
    FirstRunScreen screen;
    const bool download = inputs.download_offered && inputs.manifest_reachable;
    screen.retry_download = download && inputs.download_failed;
    auto& actions = screen.actions;
    if (download) {
        if (inputs.game_folder_found && !inputs.import_failed) {
            actions = {FirstRunAction::import_detected, FirstRunAction::download, FirstRunAction::choose_folder};
            screen.preselected = inputs.download_failed ? FirstRunAction::download : FirstRunAction::import_detected;
        } else if (inputs.game_folder_found) {
            // The Steam copy did not verify: the download is the reliable fallback.
            actions = {FirstRunAction::download, FirstRunAction::choose_folder, FirstRunAction::import_detected};
            screen.preselected = FirstRunAction::download;
        } else {
            actions = {FirstRunAction::download, FirstRunAction::choose_folder};
            screen.preselected = FirstRunAction::download;
        }
        return screen;
    }
    screen.note = inputs.manifest_reachable ? FirstRunNote::download_unavailable : FirstRunNote::offline;
    if (inputs.game_folder_found && !inputs.import_failed) {
        actions = {FirstRunAction::import_detected, FirstRunAction::choose_folder, FirstRunAction::open_download_page};
        screen.preselected = FirstRunAction::import_detected;
    } else if (inputs.game_folder_found) {
        actions = {FirstRunAction::choose_folder, FirstRunAction::import_detected, FirstRunAction::open_download_page};
        screen.preselected = FirstRunAction::choose_folder;
    } else {
        actions = {FirstRunAction::choose_folder, FirstRunAction::open_download_page};
        screen.preselected = FirstRunAction::choose_folder;
    }
    return screen;
}

} // namespace battlespades::updater
