#include "battlespades/assets/asset_install.hpp"

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

class TemporaryTree final {
public:
    TemporaryTree() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
                ("battlespades-asset-install-" + std::to_string(stamp));
        std::filesystem::create_directories(root_);
    }

    ~TemporaryTree() {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    TemporaryTree(const TemporaryTree&) = delete;
    TemporaryTree& operator=(const TemporaryTree&) = delete;

    [[nodiscard]] const std::filesystem::path& root() const noexcept {
        return root_;
    }

private:
    std::filesystem::path root_{};
};

void write_text(const std::filesystem::path& path, std::string_view contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!stream) {
        throw std::runtime_error{"could not create test fixture: " + path.string()};
    }
}

#if defined(_WIN32)
void write_bytes(const std::filesystem::path& path,
                 const std::vector<unsigned char>& contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(contents.data()),
                 static_cast<std::streamsize>(contents.size()));
    if (!stream) throw std::runtime_error{"could not create binary fixture"};
}
#endif

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::filesystem::path write_manifest(const std::filesystem::path& root) {
    const auto path = root / "manifest.json";
    write_text(
        path,
        R"({
  "schema": 1,
  "file_count": 2,
  "total_bytes": 9,
  "files": [
    {
      "path": "sounds/alpha.ogg",
      "size": 5,
      "sha256": "8ed3f6ad685b959ead7022518e1af76cd816f8e8ec7ccdda1ed4018e8f2223f8"
    },
    {
      "path": "game.ico",
      "size": 4,
      "sha256": "f44e64e75f3948e9f73f8dfa94721c4ce8cbb4f265c4790c702b2d41cfbf2753"
    }
  ]
})");
    return path;
}

void manifest_rejects_unsafe_paths() {
    TemporaryTree tree;
    const auto path = tree.root() / "unsafe.json";
    write_text(
        path,
        R"({"schema":1,"file_count":1,"total_bytes":0,"files":[{"path":"../escape","size":0,"sha256":"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"}]})");
    const auto loaded = battlespades::assets::load_asset_manifest(path);
    expect(!loaded, "manifest traversal must be rejected");
}

void source_discovery_accepts_a_parent_of_src() {
    TemporaryTree tree;
    const auto loaded = battlespades::assets::load_asset_manifest(write_manifest(tree.root()));
    expect(static_cast<bool>(loaded), "fixture manifest should load");
    const auto source = tree.root() / "retail" / "src";
    write_text(source / "sounds" / "alpha.ogg", "alpha");
    write_text(source / "game.ico", "beta");

    std::string error;
    const auto resolved = battlespades::assets::find_asset_source(
        source.parent_path(), *loaded.manifest, error);
    expect(resolved.has_value(), "selecting the retail parent should discover src");
    expect(*resolved == std::filesystem::weakly_canonical(source),
           "source discovery should return the content root");
    expect(error.empty(), "successful source discovery should clear its error");
}

void source_discovery_accepts_a_steam_library_root() {
    TemporaryTree tree;
    const auto loaded = battlespades::assets::load_asset_manifest(write_manifest(tree.root()));
    expect(static_cast<bool>(loaded), "fixture manifest should load");
    const auto steam = tree.root() / "Steam";
    const auto source = steam / "steamapps" / "common" / "aceofspades";
    write_text(source / "sounds" / "alpha.ogg", "alpha");
    write_text(source / "game.ico", "beta");

    std::string error;
    const auto resolved = battlespades::assets::find_asset_source(
        steam, *loaded.manifest, error);
    expect(resolved.has_value(), "selecting a Steam root should discover aceofspades");
    expect(*resolved == std::filesystem::weakly_canonical(source),
           "Steam source discovery should return the retail content root");
}

void source_discovery_accepts_a_copied_windows_install_parent() {
    TemporaryTree tree;
    const auto loaded = battlespades::assets::load_asset_manifest(write_manifest(tree.root()));
    expect(static_cast<bool>(loaded), "fixture manifest should load");
    const auto selected = tree.root() / "Downloads";
    const auto source = selected / "AceOfSpades_no_steam_new" / "src";
    write_text(source / "sounds" / "alpha.ogg", "alpha");
    write_text(source / "game.ico", "beta");

    std::string error;
    const auto resolved = battlespades::assets::find_asset_source(
        selected, *loaded.manifest, error);
    expect(resolved.has_value(),
           "selecting a Finder parent should discover a copied Windows installation");
    expect(*resolved == std::filesystem::weakly_canonical(source),
           "copied Windows discovery should return its runtime src root");
}

void source_discovery_rejects_a_legacy_macos_bundle() {
    TemporaryTree tree;
    const auto loaded = battlespades::assets::load_asset_manifest(write_manifest(tree.root()));
    expect(static_cast<bool>(loaded), "fixture manifest should load");
    const auto source = tree.root() / "Ace of Spades.app" / "Contents" / "Resources";
    write_text(source / "sounds" / "alpha.ogg", "alpha");
    write_text(source / "game.ico", "beta");

    std::string error;
    const auto resolved = battlespades::assets::find_asset_source(
        source, *loaded.manifest, error);
    expect(!resolved.has_value(), "obsolete macOS bundle assets must fail closed");
    expect(error.find("legacy macOS .app assets are intentionally unsupported") !=
               std::string::npos,
           "legacy bundle rejection should direct the player to Windows assets");
}

void a_verified_package_is_accepted_inside_our_own_bundle() {
    TemporaryTree tree;
    const auto loaded = battlespades::assets::load_asset_manifest(write_manifest(tree.root()));
    expect(static_cast<bool>(loaded), "fixture manifest should load");
    // Where the macOS installer unpacks its own download.
    const auto package = tree.root() / "BattleSpadesClient.app" / "Contents" / "MacOS" / "assets" /
                         ".retail-download" / "extracted" / "BattleSpades-retail-assets-1.0.0";
    write_text(package / "sounds" / "alpha.ogg", "alpha");
    write_text(package / "game.ico", "beta");

    std::string error;
    const auto resolved = battlespades::assets::find_asset_source(
        package, *loaded.manifest, error, battlespades::assets::AssetSourceOrigin::verified_package);
    expect(resolved.has_value() && *resolved == std::filesystem::weakly_canonical(package),
           "the downloaded package must pass wherever the installer unpacked it: " + error);

    // The same files picked by hand from inside an .app are still refused.
    const auto picked = battlespades::assets::find_asset_source(package, *loaded.manifest, error);
    expect(!picked.has_value() && error.find("legacy macOS") != std::string::npos,
           "a folder the player picks inside an .app stays rejected");
}

void install_is_verified_and_atomic() {
    TemporaryTree tree;
    const auto loaded = battlespades::assets::load_asset_manifest(write_manifest(tree.root()));
    expect(static_cast<bool>(loaded), "fixture manifest should load");
    const auto source = tree.root() / "source";
    const auto destination = tree.root() / "package" / "assets" / "original";
    write_text(source / "sounds" / "alpha.ogg", "alpha");
    write_text(source / "game.ico", "beta");

    battlespades::assets::AssetInstallProgress final_progress{};
    const auto installed = battlespades::assets::install_asset_tree_atomic(
        source,
        destination,
        *loaded.manifest,
        [&final_progress](const battlespades::assets::AssetInstallProgress& progress) {
            final_progress = progress;
        });
    expect(static_cast<bool>(installed), "matching assets should install");
    expect(final_progress.files_completed == 2U && final_progress.bytes_completed == 9U,
           "progress should finish at the manifest totals");
    expect(read_text(destination / "sounds" / "alpha.ogg") == "alpha",
           "installed asset contents should match the source");
    expect(static_cast<bool>(battlespades::assets::verify_asset_tree(
               destination,
               *loaded.manifest,
               battlespades::assets::AssetVerificationDepth::full_hash)),
           "activated destination should pass full verification");

    write_text(source / "sounds" / "alpha.ogg", "ALPHA");
    const auto rejected = battlespades::assets::install_asset_tree_atomic(
        source, destination, *loaded.manifest);
    expect(!rejected, "same-size source corruption must fail hash verification");
    expect(read_text(destination / "sounds" / "alpha.ogg") == "alpha",
           "failed reinstall must preserve the previous verified tree");
}

void native_steam_runtime_import_is_optional_and_x86_only() {
    TemporaryTree tree;
    const auto absent = battlespades::assets::import_native_steam_runtime(
        tree.root() / "missing", tree.root() / "client");
    expect(static_cast<bool>(absent) && !absent.imported,
           "a missing optional Steam runtime must preserve offline installation");
#if defined(_WIN32)
    const auto source = tree.root() / "retail";
    std::vector<unsigned char> pe(256U, 0U);
    pe[0U] = 'M';
    pe[1U] = 'Z';
    pe[0x3CU] = 0x80U;
    pe[0x80U] = 'P';
    pe[0x81U] = 'E';
    pe[0x84U] = 0x4CU;
    pe[0x85U] = 0x01U;
    write_bytes(source / "steam_api.dll", pe);
    write_text(source / "steam_appid.txt", "224540\n");
    const auto imported = battlespades::assets::import_native_steam_runtime(
        source, tree.root() / "client");
    expect(static_cast<bool>(imported) && imported.imported,
           "a regular x86 retail runtime must import");
    expect(std::filesystem::file_size(
               tree.root() / "client" / "steam" / "win32" / "steam_api.dll") ==
               pe.size(),
           "Steam runtime import must retain the selected DLL bytes");
#endif
}

struct TestCase final {
    std::string_view name;
    std::function<void()> body;
};

} // namespace

int main() {
    const std::vector<TestCase> tests{
        {"manifest_rejects_unsafe_paths", manifest_rejects_unsafe_paths},
        {"source_discovery_accepts_a_parent_of_src", source_discovery_accepts_a_parent_of_src},
        {"source_discovery_accepts_a_steam_library_root",
         source_discovery_accepts_a_steam_library_root},
        {"source_discovery_accepts_a_copied_windows_install_parent",
         source_discovery_accepts_a_copied_windows_install_parent},
        {"source_discovery_rejects_a_legacy_macos_bundle",
         source_discovery_rejects_a_legacy_macos_bundle},
        {"a_verified_package_is_accepted_inside_our_own_bundle",
         a_verified_package_is_accepted_inside_our_own_bundle},
        {"install_is_verified_and_atomic", install_is_verified_and_atomic},
        {"native_steam_runtime_import_is_optional_and_x86_only",
         native_steam_runtime_import_is_optional_and_x86_only},
    };

    std::size_t failures{};
    for (const auto& test : tests) {
        try {
            test.body();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& exception) {
            ++failures;
            std::cerr << "[FAIL] " << test.name << ": " << exception.what() << '\n';
        }
    }
    std::cout << tests.size() - failures << '/' << tests.size() << " tests passed\n";
    return failures == 0U ? 0 : 1;
}
