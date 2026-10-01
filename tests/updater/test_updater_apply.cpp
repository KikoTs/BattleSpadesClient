// Staged update apply / rollback against a scratch install folder.

#include "updater_test_support.hpp"

#include "battlespades/updater/update_apply.hpp"

#include <fstream>

namespace fs = std::filesystem;
namespace up = battlespades::updater;
using updater_test::expect;
using updater_test::read_file;
using updater_test::write_file;

namespace {

void make_install(const fs::path& install) {
    write_file(install / "BattleSpadesClient.exe", "client v1");
    write_file(install / "BattleSpadesLauncher.exe", "launcher v1");
    write_file(install / "SDL3.dll", "sdl v1");
    write_file(install / "obsolete.dll", "old library");
    write_file(install / "battlespades-version.json", R"({"version":"0.2.0-beta.1"})");
    write_file(install / "ui-layout.json", "player edited layout");
    write_file(install / "settings.toml", "player settings");
    write_file(install / "assets" / "original" / "png" / "a.png", "retail asset");
    write_file(install / "shaders" / "dx11" / "old_shader.bin", "stale shader");
    write_file(install / "shaders" / "dx11" / "fs_block.bin", "shader v1");
    write_file(install / "server" / "_internal" / "old_module.pyd", "stale server module");
    write_file(install / "server" / "maps" / "custom.vxl", "player map");
}

void make_stage(const fs::path& stage) {
    write_file(stage / "BattleSpadesClient.exe", "client v2");
    write_file(stage / "BattleSpadesLauncher.exe", "launcher v2");
    write_file(stage / "SDL3.dll", "sdl v2");
    write_file(stage / "harfbuzz.dll", "new library");
    write_file(stage / "battlespades-version.json", R"({"version":"0.3.0"})");
    write_file(stage / "ui-layout.json", "shipped layout");
    write_file(stage / "shaders" / "dx11" / "fs_block.bin", "shader v2");
    write_file(stage / "server" / "_internal" / "new_module.pyd", "server module v2");
    write_file(stage / "server" / "BattleSpades.exe", "server v2");
}

up::ApplyOptions options() {
    up::ApplyOptions value;
    value.preserve = {"ui-layout.json"};
    value.mirror_directories = {"shaders", "server/_internal"};
    value.remove = {"obsolete.dll", "../outside.txt", "update/state.json"};
    value.from_version = "0.2.0-beta.1";
    value.to_version = "0.3.0";
    return value;
}

void test_glob() {
    expect(up::glob_match("ui-layout.json", "UI-Layout.json"), "case-insensitive literal");
    expect(up::glob_match("localization/*.json", "localization/en.json"), "star in segment");
    expect(!up::glob_match("localization/*.json", "localization/sub/en.json"), "star stays in one segment");
    expect(up::glob_match("server/**", "server/maps/a/b.vxl"), "double star crosses segments");
    expect(up::glob_match("**/*.pdb", "a/b/c.pdb") && up::glob_match("**/*.pdb", "c.pdb"), "leading double star");
    expect(up::glob_match("shader?.bin", "shader1.bin") && !up::glob_match("shader?.bin", "shader12.bin"), "question");
    expect(up::glob_match("a\\b.txt", "a/b.txt"), "separators are equivalent");
}

void test_apply_and_rollback() {
    updater_test::TempDir temp{"apply"};
    const auto install = temp.path() / "BattleSpades";
    const up::UpdateLayout layout{install};
    const auto stage = layout.staging() / "0.3.0" / "payload" / "bin";
    make_install(install);
    make_stage(stage);
    write_file(temp.path() / "outside.txt", "must survive");

    expect(up::read_installed_version(install) == "0.2.0-beta.1", "installed version read");
    const auto result = up::apply_staged_tree(layout, stage, options());
    expect(result.ok, "apply succeeds: " + result.error);
    expect(result.replaced == 5U && result.added == 3U && result.preserved == 1U && result.removed == 3U,
           "counts: replaced " + std::to_string(result.replaced) + ", added " + std::to_string(result.added) +
               ", removed " + std::to_string(result.removed) + ", preserved " + std::to_string(result.preserved));
    expect(read_file(install / "BattleSpadesClient.exe") == "client v2", "client replaced");
    expect(read_file(install / "BattleSpadesLauncher.exe") == "launcher v2", "launcher replaced");
    expect(read_file(install / "harfbuzz.dll") == "new library", "new file added");
    expect(up::read_installed_version(install) == "0.3.0", "version file updated with the package");
    expect(read_file(install / "ui-layout.json") == "player edited layout", "preserved file kept");
    expect(read_file(install / "settings.toml") == "player settings", "files outside the package untouched");
    expect(read_file(install / "assets" / "original" / "png" / "a.png") == "retail asset", "imported assets untouched");
    expect(!fs::exists(install / "shaders" / "dx11" / "old_shader.bin"), "mirrored folder drops stale files");
    expect(!fs::exists(install / "server" / "_internal" / "old_module.pyd"), "server runtime mirrored");
    expect(read_file(install / "server" / "maps" / "custom.vxl") == "player map", "unmirrored server maps kept");
    expect(!fs::exists(install / "obsolete.dll"), "explicitly obsolete file removed");
    expect(read_file(temp.path() / "outside.txt") == "must survive", "remove list cannot escape the install");
    expect(up::interrupted_components(layout).empty(), "journal marks the apply complete");
    const auto info = up::read_rollback_info(layout);
    expect(info.has_value() && info->applied && info->from_version == "0.2.0-beta.1" && info->to_version == "0.3.0",
           "rollback info recorded");

    const auto rolled = up::rollback_last_update(layout);
    expect(rolled.ok, "rollback succeeds: " + rolled.error);
    expect(read_file(install / "BattleSpadesClient.exe") == "client v1", "client restored");
    expect(read_file(install / "BattleSpadesLauncher.exe") == "launcher v1", "launcher restored");
    expect(read_file(install / "obsolete.dll") == "old library", "removed file restored");
    expect(read_file(install / "shaders" / "dx11" / "old_shader.bin") == "stale shader", "mirrored removals restored");
    expect(!fs::exists(install / "harfbuzz.dll"), "added file removed");
    expect(up::read_installed_version(install) == "0.2.0-beta.1", "version restored");
    expect(!up::rollback_last_update(layout).ok, "only one rollback is possible");

    up::UpdaterState state;
    state.skipped_versions["client"] = "0.3.0";
    state.deferrals["assets@2.0.0"] = 2U;
    std::string error;
    expect(up::save_updater_state(layout, state, error), "state saved");
    const auto loaded = up::load_updater_state(layout);
    expect(loaded.skipped_versions.at("client") == "0.3.0" && loaded.deferrals.at("assets@2.0.0") == 2U,
           "skipped versions and deferral counters persist");
}

void test_failed_apply_restores_everything() {
    updater_test::TempDir temp{"applyfail"};
    const auto install = temp.path() / "BattleSpades";
    const up::UpdateLayout layout{install};
    const auto stage = layout.staging() / "0.3.0" / "payload";
    make_install(install);
    make_stage(stage);
    // A folder where the package has a file: the apply must fail part-way
    // through (after earlier files moved) and put every file back.
    write_file(stage / "zz-conflict", "file");
    fs::create_directories(install / "zz-conflict" / "dir");
    const auto result = up::apply_staged_tree(layout, stage, options());
    expect(!result.ok && !result.error.empty(), "conflict detected");
    expect(read_file(install / "BattleSpadesClient.exe") == "client v1", "nothing replaced after a failed plan");
    expect(fs::exists(stage / "BattleSpadesClient.exe"), "the verified stage is kept for the next launch");

    // Now fail during execution: an unreadable/locked target is emulated by
    // making a staged file's parent path collide mid-way.
    fs::remove_all(install / "zz-conflict");
    fs::remove(stage / "zz-conflict");
    write_file(stage / "sub" / "file.txt", "new");
    write_file(install / "sub", "a file where the package needs a folder");
    const auto second = up::apply_staged_tree(layout, stage, options());
    expect(!second.ok, "execution failure reported");
    expect(read_file(install / "BattleSpadesClient.exe") == "client v1", "completed steps were undone");
    expect(read_file(install / "SDL3.dll") == "sdl v1", "every replaced file restored");
    expect(read_file(install / "obsolete.dll") == "old library", "removed files restored");
    expect(!fs::exists(install / "harfbuzz.dll"), "added files withdrawn");
    expect(fs::exists(stage / "harfbuzz.dll") && fs::exists(stage / "BattleSpadesClient.exe"),
           "staged files returned to the stage");
    expect(up::interrupted_components(layout).empty(), "no half-applied journal left behind");
}

void test_interrupted_apply_recovery() {
    updater_test::TempDir temp{"applycrash"};
    const auto install = temp.path() / "BattleSpades";
    const up::UpdateLayout layout{install};
    const auto stage = layout.staging() / "0.3.0" / "payload";
    make_install(install);
    make_stage(stage);
    const auto result = up::apply_staged_tree(layout, stage, options());
    expect(result.ok, "apply succeeds");
    // Simulate a power cut before the journal was marked applied, with one
    // replaced file only half done (original moved away, new one missing).
    auto journal = read_file(layout.rollback_dir("client") / "journal.json");
    updater_test::replace_all(journal, "\"applied\"", "\"applying\"");
    write_file(layout.rollback_dir("client") / "journal.json", journal);
    fs::remove(install / "SDL3.dll");
    expect(up::interrupted_components(layout) == std::vector<std::string>{"client"}, "interruption detected");
    const auto rolled = up::rollback_last_update(layout);
    expect(rolled.ok, "recovery succeeds: " + rolled.error);
    expect(read_file(install / "SDL3.dll") == "sdl v1" && read_file(install / "BattleSpadesClient.exe") == "client v1",
           "recovery restores the old version");
}

void test_package_root() {
    updater_test::TempDir temp{"root"};
    write_file(temp.path() / "BattleSpadesClient-0.3.0-Windows-AMD64" / "bin" / "BattleSpadesClient.exe", "x");
    write_file(temp.path() / "BattleSpadesClient-0.3.0-Windows-AMD64" / "include" / "x.h", "x");
    const auto found = up::find_package_root(temp.path(), "", "BattleSpadesClient.exe");
    expect(found == temp.path() / "BattleSpadesClient-0.3.0-Windows-AMD64" / "bin", "auto-detected CPack bin folder");
    const auto hinted =
        up::find_package_root(temp.path(), "BattleSpadesClient-0.3.0-Windows-AMD64/bin", "BattleSpadesClient.exe");
    expect(hinted == found, "manifest root hint");
    expect(!up::find_package_root(temp.path(), "", "missing.exe").has_value(), "no marker, no root");
}

/// A server-only update touches install/server and nothing else, and its
/// rollback is independent of the client's.
void test_component_updates() {
    updater_test::TempDir temp{"component"};
    const auto install = temp.path() / "BattleSpades";
    const up::UpdateLayout layout{install};
    make_install(install);
    write_file(install / "server" / "VERSION", "0.2.0-beta.2\n");
    write_file(install / "server" / "BattleSpades.exe", "server v1");
    write_file(install / "server" / "config.toml", "operator config");
    write_file(install / "assets" / "client" / "cosmetics" / "hat.png", "hat v1");
    const auto before_client = read_file(install / "BattleSpadesClient.exe");

    auto versions = up::read_installed_versions(install);
    expect(versions.at("client") == "0.2.0-beta.1" && versions.at("server") == "0.2.0-beta.2" &&
               !versions.contains("assets"),
           "versions come from battlespades-version.json and server/VERSION");

    const auto server_stage = temp.path() / "stage-server" / "BattleSpades-0.3.0";
    write_file(server_stage / "BattleSpades.exe", "server v2");
    write_file(server_stage / "VERSION", "0.3.0\n");
    write_file(server_stage / "config.toml", "shipped config");
    write_file(server_stage / "_internal" / "new.pyd", "new");
    up::ApplyOptions server;
    server.component = "server";
    server.target = "server";
    server.foreign_targets = {"", "server", "assets/client"};
    server.preserve = {"config.toml"};
    server.mirror_directories = {"_internal"};
    server.from_version = "0.2.0-beta.2";
    server.to_version = "0.3.0";
    server.session = "s1";
    const auto applied = up::apply_staged_tree(layout, server_stage, server);
    expect(applied.ok, "server-only update applies: " + applied.error);
    expect(read_file(install / "server" / "BattleSpades.exe") == "server v2", "server replaced");
    expect(read_file(install / "server" / "config.toml") == "operator config", "operator config preserved");
    expect(!fs::exists(install / "server" / "_internal" / "old_module.pyd"), "server runtime mirrored");
    expect(read_file(install / "server" / "maps" / "custom.vxl") == "player map", "server maps untouched");
    expect(read_file(install / "BattleSpadesClient.exe") == before_client, "client untouched by a server update");
    expect(read_file(install / "SDL3.dll") == "sdl v1", "client DLLs untouched");
    expect(up::read_installed_versions(install).at("server") == "0.3.0", "server version follows server/VERSION");
    expect(fs::exists(layout.rollback_dir("server") / "journal.json") && !fs::exists(layout.rollback_dir("client")),
           "rollback set is per component");

    // An assets update right after: the server's rollback set survives.
    const auto assets_stage = temp.path() / "stage-assets";
    write_file(assets_stage / "cosmetics" / "hat.png", "hat v2");
    up::ApplyOptions assets;
    assets.component = "assets";
    assets.target = "assets/client";
    assets.foreign_targets = server.foreign_targets;
    assets.mirror_directories = {"cosmetics"};
    assets.session = "s2";
    expect(up::apply_staged_tree(layout, assets_stage, assets).ok, "assets update applies");
    std::string error;
    expect(up::record_installed_version(layout, "assets", "2.0.0", error), "assets version recorded");
    expect(up::read_installed_versions(install).at("assets") == "2.0.0", "assets version read back from installed.json");
    expect(up::rollback_components(layout) == std::vector<std::string>{"assets", "server"}, "both rollback sets kept");
    expect(up::read_rollback_info(layout, "server")->session == "s1", "session recorded in the journal");

    const auto rolled = up::rollback_last_update(layout, "server");
    expect(rolled.ok, "server rollback: " + rolled.error);
    expect(read_file(install / "server" / "BattleSpades.exe") == "server v1" &&
               fs::exists(install / "server" / "_internal" / "old_module.pyd"),
           "server restored");
    expect(read_file(install / "assets" / "client" / "cosmetics" / "hat.png") == "hat v2",
           "rolling back the server leaves the assets update in place");

    // A client package may not carry another component's files.
    const auto bad_stage = temp.path() / "stage-bad";
    write_file(bad_stage / "BattleSpadesClient.exe", "client v3");
    write_file(bad_stage / "server" / "BattleSpades.exe", "smuggled");
    up::ApplyOptions client;
    client.foreign_targets = server.foreign_targets;
    const auto refused = up::apply_staged_tree(layout, bad_stage, client);
    expect(!refused.ok && read_file(install / "BattleSpadesClient.exe") == before_client,
           "a client package containing server files is refused before anything changes");

    // Client mirror folders never prune a foreign component's folder.
    const auto client_stage = temp.path() / "stage-client";
    write_file(client_stage / "BattleSpadesClient.exe", "client v3");
    write_file(client_stage / "assets" / "ui.png", "client ui");
    client.mirror_directories = {"assets"};
    client.preserve = {"assets/original/**"};
    expect(up::apply_staged_tree(layout, client_stage, client).ok, "client update applies");
    expect(read_file(install / "assets" / "client" / "cosmetics" / "hat.png") == "hat v2",
           "mirroring client assets/ leaves assets/client to its own component");
    expect(read_file(install / "assets" / "original" / "png" / "a.png") == "retail asset",
           "preserve globs protect imported retail files inside a mirrored folder");
}

void test_legacy_state_file() {
    updater_test::TempDir temp{"state"};
    const up::UpdateLayout layout{temp.path()};
    write_file(layout.state_file(), R"({"skipped_version":"0.3.0"})");
    expect(up::load_updater_state(layout).skipped_versions.at("client") == "0.3.0",
           "schema-1 state maps to the client component");
}

void test_unsafe_stage_rejected() {
    updater_test::TempDir temp{"unsafe"};
    const up::UpdateLayout layout{temp.path() / "install"};
    const auto stage = temp.path() / "stage";
    write_file(stage / "update" / "state.json", "{}");
    const auto result = up::apply_staged_tree(layout, stage, {});
    expect(!result.ok, "a package may not write into the updater's own folder");
}

} // namespace

int main() {
    return updater_test::run("aos_updater_apply_tests", [] {
        test_glob();
        test_apply_and_rollback();
        test_failed_apply_restores_everything();
        test_interrupted_apply_recovery();
        test_package_root();
        test_component_updates();
        test_legacy_state_file();
        test_unsafe_stage_rejected();
    });
}
