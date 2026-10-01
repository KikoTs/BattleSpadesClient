#include "battlespades/updater/steam_registration.hpp"

#include "battlespades/updater/file_util.hpp"
#include "battlespades/updater/text_vdf.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace battlespades::updater {
namespace {

[[nodiscard]] std::string key_of(const std::filesystem::path& path) {
    auto text = generic_utf8(path.lexically_normal());
    for (auto& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

[[nodiscard]] bool write_vdf(const std::filesystem::path& file, const TextVdfDocument& document,
                             std::string& error) {
    return write_file_atomic(file, document.text(), error);
}

} // namespace

const char* describe(StepResult result) noexcept {
    switch (result) {
    case StepResult::changed: return "changed";
    case StepResult::unchanged: return "unchanged";
    case StepResult::user_modified: return "left alone (changed by the user since)";
    case StepResult::not_found: return "not found";
    case StepResult::failed: return "failed";
    }
    return "unknown";
}

std::optional<RegistrationState> load_registration_state(const std::filesystem::path& file,
                                                         std::string& error) {
    RegistrationState state;
    std::error_code code;
    if (!std::filesystem::exists(file, code)) return state;
    const auto text = read_text_file(file, error);
    if (!text.has_value()) return std::nullopt;
    try {
        const auto json = nlohmann::json::parse(*text);
        if (json.value("schema", 0) != 1) {
            error = "unsupported registration state schema";
            return std::nullopt;
        }
        for (const auto& item : json.value("launch_options", nlohmann::json::array())) {
            LaunchOptionRecord record;
            record.account = item.at("account").get<std::uint32_t>();
            record.app_id = item.value("app_id", ace_of_spades_app_id);
            record.config_file = path_from_utf8(item.at("config_file").get<std::string>());
            record.had_previous = item.at("had_previous").get<bool>();
            record.previous = item.value("previous", std::string{});
            record.applied = item.at("applied").get<std::string>();
            record.backup = path_from_utf8(item.value("backup", std::string{}));
            state.launch_options.push_back(std::move(record));
        }
        for (const auto& item : json.value("shortcuts", nlohmann::json::array())) {
            ShortcutRecord record;
            record.account = item.at("account").get<std::uint32_t>();
            record.shortcuts_file = path_from_utf8(item.at("shortcuts_file").get<std::string>());
            record.app_id = item.at("app_id").get<std::uint32_t>();
            record.file_existed = item.value("file_existed", true);
            record.backup = path_from_utf8(item.value("backup", std::string{}));
            state.shortcuts.push_back(std::move(record));
        }
    } catch (const std::exception& exception) {
        error = std::string{"invalid registration state: "} + exception.what();
        return std::nullopt;
    }
    return state;
}

bool save_registration_state(const std::filesystem::path& file, const RegistrationState& state,
                             std::string& error) {
    nlohmann::json json;
    json["schema"] = 1;
    json["launch_options"] = nlohmann::json::array();
    for (const auto& record : state.launch_options) {
        json["launch_options"].push_back({
            {"account", record.account},
            {"app_id", record.app_id},
            {"config_file", path_to_utf8(record.config_file)},
            {"had_previous", record.had_previous},
            {"previous", record.previous},
            {"applied", record.applied},
            {"backup", path_to_utf8(record.backup)},
        });
    }
    json["shortcuts"] = nlohmann::json::array();
    for (const auto& record : state.shortcuts) {
        json["shortcuts"].push_back({
            {"account", record.account},
            {"shortcuts_file", path_to_utf8(record.shortcuts_file)},
            {"app_id", record.app_id},
            {"file_existed", record.file_existed},
            {"backup", path_to_utf8(record.backup)},
        });
    }
    std::error_code code;
    std::filesystem::create_directories(file.parent_path(), code);
    return write_file_atomic(file, json.dump(2) + "\n", error);
}

StepResult apply_launch_options(const std::filesystem::path& local_config, std::uint32_t account,
                                std::uint32_t app_id, std::string_view value,
                                const std::filesystem::path& backup_dir, std::string_view backup_label,
                                RegistrationState& state, std::string& error) {
    std::error_code code;
    if (!std::filesystem::is_regular_file(local_config, code)) {
        error = path_to_utf8(local_config) + " does not exist (sign in to Steam once first)";
        return StepResult::not_found;
    }
    const auto text = read_text_file(local_config, error);
    if (!text.has_value()) return StepResult::failed;
    auto document = TextVdfDocument::parse(*text, error);
    if (!document.has_value()) {
        error = "cannot parse " + path_to_utf8(local_config) + ": " + error;
        return StepResult::failed;
    }
    const auto key = launch_options_key_path(app_id);
    const auto current = document->get_string(key);

    auto existing = std::ranges::find_if(state.launch_options, [&](const LaunchOptionRecord& r) {
        return r.app_id == app_id && key_of(r.config_file) == key_of(local_config);
    });
    if (current.has_value() && *current == value) {
        if (existing == state.launch_options.end()) {
            // Already ours (e.g. a reinstall after the state was lost). Uninstall
            // then removes it rather than restoring an unknown value.
            state.launch_options.push_back(
                LaunchOptionRecord{account, app_id, local_config, false, {}, std::string{value}, {}});
        }
        return StepResult::unchanged;
    }

    const auto backup = backup_file(local_config, backup_dir, backup_label, error);
    if (!backup.has_value()) return StepResult::failed;
    if (!document->set_string(key, value, error)) return StepResult::failed;
    if (!write_vdf(local_config, *document, error)) return StepResult::failed;

    if (existing == state.launch_options.end()) {
        state.launch_options.push_back(LaunchOptionRecord{account, app_id, local_config,
                                                          current.has_value(), current.value_or(""),
                                                          std::string{value}, *backup});
    } else {
        // The original pre-BattleSpades value stays the one to restore.
        existing->applied = std::string{value};
    }
    return StepResult::changed;
}

StepResult restore_launch_options(const LaunchOptionRecord& record,
                                  const std::filesystem::path& backup_dir,
                                  std::string_view backup_label, std::string& error) {
    std::error_code code;
    if (!std::filesystem::is_regular_file(record.config_file, code)) {
        error = path_to_utf8(record.config_file) + " no longer exists";
        return StepResult::not_found;
    }
    const auto text = read_text_file(record.config_file, error);
    if (!text.has_value()) return StepResult::failed;
    auto document = TextVdfDocument::parse(*text, error);
    if (!document.has_value()) return StepResult::failed;
    const auto key = launch_options_key_path(record.app_id);
    const auto current = document->get_string(key);
    if (!current.has_value() || *current != record.applied) return StepResult::user_modified;

    if (!backup_file(record.config_file, backup_dir, backup_label, error).has_value()) {
        return StepResult::failed;
    }
    if (record.had_previous) {
        if (!document->set_string(key, record.previous, error)) return StepResult::failed;
    } else if (!document->remove(key, error) && !error.empty()) {
        return StepResult::failed;
    }
    if (!write_vdf(record.config_file, *document, error)) return StepResult::failed;
    return StepResult::changed;
}

StepResult apply_shortcut(const std::filesystem::path& shortcuts_file, std::uint32_t account,
                          const SteamShortcut& shortcut, const std::filesystem::path& backup_dir,
                          std::string_view backup_label, RegistrationState& state,
                          std::string& error) {
    std::error_code code;
    const bool existed = std::filesystem::is_regular_file(shortcuts_file, code);
    std::vector<std::uint8_t> bytes;
    std::filesystem::path backup;
    if (existed) {
        auto loaded = read_binary_file(shortcuts_file, error);
        if (!loaded.has_value()) return StepResult::failed;
        bytes = std::move(*loaded);
    }
    std::uint32_t app_id{};
    const auto updated = upsert_shortcut(bytes, shortcut, app_id, error);
    if (!updated.has_value()) {
        error = "cannot parse " + path_to_utf8(shortcuts_file) + ": " + error;
        return StepResult::failed;
    }
    const bool recorded = std::ranges::any_of(state.shortcuts, [&](const ShortcutRecord& r) {
        return r.app_id == app_id && key_of(r.shortcuts_file) == key_of(shortcuts_file);
    });
    if (*updated == bytes) {
        if (!recorded) state.shortcuts.push_back(ShortcutRecord{account, shortcuts_file, app_id, existed, {}});
        return StepResult::unchanged;
    }
    if (existed) {
        auto made = backup_file(shortcuts_file, backup_dir, backup_label, error);
        if (!made.has_value()) return StepResult::failed;
        backup = *made;
    } else {
        std::filesystem::create_directories(shortcuts_file.parent_path(), code);
    }
    const std::string_view view{reinterpret_cast<const char*>(updated->data()), updated->size()};
    if (!write_file_atomic(shortcuts_file, view, error)) return StepResult::failed;
    if (!recorded) state.shortcuts.push_back(ShortcutRecord{account, shortcuts_file, app_id, existed, backup});
    return StepResult::changed;
}

StepResult remove_registered_shortcut(const ShortcutRecord& record,
                                      const std::filesystem::path& backup_dir,
                                      std::string_view backup_label, std::string& error) {
    std::error_code code;
    if (!std::filesystem::is_regular_file(record.shortcuts_file, code)) return StepResult::not_found;
    const auto bytes = read_binary_file(record.shortcuts_file, error);
    if (!bytes.has_value()) return StepResult::failed;
    bool removed{};
    const auto updated = remove_shortcut(*bytes, record.app_id, removed, error);
    if (!updated.has_value()) return StepResult::failed;
    if (!removed) return StepResult::not_found;
    if (!backup_file(record.shortcuts_file, backup_dir, backup_label, error).has_value()) {
        return StepResult::failed;
    }
    // We created the file and it holds nothing else: leave no trace.
    const std::vector<std::uint8_t> empty_file{0x00, 's', 'h', 'o', 'r', 't', 'c', 'u', 't', 's', 0x00, 0x08, 0x08};
    if (!record.file_existed && *updated == empty_file) {
        std::filesystem::remove(record.shortcuts_file, code);
        if (!code) return StepResult::changed;
    }
    const std::string_view view{reinterpret_cast<const char*>(updated->data()), updated->size()};
    if (!write_file_atomic(record.shortcuts_file, view, error)) return StepResult::failed;
    return StepResult::changed;
}

} // namespace battlespades::updater
