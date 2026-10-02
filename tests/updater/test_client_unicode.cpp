// Cyrillic paths through the client's asset importer and local-server launch
// preparation. path::string() / generic_string() convert through the ANSI
// code page and throw (or produce '?') for characters outside it; these
// tests run without the UTF-8 code-page manifest, so they catch any such use.

#include "battlespades/assets/asset_install.hpp"
#include "battlespades/platform/hosting_gate.hpp"
#include "battlespades/platform/local_server_process.hpp"

#include <sodium.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error{message};
}

fs::path utf8_path(std::string_view text) {
    return fs::path{std::u8string{reinterpret_cast<const char8_t*>(text.data()), text.size()}};
}

std::string generic_utf8(const fs::path& path) {
    const auto encoded = path.generic_u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

std::string utf8_text(const fs::path& path) {
    const auto encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

void write_file(const fs::path& file, std::string_view text) {
    fs::create_directories(file.parent_path());
    std::ofstream output{file, std::ios::binary | std::ios::trunc};
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) throw std::runtime_error{"cannot write " + utf8_text(file)};
}

std::string read_file(const fs::path& file) {
    std::ifstream input{file, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

std::string sha256(std::string_view data) {
    unsigned char digest[crypto_hash_sha256_BYTES]{};
    crypto_hash_sha256(digest, reinterpret_cast<const unsigned char*>(data.data()), data.size());
    static constexpr char hex[] = "0123456789abcdef";
    std::string text;
    for (const auto byte : digest) {
        text.push_back(hex[byte >> 4U]);
        text.push_back(hex[byte & 0x0FU]);
    }
    return text;
}

void test_asset_importer(const fs::path& root) {
    battlespades::assets::AssetManifest manifest;
    const std::string png = "retail png bytes";
    const std::string map = "retail vxl bytes";
    manifest.files.push_back({fs::path{"png/a.png"}, png.size(), sha256(png)});
    manifest.files.push_back({fs::path{"maps/Training.vxl"}, map.size(), sha256(map)});
    manifest.total_bytes = png.size() + map.size();

    // A Steam library under a Cyrillic folder, next to other Cyrillic folders
    // the discovery walks past (their names used to throw in path_text()).
    const auto library = root / utf8_path("Библиотека Steam");
    const auto game = library / "steamapps" / "common" / "aceofspades";
    write_file(game / "png" / "a.png", png);
    write_file(game / "maps" / "Training.vxl", map);
    fs::create_directories(library / utf8_path("Мои игры"));
    fs::create_directories(library / utf8_path("Ace of Spades копия"));

    std::string error;
    const auto found = battlespades::assets::find_asset_source(library, manifest, error);
    expect(found.has_value() && fs::equivalent(*found, game), "importer finds the game in a Cyrillic library: " + error);

    // A folder that does not match reports UTF-8 details instead of throwing.
    const auto wrong = root / utf8_path("Пустая папка");
    fs::create_directories(wrong);
    expect(!battlespades::assets::find_asset_source(wrong, manifest, error).has_value() &&
               error.find("Пустая папка") != std::string::npos,
           "mismatch error names the Cyrillic folder: " + error);

    // Install into a Cyrillic install folder (assets/original).
    const auto destination = root / utf8_path("Игры") / "BattleSpades" / "assets" / "original";
    const auto installed = battlespades::assets::install_asset_tree_atomic(*found, destination, manifest, {});
    expect(installed.installed, "atomic install into a Cyrillic folder: " + installed.error);
    expect(read_file(destination / "maps" / "Training.vxl") == map, "imported bytes");
    const auto check = battlespades::assets::verify_asset_tree(destination, manifest,
                                                               battlespades::assets::AssetVerificationDepth::full_hash);
    expect(check.valid, "imported tree verifies: " + check.error);
}

// Regression (0.2.1-beta.2, "I can't import mine from Steam"): the installer
// puts BattleSpades in <Steam>\steamapps\common\aceofspades\BattleSpades, so
// the import destination <...>\aceofspades\BattleSpades\assets\original lies
// INSIDE the source folder. The importer refused that layout ("asset
// destination cannot be inside the selected source tree") and the launcher
// only logged "exit 1".
void test_import_into_install_inside_game_folder(const fs::path& root) {
    namespace assets = battlespades::assets;
    assets::AssetManifest manifest;
    const std::string png = "retail png bytes";
    const std::string map = "retail vxl bytes";
    manifest.files.push_back({fs::path{"png/a.png"}, png.size(), sha256(png)});
    manifest.files.push_back({fs::path{"maps/Training.vxl"}, map.size(), sha256(map)});
    manifest.total_bytes = png.size() + map.size();

    const auto library = root / utf8_path("Steam Библиотека");
    const auto game = library / "steamapps" / "common" / "aceofspades";
    write_file(game / "png" / "a.png", png);
    write_file(game / "maps" / "Training.vxl", map);
    write_file(game / "aos.exe", "retail exe");
    const auto install = game / "BattleSpades";
    write_file(install / "BattleSpadesClient.exe", "client");
    const auto destination = install / "assets" / "original";

    std::string error;
    const auto found = assets::find_asset_source(game, manifest, error);
    expect(found.has_value() && fs::equivalent(*found, game), "Steam folder with BattleSpades inside: " + error);

    auto installed = assets::install_asset_tree_atomic(*found, destination, manifest, {});
    expect(installed.installed, "import into <aceofspades>\\BattleSpades\\assets\\original: " + installed.error);
    auto check = assets::verify_asset_tree(destination, manifest, assets::AssetVerificationDepth::full_hash);
    expect(check.valid, "imported tree verifies: " + check.error);
    expect(fs::is_regular_file(game / "png" / "a.png"), "the retail files stay where they are");

    // Importing again (repair) replaces the previous import in place.
    installed = assets::install_asset_tree_atomic(*found, destination, manifest, {});
    expect(installed.installed, "re-import over an existing import: " + installed.error);

    // A destination that CONTAINS the source is still refused.
    const auto around = assets::install_asset_tree_atomic(game, library / "steamapps", manifest, {});
    expect(!around.installed && around.error.find("cannot contain") != std::string::npos,
           "destination containing the source refused: " + around.error);

    // A catalogued file may never be read from the destination itself.
    assets::AssetManifest sneaky = manifest;
    const std::string inner = "already imported";
    write_file(destination / "inner.bin", inner);
    sneaky.files.push_back({fs::path{"BattleSpades/assets/original/inner.bin"}, inner.size(), sha256(inner)});
    sneaky.total_bytes += inner.size();
    const auto self = assets::install_asset_tree_atomic(game, destination, sneaky, {});
    expect(!self.installed && self.error.find("inside the asset destination") != std::string::npos,
           "source file inside the destination refused: " + self.error);
    check = assets::verify_asset_tree(destination, manifest, assets::AssetVerificationDepth::full_hash);
    expect(check.valid, "a refused import leaves the previous import intact: " + check.error);

    // Errors say what to do.
    fs::remove(game / "maps" / "Training.vxl");
    expect(!assets::find_asset_source(game, manifest, error).has_value(), "missing file detected");
    expect(error.find("1 of 2 required files present") != std::string::npos,
           "the closest layout is reported, not every probed one: " + error);
    const auto explained = assets::explain_asset_install_error(error, true);
    expect(explained.starts_with(error) && explained.find("Verify integrity of game files") != std::string::npos &&
               explained.find("Download game assets") != std::string::npos,
           "missing files point at Steam's verify and the download: " + explained);
    expect(assets::explain_asset_install_error(error).find("Download game assets") == std::string::npos,
           "the download is never suggested when it is not offered");
    expect(assets::explain_asset_install_error("cannot create asset staging directory: Access is denied.")
                   .find("could not write") != std::string::npos,
           "write failures explain the destination problem");
}

void test_local_server(const fs::path& root) {
    using battlespades::platform::LocalServerLaunchConfig;

    // The client finds a server bundle installed below a Cyrillic folder.
    const auto install = root / utf8_path("Игры") / "BattleSpades";
#if defined(_WIN32)
    write_file(install / "server" / "BattleSpades.exe", "server");
#else
    write_file(install / "server" / "BattleSpades", "server");
#endif
    fs::create_directories(install / "server" / "_internal");
    fs::create_directories(install / "server" / "maps");
    fs::create_directories(install / utf8_path("Папка игрока"));
    const auto bundle = battlespades::platform::find_local_server_bundle(install);
    expect(bundle.has_value() && fs::equivalent(*bundle, install / "server"), "bundle found below a Cyrillic folder");

    // Authored maps saved under a Cyrillic profile, with a Cyrillic map name.
    const auto saved = root / utf8_path("Документы") / utf8_path("Мои карты");
    const std::string map_name = "Крепость";
    std::vector<fs::path> files;
    for (const auto* extension : {".vxl", ".txt", ".ugc"}) {
        files.push_back(saved / utf8_path(map_name + extension));
        write_file(files.back(), "x");
    }
    expect(battlespades::platform::validate_custom_map_files(map_name, files).empty(),
           "Cyrillic custom map files validate");
    files.push_back(saved / utf8_path("Чужая.png"));
    write_file(files.back(), "x");
    const auto rejected = battlespades::platform::validate_custom_map_files(map_name, files);
    expect(!rejected.empty(), "a file of another map is still refused");

    // The disposable TOML written for the child server carries UTF-8 paths.
    LocalServerLaunchConfig config;
    config.bundle_root = install / "server";
    config.session_parent = install / "local-server-sessions";
    config.maps_path = install / "local-server-sessions" / utf8_path("сессия") / "maps";
    const auto toml = battlespades::platform::build_local_server_toml(config, 27015U);
    expect(!toml.empty() && toml.find(generic_utf8(config.maps_path)) != std::string::npos,
           "maps_path is written as UTF-8 for the Python server");
}

void test_hosting_gate(const fs::path& root) {
    using battlespades::platform::HostingGateAction;
    using battlespades::platform::decide_hosting_gate;
    const auto install = root / utf8_path("Игры") / "BattleSpades";
    write_file(install / "update" / "hosting.json",
               R"({"schema":1,"state":"update_required","installed_server":"0.1.0","available_server":"0.2.0",)"
               R"("download_size":67108864,"update_available":true,"reason":"protocol 169 vs 168"})");
    const auto status = battlespades::platform::read_hosting_status(install);
    expect(status.state == "update_required" && status.update_available && status.download_size == 67108864U,
           "hosting.json read from a Cyrillic install folder");

    battlespades::platform::HostingStatusFile none;
    battlespades::platform::HostingStatusFile ready;
    ready.state = "ready";
    battlespades::platform::HostingStatusFile not_offered;
    not_offered.state = "not_installed";
    expect(decide_hosting_gate(true, ready, true, false) == HostingGateAction::start, "server present and compatible");
    expect(decide_hosting_gate(true, none, true, false) == HostingGateAction::start,
           "no status file (offline or dev build): an installed server is used");
    expect(decide_hosting_gate(false, none, true, false) == HostingGateAction::request_download,
           "first Create Match without the server asks the launcher to download it");
    expect(decide_hosting_gate(true, status, true, false) == HostingGateAction::request_download,
           "incompatible server: update requested, playing unaffected");
    expect(decide_hosting_gate(true, status, true, true) == HostingGateAction::wait_for_download,
           "a running download is not started twice");
    expect(decide_hosting_gate(false, not_offered, true, false) == HostingGateAction::unavailable,
           "nothing published: explain instead of looping");
    expect(decide_hosting_gate(false, none, false, false) == HostingGateAction::missing_launcher,
           "developer builds without a launcher keep the old message");
}

} // namespace

int main() {
    try {
        if (sodium_init() < 0) throw std::runtime_error{"libsodium failed to initialise"};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto temp = fs::temp_directory_path() / ("battlespades-client-unicode-" + std::to_string(stamp));
        const auto root = temp / utf8_path("Пользователи") / utf8_path("Тодор");
        fs::create_directories(root);
        try {
            test_asset_importer(root);
            test_import_into_install_inside_game_folder(root);
            test_local_server(root);
            test_hosting_gate(root);
        } catch (...) {
            std::error_code ignored;
            fs::remove_all(temp, ignored);
            throw;
        }
        std::error_code ignored;
        fs::remove_all(temp, ignored);
    } catch (const std::exception& exception) {
        std::cerr << "aos_client_unicode_tests: FAILED: " << exception.what() << '\n';
        return 1;
    }
    std::cout << "aos_client_unicode_tests: passed\n";
    return 0;
}
