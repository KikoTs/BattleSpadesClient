#include "battlespades/network/enet_protocol168_client.hpp"
#include "battlespades/network/server_discovery.hpp"
#include "battlespades/platform/local_server_process.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: aos_local_server_host_smoke <portable-server-directory>\n";
        return 2;
    }
    battlespades::platform::LocalServerLaunchConfig config;
    config.bundle_root = std::filesystem::path{argv[1]};
    config.server_name = "BattleSpadesClient Host Smoke";
    config.mode = "tdm";
    config.map_name = "AncientEgypt";
    config.maximum_players = 4U;
    config.match_minutes = 5U;

    battlespades::platform::LocalServerProcess process;
    std::string error;
    if (!process.start(config, error)) {
        std::cerr << "host smoke launch failed: " << error << '\n';
        return 1;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
    while (process.running() && std::chrono::steady_clock::now() < deadline) {
        const auto result = battlespades::network::probe_lan_server(
            {"127.0.0.1", process.port()}, std::chrono::milliseconds{350});
        if (result && !result.servers.empty()) {
            const auto& server = result.servers.front();
            std::cout << "local host ready: " << server.name << " "
                      << server.game.identifier() << " mode=" << server.mode_code
                      << " map=" << server.map << '\n';
            battlespades::network::Protocol168Session session{
                battlespades::network::Protocol168SessionConfig{"LocalHostSmoke"}};
            const auto connected =
                battlespades::network::run_enet_protocol168_session(
                    {"127.0.0.1", process.port(), 30'000U}, session);
            if (!connected.ready || session.initial_info() == nullptr ||
                session.map() == nullptr) {
                std::cerr << "local host Protocol 168 bootstrap failed: "
                          << connected.error << '\n';
                process.stop();
                return 1;
            }
            std::cout << "Protocol 168 bootstrap: map="
                      << session.initial_info()->map_name
                      << " voxels=" << session.map()->solid_voxels() << '\n';
            process.stop();
            return server.mode_code == "tdm" &&
                           !session.initial_info()->map_name.empty() &&
                           session.map()->solid_voxels() > 0U
                       ? 0
                       : 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }
    std::cerr << "host smoke readiness failed; log=" << process.log_path().string() << '\n';
    process.stop();
    return 1;
}
