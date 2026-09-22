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

#include <nlohmann/json.hpp>

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

void default_json_members_are_objects() {
    // Brace-initializing json from json::object() yields [{}] on clang, and
    // payload["key"] on that array threw when a macOS host published a match.
    expect(RevivalSocialRequest{}.payload.is_object(), "request payload must default to an object");
    expect(RevivalSocialLobbyMember{}.member_data.is_object(), "member data must default to an object");
    expect(RevivalSocialLobby{}.settings.is_object(), "lobby settings must default to an object");
    expect(RevivalSocialEvent{}.payload.is_object(), "event payload must default to an object");
    RevivalSocialRequest in_game;
    in_game.payload["start_id"] = "start";
    expect(in_game.payload.dump() == R"({"start_id":"start"})", "request payload must accept object keys");
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

void stale_poll_cannot_undo_a_priority_mutation() {
    std::mutex mutex;
    std::condition_variable gate;
    bool stale_poll_started{};
    bool release_stale_poll{};
    std::atomic_int fresh_polls{};

    RevivalSocialClient client{[&](const RevivalSocialRequest& request,
                                    std::stop_token stop) {
        if (request.kind == RevivalSocialRequestKind::sync) {
            bool stale{};
            {
                std::unique_lock lock{mutex};
                stale = !stale_poll_started;
                if (stale) {
                    stale_poll_started = true;
                    gate.notify_all();
                    gate.wait(lock, [&] {
                        return release_stale_poll || stop.stop_requested();
                    });
                }
            }
            if (stale) {
                return parse_revival_social_response(
                    request,
                    R"({"cursor":"1","friends":[],"invitations":[],"lobby":null,"events":[]})");
            }
            ++fresh_polls;
            return parse_revival_social_response(
                request,
                R"({"cursor":"2","friends":[],"invitations":[],"lobby":{"id":"9","owner_id":"7","name":"Current","revision":"2","members":[]},"events":[]})");
        }
        return parse_revival_social_response(
            request,
            R"({"lobby":{"id":"9","owner_id":"7","name":"Current","revision":"2","members":[]}})");
    }, RevivalSocialClientConfig{64U, 32U, 128U, 5ms, 5ms, 20ms}};

    const auto now = std::chrono::steady_clock::now();
    client.set_available(true);
    client.tick(now);
    {
        std::unique_lock lock{mutex};
        expect(gate.wait_for(lock, 1s, [&] { return stale_poll_started; }),
               "stale poll fixture did not start");
    }

    RevivalSocialRequest join;
    join.generation = 99U;
    join.kind = RevivalSocialRequestKind::lobby_action;
    join.priority = true;
    join.lobby_id = "9";
    join.action = "join";
    expect(client.enqueue(join), "priority lobby mutation must enqueue");
    wait_until([&] {
        static_cast<void>(client.drain(now));
        return client.snapshot().lobby.has_value();
    }, "priority lobby mutation did not publish its snapshot");

    {
        std::scoped_lock lock{mutex};
        release_stale_poll = true;
    }
    gate.notify_all();
    wait_until([&] {
        static_cast<void>(client.drain(now));
        return !client.status(now).normal_active;
    }, "stale poll did not complete");
    expect(client.snapshot().lobby.has_value() &&
               client.snapshot().lobby->revision == "2",
           "a pre-mutation poll must not erase the newer lobby snapshot");

    client.tick(now + 1ms);
    wait_until([&] {
        static_cast<void>(client.drain(now + 1ms));
        return fresh_polls.load() == 1;
    }, "discarding a stale poll must schedule an immediate fresh read");
    expect(client.snapshot().lobby.has_value() &&
               client.snapshot().lobby->revision == "2",
           "the fresh post-mutation poll must retain the authoritative lobby");
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

/**
 * Small authoritative social service used to exercise two independent native
 * clients against one lobby state.  This deliberately models the public API's
 * owner checks and response snapshots rather than sharing client-side state.
 */
class TwoClientLobbyService final {
public:
    [[nodiscard]] RevivalSocialResult execute(std::string account_id,
                                               const RevivalSocialRequest& request) {
        std::scoped_lock lock{mutex_};
        if (request.kind == RevivalSocialRequestKind::create_lobby) {
            owner_id_ = account_id;
            members_ = {account_id};
            state_ = "forming";
            server_id_.clear();
            settings_ = request.payload.value("settings", nlohmann::json::object());
            ++revision_;
        } else if (request.kind == RevivalSocialRequestKind::lobby_action) {
            if (request.action == "join") {
                if (std::ranges::find(members_, account_id) == members_.end()) {
                    members_.push_back(account_id);
                    ++revision_;
                }
            } else if (request.action == "leave") {
                std::erase(members_, account_id);
                if (owner_id_ == account_id) {
                    owner_id_ = members_.empty() ? std::string{} : members_.front();
                    state_ = members_.empty() ? "closed" : "forming";
                    server_id_.clear();
                }
                ++revision_;
                return response(request, account_id, false);
            } else if (request.action == "update") {
                if (account_id != owner_id_) return owner_failure(request);
                settings_ = request.payload.value("settings", settings_);
                ++revision_;
            } else if (request.action == "start") {
                if (account_id != owner_id_) return owner_failure(request);
                state_ = "starting";
                server_id_.clear();
                ++revision_;
            } else if (request.action == "publish") {
                if (account_id != owner_id_) return owner_failure(request);
                state_ = "ready";
                server_id_ = request.payload.value("server_id", std::string{});
                ++revision_;
            } else if (request.action == "chat") {
                if (std::ranges::find(members_, account_id) == members_.end()) {
                    RevivalSocialResult denied;
                    denied.request = request;
                    denied.http_status = 403L;
                    denied.error_code = "lobby_membership_required";
                    denied.error = "Join the lobby first.";
                    return denied;
                }
                events_.push_back(nlohmann::json{{"id", std::to_string(++event_id_)},
                                                 {"type", "lobby.chat"},
                                                 {"lobby_id", "42"},
                                                 {"actor_id", account_id},
                                                 {"payload", request.payload}});
            }
        }
        return response(request, account_id, true);
    }

private:
    [[nodiscard]] RevivalSocialResult owner_failure(
        const RevivalSocialRequest& request) const {
        RevivalSocialResult denied;
        denied.request = request;
        denied.http_status = 403L;
        denied.error_code = "lobby_owner_required";
        denied.error = "Only the lobby owner can perform this action.";
        return denied;
    }

    [[nodiscard]] RevivalSocialResult response(const RevivalSocialRequest& request,
                                                std::string_view account_id,
                                                bool include_current_lobby) const {
        nlohmann::json body{{"cursor", std::to_string(event_id_)},
                            {"friends", nlohmann::json::array()},
                            {"invitations", nlohmann::json::array()},
                            {"events", events_}};
        const auto member = std::ranges::find(members_, account_id) != members_.end();
        if (include_current_lobby && member && state_ != "closed") {
            nlohmann::json rows = nlohmann::json::array();
            for (const auto& id : members_) {
                rows.push_back({{"legacy_id", id},
                                {"nickname", id == "7" ? "Owner" : "Member"},
                                {"role", id == owner_id_ ? "owner" : "member"},
                                {"presence", server_id_.empty() ? "in_lobby" : "in_game"},
                                {"in_game", !server_id_.empty()}});
            }
            body["lobby"] = {{"id", "42"},
                             {"owner_id", owner_id_},
                             {"privacy", "invite"},
                             {"state", state_},
                             {"name", "Two Client Lobby"},
                             {"max_members", 24},
                             {"settings", settings_},
                             {"revision", std::to_string(revision_)},
                             {"server_id", server_id_},
                             {"members", std::move(rows)}};
        } else {
            body["lobby"] = nullptr;
        }
        return parse_revival_social_response(request, body.dump());
    }

    mutable std::mutex mutex_;
    std::string owner_id_;
    std::vector<std::string> members_;
    std::string state_{"closed"};
    std::string server_id_;
    nlohmann::json settings_ = nlohmann::json::object();
    nlohmann::json events_ = nlohmann::json::array();
    std::uint64_t revision_{};
    std::uint64_t event_id_{};
};

void two_clients_converge_through_owner_chat_start_and_leave() {
    TwoClientLobbyService service;
    const RevivalSocialClientConfig config{64U, 32U, 128U, 5ms, 5ms, 20ms};
    RevivalSocialClient owner{
        [&](const RevivalSocialRequest& request, std::stop_token) {
            return service.execute("7", request);
        },
        config};
    RevivalSocialClient member{
        [&](const RevivalSocialRequest& request, std::stop_token) {
            return service.execute("8", request);
        },
        config};
    owner.set_available(true);
    member.set_available(true);
    auto now = std::chrono::steady_clock::now();

    RevivalSocialRequest create;
    create.generation = 1U;
    create.kind = RevivalSocialRequestKind::create_lobby;
    create.priority = true;
    create.payload = nlohmann::json{{"settings", {{"map_name", "AncientEgypt"}}}};
    expect(owner.enqueue(create), "owner must enqueue lobby creation");
    wait_until([&] {
        static_cast<void>(owner.drain(now));
        return owner.snapshot().lobby.has_value();
    }, "owner did not receive the created lobby");

    RevivalSocialRequest join;
    join.generation = 1U;
    join.kind = RevivalSocialRequestKind::lobby_action;
    join.priority = true;
    join.lobby_id = "42";
    join.action = "join";
    expect(member.enqueue(join), "member must enqueue lobby join");
    wait_until([&] {
        static_cast<void>(member.drain(now));
        return member.snapshot().lobby.has_value();
    }, "member did not receive joined lobby");

    now += 10ms;
    owner.tick(now);
    wait_until([&] {
        static_cast<void>(owner.drain(now));
        return owner.snapshot().lobby.has_value() &&
               owner.snapshot().lobby->members.size() == 2U;
    }, "owner roster did not converge after member joined");

    auto forbidden = join;
    forbidden.generation = 2U;
    forbidden.action = "update";
    forbidden.payload = nlohmann::json{{"settings", {{"map_name", "Wrong"}}}};
    expect(member.enqueue(forbidden), "member owner-only mutation fixture must enqueue");
    std::vector<RevivalSocialResult> denied;
    wait_until([&] {
        denied = member.drain(now);
        return !denied.empty();
    }, "member owner-only mutation did not return");
    expect(!denied.front() && denied.front().error_code == "lobby_owner_required",
           "non-owner settings mutation must fail without corrupting cached lobby state");

    auto update = forbidden;
    update.generation = 2U;
    update.payload = nlohmann::json{{"settings", {{"map_name", "London"}}}};
    expect(owner.enqueue(update), "owner settings update must enqueue");
    wait_until([&] {
        static_cast<void>(owner.drain(now));
        return owner.snapshot().lobby.has_value() &&
               owner.snapshot().lobby->settings.value("map_name", "") == "London";
    }, "owner settings did not update authoritatively");

    auto chat = join;
    chat.generation = 3U;
    chat.action = "chat";
    chat.payload = nlohmann::json{{"message", "Привет 日本"}};
    expect(member.enqueue(chat), "member chat must enqueue");
    wait_until([&] {
        const auto rows = member.drain(now);
        return std::ranges::any_of(rows, [](const auto& row) {
            return row && !row.snapshot.events.empty();
        });
    }, "member chat event did not return");

    now += 10ms;
    owner.tick(now);
    wait_until([&] {
        const auto rows = owner.drain(now);
        return std::ranges::any_of(rows, [](const auto& row) {
            return row && row.snapshot.events.size() == 1U &&
                   row.snapshot.events.front().payload.value("message", "") == "Привет 日本";
        });
    }, "owner did not receive the member's UTF-8 chat exactly once");

    auto start = join;
    start.generation = 4U;
    start.action = "start";
    expect(owner.enqueue(start), "owner start must enqueue");
    wait_until([&] {
        static_cast<void>(owner.drain(now));
        return owner.snapshot().lobby.has_value() &&
               owner.snapshot().lobby->state == "starting";
    }, "owner start state did not become authoritative");

    auto publish = join;
    publish.generation = 5U;
    publish.action = "publish";
    publish.payload = nlohmann::json{{"server_id", "relay-public-id"}};
    expect(owner.enqueue(publish), "owner publish must enqueue");
    wait_until([&] {
        static_cast<void>(owner.drain(now));
        return owner.snapshot().lobby.has_value() &&
               owner.snapshot().lobby->server_id == "relay-public-id";
    }, "owner did not receive the published relay identifier");

    now += 10ms;
    member.tick(now);
    wait_until([&] {
        static_cast<void>(member.drain(now));
        return member.snapshot().lobby.has_value() &&
               member.snapshot().lobby->server_id == "relay-public-id";
    }, "member did not converge on the owner's public relay identifier");

    auto owner_leave = join;
    owner_leave.generation = 6U;
    owner_leave.action = "leave";
    expect(owner.enqueue(owner_leave), "owner leave must enqueue");
    wait_until([&] {
        static_cast<void>(owner.drain(now));
        return !owner.snapshot().lobby.has_value();
    }, "owner leave did not clear the local cached lobby");

    now += 10ms;
    member.tick(now);
    wait_until([&] {
        static_cast<void>(member.drain(now));
        return member.snapshot().lobby.has_value() &&
               member.snapshot().lobby->owner_id == "8" &&
               member.snapshot().lobby->state == "forming" &&
               member.snapshot().lobby->server_id.empty();
    }, "member did not become owner after deterministic owner transfer");
}

void authoritative_empty_responses_and_old_revisions_do_not_leave_stale_permissions() {
    RevivalSocialClient client{[](const RevivalSocialRequest& request, std::stop_token) {
        std::this_thread::sleep_for(10ms);
        return parse_revival_social_response(request, request.payload.dump());
    }};
    std::uint64_t generation{100U};
    const auto run = [&](RevivalSocialRequestKind kind, std::string action,
                         nlohmann::json payload, std::string lobby = "9") {
        RevivalSocialRequest request;
        request.generation = ++generation;
        request.kind = kind;
        request.priority = true;
        request.action = std::move(action);
        request.lobby_id = std::move(lobby);
        request.payload = std::move(payload);
        expect(client.enqueue(request), "fixture request must enqueue");
        std::vector<RevivalSocialResult> result;
        wait_until([&] {
            result = client.drain(std::chrono::steady_clock::now());
            return !result.empty();
        }, "fixture response did not arrive");
        return result.front();
    };
    const auto kind = RevivalSocialRequestKind::lobby_action;
    const nlohmann::json fresh{{"lobby", {{"id", "9"}, {"owner_id", "8"}, {"state", "forming"},
        {"revision", "10000000000000000001"}, {"start_id", "attempt"}}}};
    run(kind, "join", fresh);
    expect(client.snapshot().lobby->start_id == "attempt", "start attempt must survive parsing");
    const auto delayed = run(kind, "update", {{"lobby", {{"id", "9"}, {"owner_id", "7"},
        {"revision", "9999999999999999999"}}}});
    expect(client.snapshot().lobby->owner_id == "8" && delayed.snapshot.lobby->owner_id == "8",
           "an old revision must not restore former owner permissions in cache or delivered results");
    run(kind, "leave", {{"lobby", nullptr}}, "another-lobby");
    expect(client.snapshot().lobby.has_value(), "late leave from another lobby must not clear membership");
    run(kind, "close", {{"lobby", nullptr}});
    expect(!client.snapshot().lobby, "close must clear membership immediately, without waiting for a poll");
    run(RevivalSocialRequestKind::friend_action, "request", {{"friends", {{{"legacy_id", "8"}, {"nickname", "Fixture"}}}}});
    expect(client.snapshot().friends.size() == 1U, "friend fixture must seed the list");
    run(RevivalSocialRequestKind::friend_action, "remove", {{"friends", nlohmann::json::array()}});
    expect(client.snapshot().friends.empty(), "removing the last friend must apply the authoritative empty list");
}

void friend_writes_overtake_polls_and_stale_delivery_matches_the_cache() {
    std::mutex mutex;
    std::condition_variable_any gate;
    bool poll_started{};
    bool release_poll{};
    RevivalSocialClient client{[&](const RevivalSocialRequest& request, std::stop_token stop) {
        if (request.kind == RevivalSocialRequestKind::sync) {
            std::unique_lock lock{mutex};
            poll_started = true;
            gate.notify_all();
            static_cast<void>(gate.wait(lock, stop, [&] { return release_poll; }));
            return parse_revival_social_response(request,
                R"({"cursor":"old","friends":[{"legacy_id":"old","nickname":"Old"}],"invitations":[{"id":"expired","lobby_id":"old-lobby"}],"lobby":null})");
        }
        expect(request.priority, "Friend writes must use the priority lane even without a caller hint");
        return parse_revival_social_response(request,
            R"({"friends":[{"legacy_id":"new","nickname":"New","friendship_status":"accepted"}],"invitations":[]})");
    }};
    const auto now = std::chrono::steady_clock::now();
    client.set_available(true);
    client.tick(now);
    {
        std::unique_lock lock{mutex};
        expect(gate.wait_for(lock, 1s, [&] { return poll_started; }), "Slow poll fixture must start");
    }
    RevivalSocialRequest action;
    action.generation = 999U;
    action.kind = RevivalSocialRequestKind::friend_action;
    action.action = "accept";
    action.target = "new";
    expect(client.enqueue(action), "Friend fixture must enqueue");
    wait_until([&] {
        static_cast<void>(client.drain(now));
        return !client.snapshot().friends.empty() && client.snapshot().friends.front().legacy_id == "new";
    }, "Accept Friend must not wait for a blocked social poll");
    {
        std::scoped_lock lock{mutex};
        release_poll = true;
    }
    gate.notify_all();
    std::vector<RevivalSocialResult> delivered;
    wait_until([&] {
        delivered = client.drain(now);
        return !delivered.empty();
    }, "Stale poll must complete");
    expect(delivered.front().snapshot.friends.size() == 1U &&
               delivered.front().snapshot.friends.front().legacy_id == "new" &&
               delivered.front().snapshot.invitations.empty(),
           "Delivered stale polls must expose the accepted friend/invitation cache, not obsolete permissions");
    expect(client.snapshot().friends.front().legacy_id == "new",
           "The stale poll must not undo the accepted friendship");
}

void partial_responses_preserve_membership_and_consumed_invites_disappear_immediately() {
    RevivalSocialClient client{[](const RevivalSocialRequest& request, std::stop_token) {
        return parse_revival_social_response(request, request.payload.dump());
    }};
    std::uint64_t generation{700U};
    const auto run = [&](RevivalSocialRequestKind kind, std::string action, nlohmann::json payload) {
        RevivalSocialRequest request;
        request.generation = ++generation;
        request.kind = kind;
        request.action = std::move(action);
        request.lobby_id = "lobby";
        request.payload = std::move(payload);
        expect(client.enqueue(request), "Partial-response fixture must enqueue");
        std::vector<RevivalSocialResult> result;
        wait_until([&] {
            result = client.drain(std::chrono::steady_clock::now());
            return !result.empty();
        }, "Partial-response fixture did not complete");
        return result.front();
    };
    run(RevivalSocialRequestKind::sync, {}, {
        {"friends", {{{"legacy_id", "friend"}, {"nickname", "Friend"}}}},
        {"invitations", {{{"id", "invite"}, {"lobby_id", "other"}}}},
        {"lobby", {{"id", "lobby"}, {"owner_id", "self"}, {"revision", "2"}}}});
    run(RevivalSocialRequestKind::sync, {}, nlohmann::json::object());
    expect(client.snapshot().lobby && client.snapshot().friends.size() == 1U &&
               client.snapshot().invitations.size() == 1U,
           "Omitted fields must not masquerade as authoritative empty membership or collections");
    const auto malformed = run(RevivalSocialRequestKind::sync, {}, {{"lobby", false}});
    expect(!malformed && client.snapshot().lobby && client.snapshot().friends.size() == 1U,
           "Malformed membership must fail without clearing the last valid lobby");
    const auto malformed_friends = run(RevivalSocialRequestKind::sync, {}, {{"friends", false}});
    expect(!malformed_friends && client.snapshot().friends.size() == 1U,
           "Malformed friend collections must fail without erasing friends");
    run(RevivalSocialRequestKind::lobby_action, "decline_invite", {{"invitation_id", "invite"}});
    expect(client.snapshot().invitations.empty(),
           "A declined invitation must disappear on acknowledgement, before the next poll");
    run(RevivalSocialRequestKind::sync, {}, {{"friends", nlohmann::json::array()},
        {"invitations", nlohmann::json::array()}, {"lobby", nullptr}});
    expect(!client.snapshot().lobby && client.snapshot().friends.empty(),
           "Explicit authoritative empty membership and collections must still clear the cache");
}

} // namespace

int main() {
    try {
        default_json_members_are_objects();
        parser_is_bounded_and_deduplicated();
        priority_actions_overtake_a_blocked_poll();
        stale_poll_cannot_undo_a_priority_mutation();
        polling_coalesces_and_failures_back_off();
        shutdown_cancels_a_hung_request();
        a_successful_leave_clears_the_cached_lobby();
        authoritative_empty_responses_and_old_revisions_do_not_leave_stale_permissions();
        friend_writes_overtake_polls_and_stale_delivery_matches_the_cache();
        partial_responses_preserve_membership_and_consumed_invites_disappear_immediately();
        client_instances_are_unique_and_events_are_exactly_once();
        transport_failures_enter_reconnecting_without_stopping_retries();
        two_clients_converge_through_owner_chat_start_and_leave();
        std::cout << "revival social tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
