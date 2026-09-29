// Two-identity social scenario driver for a LOCAL AoSPlay social API.
//
// Drives the production RevivalIdentityService + RevivalSocialClient exactly as
// the frontend does (tick + drain on a 16 ms "frame"), so convergence times and
// errors are the ones a player would see. Point it only at a loopback dev
// server (G:/AoSRevival/aos_revival/scripts/social-dev-server.mjs), never at
// www.aosplay.net: it creates accounts, lobbies and hundreds of chat events.
//
//   aos_social_live_driver http://127.0.0.1:18790 <scratch-dir> [prefix]
//
// Prints one JSON line per step and exits non-zero if any step fails.
#include "battlespades/network/revival_identity.hpp"
#include "battlespades/network/revival_social.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using namespace battlespades::network;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

struct Player final {
    std::string name;
    std::shared_ptr<RevivalIdentityService> service;
    std::unique_ptr<RevivalSocialClient> social;
    std::string legacy_id;
    std::vector<RevivalSocialResult> results;
    std::vector<std::string> errors;
    std::uint64_t generation{1'000U};
};

int failures{};

void report(nlohmann::json line) {
    std::cout << line.dump() << std::endl;
}

std::unique_ptr<Player> make_player(const std::string& api, const std::filesystem::path& root,
                                    std::string name) {
    auto player = std::make_unique<Player>();
    player->name = name;
    RevivalIdentityConfig config;
    config.api_base = api;
    config.state_path = root / (name + ".json");
    player->service = std::make_shared<RevivalIdentityService>(config);
    const auto login = player->service->login(name, "fixture-password-123");
    if (!login.account) throw std::runtime_error{"login failed for " + name + ": " + login.error};
    player->legacy_id = login.account->legacy_id;
    auto service = player->service;
    player->social = std::make_unique<RevivalSocialClient>(
        [service](const RevivalSocialRequest& request, std::stop_token stop) {
            return service->social_request(request, stop);
        });
    player->social->set_available(true);
    player->social->set_foreground(true);  // the players sit on the Friends screen
    return player;
}

void frame(std::vector<Player*> players) {
    const auto now = Clock::now();
    for (auto* player : players) {
        player->social->tick(now);
        for (auto& result : player->social->drain(now)) {
            if (!result) {
                player->errors.push_back(result.error_code + ": " + result.error);
            }
            player->results.push_back(std::move(result));
        }
    }
    std::this_thread::sleep_for(16ms);
}

/** Frame until predicate holds; returns elapsed milliseconds or nullopt on timeout. */
std::optional<long long> until(std::vector<Player*> players, const std::function<bool()>& predicate,
                               std::chrono::milliseconds timeout) {
    const auto started = Clock::now();
    while (Clock::now() - started < timeout) {
        frame(players);
        if (predicate()) {
            return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
        }
    }
    return std::nullopt;
}

/** Enqueue an action and frame until its own result is drained. */
std::optional<RevivalSocialResult> act(std::vector<Player*> players, Player& actor, RevivalSocialRequest request,
                                       std::chrono::milliseconds timeout = 20s) {
    request.generation = ++actor.generation;
    const auto generation = request.generation;
    if (!actor.social->enqueue(request)) return std::nullopt;
    std::optional<RevivalSocialResult> found;
    const auto elapsed = until(players, [&] {
        for (const auto& result : actor.results) {
            if (result.request.generation == generation) { found = result; return true; }
        }
        return false;
    }, timeout);
    static_cast<void>(elapsed);
    return found;
}

void step(const std::string& name, std::optional<long long> elapsed, nlohmann::json extra = {}) {
    nlohmann::json line{{"step", name}, {"ok", elapsed.has_value()}};
    if (elapsed) line["ms"] = *elapsed;
    if (!elapsed) ++failures;
    if (extra.is_object()) line.update(extra);
    report(line);
}

void action_step(const std::string& name, const std::optional<RevivalSocialResult>& result) {
    nlohmann::json line{{"step", name}, {"ok", result.has_value() && static_cast<bool>(*result)}};
    if (result) {
        line["http"] = result->http_status;
        if (!*result) { line["error_code"] = result->error_code; line["error"] = result->error; }
    } else {
        line["error"] = "no result (queue refused or timed out)";
    }
    if (!line["ok"].get<bool>()) ++failures;
    report(line);
}

bool has_friend(const Player& player, const std::string& id, std::string_view status,
                std::string_view direction = {}) {
    const auto snapshot = player.social->snapshot();
    return std::ranges::any_of(snapshot.friends, [&](const RevivalSocialFriend& row) {
        return row.legacy_id == id && row.friendship_status == status &&
               (direction.empty() || row.direction == direction);
    });
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: aos_social_live_driver <loopback-api> <scratch-dir> [prefix]\n";
        return 2;
    }
    const std::string api = argv[1];
    if (!api.starts_with("http://127.0.0.1") && !api.starts_with("http://localhost")) {
        std::cerr << "refusing a non-loopback API: this driver creates fixture data\n";
        return 2;
    }
    const std::filesystem::path root = argv[2];
    std::filesystem::create_directories(root);
    const std::string prefix = argc > 3 ? argv[3] : "Live";
    // --legacy-join reproduces the pre-fix flow (join without leaving first).
    const bool legacy_join = argc > 4 && std::string_view{argv[4]} == "--legacy-join";
    try {
        auto alpha = make_player(api, root, prefix + "Alpha");
        auto bravo = make_player(api, root, prefix + "Bravo");
        std::vector<Player*> both{alpha.get(), bravo.get()};

        step("initial_sync", until(both, [&] {
            return alpha->social->status(Clock::now()).available &&
                   bravo->social->status(Clock::now()).available;
        }, 20s));

        RevivalSocialRequest search;
        search.kind = RevivalSocialRequestKind::find_friends;
        search.query = bravo->name;
        const auto found = act(both, *alpha, search);
        action_step("alpha_search_bravo", found);

        RevivalSocialRequest request;
        request.kind = RevivalSocialRequestKind::friend_action;
        request.action = "request";
        request.target = bravo->legacy_id;
        action_step("alpha_send_friend_request", act(both, *alpha, request));
        step("bravo_sees_incoming_request", until(both, [&] {
            return has_friend(*bravo, alpha->legacy_id, "pending", "incoming");
        }, 30s));

        RevivalSocialRequest accept;
        accept.kind = RevivalSocialRequestKind::friend_action;
        accept.action = "accept";
        accept.target = alpha->legacy_id;
        action_step("bravo_accept", act(both, *bravo, accept));
        step("alpha_sees_accepted", until(both, [&] {
            return has_friend(*alpha, bravo->legacy_id, "accepted");
        }, 30s));
        step("alpha_sees_bravo_online", until(both, [&] {
            const auto snapshot = alpha->social->snapshot();
            return std::ranges::any_of(snapshot.friends, [&](const auto& row) {
                return row.legacy_id == bravo->legacy_id && row.presence != "offline";
            });
        }, 30s));

        // Bravo already sits in its own lobby (the common real-world case:
        // lobbies persist while the game polls). Alpha invites Bravo.
        RevivalSocialRequest own;
        own.kind = RevivalSocialRequestKind::create_lobby;
        own.payload = {{"name", "Bravo's Lobby"}, {"privacy", "open"}, {"max_members", 8}};
        action_step("bravo_creates_own_lobby", act(both, *bravo, own));

        RevivalSocialRequest create;
        create.kind = RevivalSocialRequestKind::create_lobby;
        create.payload = {{"name", "Alpha's Lobby"}, {"privacy", "invite"}, {"max_members", 8}};
        const auto created = act(both, *alpha, create);
        action_step("alpha_create_lobby", created);
        const auto lobby_id = created && created->snapshot.lobby ? created->snapshot.lobby->id : std::string{};

        RevivalSocialRequest invite;
        invite.kind = RevivalSocialRequestKind::lobby_action;
        invite.lobby_id = lobby_id;
        invite.action = "invite";
        invite.target = bravo->legacy_id;
        action_step("alpha_invite_bravo", act(both, *alpha, invite));
        std::string invitation_id;
        step("bravo_sees_invitation", until(both, [&] {
            for (const auto& row : bravo->social->snapshot().invitations) {
                if (row.lobby_id == lobby_id) { invitation_id = row.id; return true; }
            }
            return false;
        }, 30s));

        // Mirror the frontend's LEAVE + JOIN: an ordered leave of the current
        // lobby queued immediately before the invitation join.
        if (const auto current = bravo->social->snapshot().lobby;
            current.has_value() && current->id != lobby_id && !legacy_join) {
            RevivalSocialRequest leave_own;
            leave_own.kind = RevivalSocialRequestKind::lobby_action;
            leave_own.lobby_id = current->id;
            leave_own.action = "leave";
            leave_own.generation = ++bravo->generation;
            static_cast<void>(bravo->social->enqueue(leave_own));
        }
        RevivalSocialRequest join;
        join.kind = RevivalSocialRequestKind::lobby_action;
        join.lobby_id = lobby_id;
        join.action = "join";
        join.payload = {{"invitation_id", invitation_id}};
        const auto joined = act(both, *bravo, join);
        action_step("bravo_accept_invite_while_in_own_lobby", joined);
        step("alpha_sees_two_members", until(both, [&] {
            const auto snapshot = alpha->social->snapshot();
            return snapshot.lobby && snapshot.lobby->members.size() == 2U;
        }, 30s));

        // A busy lobby: hundreds of chat events. A newly started client then
        // performs its first (cursor 0) sync.
        RevivalSocialRequest chat;
        chat.kind = RevivalSocialRequestKind::lobby_action;
        chat.lobby_id = lobby_id;
        chat.action = "chat";
        std::size_t sent{};
        for (std::size_t index{}; index < 260U; ++index) {
            chat.payload = {{"message", std::string(400U, static_cast<char>('a' + index % 26U))}};
            chat.generation = ++alpha->generation;
            if (alpha->social->enqueue(chat)) ++sent;
            if (index % 24U == 23U) {
                static_cast<void>(until(both, [&] { return alpha->social->status(Clock::now()).priority_queued == 0U; }, 60s));
            }
        }
        static_cast<void>(until(both, [&] { return alpha->social->status(Clock::now()).priority_queued == 0U &&
                                                   !alpha->social->status(Clock::now()).priority_active; }, 120s));
        report({{"step", "chat_flood_sent"}, {"count", sent}});

        auto service = alpha->service;
        RevivalSocialClient restarted{[service](const RevivalSocialRequest& value, std::stop_token stop) {
            return service->social_request(value, stop);
        }};
        restarted.set_available(true);
        std::string restart_error;
        const auto started = Clock::now();
        std::optional<long long> restart_ms;
        while (Clock::now() - started < 30s) {
            const auto now = Clock::now();
            restarted.tick(now);
            for (auto& result : restarted.drain(now)) {
                if (!result && restart_error.empty()) restart_error = result.error_code + ": " + result.error;
            }
            if (restarted.snapshot().lobby.has_value()) {
                restart_ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
                break;
            }
            std::this_thread::sleep_for(16ms);
        }
        step("restarted_client_first_sync_after_chat_flood", restart_ms, {{"first_error", restart_error}});
        restarted.shutdown(0ms);

        RevivalSocialRequest leave;
        leave.kind = RevivalSocialRequestKind::lobby_action;
        leave.lobby_id = lobby_id;
        leave.action = "leave";
        action_step("bravo_leave", act(both, *bravo, leave));
        step("alpha_sees_one_member", until(both, [&] {
            const auto snapshot = alpha->social->snapshot();
            return snapshot.lobby && snapshot.lobby->members.size() == 1U;
        }, 30s));

        RevivalSocialRequest close;
        close.kind = RevivalSocialRequestKind::lobby_action;
        close.lobby_id = lobby_id;
        close.action = "close";
        action_step("alpha_close", act(both, *alpha, close));

        for (auto* player : both) {
            report({{"player", player->name}, {"errors", player->errors}});
            player->social->shutdown(250ms);
        }
    } catch (const std::exception& error) {
        std::cerr << "driver failed: " << error.what() << '\n';
        return 1;
    }
    report({{"failures", failures}});
    return failures == 0 ? 0 : 1;
}
