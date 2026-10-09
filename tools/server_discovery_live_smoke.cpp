#include "battlespades/network/server_discovery.hpp"

#include <charconv>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] bool parse_port(std::string_view text, std::uint16_t& output) {
    unsigned int value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        value == 0U || value > 65'535U) {
        return false;
    }
    output = static_cast<std::uint16_t>(value);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    using namespace battlespades::network;

    DiscoveryResult result;
    if (argc > 1 && std::string_view{argv[1]} == "--classic-ping") {
        for (int index = 2; index < argc; ++index) {
            DiscoveredServer server;
            if (!parse_server_endpoint(argv[index], server.game, result.error)) break;
            if (server.game.protocol == GameProtocol::automatic) server.game.protocol = GameProtocol::classic075;
            if (!is_classic_protocol(server.game.protocol)) {
                result.error = "--classic-ping requires a Classic endpoint";
                break;
            }
            server.name = server.game.identifier();
            result.servers.push_back(std::move(server));
        }
        if (result.error.empty()) measure_classic_server_pings(result.servers);
    } else if (argc > 1 && std::string_view{argv[1]} == "--lan") {
        LanDiscoveryConfig config;
        config.ports.clear();
        for (int index = 2; index < argc; ++index) {
            std::uint16_t port{};
            if (!parse_port(argv[index], port)) {
                std::cerr << "invalid LAN port: " << argv[index] << '\n';
                return 2;
            }
            config.ports.push_back(port);
        }
        if (config.ports.empty()) config.ports = {27'015U, 32'887U};
        result = discover_lan_servers(config);
    } else {
        PublicDiscoveryConfig config;
        if (argc > 1) config.url = argv[1];
        result = discover_public_servers(config);
        measure_classic_server_pings(result.servers);
    }

    if (!result.error.empty()) {
        std::cerr << "discovery failed: " << result.error << '\n';
        return 1;
    }
    if (result.servers.empty()) {
        std::cerr << "discovery returned no servers\n";
        return 1;
    }
    for (const auto& server : result.servers) {
        std::cout << server.game.identifier() << '\t' << server.name << '\t'
                  << server.players << '/' << server.maximum_players << '\t'
                  << server.map << '\t' << server.mode_code << '\t'
                  << (server.ping_known ? std::to_string(server.ping_milliseconds) + "ms" : "unknown") << '\n';
    }
    return 0;
}
