// Game-file discovery (Steam/Proton/CrossOver layouts), the "Download game
// assets" decision, and the download -> verify -> extract -> import pipeline
// against a synthetic package. No retail file is ever used: every "game
// file" here is a few bytes of text.

#include "battlespades/assets/asset_install.hpp"
#include "battlespades/assets/retail_download.hpp"
#include "battlespades/updater/launch_flow.hpp"
#include "battlespades/updater/sha256.hpp"
#include "battlespades/updater/steam_library.hpp"

#include "updater/updater_test_support.hpp"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

namespace fs = std::filesystem;
namespace assets = battlespades::assets;
namespace up = battlespades::updater;
using updater_test::expect;
using updater_test::TempDir;
using updater_test::write_file;

[[nodiscard]] fs::path utf8_path(std::string_view text) { return up::path_from_utf8(text); }

const std::map<std::string, std::string>& game_files() {
    static const std::map<std::string, std::string> files{
        {"ambients/amb_test.ogg", "ambient bytes"},
        {"png/ui/logo.png", "logo bytes"},
        {"maps/Training.vxl", "map bytes"},
    };
    return files;
}

[[nodiscard]] assets::AssetManifest catalog() {
    assets::AssetManifest manifest;
    for (const auto& [path, content] : game_files()) {
        manifest.files.push_back({fs::path{path}, content.size(), up::sha256_hex(content)});
        manifest.total_bytes += content.size();
    }
    return manifest;
}

void write_game(const fs::path& folder) {
    for (const auto& [path, content] : game_files()) write_file(folder / path, content);
    write_file(folder / "aos.exe", "not really an exe");
}

[[nodiscard]] std::string vdf_escape(std::string text) {
    std::string escaped;
    for (const char c : text) {
        if (c == '\\' || c == '"') escaped.push_back('\\');
        escaped.push_back(c);
    }
    return escaped;
}

void write_library_folders(const fs::path& steam_root, const std::vector<std::string>& libraries) {
    std::string text = "\"libraryfolders\"\n{\n";
    for (std::size_t index = 0; index < libraries.size(); ++index) {
        text += "\t\"" + std::to_string(index) + "\"\n\t{\n\t\t\"path\"\t\t\"" + vdf_escape(libraries[index]) +
                "\"\n\t\t\"apps\"\n\t\t{\n\t\t\t\"224540\"\t\t\"1\"\n\t\t}\n\t}\n";
    }
    text += "}\n";
    write_file(steam_root / "steamapps" / "libraryfolders.vdf", text);
}

void write_app_manifest(const fs::path& library, std::string_view install_dir) {
    write_file(library / "steamapps" / "appmanifest_224540.acf",
               "\"AppState\"\n{\n\t\"appid\"\t\t\"224540\"\n\t\"installdir\"\t\t\"" + std::string{install_dir} +
                   "\"\n}\n");
}

[[nodiscard]] bool same_path(const fs::path& a, const fs::path& b) {
    std::error_code code;
    return fs::equivalent(a, b, code);
}

[[nodiscard]] bool contains_path(const std::vector<fs::path>& list, const fs::path& wanted) {
    for (const auto& item : list) {
        if (same_path(item, wanted)) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------

void test_wine_path_mapping() {
    const fs::path prefix = fs::path{"/home/kiwi/Library/Application Support/CrossOver/Bottles/Steam"};
    const auto inside = prefix / "drive_c" / "Program Files (x86)" / "Steam";
    const auto c_drive = assets::map_wine_drive_path(inside, "C:\\Program Files (x86)\\Steam");
    expect(c_drive.has_value() && c_drive->generic_string() ==
                                      (prefix / "drive_c" / "Program Files (x86)" / "Steam").generic_string(),
           "C: maps into drive_c");
    const auto d_drive = assets::map_wine_drive_path(inside, "D:\\Games\\SteamLibrary");
    expect(d_drive.has_value() &&
               d_drive->generic_string() == (prefix / "dosdevices" / "d:" / "Games" / "SteamLibrary").generic_string(),
           "other drives map through dosdevices");
    expect(!assets::map_wine_drive_path(inside, "/mnt/games").has_value(), "POSIX paths are not drive paths");
    expect(!assets::map_wine_drive_path(fs::path{"/home/kiwi/.steam/steam"}, "C:\\Steam").has_value(),
           "no Wine prefix, no mapping");
    expect(!assets::map_wine_drive_path(inside, "C:\\..\\..\\etc").has_value(), "'..' is refused");
}

void test_windows_steam_layout(const fs::path& root) {
    // Registry Steam root on C:, the game in a second library with a
    // non-default installdir and a Cyrillic path.
    const auto steam = root / "Program Files (x86)" / "Steam";
    const auto library = root / utf8_path("Игры") / "SteamLibrary";
    fs::create_directories(steam / "steamapps" / "common");
    write_library_folders(steam, {up::path_to_utf8(steam), up::path_to_utf8(library)});
    write_app_manifest(library, "AoS Battle Builder");
    write_game(library / "steamapps" / "common" / "AoS Battle Builder");

    assets::SteamSearchEnvironment environment;
    environment.platform = assets::HostPlatform::windows;
    environment.registry_steam_roots = {steam};
    environment.program_files = {root / "Program Files (x86)"};
    const auto roots = assets::steam_root_candidates(environment);
    expect(roots.size() == 1U && same_path(roots.front(), steam), "registry and Program Files roots are deduplicated");

    const auto games = assets::steam_game_directories(steam);
    expect(games.size() == 1U && same_path(games.front(), library / "steamapps" / "common" / "AoS Battle Builder"),
           "installdir from appmanifest_224540.acf in a second library");

    const auto manifest = catalog();
    const auto detected = assets::detect_asset_source(manifest, environment);
    expect(detected.found() && detected.verified_root.has_value() && detected.error.empty(),
           "the Windows Steam copy is detected and verifies: " + detected.error);

    // The player picks "common", the library root, "steamapps" or the Steam root.
    for (const auto& pick : {library / "steamapps" / "common", library, library / "steamapps", steam,
                             library / "steamapps" / "common" / ""}) {
        std::string error;
        const auto source = assets::find_asset_source(pick, manifest, error);
        expect(source.has_value(), "picking " + up::path_to_utf8(pick) + " finds the game: " + error);
    }
}

void test_linux_proton_layout(const fs::path& root) {
    // Steam Play installs the Windows game into the native library.
    const auto home = root / "home";
    const auto steam = home / ".local" / "share" / "Steam";
    const auto games_drive = root / "mnt" / "games" / "SteamLibrary";
    fs::create_directories(steam / "steamapps");
    write_library_folders(steam, {up::path_to_utf8(steam), up::path_to_utf8(games_drive)});
    write_app_manifest(games_drive, "aceofspades");
    write_game(games_drive / "steamapps" / "common" / "aceofspades");
    // ~/.steam/steam is a symlink to the same Steam on real systems; a plain
    // copy of the file is enough here.
    fs::create_directories(home / ".steam" / "steam" / "steamapps");

    assets::SteamSearchEnvironment environment;
    environment.platform = assets::HostPlatform::linux_desktop;
    environment.home = home;
    const auto roots = assets::steam_root_candidates(environment);
    expect(contains_path(roots, steam), "~/.local/share/Steam is searched");
    bool flatpak{};
    for (const auto& candidate : roots) {
        flatpak = flatpak || candidate.generic_string().find("com.valvesoftware.Steam") != std::string::npos;
    }
    expect(flatpak, "Flatpak Steam is searched");

    const auto detected = assets::detect_asset_source(catalog(), environment);
    expect(detected.verified_root.has_value() &&
               same_path(*detected.folder, games_drive / "steamapps" / "common" / "aceofspades"),
           "the Proton install in a second library is detected");
}

void test_macos_crossover_layout(const fs::path& root) {
    const auto home = root / "Users" / "kiwi";
    // Native macOS Steam: Ace of Spades cannot be installed there, so its
    // "common" folder has other games only (the reported situation).
    const auto native = home / "Library" / "Application Support" / "Steam";
    fs::create_directories(native / "steamapps" / "common" / "Portal 2");
    write_library_folders(native, {up::path_to_utf8(native)});

    assets::SteamSearchEnvironment environment;
    environment.platform = assets::HostPlatform::macos;
    environment.home = home;
    const auto manifest = catalog();
    auto detected = assets::detect_asset_source(manifest, environment);
    expect(!detected.found() && detected.error.empty(),
           "an empty native Steam library is not proposed and is not an error");

    std::string error;
    expect(!assets::find_asset_source(native / "steamapps" / "common", manifest, error).has_value(),
           "common without the game is refused");
    expect(error.find("no Ace of Spades game files were found") != std::string::npos,
           "and explained as 'not found', not as a broken install: " + error);
    expect(error.find("required asset is missing") == std::string::npos,
           "no misleading first-missing-file report: " + error);
    const auto explained = assets::explain_asset_install_error(error, false);
    expect(explained.find("Download game assets") == std::string::npos,
           "an unavailable download is never mentioned: " + explained);
#if !defined(_WIN32)
    expect(explained.find("Windows game") != std::string::npos,
           "macOS/Linux players learn the game needs Proton/CrossOver: " + explained);
#endif
    expect(assets::explain_asset_install_error(error, true).find("Download game assets") != std::string::npos,
           "an offered download is mentioned");

    // CrossOver bottle with Windows Steam: libraryfolders.vdf has C:\ paths.
    const auto bottle = home / "Library" / "Application Support" / "CrossOver" / "Bottles" / "Steam";
    const auto bottle_steam = bottle / "drive_c" / "Program Files (x86)" / "Steam";
    write_library_folders(bottle_steam, {"C:\\Program Files (x86)\\Steam"});
    write_app_manifest(bottle_steam, "aceofspades");
    write_game(bottle_steam / "steamapps" / "common" / "aceofspades");

    const auto roots = assets::steam_root_candidates(environment);
    expect(contains_path(roots, bottle_steam), "CrossOver bottles are searched");
#if !defined(_WIN32)
    // Drive-letter mapping only applies on hosts that run Wine.
    detected = assets::detect_asset_source(manifest, environment);
    expect(detected.verified_root.has_value() &&
               same_path(*detected.folder, bottle_steam / "steamapps" / "common" / "aceofspades"),
           "the CrossOver install is detected: " + detected.error);
#endif
    // Picking the bottle, its drive_c or the Bottles folder also works.
    for (const auto& pick : {bottle, bottle / "drive_c", bottle.parent_path()}) {
        error.clear();
        expect(assets::find_asset_source(pick, manifest, error).has_value(),
               "picking " + up::path_to_utf8(pick) + " finds the bottle's game: " + error);
    }

    // A broken copy is found but reported with its own problem.
    fs::remove(bottle_steam / "steamapps" / "common" / "aceofspades" / "maps" / "Training.vxl");
    detected = assets::detect_asset_source(manifest, environment);
#if !defined(_WIN32)
    expect(detected.found() && !detected.verified_root.has_value() &&
               detected.error.find("2 of 3 required files present") != std::string::npos,
           "a modded/incomplete copy is found and explained: " + detected.error);
#endif
}

void test_offer_decision() {
    const std::string with_retail = R"({
      "schema": 2, "product": "BattleSpades", "channel": "stable",
      "components": {
        "client": {"version": "0.2.1", "package": "c.zip", "urls": ["https://example.invalid/c.zip"],
                   "size": 10, "sha256": "0000000000000000000000000000000000000000000000000000000000000000"},
        "retail_assets": {"version": "1.0.0", "package": "r.zip", "urls": ["https://example.invalid/r.zip"],
                   "size": 123, "sha256": "1111111111111111111111111111111111111111111111111111111111111111",
                   "root": "pack"}
      }})";
    auto offer = assets::retail_offer_from_manifest(with_retail);
    expect(offer.available() && offer.release->size == 123U && offer.release->root == "pack",
           "retail_assets present: the download is offered");

    const std::string without_retail = R"({
      "schema": 2, "product": "BattleSpades",
      "components": {
        "client": {"version": "0.2.1", "package": "c.zip", "urls": ["https://example.invalid/c.zip"],
                   "size": 10, "sha256": "0000000000000000000000000000000000000000000000000000000000000000"}
      }})";
    offer = assets::retail_offer_from_manifest(without_retail);
    expect(offer.manifest_reachable && !offer.available(), "no retail_assets: no download button");
    offer = assets::retail_offer_from_manifest("{broken");
    expect(offer.manifest_reachable && !offer.available() && !offer.error.empty(),
           "a broken manifest never offers the download");

    const auto unavailable = assets::download_unavailable_message();
    expect(unavailable.find("isn't available yet") != std::string::npos &&
               unavailable.find("https://www.aosplay.net/download") != std::string::npos &&
               unavailable.find("Download game assets") == std::string::npos,
           "the 'not available' text links the download page and names no missing button");

    // The choice screen (shared with the Windows launcher).
    up::FirstRunInputs inputs;
    inputs.manifest_reachable = true;
    inputs.download_offered = false;
    auto screen = up::plan_first_run(inputs);
    for (const auto action : screen.actions) {
        expect(action != up::FirstRunAction::download, "no download action without retail_assets");
    }
    expect(screen.note == up::FirstRunNote::download_unavailable, "the 'not available yet' note is shown");
    inputs.download_offered = true;
    screen = up::plan_first_run(inputs);
    expect(screen.preselected == up::FirstRunAction::download, "nothing found + download offered: download first");

    // A fetch through a transport.
    assets::RetailTransport transport;
    transport.fetch_text = [&](const std::string& url, std::size_t, std::string& body) {
        assets::TransferResult result;
        if (url.ends_with("offline.json")) {
            result.error = "Could not resolve host";
            return result;
        }
        body = with_retail;
        result.status = 200;
        return result;
    };
    expect(assets::fetch_retail_offer(transport, "https://www.aosplay.net/updates/stable.json").available(),
           "fetched manifest with retail_assets");
    const auto offline = assets::fetch_retail_offer(transport, "https://www.aosplay.net/offline.json");
    expect(!offline.manifest_reachable && !offline.available() && !offline.error.empty(), "offline is reported");
    expect(!assets::fetch_retail_offer(transport, "http://example.com/stable.json").manifest_reachable,
           "plain http to the internet is refused");

    // aosplay.net unreachable: the GitHub copy of stable.json still offers it.
    std::vector<std::string> asked;
    assets::RetailTransport blocked_site;
    blocked_site.fetch_text = [&](const std::string& url, std::size_t, std::string& body) {
        asked.push_back(url);
        assets::TransferResult result;
        if (url.starts_with("https://www.aosplay.net/")) {
            result.error = "Connection timed out";
            return result;
        }
        body = with_retail;
        result.status = 200;
        return result;
    };
    expect(assets::fetch_retail_offer(blocked_site, "https://www.aosplay.net/updates/stable.json").available(),
           "the GitHub manifest copy is used when aosplay.net is blocked");
    expect(asked.size() == 2U && asked[1U].starts_with("https://github.com/KikoTs/BattleSpadesClient/releases/"),
           "aosplay.net is asked first, then GitHub");

    // A list that arrived once keeps the download offered when the server is
    // slow or blocked on a later start.
    {
        const auto saved = std::filesystem::temp_directory_path() / "aos_retail_saved_manifest_test.json";
        std::error_code ignored;
        std::filesystem::remove(saved, ignored);
        bool online = true;
        assets::RetailTransport flaky;
        flaky.fetch_text = [&](const std::string&, std::size_t, std::string& body) {
            assets::TransferResult result;
            if (!online) {
                result.error = "Operation timed out";
                return result;
            }
            body = with_retail;
            result.status = 200;
            return result;
        };
        const auto first = assets::fetch_retail_offer(flaky, "https://www.aosplay.net/updates/stable.json", saved);
        expect(first.available() && !first.from_saved_copy && std::filesystem::exists(saved),
               "a fetched list is saved");
        online = false;
        const auto later = assets::fetch_retail_offer(flaky, "https://www.aosplay.net/updates/stable.json", saved);
        expect(later.available() && later.from_saved_copy, "offline: the download is offered from the saved list");
        std::filesystem::remove(saved, ignored);
        const auto never = assets::fetch_retail_offer(flaky, "https://www.aosplay.net/updates/stable.json", saved);
        expect(!never.available(), "offline with no saved list: no download offered");
    }
}

// --- a stored (uncompressed) ZIP writer for the synthetic package -----------

void put16(std::string& out, std::uint32_t value) {
    out.push_back(static_cast<char>(value & 0xFFU));
    out.push_back(static_cast<char>((value >> 8U) & 0xFFU));
}
void put32(std::string& out, std::uint32_t value) {
    put16(out, value & 0xFFFFU);
    put16(out, value >> 16U);
}

[[nodiscard]] std::string make_zip(const std::vector<std::pair<std::string, std::string>>& members) {
    std::string out;
    std::string directory;
    for (const auto& [name, data] : members) {
        const auto offset = static_cast<std::uint32_t>(out.size());
        const auto crc = up::crc32(data);
        const auto size = static_cast<std::uint32_t>(data.size());
        put32(out, 0x04034b50U);
        put16(out, 20U);
        put16(out, 0x0800U);
        put16(out, 0U);
        put16(out, 0U);
        put16(out, 0x21U);
        put32(out, crc);
        put32(out, size);
        put32(out, size);
        put16(out, static_cast<std::uint32_t>(name.size()));
        put16(out, 0U);
        out += name;
        out += data;
        put32(directory, 0x02014b50U);
        put16(directory, 20U);
        put16(directory, 20U);
        put16(directory, 0x0800U);
        put16(directory, 0U);
        put16(directory, 0U);
        put16(directory, 0x21U);
        put32(directory, crc);
        put32(directory, size);
        put32(directory, size);
        put16(directory, static_cast<std::uint32_t>(name.size()));
        put16(directory, 0U);
        put16(directory, 0U);
        put16(directory, 0U);
        put16(directory, 0U);
        put32(directory, 0U);
        put32(directory, offset);
        directory += name;
    }
    const auto directory_offset = static_cast<std::uint32_t>(out.size());
    out += directory;
    put32(out, 0x06054b50U);
    put16(out, 0U);
    put16(out, 0U);
    put16(out, static_cast<std::uint32_t>(members.size()));
    put16(out, static_cast<std::uint32_t>(members.size()));
    put32(out, static_cast<std::uint32_t>(directory.size()));
    put32(out, directory_offset);
    put16(out, 0U);
    return out;
}

[[nodiscard]] std::string synthetic_pack(bool complete = true) {
    std::vector<std::pair<std::string, std::string>> members;
    for (const auto& [path, content] : game_files()) {
        if (!complete && path == "maps/Training.vxl") continue;
        members.emplace_back("BattleSpades-retail-assets-1.0.0/" + path, content);
    }
    members.emplace_back("BattleSpades-retail-assets-1.0.0/list.pnq", "extra file at the root");
    return make_zip(members);
}

/// Serves one package from memory; can drop the first transfer half-way,
/// ignore Range requests, and records every requested offset.
struct FakeServer final {
    std::string package;
    bool drop_first{};
    bool ignore_range{};
    int transfers{};
    std::vector<std::uint64_t> offsets;
    bool cancel_at_half{};

    [[nodiscard]] assets::RetailTransport transport() {
        assets::RetailTransport served;
        served.fetch_range = [this](const std::string& url, std::uint64_t offset,
                                       const std::function<bool(long, const char*, std::size_t)>& sink,
                                       const std::function<bool(std::uint64_t, std::uint64_t)>& progress) {
            assets::TransferResult result;
            if (url.find("dead-mirror") != std::string::npos) {
                result.error = "Couldn't connect to server";
                return result;
            }
            ++transfers;
            offsets.push_back(offset);
            const std::uint64_t start = ignore_range ? 0U : offset;
            result.status = start > 0U ? 206 : 200;
            const std::uint64_t end =
                drop_first && transfers == 1 ? package.size() / 2U : static_cast<std::uint64_t>(package.size());
            for (std::uint64_t at = start; at < end; at += 7U) {
                const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(7U, end - at));
                if (!sink(result.status, &package[static_cast<std::size_t>(at)], count)) return result;
                if (!progress(at + count - start, package.size() - start)) {
                    result.cancelled = true;
                    return result;
                }
                if (cancel_at_half && at > package.size() / 2U) {
                    result.cancelled = true;
                    return result;
                }
            }
            if (end < package.size()) result.error = "Connection reset by peer";
            return result;
        };
        return served;
    }
};

[[nodiscard]] up::ComponentRelease release_for(const std::string& package) {
    up::ComponentRelease release;
    release.name = "retail_assets";
    release.version = "1.0.0";
    release.package = "BattleSpades-retail-assets-1.0.0.zip";
    release.urls = {"https://dead-mirror.invalid/pack.zip", "https://mirror.invalid/pack.zip"};
    release.size = package.size();
    release.sha256 = up::sha256_hex(package);
    release.root = "BattleSpades-retail-assets-1.0.0";
    release.target = "assets/original";
    return release;
}

void test_download_pipeline(const fs::path& root) {
    const auto manifest = catalog();
    const auto install = root / utf8_path("Программы") / "BattleSpades";

    // 1. Interrupted, resumed on the next round, verified, extracted, imported.
    FakeServer server;
    server.package = synthetic_pack();
    server.drop_first = true;
    assets::RetailInstallRequest request;
    request.release = release_for(server.package);
    request.catalog = &manifest;
    request.destination = install / "assets" / "original";
    request.cache_directory = install / "assets" / ".retail-download";
    std::vector<assets::RetailStage> stages;
    const auto result = assets::download_and_install_retail_assets(
        request, server.transport(), [&](assets::RetailStage stage, std::uint64_t, std::uint64_t) {
            if (stages.empty() || stages.back() != stage) stages.push_back(stage);
            return true;
        });
    expect(static_cast<bool>(result), "download + import succeeds: " + result.error);
    bool resumed{};
    for (const auto offset : server.offsets) resumed = resumed || offset > 0U;
    expect(resumed, "the interrupted transfer was resumed with a Range offset");
    const auto check = assets::verify_asset_tree(request.destination, manifest, assets::AssetVerificationDepth::full_hash);
    expect(check.valid, "downloaded files equal a folder import: " + check.error);
    std::error_code code;
    expect(!fs::exists(request.cache_directory, code), "the download cache is removed after success");
    expect(stages.size() >= 4U && stages.front() == assets::RetailStage::downloading &&
               stages.back() == assets::RetailStage::importing,
           "progress reports download, verify, extract and import");

    // 2. A folder import of the same files ends identical.
    const auto folder_root = root / "folder-import";
    write_game(root / "game");
    const auto folder = assets::install_asset_tree_atomic(root / "game", folder_root, manifest, {});
    expect(folder.installed, "folder import: " + folder.error);
    for (const auto& [path, content] : game_files()) {
        expect(updater_test::read_file(folder_root / path) == updater_test::read_file(request.destination / path),
               "both paths produce identical " + path);
    }

    // 3. A server that ignores Range: the partial file is restarted.
    FakeServer stubborn;
    stubborn.package = server.package;
    stubborn.drop_first = true;
    stubborn.ignore_range = true;
    request.destination = install / "assets" / "original-2";
    request.cache_directory = install / "assets" / ".retail-download-2";
    const auto restarted = assets::download_and_install_retail_assets(request, stubborn.transport());
    expect(static_cast<bool>(restarted), "a server without Range support still works: " + restarted.error);

    // 4. Cancel keeps the partial file; a retry resumes it.
    FakeServer cancelling;
    cancelling.package = server.package;
    cancelling.cancel_at_half = true;
    request.destination = install / "assets" / "original-3";
    request.cache_directory = install / "assets" / ".retail-download-3";
    const auto cancelled = assets::download_and_install_retail_assets(request, cancelling.transport());
    expect(cancelled.status == assets::RetailInstallStatus::cancelled, "cancel is reported as cancelled");
    auto partial = request.cache_directory / request.release.package;
    partial += ".partial";
    expect(fs::is_regular_file(partial, code) && fs::file_size(partial, code) > 0U, "cancel keeps the partial file");
    cancelling.cancel_at_half = false;
    cancelling.offsets.clear();
    const auto retried = assets::download_and_install_retail_assets(request, cancelling.transport());
    expect(static_cast<bool>(retried) && !cancelling.offsets.empty() && cancelling.offsets.front() > 0U,
           "the retry resumes where it stopped: " + retried.error);

    // 5. A wrong SHA-256 never reaches the destination.
    FakeServer tampered;
    tampered.package = server.package;
    request.release = release_for(server.package);
    request.release.sha256 = up::sha256_hex("something else");
    request.destination = install / "assets" / "original-4";
    request.cache_directory = install / "assets" / ".retail-download-4";
    const auto mismatch = assets::download_and_install_retail_assets(request, tampered.transport());
    expect(!mismatch && mismatch.error.find("SHA-256") != std::string::npos, "hash mismatch fails: " + mismatch.error);
    expect(!fs::exists(request.destination, code), "nothing is installed from a bad download");

    // 6. A package that misses catalogued files fails the importer's check.
    FakeServer incomplete;
    incomplete.package = synthetic_pack(false);
    request.release = release_for(incomplete.package);
    request.destination = install / "assets" / "original-5";
    request.cache_directory = install / "assets" / ".retail-download-5";
    const auto rejected = assets::download_and_install_retail_assets(request, incomplete.transport());
    expect(!rejected && rejected.error.find("did not pass the asset check") != std::string::npos,
           "the same importer validation rejects an incomplete package: " + rejected.error);
    expect(!fs::exists(request.destination, code), "a rejected package installs nothing");

    // 7. macOS: the game, and so the download cache, live inside the .app
    //    bundle. The package must not be mistaken for the old retail Mac app.
    FakeServer mac;
    mac.package = server.package;
    const auto bundle = root / "BattleSpadesClient.app" / "Contents" / "MacOS";
    request.release = release_for(server.package);
    request.destination = bundle / "assets" / "original";
    request.cache_directory = bundle / "assets" / ".retail-download";
    const auto in_bundle = assets::download_and_install_retail_assets(request, mac.transport());
    expect(static_cast<bool>(in_bundle), "the download installs inside a macOS app bundle: " + in_bundle.error);
    const auto bundle_check =
        assets::verify_asset_tree(request.destination, manifest, assets::AssetVerificationDepth::full_hash);
    expect(bundle_check.valid, "the files inside the bundle are complete: " + bundle_check.error);

    // 8. Every mirror down: a clear, retryable error.
    assets::RetailTransport down;
    down.fetch_range = [](const std::string&, std::uint64_t, const auto&, const auto&) {
        assets::TransferResult failure;
        failure.error = "Could not resolve host";
        return failure;
    };
    request.release = release_for(server.package);
    request.destination = install / "assets" / "original-6";
    request.cache_directory = install / "assets" / ".retail-download-6";
    const auto offline = assets::download_and_install_retail_assets(request, down);
    expect(!offline && offline.error.find("internet connection") != std::string::npos &&
               offline.error.find("Could not resolve host") != std::string::npos,
           "offline errors explain the retry: " + offline.error);

    // 9. A transfer completed before shutdown, but was not promoted yet.
    //    Its valid bytes must install even when all mirrors are offline.
    request.destination = install / "assets" / "original-complete-partial";
    request.cache_directory = install / "assets" / ".retail-download-complete-partial";
    write_file(request.cache_directory / (request.release.package + ".partial"), server.package);
    unsigned requests{};
    down.fetch_range = [&](const std::string&, std::uint64_t, const auto&, const auto&) {
        ++requests;
        assets::TransferResult failure;
        failure.error = "offline";
        return failure;
    };
    const auto complete = assets::download_and_install_retail_assets(request, down);
    expect(static_cast<bool>(complete) && requests == 0U,
           "a complete partial installs without any network transfer: " + complete.error);

    // 10. Same file name and version, different bytes: do not waste a full
    //     transfer stitching together prefixes from different releases.
    request.destination = install / "assets" / "original-replaced-package";
    request.cache_directory = install / "assets" / ".retail-download-replaced-package";
    FakeServer replaced;
    replaced.package = server.package;
    replaced.cancel_at_half = true;
    const auto stopped = assets::download_and_install_retail_assets(request, replaced.transport());
    expect(stopped.status == assets::RetailInstallStatus::cancelled, "prepare an interrupted old release");
    replaced.package = synthetic_pack();
    // A ZIP comment changes its identity without changing the catalogued
    // contents that the importer checks.
    replaced.package[replaced.package.size() - 2U] = 19;
    replaced.package += "new archive comment";
    request.release = release_for(replaced.package);
    replaced.cancel_at_half = false;
    replaced.offsets.clear();
    const auto replacement = assets::download_and_install_retail_assets(request, replaced.transport());
    expect(static_cast<bool>(replacement) && replaced.offsets == std::vector<std::uint64_t>{0U},
           "replacement package downloads exactly once from zero: " + replacement.error);
}

void test_destination(const fs::path& root) {
    const auto executable = root / utf8_path("BattleSpadesClient.app") / "Contents" / "MacOS";
    fs::create_directories(executable / "assets" / "client" / "ui");
    write_file(executable / "assets" / "client" / "ui" / "menu.rml", "menu");
    std::string error;
    const auto chosen = assets::choose_asset_destination(executable, error);
    expect(chosen.has_value() && same_path(chosen->parent_path(), executable / "assets"),
           "a writable app folder keeps the packaged destination: " + error);

    // The user data tree links <data>/assets/client to the packaged one.
    const auto data_assets = root / "data" / "assets";
    expect(assets::link_packaged_client_assets(data_assets, executable / "assets", error),
           "link the client assets: " + error);
    expect(updater_test::read_file(data_assets / "client" / "ui" / "menu.rml") == "menu",
           "client assets are reachable through the user data tree");
    // A moved app re-points the link.
    const auto moved = root / "moved" / "MacOS";
    fs::create_directories(moved / "assets" / "client");
    write_file(moved / "assets" / "client" / "new.txt", "new");
    expect(assets::link_packaged_client_assets(data_assets, moved / "assets", error), "relink: " + error);
    expect(updater_test::read_file(data_assets / "client" / "new.txt") == "new", "the link follows the moved app");
    std::error_code code;
    expect(fs::exists(executable / "assets" / "client" / "ui" / "menu.rml", code),
           "relinking never deletes the packaged files");

    expect(assets::user_data_directory().has_value(), "a user data folder is known");

#if !defined(_WIN32)
    if (geteuid() != 0) {
        const auto read_only = root / "read-only";
        fs::create_directories(read_only / "assets");
        static_cast<void>(::chmod((read_only / "assets").c_str(), 0555));
        expect(!assets::directory_is_writable(read_only / "assets"), "a read-only folder is detected");
        static_cast<void>(::chmod((read_only / "assets").c_str(), 0755));
    }
#endif
}

} // namespace

int main() {
    try {
        TempDir temp{"retail-download"};
        test_wine_path_mapping();
        test_windows_steam_layout(temp.path() / "windows");
        test_linux_proton_layout(temp.path() / "linux");
        test_macos_crossover_layout(temp.path() / "macos");
        test_offer_decision();
        test_download_pipeline(temp.path() / "download");
        test_destination(temp.path() / "destination");
    } catch (const std::exception& exception) {
        std::cerr << "aos_retail_download_tests: " << exception.what() << '\n';
        return 1;
    }
    std::cout << "aos_retail_download_tests: ok\n";
    return 0;
}
