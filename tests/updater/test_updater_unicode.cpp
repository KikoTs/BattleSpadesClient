// Cyrillic (non-ASCII) paths through every updater path: ZIP extraction,
// staged apply/rollback, hashing, atomic writes and backups, text/binary VDF
// round trips and Steam library detection. The temp folders are named like a
// real Russian profile ("Тодор", "Игры"); none of this may go through the
// ANSI code page (path::string(), narrow Win32 APIs, tar.exe).

#include "updater_test_support.hpp"

#include "battlespades/updater/file_util.hpp"
#include "battlespades/updater/launcher_args.hpp"
#include "battlespades/updater/sha256.hpp"
#include "battlespades/updater/steam_library.hpp"
#include "battlespades/updater/steam_registration.hpp"
#include "battlespades/updater/text_vdf.hpp"
#include "battlespades/updater/update_apply.hpp"
#include "battlespades/updater/zip_extract.hpp"

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
namespace up = battlespades::updater;
using updater_test::expect;
using updater_test::read_file;
using updater_test::write_file;

namespace {

const fs::path zip_fixtures{AOS_UPDATER_ZIP_FIXTURE_DIR};
const fs::path steam_fixtures{AOS_UPDATER_FIXTURE_DIR};

fs::path cyrillic(const fs::path& base, std::string_view utf8) { return base / up::path_from_utf8(utf8); }

void test_zip_extraction(const fs::path& root) {
    // Archive and destination both under Cyrillic folders (tar.exe cannot open these).
    const auto folder = cyrillic(root, "Загрузки");
    fs::create_directories(folder);
    const auto archive = cyrillic(folder, "пакет обновления.zip");
    fs::copy_file(zip_fixtures / "unicode-package.zip", archive);
    const auto destination = cyrillic(root, "Распаковка");
    std::string error;
    std::uint64_t last_done{};
    expect(up::extract_zip_archive(archive, destination, error,
                                   [&](std::uint64_t done, std::uint64_t) {
                                       last_done = done;
                                       return true;
                                   }),
           "Cyrillic archive extracts: " + error);
    const auto expected = nlohmann::json::parse(read_file(zip_fixtures / "unicode-package.sha256.json"));
    for (const auto& [name, digest] : expected.items()) {
        const auto file = destination / up::path_from_utf8(name);
        std::string hash_error;
        const auto actual = up::sha256_hex_file(file, hash_error);
        expect(actual.has_value() && *actual == digest.get<std::string>(),
               "extracted bytes match for " + name + " " + hash_error);
    }
    expect(fs::is_directory(cyrillic(destination, "корень/пустая")), "directory entries are created");
    expect(last_done > 70000U, "progress reported");

    // Zip slip is refused before anything is written.
    const auto slip = cyrillic(root, "опасный");
    expect(!up::extract_zip_archive(zip_fixtures / "zip-slip.zip", slip, error) &&
               error.find("escapes") != std::string::npos,
           "zip slip refused: " + error);
    expect(!fs::exists(root / "escape.txt") && !fs::exists(slip / "ok.txt"), "nothing extracted from a hostile archive");

    // A corrupted member fails its CRC (or deflate) check.
    auto bytes = read_file(zip_fixtures / "unicode-package.zip");
    const auto marker = bytes.find("stored member");
    expect(marker != std::string::npos, "fixture has a stored member");
    bytes[marker] = 'S';
    const auto corrupt = cyrillic(folder, "испорченный.zip");
    write_file(corrupt, bytes);
    expect(!up::extract_zip_archive(corrupt, cyrillic(root, "испорченный"), error) &&
               error.find("CRC") != std::string::npos,
           "corrupt member detected: " + error);

    expect(!up::extract_zip_archive(cyrillic(folder, "нет.zip"), destination, error), "missing archive reported");
    write_file(cyrillic(folder, "не zip.zip"), "not a zip file at all, just text");
    expect(!up::extract_zip_archive(cyrillic(folder, "не zip.zip"), destination, error), "non-zip refused");
}

void test_apply_and_rollback(const fs::path& root) {
    const auto install = cyrillic(root, "Игры/BattleSpades");
    const up::UpdateLayout layout{install};
    write_file(install / "BattleSpadesClient.exe", "client v1");
    write_file(cyrillic(install, "локализация/русский.json"), "старый");
    write_file(install / "battlespades-version.json", R"({"version":"0.2.0","protocol":168})");
    const auto stage = cyrillic(root, "этап");
    write_file(stage / "BattleSpadesClient.exe", "client v2");
    write_file(cyrillic(stage, "локализация/русский.json"), "новый");
    write_file(cyrillic(stage, "новый файл.txt"), "добавлен");
    up::ApplyOptions options;
    options.to_version = "0.3.0";
    const auto applied = up::apply_staged_tree(layout, stage, options);
    expect(applied.ok && applied.replaced == 2U && applied.added == 1U, "apply in a Cyrillic folder: " + applied.error);
    expect(read_file(cyrillic(install, "локализация/русский.json")) == "новый", "Cyrillic file replaced");
    const auto journal = read_file(layout.rollback_dir("client") / "journal.json");
    expect(journal.find("локализация/русский.json") != std::string::npos, "journal stores UTF-8 names");
    const auto rolled = up::rollback_last_update(layout, "client");
    expect(rolled.ok, "rollback in a Cyrillic folder: " + rolled.error);
    expect(read_file(cyrillic(install, "локализация/русский.json")) == "старый" &&
               !fs::exists(cyrillic(install, "новый файл.txt")),
           "Cyrillic files restored");
    expect(up::read_client_protocol(install) == 168U, "client protocol read from a Cyrillic folder");

    // Hashing, atomic writes and backups.
    const auto file = cyrillic(root, "Документы/настройки.vdf");
    std::string error;
    expect(up::write_file_atomic(file, "данные", error), "atomic write: " + error);
    const auto digest = up::sha256_hex_file(file, error);
    expect(digest == up::sha256_hex("данные"), "hash of a Cyrillic path");
    const auto backup = up::backup_file(file, cyrillic(root, "Резервные копии"), "метка", error);
    expect(backup.has_value() && read_file(*backup) == "данные" &&
               up::path_to_utf8(backup->filename()) == "настройки-метка.vdf",
           "backup keeps the Cyrillic name: " + error);
    // Error messages must not throw for paths outside the ANSI code page.
    const auto missing = up::sha256_hex_file(cyrillic(root, "нет файла.bin"), error);
    expect(!missing.has_value() && error.find("нет файла.bin") != std::string::npos, "UTF-8 error message: " + error);
}

void test_steam_files(const fs::path& root) {
    // A Steam library on a Cyrillic path, recorded the way Steam writes it (UTF-8, escaped).
    const auto steam = cyrillic(root, "Steam");
    const auto library = cyrillic(root, "Игры Steam");
    fs::create_directories(steam / "steamapps");
    fs::create_directories(library / "steamapps" / "common" / "aceofspades");
    auto folders = read_file(steam_fixtures / "libraryfolders.vdf.in");
    updater_test::replace_all(folders, "@STEAM_ROOT@", up::escape_vdf_string(up::path_to_utf8(steam)));
    updater_test::replace_all(folders, "@LIBRARY1@", up::escape_vdf_string(up::path_to_utf8(library)));
    write_file(steam / "steamapps" / "libraryfolders.vdf", folders);
    fs::copy_file(steam_fixtures / "appmanifest_224540.acf", library / "steamapps" / "appmanifest_224540.acf");
    std::string error;
    const auto located = up::locate_steam_app(steam, up::ace_of_spades_app_id, error);
    expect(located.has_value() && located->install_dir == library / "steamapps" / "common" / "aceofspades",
           "Ace of Spades found in a Cyrillic library: " + error);

    // Launch options pointing at a Cyrillic install, round-tripped through localconfig.vdf.
    const auto config = up::local_config_path(steam, 42U);
    fs::create_directories(config.parent_path());
    fs::copy_file(steam_fixtures / "localconfig.vdf", config);
    const auto original = read_file(config);
    const auto launcher = located->install_dir / "BattleSpades" / "BattleSpadesLauncher.exe";
    const auto value = up::make_launch_options(launcher);
    expect(value.find("Игры Steam") != std::string::npos, "launch option text is UTF-8");
    up::RegistrationState state;
    const auto backups = cyrillic(root, "Резерв Steam");
    expect(up::apply_launch_options(config, 42U, up::ace_of_spades_app_id, value, backups, "t", state, error) ==
               up::StepResult::changed,
           "launch options written: " + error);
    const auto reparsed = up::TextVdfDocument::parse(read_file(config), error);
    expect(reparsed->get_string(up::launch_options_key_path(up::ace_of_spades_app_id)) == value,
           "Cyrillic launch option reads back exactly");
    const auto state_file = cyrillic(root, "Состояние/steam-registration.json");
    expect(up::save_registration_state(state_file, state, error), "state saved under a Cyrillic path");
    const auto loaded = up::load_registration_state(state_file, error);
    expect(loaded.has_value() && loaded->launch_options[0].config_file == config, "state paths round-trip");
    expect(up::restore_launch_options(loaded->launch_options[0], backups, "u", error) == up::StepResult::changed &&
               read_file(config) == original,
           "uninstall restores the original bytes");

    // Non-Steam shortcut with a Cyrillic exe path (binary VDF stores UTF-8).
    up::SteamShortcut shortcut;
    shortcut.app_name = "BattleSpades";
    shortcut.exe = launcher;
    shortcut.start_dir = launcher.parent_path();
    std::uint32_t id{};
    const auto bytes = up::upsert_shortcut({}, shortcut, id, error);
    const std::string as_text{bytes->begin(), bytes->end()};
    auto preferred = launcher;
    preferred.make_preferred();
    expect(as_text.find(up::path_to_utf8(preferred.lexically_normal())) != std::string::npos, "shortcut Exe is UTF-8");
    bool removed{};
    const auto empty = up::remove_shortcut(*bytes, id, removed, error);
    expect(removed && empty.has_value(), "Cyrillic shortcut removed again");

    expect(up::build_windows_command_line(up::path_to_utf8(launcher), {"--connect", "Сервер"}).find("Игры Steam") !=
               std::string::npos,
           "command line keeps UTF-8 arguments");
}

} // namespace

int main() {
    return updater_test::run("aos_updater_unicode_tests", [] {
        updater_test::TempDir temp{"unicode"};
        const auto root = cyrillic(temp.path(), "Пользователи/Тодор");
        fs::create_directories(root);
        test_zip_extraction(root);
        test_apply_and_rollback(root);
        test_steam_files(root);
    });
}
