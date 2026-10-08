#include "battlespades/platform/relay_host_tunnel.hpp"
#include "battlespades/platform/socket_select.hpp"

#include <sodium.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

using namespace std::chrono_literals;

constexpr std::size_t header_bytes{36U};
constexpr std::size_t mac_bytes{crypto_auth_hmacsha256_BYTES};
constexpr std::array<unsigned char, 16U> allocation{
    0x55U, 0x0eU, 0x84U, 0x00U, 0xe2U, 0x9bU, 0x41U, 0xd4U,
    0xa7U, 0x16U, 0x44U, 0x66U, 0x55U, 0x44U, 0x00U, 0x00U};
constexpr std::array<unsigned char, 32U> key{
    0U,  1U,  2U,  3U,  4U,  5U,  6U,  7U,
    8U,  9U,  10U, 11U, 12U, 13U, 14U, 15U,
    16U, 17U, 18U, 19U, 20U, 21U, 22U, 23U,
    24U, 25U, 26U, 27U, 28U, 29U, 30U, 31U};

#if defined(_WIN32)
using Socket = SOCKET;
using SendLength = int;  // Winsock counts bytes in int
constexpr Socket invalid_socket{INVALID_SOCKET};
void close_socket(Socket socket) noexcept {
    if (socket != invalid_socket) static_cast<void>(closesocket(socket));
}
#else
using Socket = int;
using SendLength = std::size_t;
constexpr Socket invalid_socket{-1};
void close_socket(Socket socket) noexcept {
    if (socket != invalid_socket) static_cast<void>(close(socket));
}
#endif

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

void write_u16(std::span<unsigned char> output, std::size_t offset,
               std::uint16_t value) {
    output[offset] = static_cast<unsigned char>(value >> 8U);
    output[offset + 1U] = static_cast<unsigned char>(value);
}

void write_u32(std::span<unsigned char> output, std::size_t offset,
               std::uint32_t value) {
    for (std::size_t index{}; index < 4U; ++index) {
        output[offset + index] =
            static_cast<unsigned char>(value >> ((3U - index) * 8U));
    }
}

void write_u64(std::span<unsigned char> output, std::size_t offset,
               std::uint64_t value) {
    for (std::size_t index{}; index < 8U; ++index) {
        output[offset + index] =
            static_cast<unsigned char>(value >> ((7U - index) * 8U));
    }
}

[[nodiscard]] std::vector<unsigned char> frame(
    std::uint8_t type,
    std::uint64_t sequence,
    std::uint32_t client,
    std::span<const unsigned char> payload = {}) {
    std::vector<unsigned char> output(header_bytes + payload.size() + mac_bytes);
    output[0U] = 'A';
    output[1U] = 'O';
    output[2U] = 'S';
    output[3U] = 'R';
    output[4U] = 1U;
    output[5U] = type;
    std::ranges::copy(allocation, output.begin() + 6);
    write_u64(output, 22U, sequence);
    write_u32(output, 30U, client);
    write_u16(output, 34U, static_cast<std::uint16_t>(payload.size()));
    std::ranges::copy(payload,
                      output.begin() + static_cast<std::ptrdiff_t>(header_bytes));
    static_cast<void>(crypto_auth_hmacsha256(
        output.data() + header_bytes + payload.size(), output.data(),
        static_cast<unsigned long long>(header_bytes + payload.size()), key.data()));
    return output;
}

[[nodiscard]] bool valid_frame(std::span<const unsigned char> input) {
    if (input.size() < header_bytes + mac_bytes || input[0U] != 'A' ||
        input[1U] != 'O' || input[2U] != 'S' || input[3U] != 'R' ||
        input[4U] != 1U ||
        !std::ranges::equal(allocation, input.subspan(6U, 16U))) {
        return false;
    }
    const auto payload = static_cast<std::size_t>(
        (static_cast<std::uint16_t>(input[34U]) << 8U) | input[35U]);
    return input.size() == header_bytes + payload + mac_bytes &&
           crypto_auth_hmacsha256_verify(
               input.data() + header_bytes + payload,
               input.data(), static_cast<unsigned long long>(header_bytes + payload),
               key.data()) == 0;
}

void receive_timeout(Socket socket) {
#if defined(_WIN32)
    const DWORD timeout{5'000U};
    static_cast<void>(setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                                 reinterpret_cast<const char*>(&timeout),
                                 static_cast<int>(sizeof(timeout))));
#else
    const timeval timeout{5, 0};
    static_cast<void>(setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                                 &timeout, sizeof(timeout)));
#endif
}

[[nodiscard]] std::pair<Socket, std::uint16_t> bound_loopback_socket() {
    const auto socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    expect(socket != invalid_socket, "could not create UDP fixture socket");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        close_socket(socket);
        throw std::runtime_error{"could not bind UDP fixture socket"};
    }
    socklen_t length = static_cast<socklen_t>(sizeof(address));
    if (getsockname(socket, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        close_socket(socket);
        throw std::runtime_error{"could not inspect UDP fixture socket"};
    }
    receive_timeout(socket);
    return {socket, ntohs(address.sin_port)};
}

struct LoopbackSocket final {
    Socket socket{invalid_socket};
    std::uint16_t port{};
    LoopbackSocket() {
        const auto bound = bound_loopback_socket();
        socket = bound.first;
        port = bound.second;
    }
    ~LoopbackSocket() { close_socket(socket); }
};

bool readable(Socket socket, std::chrono::milliseconds duration) {
    fd_set sockets;
    FD_ZERO(&sockets);
    battlespades::platform::select_add_socket(socket, sockets);
    const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
    // The field types differ by platform (long on Windows and Linux, int microseconds on macOS).
    timeval timeout{};
    timeout.tv_sec = static_cast<decltype(timeout.tv_sec)>(microseconds / 1'000'000);
    timeout.tv_usec = static_cast<decltype(timeout.tv_usec)>(microseconds % 1'000'000);
    return select(static_cast<int>(socket + 1), &sockets, nullptr, nullptr, &timeout) > 0;
}

struct RelayFixture final {
    LoopbackSocket endpoint;
    std::atomic_bool acknowledge{true};
    std::atomic_uint64_t sequence{};
    std::atomic_uint keepalives{};
    std::mutex mutex;
    sockaddr_in host{};
    std::jthread worker;

    RelayFixture() : worker{[this](std::stop_token stop) {
        std::array<unsigned char, 33'000U> bytes{};
        while (!stop.stop_requested()) {
            if (!readable(endpoint.socket, 25ms)) continue;
            sockaddr_in remote{};
            socklen_t length = static_cast<socklen_t>(sizeof(remote));
            const auto count = recvfrom(endpoint.socket, reinterpret_cast<char*>(bytes.data()),
                static_cast<int>(bytes.size()), 0, reinterpret_cast<sockaddr*>(&remote), &length);
            if (count <= 0 || !valid_frame({bytes.data(), static_cast<std::size_t>(count)})) continue;
            if (bytes[5U] != 1U && bytes[5U] != 5U) continue;
            {
                std::scoped_lock lock{mutex};
                host = remote;
            }
            if (bytes[5U] == 5U) ++keepalives;
            if (acknowledge.load()) static_cast<void>(send(2U, 0U));
        }
    }} {}

    bool send(std::uint8_t type, std::uint32_t client,
              std::span<const unsigned char> payload = {}) {
        std::scoped_lock lock{mutex};
        if (host.sin_port == 0U) return false;
        const auto packet = frame(type, ++sequence, client, payload);
        const auto sent = sendto(endpoint.socket, reinterpret_cast<const char*>(packet.data()),
            static_cast<SendLength>(packet.size()), 0, reinterpret_cast<const sockaddr*>(&host),
            sizeof(host));
        return sent >= 0 && static_cast<std::size_t>(sent) == packet.size();
    }

    battlespades::platform::RelayHostTunnelConfig config(std::uint16_t local_port) const {
        std::array<char, sodium_base64_ENCODED_LEN(key.size(),
            sodium_base64_VARIANT_URLSAFE_NO_PADDING)> encoded{};
        sodium_bin2base64(encoded.data(), encoded.size(), key.data(), key.size(),
                          sodium_base64_VARIANT_URLSAFE_NO_PADDING);
        battlespades::platform::RelayHostTunnelConfig result;
        result.allocation_id = "550e8400-e29b-41d4-a716-446655440000";
        result.relay_host = "127.0.0.1";
        result.relay_port = endpoint.port;
        result.host_key_base64url = encoded.data();
        result.local_server_port = local_port;
        result.keepalive = 1s;
        return result;
    }
};

std::uint16_t receive_local_byte(Socket socket, unsigned char expected) {
    expect(readable(socket, 1500ms), "relay data did not reach the expected local server");
    std::array<unsigned char, 8U> bytes{};
    sockaddr_in peer{};
    socklen_t length = static_cast<socklen_t>(sizeof(peer));
    const auto count = recvfrom(socket, reinterpret_cast<char*>(bytes.data()),
        static_cast<int>(bytes.size()), 0, reinterpret_cast<sockaddr*>(&peer), &length);
    expect(count == 1 && bytes.front() == expected, "unexpected local relay payload");
    return ntohs(peer.sin_port);
}

void silent_relay_loss_expires_readiness_and_restart_rebinds_clients() {
    RelayFixture relay;
    LoopbackSocket first_server;
    LoopbackSocket second_server;
    battlespades::platform::RelayHostTunnel tunnel;
    std::string error;
    expect(tunnel.start(relay.config(first_server.port), error), error.c_str());
    constexpr std::array<unsigned char, 1U> request{42U};
    expect(relay.send(4U, 7U, request), "fixture client packet send failed");
    static_cast<void>(receive_local_byte(first_server.socket, request.front()));
    relay.acknowledge = false;
    const auto deadline = std::chrono::steady_clock::now() + 4500ms;
    // Authenticated but structurally invalid ACKs must not extend liveness.
    while (tunnel.running() && std::chrono::steady_clock::now() < deadline) {
        static_cast<void>(relay.send(2U, 0U, request));
        std::this_thread::sleep_for(50ms);
    }
    expect(!tunnel.running() && !tunnel.ready() && relay.keepalives.load() >= 2U,
           "silent relay loss must retire a formerly ready tunnel despite successful UDP sends");
    expect(tunnel.last_error().find("acknowledging") != std::string::npos,
           "relay liveness loss must provide an actionable error");
    relay.acknowledge = true;
    expect(tunnel.start(relay.config(second_server.port), error), error.c_str());
    expect(tunnel.ready() && tunnel.running() && tunnel.last_error().empty(),
           "restarted tunnel inherited terminal state");
    expect(relay.send(4U, 7U, request), "replacement client packet send failed");
    static_cast<void>(receive_local_byte(second_server.socket, request.front()));
    expect(!readable(first_server.socket, 100ms), "restart retained the previous local server socket");
    tunnel.stop();
    expect(!tunnel.ready() && !tunnel.running(), "stopped tunnel retained readiness");
}

void retired_client_ids_do_not_exhaust_lobby_capacity() {
    RelayFixture relay;
    LoopbackSocket server;
    battlespades::platform::RelayHostTunnel tunnel;
    auto config = relay.config(server.port);
    config.maximum_clients = 2U;
    config.client_idle_timeout = 1s;
    std::string error;
    expect(tunnel.start(config, error), error.c_str());
    constexpr std::array<unsigned char, 1U> first{1U}, second{2U}, third{3U};
    expect(relay.send(4U, 1U, first), "first guest packet send failed");
    const auto first_peer = receive_local_byte(server.socket, first.front());
    expect(relay.send(4U, 2U, second), "second guest packet send failed");
    const auto live_peer = receive_local_byte(server.socket, second.front());
    expect(first_peer != live_peer, "guests must have independent local ENet endpoints");
    std::this_thread::sleep_for(600ms);
    expect(relay.send(4U, 2U, second), "active guest refresh failed");
    expect(receive_local_byte(server.socket, second.front()) == live_peer,
           "active guest changed local peer identity");
    std::this_thread::sleep_for(600ms);
    expect(relay.send(4U, 3U, third), "replacement guest packet send failed");
    static_cast<void>(receive_local_byte(server.socket, third.front()));
    expect(relay.send(4U, 2U, second), "surviving guest packet send failed");
    expect(receive_local_byte(server.socket, second.front()) == live_peer,
           "retiring an idle guest must not disconnect the active guest");
    tunnel.stop();
}

void authenticated_relay_round_trip_reaches_one_local_peer() {
    expect(sodium_init() >= 0, "libsodium did not initialize");
#if defined(_WIN32)
    WSADATA data{};
    expect(WSAStartup(MAKEWORD(2, 2), &data) == 0,
           "Winsock did not initialize");
#endif
    const auto [relay_socket, relay_port] = bound_loopback_socket();
    const auto [server_socket, server_port] = bound_loopback_socket();
    std::mutex mutex;
    std::condition_variable completed;
    bool reply_seen{};
    std::string failure;

    std::jthread echo{[&] {
        std::array<unsigned char, 256U> packet{};
        sockaddr_in peer{};
        socklen_t peer_size = static_cast<socklen_t>(sizeof(peer));
        const auto bytes = recvfrom(server_socket,
                                    reinterpret_cast<char*>(packet.data()),
                                    static_cast<int>(packet.size()), 0,
                                    reinterpret_cast<sockaddr*>(&peer), &peer_size);
        if (bytes != 4) return;
        std::ranges::reverse(packet.begin(), packet.begin() + bytes);
        static_cast<void>(sendto(server_socket,
                                 reinterpret_cast<const char*>(packet.data()), static_cast<SendLength>(bytes), 0,
                                 reinterpret_cast<const sockaddr*>(&peer), peer_size));
    }};

    std::jthread relay{[&] {
        std::array<unsigned char, 33'000U> packet{};
        sockaddr_in host{};
        socklen_t host_size = static_cast<socklen_t>(sizeof(host));
        auto bytes = recvfrom(relay_socket, reinterpret_cast<char*>(packet.data()),
                              static_cast<int>(packet.size()), 0,
                              reinterpret_cast<sockaddr*>(&host), &host_size);
        if (bytes <= 0 || !valid_frame(std::span<const unsigned char>{
                              packet.data(), static_cast<std::size_t>(bytes)}) ||
            packet[5U] != 1U) {
            std::scoped_lock lock{mutex};
            failure = "host HELLO was missing or invalid";
            completed.notify_all();
            return;
        }
        const auto acknowledgement = frame(2U, 1U, 0U);
        static_cast<void>(sendto(relay_socket,
                                 reinterpret_cast<const char*>(acknowledgement.data()),
                                 static_cast<SendLength>(acknowledgement.size()), 0,
                                 reinterpret_cast<const sockaddr*>(&host), host_size));
        constexpr std::array<unsigned char, 4U> request{1U, 2U, 3U, 4U};
        const auto inbound = frame(4U, 2U, 7U, request);
        static_cast<void>(sendto(relay_socket,
                                 reinterpret_cast<const char*>(inbound.data()),
                                 static_cast<SendLength>(inbound.size()), 0,
                                 reinterpret_cast<const sockaddr*>(&host), host_size));

        for (;;) {
            bytes = recvfrom(relay_socket, reinterpret_cast<char*>(packet.data()),
                             static_cast<int>(packet.size()), 0,
                             reinterpret_cast<sockaddr*>(&host), &host_size);
            if (bytes <= 0) break;
            const auto view = std::span<const unsigned char>{
                packet.data(), static_cast<std::size_t>(bytes)};
            if (!valid_frame(view) || packet[5U] != 3U) continue;
            const auto client = (static_cast<std::uint32_t>(packet[30U]) << 24U) |
                                (static_cast<std::uint32_t>(packet[31U]) << 16U) |
                                (static_cast<std::uint32_t>(packet[32U]) << 8U) |
                                packet[33U];
            constexpr std::array<unsigned char, 4U> expected{4U, 3U, 2U, 1U};
            if (client == 7U &&
                std::ranges::equal(expected, view.subspan(header_bytes, expected.size()))) {
                std::scoped_lock lock{mutex};
                reply_seen = true;
                completed.notify_all();
                return;
            }
        }
        std::scoped_lock lock{mutex};
        failure = "host reply did not return through the relay";
        completed.notify_all();
    }};

    std::array<char, sodium_base64_ENCODED_LEN(key.size(),
                                               sodium_base64_VARIANT_URLSAFE_NO_PADDING)> encoded{};
    sodium_bin2base64(encoded.data(), encoded.size(), key.data(), key.size(),
                      sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    battlespades::platform::RelayHostTunnel tunnel;
    battlespades::platform::RelayHostTunnelConfig config;
    config.allocation_id = "550e8400-e29b-41d4-a716-446655440000";
    config.relay_host = "127.0.0.1";
    config.relay_port = relay_port;
    config.host_key_base64url = encoded.data();
    config.local_server_port = server_port;
    config.maximum_clients = 8U;
    config.keepalive = 5s;
    std::string error;
    expect(tunnel.start(std::move(config), error), error.c_str());
    {
        std::unique_lock lock{mutex};
        expect(completed.wait_for(lock, 5s, [&] { return reply_seen || !failure.empty(); }),
               "relay round trip timed out");
        expect(failure.empty() && reply_seen,
               failure.empty() ? "relay reply was not observed" : failure.c_str());
    }
    tunnel.stop();
    close_socket(server_socket);
    close_socket(relay_socket);
#if defined(_WIN32)
    WSACleanup();
#endif
}

} // namespace

int main() {
    try {
        authenticated_relay_round_trip_reaches_one_local_peer();
        silent_relay_loss_expires_readiness_and_restart_rebinds_clients();
        retired_client_ids_do_not_exhaust_lobby_capacity();
        std::cout << "relay host tunnel round trip, liveness, restart and guest retirement passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "relay host tunnel failure: " << error.what() << '\n';
        return 1;
    }
}
