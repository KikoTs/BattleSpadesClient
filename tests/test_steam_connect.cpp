#include "battlespades/platform/native_steam_client.hpp"
#include "battlespades/platform/steam_connect.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {

using battlespades::platform::endpoint_connect_string;
using battlespades::platform::parse_steam_host_address;
using battlespades::platform::parse_steam_join_target;
using battlespades::platform::steam_host_connect_string;
using battlespades::platform::SteamJoinDeduplicator;
using battlespades::platform::SteamJoinTarget;
using battlespades::platform::SteamJoinTargetKind;

void expect(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error{std::string{message}};
}

void connect_strings_round_trip() {
    const auto published = steam_host_connect_string(76561198000000001ULL);
    expect(published == "+connect steam:76561198000000001",
           "presence connect must be a whole switch Steam can append to a command line");
    const auto parsed = parse_steam_join_target(published);
    expect(parsed.has_value() && parsed->kind == SteamJoinTargetKind::steam_host &&
               parsed->steam_id == 76561198000000001ULL,
           "our own connect value must parse back to the host");
    expect(parse_steam_host_address(published) == 76561198000000001ULL,
           "the browser's host parser must accept the switch form");
    expect(parse_steam_host_address("steam:76561198000000001") == 76561198000000001ULL,
           "the bare form older builds published must still parse");
}

void join_targets_cover_every_steam_form() {
    const auto endpoint = parse_steam_join_target("+connect aos.example.net:32887");
    expect(endpoint.has_value() && endpoint->kind == SteamJoinTargetKind::endpoint &&
               endpoint->endpoint == "aos.example.net:32887",
           "+connect host:port must name a dedicated server");
    const auto lobby = parse_steam_join_target("+connect_lobby 109775241021923456");
    expect(lobby.has_value() && lobby->kind == SteamJoinTargetKind::lobby &&
               lobby->steam_id == 109775241021923456ULL,
           "+connect_lobby must name the lobby");
    const auto launch = parse_steam_join_target("  -windowed +connect steam:5  ");
    expect(launch.has_value() && launch->steam_id == 5U,
           "a launch command line with other switches still yields the join");
    expect(!parse_steam_join_target("").has_value(), "empty is no join");
    expect(!parse_steam_join_target("+connect").has_value(), "a switch without value fails");
    expect(!parse_steam_join_target("+connect steam:").has_value(), "no id fails");
    expect(!parse_steam_join_target("+connect steam:12ab").has_value(), "bad id fails");
    expect(!parse_steam_join_target("+connect evil;host:1").has_value(),
           "a host outside the DNS alphabet is refused");
    expect(!parse_steam_join_target("+connect host:99999").has_value(),
           "an out-of-range port is refused");
    expect(!parse_steam_join_target("a b c").has_value(),
           "loose tokens are some other launcher's arguments");
}

void loopback_endpoints_are_never_advertised() {
    expect(!endpoint_connect_string("127.0.0.1", 27015U).has_value(),
           "the local tunnel/server endpoint means nothing to a friend");
    expect(!endpoint_connect_string("localhost", 27015U).has_value(), "localhost is loopback");
    expect(!endpoint_connect_string("0.0.0.0", 27015U).has_value(), "unspecified is not dialable");
    expect(!endpoint_connect_string("1.2.3.4", 0U).has_value(), "a zero port is not dialable");
    expect(endpoint_connect_string("1.2.3.4", 32887U) == "+connect 1.2.3.4:32887",
           "a public server is advertised as +connect host:port");
}

void duplicates_from_both_steam_processes_are_dropped() {
    SteamJoinDeduplicator deduplicator{std::chrono::milliseconds{5'000}};
    const SteamJoinTarget target{SteamJoinTargetKind::steam_host, 9U, {}};
    const auto now = std::chrono::steady_clock::now();
    expect(deduplicator.accept(target, now), "the first request is accepted");
    expect(!deduplicator.accept(target, now + std::chrono::milliseconds{100}),
           "the bridge's copy of the same click is dropped");
    expect(deduplicator.accept(SteamJoinTarget{SteamJoinTargetKind::steam_host, 10U, {}},
                               now + std::chrono::milliseconds{200}),
           "a different target is a new request");
    expect(deduplicator.accept(SteamJoinTarget{SteamJoinTargetKind::steam_host, 10U, {}},
                               now + std::chrono::seconds{6}),
           "the same target later is a deliberate retry");
}

#if defined(_WIN32) && defined(AOS_FAKE_STEAM_BRIDGE)
using battlespades::platform::NativeSteamClient;
using battlespades::platform::NativeSteamClientConfig;
using battlespades::platform::NativeSteamState;

NativeSteamClientConfig fake_config() {
    NativeSteamClientConfig config;
    config.bridge_executable = AOS_FAKE_STEAM_BRIDGE;
    // Any existing file satisfies the imported-runtime check.
    config.steam_api_library = AOS_FAKE_STEAM_BRIDGE;
    config.startup_timeout = std::chrono::milliseconds{8'000};
    config.retry_interval = std::chrono::milliseconds{200};
    return config;
}

void set_mode(const char* mode) {
    static_cast<void>(_putenv_s("AOS_FAKE_BRIDGE_MODE", mode));
}

NativeSteamState settle(NativeSteamClient& client, std::chrono::milliseconds limit) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    auto state = client.poll();
    while (state == NativeSteamState::starting && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
        state = client.poll();
    }
    return state;
}

void bridge_handshake_ignores_steam_dll_noise_and_delivers_joins() {
    set_mode("junk");
    NativeSteamClient client{fake_config()};
    expect(client.begin_start(), "the fake bridge must launch");
    expect(client.state() == NativeSteamState::starting,
           "begin_start must not wait for Steam");
    expect(settle(client, std::chrono::seconds{5}) == NativeSteamState::ready,
           "breakpad lines before READY must not fail the handshake");
    expect(client.identity() != nullptr && client.identity()->persona_name == "Spade",
           "the identity must come from READY");
    const auto ticket = client.session_ticket();
    expect(ticket.has_value() && ticket->handle == 7U,
           "noise between a command and its answer must be skipped");
    static_cast<void>(client.poll());
    const auto events = client.take_join_events();
    expect(events.size() == 2U && events[0].connect == "+connect steam:76561198000000002" &&
               events[0].friend_id == 42U && events[1].lobby_id == 109775241021923456ULL,
           "join and lobby events must reach the frontend in order");
    client.stop();
    expect(client.state() == NativeSteamState::unavailable, "stop tears the bridge down");
}

void slow_steam_is_waited_for_not_abandoned() {
    set_mode("slow");
    NativeSteamClient client{fake_config()};
    expect(client.begin_start(), "the fake bridge must launch");
    expect(settle(client, std::chrono::seconds{6}) == NativeSteamState::ready,
           "a Steam answering after 2.5 s (over the old 2 s bound) must still attach");
    client.stop();
}

void transient_failures_retry_and_permanent_ones_stop() {
    set_mode("fail");
    NativeSteamClient client{fake_config()};
    expect(client.begin_start(), "the fake bridge must launch");
    expect(settle(client, std::chrono::seconds{5}) == NativeSteamState::retrying,
           "Steam not running is transient and schedules another attempt");
    expect(client.last_error().find("not running") != std::string_view::npos,
           "the bridge's reason must reach the identity screen");
    // Steam comes up: the next scheduled attempt succeeds without a restart.
    set_mode("");
    std::this_thread::sleep_for(std::chrono::milliseconds{250});
    static_cast<void>(client.poll());
    expect(settle(client, std::chrono::seconds{5}) == NativeSteamState::ready,
           "the retry must attach once Steam is available");
    client.stop();

    set_mode("permanent");
    NativeSteamClient broken{fake_config()};
    expect(broken.begin_start(), "the fake bridge must launch");
    expect(settle(broken, std::chrono::seconds{5}) == NativeSteamState::unavailable,
           "an unusable runtime must not be retried forever");
    set_mode("");
}
#endif

} // namespace

int main() {
    try {
        connect_strings_round_trip();
        join_targets_cover_every_steam_form();
        loopback_endpoints_are_never_advertised();
        duplicates_from_both_steam_processes_are_dropped();
#if defined(_WIN32) && defined(AOS_FAKE_STEAM_BRIDGE)
        bridge_handshake_ignores_steam_dll_noise_and_delivers_joins();
        slow_steam_is_waited_for_not_abandoned();
        transient_failures_retry_and_permanent_ones_stop();
#endif
        std::cout << "steam connect tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "steam connect tests failed: " << error.what() << '\n';
        return 1;
    }
}
