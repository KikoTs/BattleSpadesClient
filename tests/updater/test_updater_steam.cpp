// Text/binary VDF, Steam library detection and the Steam registration round
// trip, run only against fixture copies in a temp folder (never real Steam).

#include "updater_test_support.hpp"

#include "battlespades/updater/binary_vdf.hpp"
#include "battlespades/updater/file_util.hpp"
#include "battlespades/updater/steam_library.hpp"
#include "battlespades/updater/steam_registration.hpp"
#include "battlespades/updater/text_vdf.hpp"

namespace fs = std::filesystem;
namespace up = battlespades::updater;
using updater_test::expect;
using updater_test::read_file;
using updater_test::write_file;

namespace {

const fs::path fixtures{AOS_UPDATER_FIXTURE_DIR};

std::string escaped(const fs::path& path) { return up::escape_vdf_string(up::path_to_utf8(path.lexically_normal().make_preferred())); }

void test_text_vdf() {
    std::string error;
    const auto original = read_file(fixtures / "localconfig.vdf");
    auto document = up::TextVdfDocument::parse(original, error);
    expect(document.has_value(), "localconfig fixture parses: " + error);
    expect(document->get_string({"UserLocalConfigStore", "software", "valve", "steam", "apps", "320",
                                 "launchoptions"}) == "-insecure",
           "lookups are case-insensitive");
    expect(document->get_string({"UserLocalConfigStore", "WebStorage", "FriendStoreLocalPrefs_198097034"}) ==
               "{\"ePerFriendRunningGameNotification\":1}",
           "escaped quotes are decoded");

    // Add LaunchOptions to an existing app block: one inserted line, nothing else changes.
    const auto key = up::launch_options_key_path(up::ace_of_spades_app_id);
    const std::string value = "\"C:\\Games\\aceofspades\\BattleSpades\\BattleSpadesLauncher.exe\" %command%";
    expect(document->set_string(key, value, error), "set LaunchOptions: " + error);
    const std::string inserted =
        "\t\t\t\t\t\t\"LaunchOptions\"\t\t\"\\\"C:\\\\Games\\\\aceofspades\\\\BattleSpades\\\\BattleSpadesLauncher.exe\\\" %command%\"\n";
    std::string expected = original;
    const auto anchor = expected.find("\t\t\t\t\t}\n\t\t\t\t\t\"320\"");
    expect(anchor != std::string::npos, "fixture anchor");
    expected.insert(anchor, inserted);
    expect(document->text() == expected, "insertion is byte-exact with Steam's indentation and escaping");
    expect(document->get_string(key) == value, "value reads back unescaped");

    // Replace in place, then remove: back to the original bytes.
    expect(document->set_string(key, "-foo", error) && document->get_string(key) == "-foo", "replace in place");
    expect(document->remove(key, error), "remove");
    expect(document->text() == original, "remove restores the original bytes");
    expect(!document->remove(key, error) && error.empty(), "removing an absent key is not an error");

    // Creating the whole apps/<id> chain in a file that lacks it keeps CRLF endings.
    auto minimal = up::TextVdfDocument::parse(read_file(fixtures / "localconfig_minimal_crlf.vdf"), error);
    expect(minimal.has_value(), "CRLF fixture parses");
    expect(minimal->set_string(key, "x", error), "create missing parents: " + error);
    expect(minimal->get_string(key) == "x", "created value reads back");
    expect(minimal->text().find("\t\"Software\"\r\n\t{\r\n\t\t\"Valve\"\r\n") != std::string::npos,
           "new objects use the file's CRLF line endings and tab indentation");
    expect(minimal->text().find("\n\n") == std::string::npos && minimal->text().find("\r\r") == std::string::npos,
           "no stray line breaks");
    expect(up::TextVdfDocument::parse(minimal->text(), error).has_value(), "result re-parses");

    // Robustness.
    expect(!up::TextVdfDocument::parse("\"a\"\n{\n\t\"b\"\t\"c\"\n", error).has_value(), "missing brace rejected");
    expect(!up::TextVdfDocument::parse("\"a\"\t\"unterminated", error).has_value(), "unterminated string rejected");
    expect(up::TextVdfDocument::parse("// comment\n\"a\" { \"b\" \"c\" [$WIN32] }\n", error).has_value(),
           "comments and conditionals are tolerated");
    auto inline_braces = up::TextVdfDocument::parse("\"a\" { \"b\" \"c\" }", error);
    expect(inline_braces->set_string({"a", "d"}, "e", error) && inline_braces->get_string({"a", "b"}) == "c" &&
               inline_braces->get_string({"a", "d"}) == "e",
           "insertion into an inline object");
    expect(!document->set_string({"UserLocalConfigStore", "Software"}, "x", error), "cannot overwrite an object");
    std::string deep;
    for (int i = 0; i < 100; ++i) deep += "\"k\" {";
    expect(!up::TextVdfDocument::parse(deep, error).has_value(), "nesting depth is bounded");
    expect(up::TextVdfDocument::parse("\xEF\xBB\xBF\"a\" { \"b\" \"c\" }", error).has_value(), "UTF-8 BOM tolerated");
}

void test_library_detection() {
    updater_test::TempDir temp{"steamlib"};
    const auto steam = temp.path() / "Steam";
    const auto library = temp.path() / "Library Two";
    fs::create_directories(steam / "steamapps" / "common");
    fs::create_directories(library / "steamapps" / "common" / "aceofspades");

    auto folders_text = read_file(fixtures / "libraryfolders.vdf.in");
    updater_test::replace_all(folders_text, "@STEAM_ROOT@", escaped(steam));
    updater_test::replace_all(folders_text, "@LIBRARY1@", escaped(library));
    write_file(steam / "steamapps" / "libraryfolders.vdf", folders_text);

    std::string error;
    const auto folders = up::parse_library_folders(folders_text, error);
    expect(folders.has_value() && folders->size() == 2U, "library folders parse: " + error);
    expect((*folders)[1].path == library && (*folders)[1].apps.size() == 2U, "paths are unescaped, apps listed");

    std::string not_installed;
    expect(!up::locate_steam_app(steam, up::ace_of_spades_app_id, not_installed).has_value(),
           "a library counts only once its appmanifest exists");

    fs::copy_file(fixtures / "appmanifest_224540.acf", library / "steamapps" / "appmanifest_224540.acf");
    const auto located = up::locate_steam_app(steam, up::ace_of_spades_app_id, error);
    expect(located.has_value(), "app 224540 located: " + error);
    expect(located->library == library && located->install_dir == library / "steamapps" / "common" / "aceofspades",
           "install dir is <library>/steamapps/common/<installdir>");

    // The legacy (pre-2021) format lists bare paths.
    auto legacy = read_file(fixtures / "libraryfolders_legacy.vdf.in");
    updater_test::replace_all(legacy, "@LIBRARY1@", escaped(library));
    write_file(steam / "steamapps" / "libraryfolders.vdf", legacy);
    const auto legacy_located = up::locate_steam_app(steam, up::ace_of_spades_app_id, error);
    expect(legacy_located.has_value() && legacy_located->library == library, "legacy library format");

    // A manifest in the Steam root itself, with no libraryfolders.vdf at all.
    fs::remove(steam / "steamapps" / "libraryfolders.vdf");
    fs::remove(library / "steamapps" / "appmanifest_224540.acf");
    fs::copy_file(fixtures / "appmanifest_224540.acf", steam / "steamapps" / "appmanifest_224540.acf");
    fs::create_directories(steam / "steamapps" / "common" / "aceofspades");
    const auto root_located = up::locate_steam_app(steam, up::ace_of_spades_app_id, error);
    expect(root_located.has_value() && root_located->library == steam, "Steam root is always a library");

    expect(!up::parse_app_manifest_install_dir("\"AppState\" { \"installdir\" \"..\\\\..\\\\Windows\" }").has_value(),
           "installdir cannot escape steamapps/common");

    // Account selection.
    fs::create_directories(steam / "config");
    fs::copy_file(fixtures / "loginusers.vdf", steam / "config" / "loginusers.vdf");
    expect(!up::pick_steam_account(steam).has_value(), "no userdata folders, no account");
    const auto older = up::account_id_from_steam_id(76561198158362762ULL);
    const auto newest = up::account_id_from_steam_id(76561198000000001ULL);
    fs::create_directories(steam / "userdata" / std::to_string(older) / "config");
    fs::create_directories(steam / "userdata" / std::to_string(newest) / "config");
    fs::create_directories(steam / "userdata" / "0");
    expect(older == 198097034U, "account id is the low 32 bits of the SteamID64");
    expect(up::list_userdata_accounts(steam).size() == 2U, "userdata/0 (anonymous) is ignored");
    expect(up::pick_steam_account(steam) == newest,
           "newest Timestamp wins when no MostRecent; users without userdata are skipped");
    auto login = read_file(fixtures / "loginusers.vdf");
    updater_test::replace_all(login, "\"MostRecent\"\t\t\"0\"", "\"MostRecent\"\t\t\"1\"");
    write_file(steam / "config" / "loginusers.vdf", login);
    expect(up::pick_steam_account(steam) == older, "MostRecent=1 wins");
}

void test_binary_vdf_and_shortcuts() {
    // The 13-byte file Steam writes for "no shortcuts".
    const std::vector<std::uint8_t> empty_file{0x00, 's', 'h', 'o', 'r', 't', 'c', 'u', 't', 's', 0x00, 0x08, 0x08};
    std::string error;
    const auto parsed = up::parse_binary_vdf(empty_file, error);
    expect(parsed.has_value() && parsed->size() == 1U && up::serialize_binary_vdf(*parsed) == empty_file,
           "empty shortcuts.vdf round-trips");

    expect(up::crc32("123456789") == 0xCBF43926U, "CRC-32 check value");

    up::SteamShortcut other;
    other.app_name = "Other Game";
    other.exe = "C:\\Games\\Other\\other.exe";
    other.start_dir = "C:\\Games\\Other";
    std::uint32_t other_id{};
    auto file = up::upsert_shortcut(empty_file, other, other_id, error);
    expect(file.has_value(), "first shortcut added: " + error);
    // Give the foreign entry a field type we do not create ourselves to prove it survives.
    auto with_extra = up::parse_binary_vdf(*file, error);
    auto& entry = with_extra->front().children.front();
    up::BinaryVdfNode unusual;
    unusual.type = up::BinaryVdfType::uint64;
    unusual.key = "Custom64";
    unusual.raw = {1, 2, 3, 4, 5, 6, 7, 8};
    entry.children.push_back(unusual);
    const auto foreign = up::serialize_binary_vdf(*with_extra);

    up::SteamShortcut ours;
    ours.app_name = "BattleSpades";
    ours.exe = "C:\\Steam\\steamapps\\common\\aceofspades\\BattleSpades\\BattleSpadesLauncher.exe";
    ours.start_dir = "C:\\Steam\\steamapps\\common\\aceofspades\\BattleSpades";
    ours.icon = ours.exe;
    std::uint32_t our_id{};
    const auto added = up::upsert_shortcut(foreign, ours, our_id, error);
    expect(added.has_value() && up::has_shortcut(*added, our_id) && up::has_shortcut(*added, other_id),
           "our shortcut is added beside the existing one");
    expect((our_id & 0x80000000U) != 0U &&
               our_id == up::shortcut_app_id("\"" + up::path_to_utf8(fs::path{ours.exe}.lexically_normal()) + "\"",
                                             "BattleSpades"),
           "shortcut id follows Steam's crc32(exe + name) | 0x80000000");
    std::uint32_t again_id{};
    const auto again = up::upsert_shortcut(*added, ours, again_id, error);
    expect(again.has_value() && *again == *added && again_id == our_id, "re-adding is idempotent");

    bool removed{};
    const auto after = up::remove_shortcut(*added, our_id, removed, error);
    expect(after.has_value() && removed && *after == foreign,
           "removing ours restores the previous file byte-for-byte");

    // Removing an earlier entry renumbers the rest.
    const auto both = up::upsert_shortcut(foreign, ours, our_id, error);
    const auto without_other = up::remove_shortcut(*both, other_id, removed, error);
    const auto roots = up::parse_binary_vdf(*without_other, error);
    expect(roots->front().children.size() == 1U && roots->front().children.front().key == "0",
           "entries are renumbered from 0");

    std::vector<std::uint8_t> truncated(foreign.begin(), foreign.end() - 3);
    expect(!up::parse_binary_vdf(truncated, error).has_value(), "truncated binary VDF rejected");
    auto trailing = foreign;
    trailing.push_back(0x08);
    expect(!up::parse_binary_vdf(trailing, error).has_value(), "trailing garbage rejected");
}

void test_registration_round_trip() {
    updater_test::TempDir temp{"steamreg"};
    const auto steam = temp.path() / "Steam";
    const auto account = 198097034U;
    const auto config = up::local_config_path(steam, account);
    fs::create_directories(config.parent_path());
    fs::copy_file(fixtures / "localconfig.vdf", config);
    const auto original = read_file(config);
    const auto backups = temp.path() / "backup";
    const auto state_file = temp.path() / "state" / "steam-registration.json";
    const auto launcher = fs::path{"C:\\Steam\\steamapps\\common\\aceofspades\\BattleSpades\\BattleSpadesLauncher.exe"};
    const auto value = up::make_launch_options(launcher);
    expect(value == "\"C:\\Steam\\steamapps\\common\\aceofspades\\BattleSpades\\BattleSpadesLauncher.exe\" %command%",
           "launch option text");

    std::string error;
    up::RegistrationState state;
    auto result = up::apply_launch_options(config, account, up::ace_of_spades_app_id, value, backups, "t1", state, error);
    expect(result == up::StepResult::changed, std::string{"launch options applied: "} + up::describe(result) + " " + error);
    expect(fs::exists(backups / "localconfig-t1.vdf") && read_file(backups / "localconfig-t1.vdf") == original,
           "the untouched file was backed up first");
    expect(state.launch_options.size() == 1U && !state.launch_options[0].had_previous, "no previous value recorded");
    expect(up::save_registration_state(state_file, state, error), "state saved");

    // Re-running the installer changes nothing and keeps the original record.
    result = up::apply_launch_options(config, account, up::ace_of_spades_app_id, value, backups, "t2", state, error);
    expect(result == up::StepResult::unchanged && state.launch_options.size() == 1U, "idempotent reinstall");

    const auto loaded = up::load_registration_state(state_file, error);
    expect(loaded.has_value() && loaded->launch_options.size() == 1U &&
               loaded->launch_options[0].applied == value && loaded->launch_options[0].config_file == config,
           "state file round-trips");
    result = up::restore_launch_options(loaded->launch_options[0], backups, "u1", error);
    expect(result == up::StepResult::changed && read_file(config) == original,
           "uninstall removes the key and restores the original bytes");

    // A pre-existing value is put back, and a value the user changed later is left alone.
    const auto with_options = up::local_config_path(steam, 5U);
    fs::create_directories(with_options.parent_path());
    fs::copy_file(fixtures / "localconfig_with_options.vdf", with_options);
    const auto before = read_file(with_options);
    up::RegistrationState second;
    result = up::apply_launch_options(with_options, 5U, up::ace_of_spades_app_id, value, backups, "t3", second, error);
    expect(result == up::StepResult::changed && second.launch_options[0].had_previous &&
               second.launch_options[0].previous == "-windowed",
           "previous launch options recorded");
    result = up::restore_launch_options(second.launch_options[0], backups, "u2", error);
    expect(result == up::StepResult::changed && read_file(with_options) == before, "previous value restored exactly");

    result = up::apply_launch_options(with_options, 5U, up::ace_of_spades_app_id, value, backups, "t4", second, error);
    auto edited = up::TextVdfDocument::parse(read_file(with_options), error);
    expect(edited->set_string(up::launch_options_key_path(up::ace_of_spades_app_id), "-user-choice", error),
           "simulate a user edit");
    write_file(with_options, edited->text());
    result = up::restore_launch_options(second.launch_options[0], backups, "u3", error);
    expect(result == up::StepResult::user_modified &&
               read_file(with_options).find("-user-choice") != std::string::npos,
           "a value the user changed since is never overwritten");

    expect(up::apply_launch_options(temp.path() / "missing.vdf", 1U, up::ace_of_spades_app_id, value, backups, "t5",
                                    second, error) == up::StepResult::not_found,
           "a missing localconfig.vdf is reported, not created");

    // Non-Steam shortcut: created in a new file, removed on uninstall.
    const auto shortcuts = up::shortcuts_path(steam, account);
    up::SteamShortcut shortcut;
    shortcut.app_name = "BattleSpades";
    shortcut.exe = launcher;
    shortcut.start_dir = launcher.parent_path();
    shortcut.icon = launcher;
    up::RegistrationState third;
    result = up::apply_shortcut(shortcuts, account, shortcut, backups, "t6", third, error);
    expect(result == up::StepResult::changed && fs::exists(shortcuts) && third.shortcuts.size() == 1U &&
               !third.shortcuts[0].file_existed,
           "shortcut written: " + error);
    const auto bytes = up::read_binary_file(shortcuts, error);
    expect(up::has_shortcut(*bytes, third.shortcuts[0].app_id), "shortcut present");
    result = up::remove_registered_shortcut(third.shortcuts[0], backups, "u4", error);
    expect(result == up::StepResult::changed && !fs::exists(shortcuts),
           "a shortcuts.vdf we created is deleted again on uninstall");

    // In an existing file only our entry goes.
    up::SteamShortcut other;
    other.app_name = "Other";
    other.exe = "C:\\Other\\other.exe";
    std::uint32_t other_id{};
    const auto existing = up::upsert_shortcut({}, other, other_id, error);
    write_file(shortcuts, std::string{existing->begin(), existing->end()});
    up::RegistrationState fourth;
    result = up::apply_shortcut(shortcuts, account, shortcut, backups, "t7", fourth, error);
    expect(result == up::StepResult::changed && fourth.shortcuts[0].file_existed, "added to an existing file");
    result = up::remove_registered_shortcut(fourth.shortcuts[0], backups, "u5", error);
    const auto after = up::read_binary_file(shortcuts, error);
    expect(result == up::StepResult::changed && after.has_value() && *after == *existing,
           "uninstall leaves the other shortcuts byte-identical");
}

} // namespace

int main() {
    return updater_test::run("aos_updater_steam_tests", [] {
        test_text_vdf();
        test_library_detection();
        test_binary_vdf_and_shortcuts();
        test_registration_round_trip();
    });
}
