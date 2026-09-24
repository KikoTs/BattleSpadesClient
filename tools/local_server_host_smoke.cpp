#include "battlespades/network/enet_protocol168_client.hpp"
#include "battlespades/network/revival_identity.hpp"
#include "battlespades/network/server_discovery.hpp"
#include "battlespades/platform/local_server_process.hpp"
#include "battlespades/platform/relay_host_tunnel.hpp"
#if defined(AOS_HAS_STEAM_NETWORKING)
#include "battlespades/platform/steam_networking.hpp"
#endif

#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

int main(int argc, char** argv) {
    const bool public_relay = argc == 3 && std::string_view{argv[2]} == "--public";
    const bool native_bridge = argc == 3 && std::string_view{argv[2]} == "--native-bridge";
    // Drives the join a friend makes: the bundled server, the Steam transport
    // carrying its datagrams, the host's hello, and a real Protocol 168
    // bootstrap through the tunnel. Steam refuses a peer-to-peer connection to
    // your own account, so the transport runs in its direct mode, which
    // exercises the same accept, hello and forwarding code on one machine.
    const bool steam_tunnel = argc == 3 && std::string_view{argv[2]} == "--steam-tunnel";
    if (argc != 2 && !public_relay && !native_bridge && !steam_tunnel) {
        std::cerr << "usage: aos_local_server_host_smoke <portable-server-directory> "
                     "[--public|--native-bridge|--steam-tunnel]\n";
        return 2;
    }
#if !defined(AOS_HAS_STEAM_NETWORKING)
    if (steam_tunnel) {
        std::cerr << "this build has no Steam transport\n";
        return 2;
    }
#endif
    battlespades::platform::LocalServerLaunchConfig config;
    config.bundle_root = std::filesystem::path{argv[1]};
    config.server_name = "BattleSpadesClient Host Smoke";
    config.mode = "tdm";
    config.map_name = "AncientEgypt";
    config.maximum_players = 4U;
    config.match_minutes = 5U;

    battlespades::network::RevivalIdentityService identity;
    std::optional<battlespades::network::RevivalRelayLobby> relay;
    if (public_relay) {
        battlespades::network::RevivalRelayLobbyRequest request;
        request.name = config.server_name;
        request.map = config.map_name;
        request.game_mode = "TDM_TITLE";
        request.mode_tla = config.mode;
        request.max_players = config.maximum_players;
        request.texture_skin = "classic";
        auto allocated = identity.create_relay_lobby(request);
        if (!allocated || !allocated.lobby.has_value()) {
            std::cerr << "public relay allocation failed: " << allocated.error << '\n';
            return 1;
        }
        relay = std::move(*allocated.lobby);
        config.environment_overrides = {
            {"AOS_MASTER_URL", relay->master_url},
            {"AOS_MASTER_WRITE_TOKEN", relay->server_token},
            {"AOS_PUBLIC_HOST", relay->relay_host},
            {"AOS_PUBLIC_PORT", std::to_string(relay->relay_port)},
            {"AOS_PUBLIC_QUERY_PORT", std::to_string(relay->relay_port)},
            {"AOS_SERVER_ID", relay->server_id},
        };
    }

    battlespades::platform::LocalServerProcess process;
    battlespades::platform::RelayHostTunnel tunnel;
#if defined(AOS_HAS_STEAM_NETWORKING)
    battlespades::platform::SteamNetworkingRuntime steam_runtime;
    battlespades::platform::SteamP2PHost steam_host;
    battlespades::platform::SteamP2PClient steam_client;
#endif
    const auto cleanup = [&] {
#if defined(AOS_HAS_STEAM_NETWORKING)
        steam_client.stop();
        steam_host.stop();
        steam_runtime.stop();
#endif
        tunnel.stop();
        process.stop();
        if (relay.has_value()) {
            static_cast<void>(identity.close_relay_lobby(*relay));
        }
    };
    std::string error;
    const auto started = std::chrono::steady_clock::now();
    if (!process.start(config, error)) {
        std::cerr << "host smoke launch failed: " << error << '\n';
        if (relay.has_value()) static_cast<void>(identity.close_relay_lobby(*relay));
        return 1;
    }
    if (relay.has_value()) {
        battlespades::platform::RelayHostTunnelConfig tunnel_config;
        tunnel_config.allocation_id = relay->allocation_id;
        tunnel_config.relay_host = relay->relay_host;
        tunnel_config.relay_port = relay->relay_port;
        tunnel_config.host_key_base64url = relay->host_key;
        tunnel_config.local_server_port = process.port();
        tunnel_config.maximum_clients = config.maximum_players;
        tunnel_config.keepalive = std::chrono::seconds{relay->keepalive_seconds};
        if (!tunnel.start(std::move(tunnel_config), error)) {
            std::cerr << "public relay tunnel failed: " << error << '\n';
            cleanup();
            return 1;
        }
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
    while (process.running() && std::chrono::steady_clock::now() < deadline) {
        if (native_bridge && process.status() != battlespades::platform::LocalServerState::ready) {
            std::this_thread::sleep_for(std::chrono::milliseconds{25});
            continue;
        }
        if (native_bridge) {
            std::cout << "native bridge ready after " << std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count() << " ms\n";
        }
        const auto result = battlespades::network::probe_lan_server(
            {"127.0.0.1", process.port()}, std::chrono::milliseconds{350});
        if (result && !result.servers.empty()) {
            const auto& server = result.servers.front();
            std::cout << "local host ready: " << server.name << " "
                      << server.game.identifier() << " mode=" << server.mode_code
                      << " map=" << server.map << '\n';
            battlespades::network::Protocol168SessionConfig session_config;
            auto endpoint = battlespades::network::ServerEndpoint{"127.0.0.1", process.port()};
            session_config.player_name = "LocalHostSmoke";
#if defined(AOS_HAS_STEAM_NETWORKING)
            if (steam_tunnel) {
                constexpr std::uint16_t direct_port{27099U};
                battlespades::platform::SteamNetworkingRuntimeConfig runtime_config;
                // The direct mode never touches the relays, so waiting for them
                // would only slow the check down.
                runtime_config.relay_timeout = std::chrono::seconds{0};
                // The build copies libsteam_api beside this tool, exactly as
                // the install rules place it beside the game.
                runtime_config.search_directory = std::filesystem::path{argv[0]}.parent_path();
                if (!steam_runtime.start(std::move(runtime_config), error)) {
                    std::cerr << "steam runtime failed: " << error << '\n';
                    cleanup();
                    return 1;
                }
                battlespades::platform::SteamP2PHostConfig host_config;
                host_config.local_server_port = process.port();
                host_config.direct_listen_port = direct_port;
                if (!steam_host.start(steam_runtime, host_config, error)) {
                    std::cerr << "steam host failed: " << error << '\n';
                    cleanup();
                    return 1;
                }
                battlespades::platform::SteamP2PClientConfig client_config;
                client_config.direct_connect_port = direct_port;
                if (!steam_client.start(steam_runtime, client_config, error)) {
                    std::cerr << "steam join failed: " << error << '\n';
                    cleanup();
                    return 1;
                }
                const auto hello = steam_client.wait_for_host_hello(std::chrono::seconds{10});
                if (!hello.has_value()) {
                    std::cerr << "the host never sent its hello: " << steam_client.last_error()
                              << '\n';
                    cleanup();
                    return 1;
                }
                std::cout << "steam tunnel ready: loopback port " << steam_client.local_port()
                          << ", host asks for a ticket for "
                          << (hello->empty() ? "no identity" : *hello) << '\n';
                endpoint = {"127.0.0.1", steam_client.local_port()};
            }
#endif
            if (relay.has_value()) {
                const auto publish_deadline = std::chrono::steady_clock::now() +
                                              std::chrono::seconds{30};
                bool published{};
                while (std::chrono::steady_clock::now() < publish_deadline) {
                    const auto discovered = battlespades::network::discover_public_servers({});
                    if (battlespades::network::find_discovered_server(
                            discovered.servers, relay->server_id).has_value()) {
                        published = true;
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds{250});
                }
                if (!published) {
                    std::cerr << "public host never appeared in AoSPlay discovery\n";
                    cleanup();
                    return 1;
                }
                const auto ticket = identity.game_ticket(relay->server_id);
                if (!ticket) {
                    std::cerr << "public host ticket failed: " << ticket.error << '\n';
                    cleanup();
                    return 1;
                }
                session_config.player_name = ticket.join_code;
                endpoint = {relay->relay_host, relay->relay_port};
                std::cout << "public host published: " << relay->server_id << '\n';
            }
            battlespades::network::Protocol168Session session{session_config};
            const auto connected =
                battlespades::network::run_enet_protocol168_session(
                    {endpoint.host, endpoint.port, 30'000U}, session);
            if (!connected.ready || session.initial_info() == nullptr ||
                session.map() == nullptr) {
                std::cerr << "local host Protocol 168 bootstrap failed: "
                          << connected.error << '\n';
                cleanup();
                return 1;
            }
            std::cout << "Protocol 168 bootstrap: map="
                      << session.initial_info()->map_name
                      << " voxels=" << session.map()->solid_voxels() << '\n';
            const auto valid = server.mode_code == "tdm" &&
                               !session.initial_info()->map_name.empty() &&
                               session.map()->solid_voxels() > 0U;
            cleanup();
            return valid ? 0 : 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }
    std::cerr << "host smoke readiness failed; log=" << process.log_path().string() << '\n';
    cleanup();
    return 1;
}
