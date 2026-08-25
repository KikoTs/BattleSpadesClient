#include "battlespades/network/revival_social.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace battlespades::network;
using namespace std::chrono_literals;

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

[[nodiscard]] bool is_canonical_v4_uuid(std::string_view value) {
    if (value.size() != 36U || value[8U] != '-' || value[13U] != '-' ||
        value[18U] != '-' || value[23U] != '-' || value[14U] != '4') {
        return false;
    }
    const auto variant = static_cast<char>(std::tolower(
        static_cast<unsigned char>(value[19U])));
    if (variant != '8' && variant != '9' && variant != 'a' && variant != 'b') {
        return false;
    }
    for (std::size_t index{}; index < value.size(); ++index) {
        if (index == 8U || index == 13U || index == 18U || index == 23U) continue;
        if (std::isxdigit(static_cast<unsigned char>(value[index])) == 0) return false;
    }
    return true;
}

template <typename Predicate>
void wait_until(Predicate predicate, const char* message) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return;
        std::this_thread::sleep_for(2ms);
    }
    throw std::runtime_error{message};
}

RevivalSocialResult success(const RevivalSocialRequest& request,
                            std::string_view cursor = "1") {
    return parse_revival_social_response(
        request,
        std::string{"{\"cursor\":\""} + std::string{cursor} +
            "\",\"friends\":[],\"invitations\":[],\"events\":[]}");
}

void parser_is_bounded_and_deduplicated() {
    RevivalSocialRequest request;
    request.generation = 1U;
    const auto parsed = parse_revival_social_response(
        request,
        R"({"cursor":"2","friends":[
          {"legacy_id":"7","nickname":"Alpha","friendship_status":"accepted"},
          {"legacy_id":"7","nickname":"Duplicate"},
          {"legacy_id":"","nickname":"Invalid"},42],
          "invitations":[{"id":"i","lobby_id":"9","lobby_name":"Lobby",
            "inviter":{"legacy_id":"8","nickname":"Beta"}}],
          "lobby":{"id":"9","owner_id":"7","name":"Lobby","max_members":24,
            "members":[{"legacy_id":"7","nickname":"Alpha"},
                       {"legacy_id":"7","nickname":"Duplicate"}]},
          "events":[{"id":"e","type":"lobby.chat","lobby_id":"9",
                     "payload":{"message":"hello"}}]})");
    expect(static_cast<bool>(parsed), "valid social fixture must parse");
    expect(parsed.snapshot.cursor == "2" && parsed.snapshot.friends.size() == 1U,
           "friends must be validated and deduplicated");
    expect(parsed.snapshot.invitations.size() == 1U && parsed.snapshot.lobby.has_value() &&
               parsed.snapshot.lobby->members.size() == 1U,
           "lobby rows must be bounded and deduplicated");
    expect(parsed.snapshot.events.size() == 1U,
           "valid authoritative events must survive parsing");

    const auto malformed = parse_revival_social_response(request, "[");
    expect(!malformed && malformed.error_code == "invalid_response",
           "malformed JSON must fail closed");
}

void priority_actions_overtake_a_blocked_poll() {
    std::mutex mutex;
    std::condition_variable gate;
    bool poll_started{};
    bool release_poll{};
    std::vector<std::string> started;
    RevivalSocialClient client{[&](const RevivalSocialRequest& request, std::stop_token stop) {
        {
            std::unique_lock lock{mutex};
            started.push_back(request.action.empty() ? "sync" : request.action);
            if (request.kind == RevivalSocialRequestKind::sync) {
                poll_started = true;
                gate.notify_all();
                gate.wait(lock, [&] { return release_poll || stop.stop_requested(); });
            }
        }
        return success(request);
    }};
    client.set_available(true);
    client.tick(std::chrono::steady_clock::now());
    {
        std::unique_lock lock{mutex};
        expect(gate.wait_for(lock, 1s, [&] { return poll_started; }),
               "normal poll did not start");
    }

    RevivalSocialRequest start;
    start.generation = 99U;
    start.kind = RevivalSocialRequestKind::lobby_action;
    start.priority = true;
    start.lobby_id = "9";
    start.action = "start";
    expect(client.enqueue(start), "priority action must enqueue");
    wait_until([&] {
        std::scoped_lock lock{mutex};
        return std::ranges::find(started, "start") != started.end();
    }, "priority action waited behind the poll");
    {
        std::scoped_lock lock{mutex};
        release_poll = true;
    }
    gate.notify_all();
}

void polling_coalesces_and_failures_back_off() {
    std::atomic_int sync_calls{};
    RevivalSocialClient client{[&](const RevivalSocialRequest& request, std::stop_token) {
        ++sync_calls;
        RevivalSocialResult failure;
        failure.request = request;
        failure.error_code = "service_unavailable";
        failure.error = "offline";
        return failure;
    }, RevivalSocialClientConfig{64U, 32U, 128U, 10ms, 5ms, 40ms}};
    client.set_available(true);
    const auto now = std::chrono::steady_clock::now();
    client.tick(now);
    client.tick(now);
    wait_until([&] { return sync_calls.load() == 1; }, "coalesced poll did not execute");
    static_cast<void>(client.drain(now));
    const auto first = client.status(now);
    expect(first.retry_after >= 9ms && first.retry_after <= 10ms,
           "first failure must retain the base retry delay");
    client.tick(now + 5ms);
    expect(sync_calls.load() == 1, "poll must not run before its retry deadline");
    client.tick(now + 11ms);
    wait_until([&] { return sync_calls.load() == 2; }, "second poll did not execute");
    static_cast<void>(client.drain(now + 11ms));
    expect(client.status(now + 11ms).retry_after >= 19ms,
           "retry delay must grow exponentially");
}

void shutdown_cancels_a_hung_request() {
    std::atomic_bool started{};
    RevivalSocialClient client{[&](const RevivalSocialRequest& request, std::stop_token stop) {
        started = true;
        while (!stop.stop_requested()) std::this_thread::sleep_for(1ms);
        RevivalSocialResult result;
        result.request = request;
        result.error_code = "cancelled";
        result.error = "cancelled";
        return result;
    }};
    RevivalSocialRequest request;
    request.generation = 1U;
    request.kind = RevivalSocialRequestKind::find_friends;
    expect(client.enqueue(request), "hung fixture must enqueue");
    wait_until([&] { return started.load(); }, "hung fixture did not start");
    const auto before = std::chrono::steady_clock::now();
    client.shutdown(0ms);
    const auto elapsed = std::chrono::steady_clock::now() - before;
    expect(elapsed < 100ms, "shutdown must signal cancellation without waiting for timeout");
}

void a_successful_leave_clears_the_cached_lobby() {
    RevivalSocialRequest join;
    join.generation = 1U;
    join.kind = RevivalSocialRequestKind::lobby_action;
    join.priority = true;
    join.lobby_id = "9";
    join.action = "join";
    RevivalSocialResult initial;
    initial.request = join;
    RevivalSocialLobby lobby;
    lobby.id = "9";
    lobby.owner_id = "7";
    lobby.name = "Lobby";
    initial.snapshot.lobby = std::move(lobby);

    RevivalSocialClient seeded{[initial](const RevivalSocialRequest& request, std::stop_token) {
        auto result = initial;
        result.request = request;
        if (request.action == "leave") result.snapshot.lobby.reset();
        return result;
    }};
    expect(seeded.enqueue(join), "join fixture must enqueue");
    wait_until([&] {
        static_cast<void>(seeded.drain(std::chrono::steady_clock::now()));
        return seeded.snapshot().lobby.has_value();
    },
               "join fixture did not finish");
    expect(seeded.snapshot().lobby.has_value(), "join must seed the cached lobby");
    auto leave = join;
    leave.generation = 2U;
    leave.action = "leave";
    expect(seeded.enqueue(leave), "leave fixture must enqueue");
    wait_until([&] {
        static_cast<void>(seeded.drain(std::chrono::steady_clock::now()));
        return !seeded.snapshot().lobby.has_value();
    },
               "leave fixture did not finish");
    expect(!seeded.snapshot().lobby.has_value(),
           "a successful leave must clear the cached lobby immediately");
}

void client_instances_are_unique_and_events_are_exactly_once() {
    std::mutex mutex;
    std::vector<std::string> instance_ids;
    auto executor = [&](const RevivalSocialRequest& request, std::stop_token) {
        {
            std::scoped_lock lock{mutex};
            instance_ids.push_back(request.client_instance_id);
        }
        return parse_revival_social_response(
            request,
            R"({"cursor":"2","friends":[],"invitations":[],"events":[{"id":"once","type":"lobby.started"}]})");
    };
    RevivalSocialClient first{executor};
    RevivalSocialClient second{executor};
    const auto now = std::chrono::steady_clock::now();
    first.set_available(true);
    second.set_available(true);
    first.tick(now);
    second.tick(now);
    wait_until([&] {
        std::scoped_lock lock{mutex};
        return instance_ids.size() >= 2U;
    }, "presence clients did not poll");
    {
        std::scoped_lock lock{mutex};
        expect(!instance_ids[0U].empty() && !instance_ids[1U].empty() &&
                   instance_ids[0U] != instance_ids[1U],
               "each process-level social client needs an independent presence lease");
        expect(is_canonical_v4_uuid(instance_ids[0U]) &&
                   is_canonical_v4_uuid(instance_ids[1U]),
               "AoSPlay presence leases must be canonical version-4 UUIDs");
    }
    auto first_delivery = first.drain(now);
    expect(first_delivery.size() == 1U &&
               first_delivery.front().snapshot.events.size() == 1U,
           "the first authoritative event must be delivered");
    first.tick(now + 4s);
    wait_until([&] {
        std::scoped_lock lock{mutex};
        return instance_ids.size() >= 3U;
    }, "second social poll did not run");
    auto duplicate = first.drain(now + 4s);
    expect(duplicate.size() == 1U && duplicate.front().snapshot.events.empty(),
           "a repeated authoritative event must be suppressed across polls");
}

void transport_failures_enter_reconnecting_without_stopping_retries() {
    std::atomic_int attempts{};
    RevivalSocialClient client{[&](const RevivalSocialRequest& request, std::stop_token) {
        ++attempts;
        RevivalSocialResult failure;
        failure.request = request;
        failure.error_code = "network_error";
        failure.error = "offline";
        return failure;
    }, RevivalSocialClientConfig{64U, 32U, 128U, 10ms, 5ms, 40ms}};
    const auto now = std::chrono::steady_clock::now();
    client.set_available(true);
    client.tick(now);
    wait_until([&] { return attempts.load() == 1; }, "network failure did not execute");
    static_cast<void>(client.drain(now));
    expect(!client.status(now).available,
           "a failed sync must expose reconnecting state to the menu");
    client.tick(now + 11ms);
    wait_until([&] { return attempts.load() == 2; },
               "reconnecting state must continue its bounded retry loop");
}

} // namespace

int main() {
    try {
        parser_is_bounded_and_deduplicated();
        priority_actions_overtake_a_blocked_poll();
        polling_coalesces_and_failures_back_off();
        shutdown_cancels_a_hung_request();
        a_successful_leave_clears_the_cached_lobby();
        client_instances_are_unique_and_events_are_exactly_once();
        transport_failures_enter_reconnecting_without_stopping_retries();
        std::cout << "revival social tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
