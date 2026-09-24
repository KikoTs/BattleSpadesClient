// Bounded check of the Steam relay transport: one side hosts an echo service,
// the other joins by Steam id and measures the round trip through Valve's
// relays. No AoSPlay service, relay or game server is involved.
#include "battlespades/platform/steam_networking.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
using namespace std::chrono_literals;
namespace platform = battlespades::platform;

#if defined(_WIN32)
using Socket = SOCKET;
using BufferLength = int;
constexpr Socket invalid_socket{INVALID_SOCKET};
void close_socket(Socket value) noexcept {
    if (value != invalid_socket) static_cast<void>(closesocket(value));
}
/** Winsock counts a receive timeout in whole milliseconds. */
void set_receive_timeout(Socket socket, std::chrono::milliseconds timeout) noexcept {
    const DWORD milliseconds{static_cast<DWORD>(timeout.count())};
    static_cast<void>(setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                                 reinterpret_cast<const char*>(&milliseconds),
                                 sizeof(milliseconds)));
}
#else
using Socket = int;
using BufferLength = std::size_t;
constexpr Socket invalid_socket{-1};
void close_socket(Socket value) noexcept {
    if (value != invalid_socket) static_cast<void>(close(value));
}
void set_receive_timeout(Socket socket, std::chrono::milliseconds timeout) noexcept {
    timeval value{};
    value.tv_sec = static_cast<decltype(value.tv_sec)>(timeout.count() / 1000);
    value.tv_usec = static_cast<decltype(value.tv_usec)>((timeout.count() % 1000) * 1000);
    static_cast<void>(setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &value, sizeof(value)));
}
#endif

/** Winsock needs starting before the first socket; POSIX needs nothing. */
[[nodiscard]] bool sockets_ready() noexcept {
    static const bool ready = [] {
#if defined(_WIN32)
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
        return true;
#endif
    }();
    return ready;
}

std::atomic_bool running{true};

/** Loopback echo service standing in for the bundled server. */
class EchoService final {
public:
    [[nodiscard]] bool start(std::uint16_t& port) {
        if (!sockets_ready()) return false;
        socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket_ == invalid_socket) return false;
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
        // A short timeout lets the worker notice that the run finished instead
        // of parking in recvfrom until the process exits.
        set_receive_timeout(socket_, 250ms);
        worker_ = std::thread{[this] {
            std::vector<char> buffer(2048U);
            while (running.load()) {
                sockaddr_in from{};
                socklen_t from_size = sizeof(from);
                const auto bytes =
                    ::recvfrom(socket_, buffer.data(), static_cast<BufferLength>(buffer.size()), 0,
                               reinterpret_cast<sockaddr*>(&from), &from_size);
                if (bytes <= 0) continue;
                static_cast<void>(::sendto(socket_, buffer.data(),
                                           static_cast<BufferLength>(bytes), 0,
                                           reinterpret_cast<const sockaddr*>(&from), from_size));
            }
        }};
        return true;
    }

    ~EchoService() {
        running.store(false);
        if (worker_.joinable()) worker_.join();
        close_socket(socket_);
    }

private:
    Socket socket_{invalid_socket};
    std::thread worker_;
};

/**
 * A zero app id keeps the shipping preference: Ace of Spades, else Spacewar.
 *
 * An explicit pair exercises the refusal path on any account, since an owner
 * cannot otherwise reach it: pass an application id nobody owns and the
 * fallback Steam should land on.
 */
[[nodiscard]] platform::SteamNetworkingRuntime* start_runtime(platform::SteamNetworkingRuntime& runtime,
                                                              std::uint32_t app_id,
                                                              std::uint32_t fallback_app_id) {
    platform::SteamNetworkingRuntimeConfig config;
    if (app_id != 0U) {
        config.app_id = app_id;
        config.fallback_app_id = fallback_app_id;
    }
    std::string error;
    if (!runtime.start(std::move(config), error)) {
        std::printf("steam runtime failed: %s\n", error.c_str());
        return nullptr;
    }
    const auto relays = runtime.relay_status();
    std::printf("account  : %s (%llu)\nrelays   : %s (%s)\n", runtime.persona_name().c_str(),
                static_cast<unsigned long long>(runtime.steam_id()),
                relays.available ? "ready" : "unavailable", relays.detail.c_str());
    // Statistics and achievements belong to the attached application's schema,
    // so Spacewar must report nothing. Printing both makes the gate checkable
    // rather than assumed.
    std::printf("app      : %u\ntracking : %s\n", runtime.app_id(),
                runtime.tracking_enabled() ? "enabled" : "disabled (attached as Spacewar)");
    // Publishing and clearing proves the rich presence binding resolves and
    // that Steam accepts both keys, which hosting a real match otherwise only
    // exercises on a second machine.
    const auto presence = runtime.publish_presence(
        "Checking the Steam transport", "steam:" + std::to_string(runtime.steam_id()));
    std::printf("presence : %s\n", presence ? "published and cleared" : "refused");
    runtime.clear_presence();
    // A lobby answers asynchronously, so creating one and reading a key back
    // proves the call-result dispatch works. One account is enough.
    const auto connect = "steam:" + std::to_string(runtime.steam_id());
    if (const auto lobby = runtime.create_lobby("Checking the lobby", connect, 2);
        lobby != 0U) {
        const auto stored = runtime.lobby_data(lobby, "connect");
        std::printf("lobby    : %llu, connect key reads %s\n",
                    static_cast<unsigned long long>(lobby),
                    stored == connect ? "back correctly" : "WRONG");
        runtime.leave_lobby(lobby);
    } else {
        std::printf("lobby    : refused or timed out\n");
    }
    return &runtime;
}
} // namespace

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "";
    const std::uint32_t app_id = argc > 3 ? static_cast<std::uint32_t>(std::stoul(argv[3])) : 0U;
    const std::uint32_t fallback_app_id =
        argc > 4 ? static_cast<std::uint32_t>(std::stoul(argv[4])) : 0U;
    platform::SteamNetworkingRuntime runtime;

    if (mode == "host") {
        if (start_runtime(runtime, app_id, fallback_app_id) == nullptr) return 1;
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
        // Two people coordinating across machines spend longer than one person
        // typing both commands: a host that expires early reads on the other
        // side as Steam timing out the connect, which costs an hour to unpick.
        const auto deadline = std::chrono::steady_clock::now() + 30min;
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
        if (start_runtime(runtime, app_id, fallback_app_id) == nullptr) return 1;
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
        if (!sockets_ready()) {
            std::printf("sockets unavailable\n");
            return 1;
        }
        const Socket probe = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (probe == invalid_socket) {
            std::printf("probe socket failed\n");
            return 1;
        }
        sockaddr_in tunnel{};
        tunnel.sin_family = AF_INET;
        tunnel.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        tunnel.sin_port = htons(client.local_port());
        set_receive_timeout(probe, 2s);

        std::vector<double> samples;
        for (int attempt{}; attempt < 20 && client.running(); ++attempt) {
            const auto payload = "battlespades-steam-probe-" + std::to_string(attempt);
            const auto sent = std::chrono::steady_clock::now();
            static_cast<void>(::sendto(probe, payload.data(),
                                       static_cast<BufferLength>(payload.size()), 0,
                                       reinterpret_cast<const sockaddr*>(&tunnel), sizeof(tunnel)));
            std::vector<char> buffer(2048U);
            const auto bytes =
                ::recv(probe, buffer.data(), static_cast<BufferLength>(buffer.size()), 0);
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
        close_socket(probe);
        client.stop();
        return samples.empty() ? 1 : 0;
    }

    std::printf("usage: aos_steam_p2p_smoke host [local] [app-id] [fallback-app-id]\n"
                "       aos_steam_p2p_smoke join <host-steam-id|local> [app-id] [fallback-app-id]\n");
    return 2;
}
