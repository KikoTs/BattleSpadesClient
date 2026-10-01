// stable.json (schema 2): components, mirrors, required, independent
// rollouts, hosting compatibility; the update planner (large-update
// deferral, opt-in installs) and mirror fallback order.

#include "updater_test_support.hpp"

#include "battlespades/updater/launcher_args.hpp"
#include "battlespades/updater/update_manifest.hpp"
#include "battlespades/updater/update_plan.hpp"
#include "battlespades/updater/updater_config.hpp"

#include <algorithm>

namespace up = battlespades::updater;
using updater_test::expect;
using updater_test::replace_all;

namespace {

const std::string digest(64U, 'a');

std::string manifest_json() {
    return R"({
  "schema": 2,
  "product": "BattleSpades",
  "channel": "stable",
  "published": "2026-10-01T12:00:00Z",
  "components": {
    "client": {
      "version": "0.3.0",
      "protocol": 168,
      "hosting_server": ">=0.3.0 <0.4.0",
      "package": "BattleSpades-client-0.3.0-windows-x64.zip",
      "size": 1000,
      "sha256": ")" + digest + R"(",
      "root": "BattleSpades-client-0.3.0-windows-x64",
      "urls": ["https://github.com/KikoTs/BattleSpadesClient/releases/download/v0.3.0/BattleSpades-client-0.3.0-windows-x64.zip",
               "https://updates.aosplay.net/client/BattleSpades-client-0.3.0-windows-x64.zip"],
      "preserve": ["ui-layout.json"],
      "mirror_directories": ["shaders"]
    },
    "server": {
      "version": "0.3.0",
      "protocol": 168,
      "package": "BattleSpades-server-0.3.0-windows-x64.zip",
      "size": 2000,
      "sha256": ")" + digest + R"(",
      "urls": ["https://updates.aosplay.net/server/BattleSpades-server-0.3.0-windows-x64.zip"],
      "preserve": ["config.toml", "bans.json"],
      "mirror_directories": ["_internal"]
    },
    "assets": {
      "version": "2.0.0",
      "package": "BattleSpades-assets-2.0.0.zip",
      "size": 500000000,
      "sha256": ")" + digest + R"(",
      "urls": ["https://updates.aosplay.net/assets/BattleSpades-assets-2.0.0.zip"]
    },
    "retail_assets": {
      "version": "1.0.0",
      "package": "retail-assets-1.0.0.zip",
      "size": 400000000,
      "sha256": ")" + digest + R"(",
      "urls": ["https://kiril-host.example/retail-assets-1.0.0.zip"]
    }
  }
})";
}

up::UpdateManifest parse(const std::string& json) {
    std::string error;
    auto manifest = up::parse_update_manifest(json, error);
    expect(manifest.has_value(), "manifest parses: " + error);
    return *manifest;
}

bool rejects(std::string json, std::string_view from, std::string_view to) {
    replace_all(json, from, to);
    std::string error;
    return !up::parse_update_manifest(json, error).has_value() && !error.empty();
}

void test_manifest_parsing() {
    const auto manifest = parse(manifest_json());
    expect(manifest.components.size() == 4U && !manifest.required, "four components");
    const auto* client = manifest.component("client");
    const auto* server = manifest.component("server");
    const auto* assets = manifest.component("assets");
    expect(client && server && assets, "components by name");
    expect(client->urls.size() == 2U && client->urls[0].find("github.com") != std::string::npos,
           "mirrors keep their order");
    expect(client->target.empty() && server->target == "server" && assets->target == "assets/client",
           "default install folders per component");
    expect(client->protocol == 168U && client->hosting_server == ">=0.3.0 <0.4.0", "protocol and hosting rule");
    expect(manifest.component("retail_assets")->target == "assets/original", "retail assets default folder");
    expect(server->preserve.size() == 2U && server->mirror_directories == std::vector<std::string>{"_internal"},
           "apply options per component");

    expect(rejects(manifest_json(), "\"schema\": 2", "\"schema\": 1"), "schema 1 is the old GitHub manifest");
    expect(rejects(manifest_json(), "\"product\": \"BattleSpades\"", "\"product\": \"Other\""), "product checked");
    expect(rejects(manifest_json(), "https://updates.aosplay.net/server/", "http://updates.aosplay.net/server/"),
           "plain-http mirrors rejected");
    expect(rejects(manifest_json(), "\"size\": 2000", "\"size\": 0"), "a size is mandatory");
    expect(rejects(manifest_json(), "\"size\": 2000,\n      \"sha256\": \"" + digest, "\"size\": 2000,\n      \"sha256\": \"xyz"),
           "bad digest rejected");
    expect(rejects(manifest_json(), "\"urls\": [\"https://updates.aosplay.net/server/BattleSpades-server-0.3.0-windows-x64.zip\"]",
                   "\"urls\": []"),
           "a component needs a mirror");
    expect(rejects(manifest_json(), "\"preserve\": [\"config.toml\", \"bans.json\"]", "\"preserve\": [\"../x\"]"),
           "escaping paths rejected");
    expect(rejects(manifest_json(), "\"version\": \"2.0.0\",", "\"version\": \"2.0.0\", \"target\": \"update\","),
           "no component may target the updater's folder");
    expect(rejects(manifest_json(), "\"version\": \"2.0.0\",", "\"version\": \"2.0.0\", \"target\": \"server\","),
           "two components cannot share a folder");
    expect(rejects(manifest_json(), "\"mirror_directories\": [\"shaders\"]", "\"mirror_directories\": [\"assets\"]"),
           "the client may not mirror (prune) the assets component's folder");
    expect(rejects(manifest_json(), "\"version\": \"2.0.0\",", "\"version\": \"2.0.0\", \"target\": \"server/packs\","),
           "nested component folders rejected");
    expect(rejects(manifest_json(), ">=0.3.0 <0.4.0", ">=banana"), "invalid hosting constraint rejected");
    expect(rejects(manifest_json(), "\"mirror_directories\": [\"shaders\"]", "\"mirror_directories\": [\"assets/original\"]"),
           "the client may never prune the imported retail files");
    expect(rejects(manifest_json(), "\"package\": \"BattleSpades-assets-2.0.0.zip\"", "\"package\": \"../x.zip\""),
           "package is a plain file name");

    std::string required = manifest_json();
    replace_all(required, "\"channel\": \"stable\",", "\"channel\": \"stable\", \"required\": true,");
    expect(parse(required).required, "release-wide required flag");
}

void test_constraints() {
    expect(up::satisfies_constraint("0.3.1", ">=0.3.0 <0.4.0"), "range");
    expect(!up::satisfies_constraint("0.4.0", ">=0.3.0 <0.4.0"), "upper bound exclusive");
    expect(!up::satisfies_constraint("0.3.0-beta.1", ">=0.3.0"), "a pre-release is below its release");
    expect(up::satisfies_constraint("0.3.0", "0.3.0") && up::satisfies_constraint("0.3.0", "==0.3.0"), "exact");
    expect(up::satisfies_constraint("1.0.0", ">=0.9.0,<=1.0.0"), "comma separated");
    expect(up::satisfies_constraint("1.0.0", ""), "empty constraint always holds");
    expect(!up::satisfies_constraint("garbage", ">=0.1.0"), "unparseable version fails");
}

up::InstalledVersions old_install() {
    return {{"client", "0.2.0"}, {"server", "0.2.0"}, {"assets", "1.0.0"}};   // retail assets imported from a folder
}

const up::PlannedUpdate* find(const up::UpdatePlan& plan, std::string_view name) {
    for (const auto& update : plan.updates) {
        if (update.release.name == name) return &update;
    }
    return nullptr;
}

void test_planner() {
    const auto manifest = parse(manifest_json());
    up::UpdaterState state;
    const up::PlanPolicy policy{200ULL * 1024ULL * 1024ULL, 3U};

    auto plan = up::plan_updates(manifest, old_install(), state, policy);
    expect(plan.updates.size() == 3U, "three installed components updated");
    expect(find(plan, "client")->prompt == up::UpdatePrompt::automatic &&
               find(plan, "server")->prompt == up::UpdatePrompt::automatic,
           "small updates are automatic");
    expect(find(plan, "assets")->large && find(plan, "assets")->prompt == up::UpdatePrompt::ask,
           "a 500 MB update asks Update now / Later");

    // Each Later is counted; the fourth launch makes it mandatory.
    for (std::uint32_t laters = 1U; laters <= 3U; ++laters) {
        state.deferrals[up::deferral_key(*manifest.component("assets"))] = laters;
        plan = up::plan_updates(manifest, old_install(), state, policy);
        const auto expected = laters >= 3U ? up::UpdatePrompt::mandatory : up::UpdatePrompt::ask;
        expect(find(plan, "assets")->prompt == expected && find(plan, "assets")->deferrals == laters,
               "deferral " + std::to_string(laters));
    }
    state.deferrals.clear();

    up::InstalledVersions current{{"client", "0.3.0"}, {"server", "0.3.0"}, {"assets", "2.0.0"}};
    expect(up::plan_updates(manifest, current, state, policy).updates.empty(), "nothing to do when current");

    // Optional components that are not installed are offered, never forced -
    // not even when the new client's hosting rule would want a newer server.
    up::InstalledVersions main_build{{"client", "0.2.0"}, {"assets", "2.0.0"}};
    plan = up::plan_updates(manifest, main_build, state, policy);
    expect(find(plan, "server") == nullptr && find(plan, "retail_assets") == nullptr && find(plan, "client") != nullptr,
           "main build: server and retail assets are not forced");
    expect(std::ranges::any_of(plan.notes, [](const std::string& n) { return n.find("server is not installed") != std::string::npos; }),
           "the skipped optional component is noted");

    // Independent rollouts: each side updates on its own, in both directions.
    up::InstalledVersions client_ahead{{"client", "0.3.0"}, {"server", "0.1.0"}};
    plan = up::plan_updates(manifest, client_ahead, state, policy);
    expect(plan.updates.size() == 1U && find(plan, "server") != nullptr && find(plan, "server")->prompt == up::UpdatePrompt::automatic,
           "client 0.3 + server 0.1: only the server is offered, nothing is forced");
    up::InstalledVersions server_ahead{{"client", "0.1.0"}, {"server", "0.3.0"}};
    plan = up::plan_updates(manifest, server_ahead, state, policy);
    expect(plan.updates.size() == 1U && find(plan, "client") != nullptr, "client 0.1 + server 0.3: only the client");

    // Explicit opt-in installs ("Download game assets", "Enable hosting").
    const auto installs = up::plan_install(manifest, main_build, {"server", "retail_assets", "missing"});
    expect(installs.size() == 2U && installs[0].release.name == "server" && installs[1].release.name == "retail_assets",
           "opt-in installs of offered components");
    expect(installs[1].release.target == "assets/original", "retail assets land where the importer writes");
    expect(up::plan_install(manifest, current, {"server"}).empty(), "an up-to-date server is not reinstalled");

    // A rolled-back version is not reinstalled unless required.
    state.skipped_versions["client"] = "0.3.0";
    plan = up::plan_updates(manifest, old_install(), state, policy);
    expect(find(plan, "client") == nullptr && !plan.notes.empty(), "rolled-back client skipped");
    state.skipped_versions.clear();

    // Required: per component and release-wide; only non-server ones block play.
    std::string required_server = manifest_json();
    replace_all(required_server, "\"preserve\": [\"config.toml\", \"bans.json\"],",
                "\"preserve\": [\"config.toml\", \"bans.json\"], \"required\": true,");
    plan = up::plan_updates(parse(required_server), old_install(), state, policy);
    expect(find(plan, "server")->prompt == up::UpdatePrompt::mandatory, "required component is mandatory");
    expect(find(plan, "client")->prompt == up::UpdatePrompt::automatic, "other components stay optional");
    expect(!up::blocks_play(*find(plan, "server")), "a failed required server update never blocks playing");
    std::string required_all = manifest_json();
    replace_all(required_all, "\"channel\": \"stable\",", "\"channel\": \"stable\", \"required\": true,");
    plan = up::plan_updates(parse(required_all), old_install(), state, policy);
    expect(std::ranges::all_of(plan.updates, [](const auto& u) { return u.prompt == up::UpdatePrompt::mandatory; }),
           "release-wide required makes every update mandatory");
    expect(up::blocks_play(*find(plan, "client")), "a required client update blocks until installed");

    // Declining drops only the declined, non-mandatory update.
    plan = up::plan_updates(manifest, old_install(), state, policy);
    const auto kept = up::without_declined(plan.updates, {"assets"});
    expect(kept.size() == 2U, "declining assets keeps client and server");
}

void test_hosting_status() {
    const auto manifest = parse(manifest_json());

    // Not installed (main build): offer the download.
    auto status = up::hosting_status(&manifest, {{"client", "0.3.0"}}, 168U, std::nullopt);
    expect(status.state == up::HostingState::not_installed && status.update_available &&
               status.available_server == "0.3.0" && status.download_size == 2000U,
           "missing server: hosting offers the download");

    // Client 0.3 with server 0.3: ready.
    status = up::hosting_status(&manifest, {{"client", "0.3.0"}, {"server", "0.3.0"}}, 168U, std::nullopt);
    expect(status.state == up::HostingState::ready, "matching protocol and rule: ready");

    // Client 0.3 with server 0.1: the client's hosting rule wants >=0.3.0.
    status = up::hosting_status(&manifest, {{"client", "0.3.0"}, {"server", "0.1.0"}}, 168U, std::nullopt);
    expect(status.state == up::HostingState::update_required && status.update_available &&
               status.reason.find("hosting needs server >=0.3.0 <0.4.0") != std::string::npos,
           "old server: update required to host, update offered: " + status.reason);

    // Client 0.1 with server 0.3: no rule known for that client, same protocol: ready.
    status = up::hosting_status(&manifest, {{"client", "0.1.0"}, {"server", "0.3.0"}}, 168U, std::nullopt);
    expect(status.state == up::HostingState::ready, "newer server, older client, same protocol: ready");

    // Protocol mismatch: hosting disabled with an update prompt, never a block.
    std::string newer_protocol = manifest_json();
    replace_all(newer_protocol, "\"protocol\": 168,\n      \"package\": \"BattleSpades-server",
                "\"protocol\": 169,\n      \"package\": \"BattleSpades-server");
    const auto protocol_manifest = parse(newer_protocol);   // different client/server protocols are allowed now
    status = up::hosting_status(&protocol_manifest, {{"client", "0.2.0"}, {"server", "0.3.0"}}, 168U, std::nullopt);
    expect(status.state == up::HostingState::update_required && !status.update_available &&
               status.reason.find("protocol 169, this client 168") != std::string::npos,
           "protocol mismatch: update required to host, no compatible server offered: " + status.reason);
    status = up::hosting_status(&protocol_manifest, {{"client", "0.2.0"}, {"server", "0.2.0"}}, 168U, std::nullopt);
    expect(status.state == up::HostingState::ready && !status.update_available,
           "an incompatible newer server is never offered as the fix");

    // Offline (no manifest): an installed server is assumed usable.
    status = up::hosting_status(nullptr, {{"client", "0.2.0"}, {"server", "0.1.0"}}, 168U, std::nullopt);
    expect(status.state == up::HostingState::ready, "offline with a server: ready");
    status = up::hosting_status(nullptr, {{"client", "0.2.0"}}, 168U, std::nullopt);
    expect(status.state == up::HostingState::not_installed && !status.update_available, "offline without a server");

    const auto json = up::hosting_status_json(status);
    expect(json.find("\"state\": \"not_installed\"") != std::string::npos, "hosting.json for the client");

    // Legacy "requires": {"server": ...} maps to the hosting rule.
    std::string legacy = manifest_json();
    replace_all(legacy, "\"hosting_server\"", "\"requires\": {\"server\": \">=0.3.0\"}, \"x_unused\"");
    expect(parse(legacy).component("client")->hosting_server == ">=0.3.0", "legacy requires.server read as hosting rule");
}

void test_mirror_fallback() {
    const std::vector<std::string> urls{"https://github.example/pkg.zip", "https://r2.example/pkg.zip",
                                        "https://third.example/pkg.zip"};
    std::vector<std::string> tried;
    auto result = up::try_mirrors(urls, [&](const std::string& url, std::string& error) {
        tried.push_back(url);
        if (url == urls[0]) {
            error = "HTTP 503";
            return up::MirrorOutcome::failed;
        }
        if (url == urls[1]) {
            error = "SHA-256 mismatch";
            return up::MirrorOutcome::failed;
        }
        return up::MirrorOutcome::success;
    });
    expect(result.outcome == up::MirrorOutcome::success && result.used == 2U, "third mirror used");
    expect(tried == urls, "mirrors tried strictly in order");
    expect(result.errors.size() == 2U && result.errors[0].find("503") != std::string::npos &&
               result.errors[1].find("mismatch") != std::string::npos,
           "each failure recorded");

    tried.clear();
    result = up::try_mirrors(urls, [&](const std::string& url, std::string&) {
        tried.push_back(url);
        return up::MirrorOutcome::success;
    });
    expect(result.used == 0U && tried.size() == 1U, "the first good mirror ends the search");

    tried.clear();
    result = up::try_mirrors(urls, [&](const std::string& url, std::string&) {
        tried.push_back(url);
        return url == urls[0] ? up::MirrorOutcome::failed : up::MirrorOutcome::cancelled;
    });
    expect(result.outcome == up::MirrorOutcome::cancelled && tried.size() == 2U, "cancel stops immediately");

    result = up::try_mirrors(urls, [](const std::string&, std::string& error) {
        error = "timeout";
        return up::MirrorOutcome::failed;
    });
    expect(result.outcome == up::MirrorOutcome::failed && result.errors.size() == 3U, "all mirrors failed");
}

void test_config_and_arguments() {
    std::string error;
    auto config = up::parse_updater_config("{}", error);
    expect(up::manifest_endpoint(config) == "https://www.aosplay.net/updates/stable.json", "default manifest URL");
    expect(!config.github_fallback && config.large_update_bytes == 200ULL * 1024ULL * 1024ULL && config.max_deferrals == 3U,
           "defaults");
    config = up::parse_updater_config(R"({"channel": "beta"})", error);
    expect(up::manifest_endpoint(config) == "https://www.aosplay.net/updates/beta.json", "channel file");
    config = up::parse_updater_config(R"({"channel": "../x", "manifest_url": "http://evil.example/x.json"})", error);
    expect(up::manifest_endpoint(config) == "https://www.aosplay.net/updates/stable.json", "bad channel and URL ignored");
    config = up::parse_updater_config(
        R"({"manifest_url": "https://mirror.example/s.json", "large_update_bytes": 1000, "max_deferrals": 5, "github_fallback": true})",
        error);
    expect(up::manifest_endpoint(config) == "https://mirror.example/s.json" && config.large_update_bytes == 1000U &&
               config.max_deferrals == 5U && config.github_fallback,
           "explicit settings");

    const auto arguments = up::parse_launcher_arguments({"--update-manifest", "http://127.0.0.1:9/s.json", "--update-only"});
    expect(arguments.manifest_url == "http://127.0.0.1:9/s.json" && arguments.update_only && arguments.forwarded.empty(),
           "--update-manifest switch");
}

} // namespace

int main() {
    return updater_test::run("aos_updater_manifest_tests", [] {
        test_manifest_parsing();
        test_constraints();
        test_planner();
        test_hosting_status();
        test_mirror_fallback();
        test_config_and_arguments();
    });
}
