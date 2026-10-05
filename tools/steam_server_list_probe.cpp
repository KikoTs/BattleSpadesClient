// Lists what Steam's own game server list (ISteamMatchmakingServers) returns
// for Ace of Spades, the list the client merges into its server browser.
// Needs a running Steam client; usage: aos_steam_server_list_probe [app_id]

#include "battlespades/platform/steam_networking.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

int main(int argc, char* argv[]) {
    const auto app_id = argc > 1 ? static_cast<std::uint32_t>(std::strtoul(argv[1], nullptr, 10)) : 224540U;
    battlespades::platform::SteamNetworkingRuntime runtime;
    std::string error;
    if (!runtime.start({}, error)) {
        std::fprintf(stderr, "Steam did not start: %s\n", error.c_str());
        return 1;
    }
    if (!runtime.begin_internet_server_query(app_id)) {
        std::fprintf(stderr, "Steam refused the server list query\n");
        return 1;
    }
    const auto started = std::chrono::steady_clock::now();
    while (!runtime.internet_server_query_done() &&
           std::chrono::steady_clock::now() - started < std::chrono::seconds{15}) {
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }
    const auto servers = runtime.internet_servers();
    std::printf("%zu server(s) for app %u%s\n", servers.size(), app_id,
                runtime.internet_server_query_done() ? "" : " (query still running)");
    for (const auto& server : servers) {
        std::printf("%u.%u.%u.%u:%u  %2u/%-2u  ping %4d  %-32s  %-16s  %s%s\n",
                    (server.ip >> 24U) & 0xFFU, (server.ip >> 16U) & 0xFFU, (server.ip >> 8U) & 0xFFU,
                    server.ip & 0xFFU, server.port, server.players, server.maximum_players, server.ping,
                    server.name.c_str(), server.map.c_str(), server.tags.c_str(),
                    server.password ? "  [password]" : "");
    }
    runtime.stop();
    return 0;
}
