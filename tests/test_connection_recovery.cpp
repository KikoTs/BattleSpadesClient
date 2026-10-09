#include "battlespades/network/live_protocol168_connection.hpp"
#include "battlespades/network/protocol168_clock.hpp"

#include <enet/enet.h>

#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace battlespades::network;

void expect(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error{std::string{message}};
}

// Exercise actual ENet event delivery. Loading clients receive no MapEnded(52)
// before the server closes them during rotation; testing a constructed status
// alone would miss the worker dropping event.data, which caused this failure.
void disconnect_during_handshake(std::uint32_t reason) {
    ENetAddress address{};
    expect(enet_address_set_host_ip(&address, "127.0.0.1") == 0, "loopback address");
    const std::unique_ptr<ENetHost, decltype(&enet_host_destroy)> server{
        enet_host_create(&address, 1U, 1U, 0U, 0U), &enet_host_destroy};
    expect(server != nullptr, "create loopback ENet server");
    expect(enet_host_compress_with_range_coder(server.get()) == 0, "enable retail compression");
    expect(enet_socket_get_address(server->socket, &address) == 0 && address.port != 0U,
           "resolve ephemeral server port");

    LiveProtocol168Connection connection;
    Protocol168SessionConfig session;
    session.auto_join = false;
    expect(connection.start({"127.0.0.1", address.port, 5'000U}, session), "start live client");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    bool saw_connect{};
    LiveProtocol168Status status;
    do {
        ENetEvent event{};
        const auto serviced = enet_host_service(server.get(), &event, 10U);
        expect(serviced >= 0, "service loopback ENet server");
        if (serviced > 0) {
            if (event.type == ENET_EVENT_TYPE_CONNECT) {
                expect(event.data == 168U, "native transport must retain retail protocol version");
                saw_connect = true;
                enet_peer_disconnect(event.peer, reason);
                enet_host_flush(server.get());
            } else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                enet_packet_destroy(event.packet);
            }
        }
        status = connection.status();
    } while (status.phase != LiveProtocol168Phase::disconnected &&
             status.phase != LiveProtocol168Phase::failed &&
             std::chrono::steady_clock::now() < deadline);

    expect(saw_connect, "server must accept the actual ENet connection");
    expect(status.phase == LiveProtocol168Phase::disconnected, "receive explicit server disconnect");
    expect(status.disconnect_reason == reason, "preserve server disconnect reason through the worker");
    expect(status.received_datagrams == 0U && connection.take_bootstrap() == nullptr,
           "rotation fixture must not send MapEnded or finish a map bootstrap");
    expect(status.error.find("reason " + std::to_string(reason)) != std::string::npos,
           "connection diagnostics must include the disconnect reason");
    expect(protocol168_should_reconnect_after_map_change(status, false) == (reason == 18U),
           "only match-ended may start rotation recovery without an earlier MapEnded");
    expect(protocol168_should_reconnect_after_map_change(status, true) ==
               (reason == 18U || reason == 0U),
           "an earlier MapEnded must not override an explicit server rejection");
    expect(!connection.start({"127.0.0.1", 0U, 5'000U}, session),
           "an invalid replacement endpoint must fail synchronously");
    status = connection.status();
    expect(status.phase == LiveProtocol168Phase::failed && !status.disconnect_reason &&
               status.received_datagrams == 0U && status.sent_datagrams == 0U &&
               status.queued_inbound == 0U && status.queued_outbound == 0U,
           "failed replacement must not retain the prior server's disconnect or packet state");
}

void recovery_requires_map_change_evidence() {
    LiveProtocol168Status status;
    status.phase = LiveProtocol168Phase::failed;
    status.error = "Protocol 168 handshake timed out";
    expect(!protocol168_should_reconnect_after_map_change(status, false),
           "an ordinary failed join must not start rotation recovery");
    expect(protocol168_should_reconnect_after_map_change(status, true),
           "a slow rotation may retry inside the existing bounded recovery window");
    status.phase = LiveProtocol168Phase::ready;
    expect(!protocol168_should_reconnect_after_map_change(status, true),
           "a healthy connection must not be replaced");
}

void cancellation_signal_is_nonblocking_and_owner_can_restart() {
    ENetAddress address{};
    expect(enet_address_set_host_ip(&address, "127.0.0.1") == 0, "cancellation loopback address");
    const std::unique_ptr<ENetHost, decltype(&enet_host_destroy)> server{
        enet_host_create(&address, 1U, 1U, 0U, 0U), &enet_host_destroy};
    expect(server != nullptr && enet_socket_get_address(server->socket, &address) == 0 &&
               address.port != 0U,
           "bind cancellation fixture so startup cannot race an unrelated port refusal");
    LiveProtocol168Connection connection;
    Protocol168SessionConfig session;
    for (unsigned attempt{}; attempt < 8U; ++attempt) {
        expect(connection.start({"127.0.0.1", address.port, 5'000U}, session), "start cancellation fixture");
        const auto before = std::chrono::steady_clock::now();
        connection.request_stop();
        expect(std::chrono::steady_clock::now() - before < std::chrono::milliseconds{100},
               "cancellation signal must not wait for the transport worker");
        connection.stop();
        expect(connection.status().phase == LiveProtocol168Phase::stopped,
               "canceled startup must settle as stopped across repeated lifecycle reuse");
    }
}

void live_queue_bounds_memory_without_losing_order_or_capacity() {
    using namespace battlespades::network::detail;
    Protocol168PacketQueue queue{live_inbound_packet_limit, live_inbound_byte_limit};
    constexpr auto packet_bytes = 1U << 20U;
    constexpr auto burst = live_inbound_byte_limit / packet_bytes;
    for (std::size_t index{}; index < burst; ++index) {
        expect(queue.push(std::vector<std::byte>(packet_bytes, static_cast<std::byte>(index))),
               "legitimate bounded live burst must fit");
    }
    expect(queue.size() == burst && queue.retained_bytes() == live_inbound_byte_limit &&
               !queue.push(std::vector<std::byte>{std::byte{99U}}),
           "large decoded packets must reach the byte limit before the packet-count limit");
    auto first = queue.take(1U);
    expect(first.size() == 1U && first.front().front() == std::byte{} &&
               queue.retained_bytes() == live_inbound_byte_limit - packet_bytes,
           "draining a packet must reclaim exactly its retained allocation");
    expect(queue.push(std::vector<std::byte>(packet_bytes, std::byte{99U})),
           "reclaimed capacity must admit a replacement after a rejected push");
    const auto remaining = queue.take(live_inbound_packet_limit);
    expect(remaining.size() == burst && queue.size() == 0U && queue.retained_bytes() == 0U,
           "full drain must reset memory accounting");
    for (std::size_t index{}; index + 1U < remaining.size(); ++index) {
        expect(remaining[index].front() == static_cast<std::byte>(index + 1U),
               "overflow must not discard or reorder authoritative packets");
    }
    expect(remaining.back().front() == std::byte{99U}, "replacement must follow retained packets");

    for (std::size_t index{}; index < live_inbound_packet_limit; ++index)
        expect(queue.push(std::vector<std::byte>{std::byte{1U}}), "small packets must reach count limit");
    expect(!queue.push(std::vector<std::byte>{std::byte{2U}}),
           "byte budgeting must retain the existing packet-count bound");
    queue.clear();
    expect(queue.retained_bytes() == 0U && queue.size() == 0U &&
               queue.push(std::vector<std::byte>(packet_bytes, std::byte{})),
           "restart/reset must not inherit consumed capacity");

    Protocol168PacketQueue capacity_fixture{2U, 64U};
    std::vector<std::byte> reserved;
    reserved.reserve(65U);
    reserved.push_back(std::byte{1U});
    expect(!capacity_fixture.push(std::move(reserved)) && capacity_fixture.retained_bytes() == 0U,
           "unused decoded allocation must count toward the retained memory bound");
    expect(capacity_fixture.push(std::vector<std::byte>(64U, std::byte{3U})), "fill move fixture");
    auto moved = std::move(capacity_fixture);
    expect(moved.retained_bytes() == 64U && capacity_fixture.retained_bytes() == 0U &&
               capacity_fixture.push(std::vector<std::byte>{std::byte{4U}}),
           "moving a queue must transfer memory ownership and leave the source reusable");
}
[[nodiscard]] std::unique_ptr<ENetHost, decltype(&enet_host_destroy)>
loopback_server(ENetAddress& address) {
    expect(enet_address_set_host_ip(&address, "127.0.0.1") == 0, "timeout loopback address");
    std::unique_ptr<ENetHost, decltype(&enet_host_destroy)> server{
        enet_host_create(&address, 1U, 1U, 0U, 0U), &enet_host_destroy};
    expect(server != nullptr && enet_host_compress_with_range_coder(server.get()) == 0 &&
               enet_socket_get_address(server->socket, &address) == 0 && address.port != 0U,
           "bind timeout fixture");
    return server;
}

void live_ticket_preserves_explicit_extension_policy() {
    for (const bool negotiate : {false, true}) {
        ENetAddress address{};
        const auto server = loopback_server(address);
        LiveProtocol168Connection connection;
        Protocol168SessionConfig session;
        session.auto_join = false;
        session.negotiate_flight_profile = negotiate;
        session.steam_ticket = {std::byte{'a'}, std::byte{'1'}, std::byte{'B'}};
        EnetProtocol168Config transport{"127.0.0.1", address.port, 5'000U};
        transport.protocol = GameProtocol::retail168;
        expect(connection.start(transport, session), "start extension-policy client");

        std::vector<std::byte> expected{
            std::byte{0x30U}, std::byte{105U}, std::byte{3U}, std::byte{},
            std::byte{}, std::byte{}, std::byte{'a'}, std::byte{'1'}, std::byte{'B'}};
        if (negotiate) {
            expected.insert(expected.end(), {std::byte{'B'}, std::byte{'S'},
                                            std::byte{'C'}, std::byte{'F'}, std::byte{2U}});
        }
        bool received{};
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        while (!received && std::chrono::steady_clock::now() < deadline) {
            ENetEvent event{};
            expect(enet_host_service(server.get(), &event, 5U) >= 0,
                   "service extension-policy server");
            if (event.type == ENET_EVENT_TYPE_CONNECT) {
                expect(event.data == 168U, "extension policy keeps the retail ENet version");
            } else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                const std::unique_ptr<ENetPacket, decltype(&enet_packet_destroy)> packet{
                    event.packet, &enet_packet_destroy};
                const std::vector<std::byte> actual{
                    reinterpret_cast<const std::byte*>(packet->data),
                    reinterpret_cast<const std::byte*>(packet->data + packet->dataLength)};
                expect(event.channelID == 0U &&
                           (packet->flags & ENET_PACKET_FLAG_RELIABLE) != 0U,
                       "retail authentication stays reliable on channel zero");
                expect(actual == expected,
                       "live transport must preserve exact stock or explicitly opted-in BSCF ticket bytes");
                received = true;
            }
        }
        expect(received, "loopback server must receive the authentication packet");
        connection.stop();
    }
}

void unanswered_connect_times_out_after_five_seconds_as_error_timeout() {
    expect(EnetProtocol168Config{}.connect_timeout_ms == 5'000U &&
               EnetProtocol168Config{}.timeout_ms == 30'000U,
           "retail NetworkClient.timeout = 5, loadingMenu no-progress = 30 s");
    // A bound but never-serviced host swallows the CONNECT command.
    ENetAddress address{};
    const auto silent = loopback_server(address);
    expect(silent != nullptr, "silent host stays bound for the whole test");
    LiveProtocol168Connection connection;
    Protocol168SessionConfig session;
    session.auto_join = false;
    EnetProtocol168Config transport{"127.0.0.1", address.port, 5'000U};
    transport.connect_timeout_ms = 300U;
    const auto started = std::chrono::steady_clock::now();
    expect(connection.start(transport, session), "start unanswered connect");
    LiveProtocol168Status status;
    do {
        status = connection.status();
    } while (status.phase != LiveProtocol168Phase::failed &&
             std::chrono::steady_clock::now() - started < std::chrono::seconds{4});
    const auto elapsed = std::chrono::steady_clock::now() - started;
    expect(status.phase == LiveProtocol168Phase::failed && status.disconnect_reason == 11U,
           "an unanswered connect reports DISCONNECT.ERROR_TIMEOUT (11)");
    expect(elapsed >= std::chrono::milliseconds{250} && elapsed < std::chrono::seconds{3},
           "the connect deadline, not the 5 s no-progress timer, fires");
    connection.stop();
}

void connected_handshake_without_progress_times_out() {
    ENetAddress address{};
    const auto server = loopback_server(address);
    LiveProtocol168Connection connection;
    Protocol168SessionConfig session;
    session.auto_join = false;
    EnetProtocol168Config transport{"127.0.0.1", address.port, 500U};
    transport.connect_timeout_ms = 2'000U;
    expect(connection.start(transport, session), "start silent handshake");
    const auto started = std::chrono::steady_clock::now();
    LiveProtocol168Status status;
    bool saw_connect{};
    do {
        ENetEvent event{};
        if (enet_host_service(server.get(), &event, 5U) > 0) {
            if (event.type == ENET_EVENT_TYPE_CONNECT) saw_connect = true;
            if (event.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(event.packet);
        }
        status = connection.status();
    } while (status.phase != LiveProtocol168Phase::failed &&
             std::chrono::steady_clock::now() - started < std::chrono::seconds{4});
    expect(saw_connect, "the server accepted the ENet connection");
    expect(status.phase == LiveProtocol168Phase::failed && !status.disconnect_reason &&
               status.failure_key == "ERROR_TIMEOUT",
           "no handshake progress within the timer fails as ERROR_TIMEOUT");
    connection.stop();
}

void only_clock_sync_and_client_data_are_unsequenced() {
    for (unsigned id{}; id < 256U; ++id) {
        expect(protocol168_client_packet_unsequenced(static_cast<std::uint8_t>(id)) ==
                   (id == 0U || id == 4U),
               "send_packet(unreliable=True) only at send_clock_sync and send_client_data");
    }
}

void clock_sync_uses_retail_half_rtt_lead_and_dead_band() {
    expect(clock_sync_max_ping_ms == 10'000 && max_clock_sync_difference == 10,
           "MAX_PING = 10000 (initgameScene), MAX_CLOCK_SYNC_DIFFERENCE = 10");
    expect(clock_sync_client_time(123'456U) == 3'456, "client_time = time_get() % MAX_PING");
    expect(clock_sync_ping_ms(123'556U, 3'456) == 100, "ping is the echo round trip");
    expect(clock_sync_ping_ms(130'050U, 9'950) == 100, "a MAX_PING wrap adds MAX_PING back");
    // 200 ms RTT: latency 0.1 s -> int(6.0) = 6 loops of lead.
    expect(clock_sync_relabel(1'000, 1'000, 200) == std::nullopt,
           "a 6-loop error sits inside the +/-10 dead band");
    expect(clock_sync_relabel(1'000, 1'010, 200) == 1'016,
           "16 loops off adopts server loop + half-RTT lead");
    expect(clock_sync_relabel(1'100, 1'000, 0) == 1'000,
           "retail also relabels backwards (the caller keeps labels monotonic)");
    expect(clock_sync_relabel(1'000, 990, 0) == std::nullopt,
           "exactly 10 loops is still inside the dead band");
}

void watchdog_warns_after_five_seconds_and_times_out_at_retail_limits() {
    RetailConnectionWatchdog never;
    RetailConnectionWatchdog::Verdict verdict;
    for (int loop{}; loop < 300; ++loop) verdict = never.tick(false);
    expect(!verdict.seconds_left && !verdict.timed_out, "5 s of silence is still quiet");
    verdict = never.tick(false);
    expect(verdict.seconds_left.has_value() &&
               std::abs(*verdict.seconds_left - (20.0 - 301.0 / 60.0)) < 1.0e-9,
           "never answered: CONNECTION_PROBLEMS counts down from 20 s");
    for (int loop{301}; loop < 1200; ++loop) verdict = never.tick(false);
    expect(verdict.seconds_left.has_value() && !verdict.timed_out, "20.0 s exactly still warns");
    verdict = never.tick(false);
    expect(verdict.timed_out, "past SERVER_TIMEOUT_BEFORE_FIRST_RESPONSE disconnects");

    for (const bool ugc : {false, true}) {
        RetailConnectionWatchdog answered;
        answered.note_response();
        for (int loop{}; loop < 1802; ++loop) verdict = answered.tick(ugc);
        expect(verdict.timed_out == !ugc, "30 s after a response; UGC waits 60 s");
        answered.note_response();
        verdict = answered.tick(ugc);
        expect(!verdict.seconds_left && !verdict.timed_out, "a response clears the warning");
    }
}
} // namespace

int main() {
    if (enet_initialize() != 0) return 1;
    struct EnetGuard final {
        ~EnetGuard() { enet_deinitialize(); }
    } guard;
    try {
        for (const auto reason : {18U, 0U, 1U, 2U, 4U, 13U}) {
            disconnect_during_handshake(reason);
        }
        recovery_requires_map_change_evidence();
        cancellation_signal_is_nonblocking_and_owner_can_restart();
        live_queue_bounds_memory_without_losing_order_or_capacity();
        live_ticket_preserves_explicit_extension_policy();
        unanswered_connect_times_out_after_five_seconds_as_error_timeout();
        connected_handshake_without_progress_times_out();
        only_clock_sync_and_client_data_are_unsequenced();
        clock_sync_uses_retail_half_rtt_lead_and_dead_band();
        watchdog_warns_after_five_seconds_and_times_out_at_retail_limits();
        std::cout << "connection recovery tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
