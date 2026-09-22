// Bounded check of the Steam relay transport: one side hosts an echo service,
// the other joins by Steam id and measures the round trip through Valve's
// relays. No AoSPlay service, relay or game server is involved.
#include "battlespades/platform/steam_networking.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include <fcntl.h>
#include <sys/socket.h>

namespace {
using namespace std::chrono_literals;
namespace platform = battlespades::platform;

std::atomic_bool running{true};

/** Loopback echo service standing in for the bundled server. */
class EchoService final {
public:
    [[nodiscard]] bool start(std::uint16_t& port) {
        socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket_ < 0) return false;
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(socket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
            return false;
        }
        sockaddr_in assigned{};
        socklen_t size = sizeof(assigned);
        if (::getsockname(socket_, reinterpret_cast<sockaddr*>(&assigned), &size) != 0) return false;
        port = ntohs(assigned.sin_port);
        worker_ = std::thread{[this] {
            std::vector<char> buffer(2048U);
            while (running.load()) {
                sockaddr_in from{};
                socklen_t from_size = sizeof(from);
                const auto bytes = ::recvfrom(socket_, buffer.data(), buffer.size(), 0,
                                              reinterpret_cast<sockaddr*>(&from), &from_size);
                if (bytes <= 0) continue;
                static_cast<void>(::sendto(socket_, buffer.data(), static_cast<std::size_t>(bytes),
                                           0, reinterpret_cast<const sockaddr*>(&from),
                                           from_size));
            }
        }};
        return true;
    }

    ~EchoService() {
        if (socket_ >= 0) ::close(socket_);
        if (worker_.joinable()) worker_.join();
    }

private:
    int socket_{-1};
    std::thread worker_;
};

[[nodiscard]] platform::SteamNetworkingRuntime* start_runtime(platform::SteamNetworkingRuntime& runtime,
                                                              std::uint32_t app_id) {
    platform::SteamNetworkingRuntimeConfig config;
    config.app_id = app_id;
    std::string error;
    if (!runtime.start(std::move(config), error)) {
        std::printf("steam runtime failed: %s\n", error.c_str());
        return nullptr;
    }
    const auto relays = runtime.relay_status();
    std::printf("account  : %s (%llu)\nrelays   : %s (%s)\n", runtime.persona_name().c_str(),
                static_cast<unsigned long long>(runtime.steam_id()),
                relays.available ? "ready" : "unavailable", relays.detail.c_str());
    return &runtime;
}
} // namespace

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "";
    const std::uint32_t app_id = argc > 3 ? static_cast<std::uint32_t>(std::stoul(argv[3])) : 480U;
    platform::SteamNetworkingRuntime runtime;

    if (mode == "host") {
        if (start_runtime(runtime, app_id) == nullptr) return 1;
        EchoService echo;
        std::uint16_t echo_port{};
        if (!echo.start(echo_port)) {
            std::printf("echo service failed to bind\n");
            return 1;
        }
        platform::SteamP2PHost host;
        platform::SteamP2PHostConfig config;
        config.local_server_port = echo_port;
        if (argc > 2 && std::string{argv[2]} == "local") config.direct_listen_port = 27099U;
        std::string error;
        if (!host.start(runtime, config, error)) {
            std::printf("host failed: %s\n", error.c_str());
            return 1;
        }
        std::printf("hosting  : join with  aos_steam_p2p_smoke join %s\n",
                    config.direct_listen_port != 0U
                        ? "local"
                        : std::to_string(runtime.steam_id()).c_str());
        const auto deadline = std::chrono::steady_clock::now() + 5min;
        while (std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(2s);
            std::printf("clients  : %zu\n", host.connected_clients());
            std::fflush(stdout);
        }
        running.store(false);
        host.stop();
        return 0;
    }

    if (mode == "join" && argc > 2) {
        if (start_runtime(runtime, app_id) == nullptr) return 1;
        platform::SteamP2PClient client;
        platform::SteamP2PClientConfig config;
        if (std::string{argv[2]} == "local") {
            config.direct_connect_port = 27099U;
        } else {
            config.host_steam_id = std::stoull(argv[2]);
        }
        std::string error;
        if (!client.start(runtime, config, error)) {
            std::printf("join failed: %s\n", error.c_str());
            return 1;
        }
        const int probe = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in tunnel{};
        tunnel.sin_family = AF_INET;
        tunnel.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        tunnel.sin_port = htons(client.local_port());
        timeval timeout{};
        timeout.tv_sec = 2;
        static_cast<void>(setsockopt(probe, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)));

        std::vector<double> samples;
        for (int attempt{}; attempt < 20 && client.running(); ++attempt) {
            const auto payload = "battlespades-steam-probe-" + std::to_string(attempt);
            const auto sent = std::chrono::steady_clock::now();
            static_cast<void>(::sendto(probe, payload.data(), payload.size(), 0,
                                       reinterpret_cast<const sockaddr*>(&tunnel), sizeof(tunnel)));
            std::vector<char> buffer(2048U);
            const auto bytes = ::recv(probe, buffer.data(), buffer.size(), 0);
            if (bytes > 0) {
                const auto elapsed = std::chrono::duration<double, std::milli>(
                                         std::chrono::steady_clock::now() - sent)
                                         .count();
                samples.push_back(elapsed);
                std::printf("echo %2d  : %6.1f ms round trip (steam ping %d ms)\n", attempt,
                            elapsed, client.ping_milliseconds());
            } else {
                std::printf("echo %2d  : no reply\n", attempt);
            }
            std::fflush(stdout);
            std::this_thread::sleep_for(250ms);
        }
        if (!samples.empty()) {
            std::ranges::sort(samples);
            std::printf("samples  : %zu, median %.1f ms, best %.1f ms, worst %.1f ms\n",
                        samples.size(), samples[samples.size() / 2U], samples.front(),
                        samples.back());
        } else {
            std::printf("no datagram completed the round trip: %s\n", client.last_error().c_str());
        }
        running.store(false);
        if (probe >= 0) ::close(probe);
        client.stop();
        return samples.empty() ? 1 : 0;
    }

    std::printf("usage: aos_steam_p2p_smoke host [_ app-id] | join <host-steam-id> [app-id]\n");
    return 2;
}
