#include "battlespades/network/enet_protocol168_client.hpp"
#include "battlespades/network/protocol168_ugc.hpp"
#include "battlespades/network/server_discovery.hpp"
#include "battlespades/platform/local_server_process.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: aos_ugc_local_server_host_smoke "
                     "<portable-server-directory> <retail-asset-root>\n";
        return 2;
    }
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto temporary = std::filesystem::temp_directory_path() /
                           ("aos-ugc-host-smoke-" + unique);
    const auto publish_root = temporary / "hosted_ugc";

    battlespades::platform::LocalServerLaunchConfig config;
    config.program = battlespades::platform::LocalServerProgram::map_creator;
    config.bundle_root = std::filesystem::path{argv[1]};
    config.session_parent = temporary / "sessions";
    config.server_name = "BattleSpadesClient UGC Host Smoke";
    config.mode = "ugc";
    config.map_name = "DesertBaseplate";
    config.maximum_players = 4U;
    config.bot_count = 0U;
    config.map_creator = battlespades::platform::LocalMapCreatorLaunchConfig{
        .project = "ClientUgcSmoke",
        .terrain = "desert",
        .target_mode = "tdm",
        .title = "Client UGC Smoke",
        .author = "ParityTest",
        .publish_root = publish_root,
        .retail_root = std::filesystem::path{argv[2]},
    };

    battlespades::platform::LocalServerProcess process;
    std::string error;
    if (!process.start(config, error)) {
        std::cerr << "UGC host smoke launch failed: " << error << '\n';
        std::filesystem::remove_all(temporary);
        return 1;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{25};
    while (process.running() && std::chrono::steady_clock::now() < deadline) {
        const auto result = battlespades::network::probe_lan_server(
            {"127.0.0.1", process.port()}, std::chrono::milliseconds{350});
        if (result && !result.servers.empty()) {
            const auto& server = result.servers.front();
            battlespades::network::Protocol168SessionConfig session_config;
            session_config.player_name = "UgcHostSmoke";
            battlespades::network::Protocol168Session session{session_config};
            const auto connected = battlespades::network::run_enet_protocol168_session(
                {"127.0.0.1", process.port(), 30'000U}, session);
            const auto* info = session.initial_info();
            const bool project_written =
                std::filesystem::is_regular_file(publish_root / "maps" / "ClientUgcSmoke.ugc") &&
                std::filesystem::is_regular_file(publish_root / "maps" / "ClientUgcSmoke.vxl") &&
                std::filesystem::is_regular_file(publish_root / "maps" / "ClientUgcSmoke.txt");
            const bool ready = connected.ready && info != nullptr && session.map() != nullptr &&
                               server.mode_code == "ugc" && info->map_is_ugc() &&
                               info->ugc_role == battlespades::network::UgcRole::client &&
                               info->map_name == "DesertBaseplate" && project_written;
            std::cout << "UGC Protocol 168 bootstrap: ready=" << (ready ? "true" : "false")
                      << " transport=" << (connected.ready ? "ready" : "failed")
                      << " world=" << (session.map() != nullptr ? "ready" : "missing")
                      << " mode=" << server.mode_code
                      << " map=" << (info == nullptr ? std::string{} : info->map_name)
                      << " role="
                      << (info == nullptr ? -1 : static_cast<int>(info->ugc_role))
                      << " project=" << (project_written ? "written" : "missing")
                      << " error=" << connected.error << '\n';
            process.stop();
            std::filesystem::remove_all(temporary);
            return ready ? 0 : 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }
    std::cerr << "UGC host smoke readiness failed; log=" << process.log_path().string() << '\n';
    process.stop();
    std::filesystem::remove_all(temporary);
    return 1;
}
