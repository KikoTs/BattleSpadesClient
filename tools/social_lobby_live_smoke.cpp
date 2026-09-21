#include "battlespades/network/live_protocol168_connection.hpp"
#include "battlespades/network/revival_identity.hpp"
#include "battlespades/platform/local_server_process.hpp"
#include "battlespades/platform/relay_host_tunnel.hpp"

#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {
namespace net = battlespades::network;
namespace platform = battlespades::platform;
using namespace std::chrono_literals;
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error{message};
}

struct Actor {
    std::unique_ptr<net::RevivalIdentityService> identity;
    std::unique_ptr<net::RevivalSocialClient> social;
    std::unique_ptr<net::LiveProtocol168Connection> game;
    std::map<std::uint64_t, net::RevivalSocialResult> replies;
    std::atomic_uint polls{};
    std::atomic_bool lose_reply{};
    std::atomic_bool delay_request{};
    std::string id;
    unsigned delay_offset{};
    bool synced{};
    std::uint8_t player_id{};
    std::uint8_t tool_id{};
    std::uint32_t input_loop{};
    std::chrono::milliseconds fault_delay{1600};
};

// All fault injection wraps the real authenticated HTTPS executor. No simulated
// lobby store, relay, game server, or Protocol 168 peer is used in this tool.
class Fixture {
public:
    std::array<Actor, 3> actors;
    std::string lobby_id;
    std::filesystem::path bundle;
    platform::LocalServerProcess server;
    platform::RelayHostTunnel tunnel;
    std::optional<net::RevivalRelayLobby> relay;
    std::size_t host{};
    std::uint64_t generation{100'000U};
    std::chrono::milliseconds fault_delay{1600};

    ~Fixture() {
        stop_host();
        for (auto& actor : actors) {
            if (actor.social) actor.social->shutdown(1500ms);
        }
        // Only the membership created by this fixture may be removed here.
        for (auto& actor : actors) {
            if (!actor.identity || lobby_id.empty()) continue;
            net::RevivalSocialRequest leave;
            leave.kind = net::RevivalSocialRequestKind::lobby_action;
            leave.lobby_id = lobby_id;
            leave.action = "leave";
            static_cast<void>(actor.identity->social_request(leave));
        }
    }

    void initialize(const std::filesystem::path& state) {
        std::filesystem::create_directories(state);
        for (std::size_t i{}; i < actors.size(); ++i) {
            auto& actor = actors[i];
            actor.delay_offset = static_cast<unsigned>(i);
            actor.fault_delay = fault_delay;
            net::RevivalIdentityConfig config;
            config.state_path = state / ("lobby-fixture-" + std::to_string(i) + ".json");
            config.timeout = 10s;
            actor.identity = std::make_unique<net::RevivalIdentityService>(config);
            auto authenticated = actor.identity->cached_account() ? actor.identity->refresh()
                                                                : actor.identity->guest_login();
            require(static_cast<bool>(authenticated), "Fixture login failed: " + authenticated.error);
            actor.id = authenticated.account->legacy_id;
            net::RevivalSocialClientConfig social_config;
            social_config.menu_poll_interval = 1500ms;
            social_config.active_lobby_poll_interval = 1000ms;
            actor.social = std::make_unique<net::RevivalSocialClient>([&actor](const auto& request, auto stop) {
                const auto delay = [&](auto duration) {
                    const auto end = Clock::now() + duration;
                    while (!stop.stop_requested() && Clock::now() < end) std::this_thread::sleep_for(20ms);
                };
                if (request.priority && actor.delay_request.exchange(false)) delay(actor.fault_delay);
                auto result = actor.identity->social_request(request, stop);
                if (request.kind == net::RevivalSocialRequestKind::sync) {
                    delay((++actor.polls + actor.delay_offset) % 3U == 0U ? actor.fault_delay : 250ms);
                } else if (request.priority && actor.lose_reply.exchange(false) && result) {
                    result = {};
                    result.request = request;
                    result.error_code = "transport_error";
                    result.error = "Fixture: committed response lost in transit";
                }
                return result;
            }, social_config);
            actor.social->set_available(true);
            std::cout << "Fixture actor " << i << " authenticated (account " << actor.id << ")\n";
        }
        wait([&] { for (auto& a : actors) if (!a.synced) return false; return true; }, "initial sync");
        // Repeated runs reuse these protected fixture identities, never the
        // user's normal launcher state. Clean any interrupted fixture run.
        for (std::size_t i{}; i < actors.size(); ++i) {
            const auto old = actors[i].social->snapshot().lobby;
            if (old) {
                lobby_id = old->id;
                const auto result = action(i, old->owner_id == actors[i].id ? "close" : "leave", Json::object(), -1);
                require(result.http_status == 200 || result.http_status == 404, "Could not clean prior fixture membership");
            }
        }
        lobby_id.clear();
    }

    void pump() {
        const auto now = Clock::now();
        for (auto& actor : actors) {
            if (actor.social) {
                actor.social->tick(now);
                for (auto& reply : actor.social->drain(now)) {
                    if (reply.request.kind == net::RevivalSocialRequestKind::sync && reply) actor.synced = true;
                    if (reply.request.generation >= 100'000U)
                        actor.replies.insert_or_assign(reply.request.generation, std::move(reply));
                }
            }
            if (actor.game) static_cast<void>(actor.game->take_inbound(256U));
        }
    }

    void wait(const std::function<bool()>& ready, const std::string& step, std::chrono::seconds timeout = 40s) {
        const auto end = Clock::now() + timeout;
        while (Clock::now() < end) {
            pump();
            if (ready()) return;
            std::this_thread::sleep_for(20ms);
        }
        throw std::runtime_error{"Timed out: " + step};
    }

    net::RevivalSocialResult send(std::size_t who, net::RevivalSocialRequest request, long status = 200) {
        request.priority = true;
        request.generation = ++generation;
        const auto id = request.generation;
        const auto label = request.action.empty() ? "create" : request.action;
        auto& actor = actors[who];
        require(actor.social->enqueue(std::move(request)), "Request queue rejected " + label);
        wait([&] { return actor.replies.contains(id); }, label);
        auto result = std::move(actor.replies.at(id));
        actor.replies.erase(id);
        require(status < 0 || result.http_status == status,
            label + " actor " + std::to_string(who) + ": expected " + std::to_string(status) +
            ", got " + std::to_string(result.http_status) + " " + result.error_code + " " + result.error);
        return result;
    }

    net::RevivalSocialResult action(std::size_t who, std::string name, Json payload = Json::object(), long status = 200) {
        net::RevivalSocialRequest request;
        request.kind = net::RevivalSocialRequestKind::lobby_action;
        request.lobby_id = lobby_id;
        request.action = std::move(name);
        request.payload = std::move(payload);
        return send(who, std::move(request), status);
    }

    void converge(std::size_t owner, std::size_t members, std::string state, std::size_t first = 0U) {
        wait([&] {
            for (std::size_t i = first; i < actors.size(); ++i) {
                if (!actors[i].social) continue;
                const auto lobby = actors[i].social->snapshot().lobby;
                if (!lobby || lobby->id != lobby_id || lobby->owner_id != actors[owner].id ||
                    lobby->members.size() != members || lobby->state != state) return false;
            }
            return true;
        }, "all clients converge on leader, membership and state");
    }

    net::RevivalSocialLobby current(std::size_t who) {
        const auto lobby = actors[who].social->snapshot().lobby;
        require(lobby.has_value(), "Missing current lobby");
        return *lobby;
    }

    void stop_host() {
        for (auto& actor : actors) if (actor.game) { actor.game->stop(); actor.game.reset(); }
        tunnel.stop();
        server.stop();
        if (relay) {
            if (actors[host].identity && !actors[host].identity->close_relay_lobby(*relay))
                std::cerr << "Relay cleanup did not acknowledge closure\n";
            relay.reset();
        }
    }

    std::string launch(std::size_t owner, bool lost_start = false) {
        host = owner;
        const auto attempt = net::new_revival_social_id();
        auto& actor = actors[owner];
        actor.lose_reply = lost_start;
        actor.delay_request = true;
        action(owner, "start", {{"start_id", attempt}}, lost_start ? 0 : 200);
        wait([&] { const auto l = current(owner); return l.state == "starting" && l.start_id == attempt; }, "start acknowledgement recovery");
        action(owner, "start", {{"start_id", attempt}}); // Retry after a lost acknowledgement.
        action(owner, "start", {{"start_id", net::new_revival_social_id()}}, 409);
        net::RevivalRelayLobbyRequest request;
        request.name = "AoS Lobby Stability Fixture";
        request.map = "AncientEgypt";
        request.game_mode = "TDM_TITLE";
        request.mode_tla = "tdm";
        request.max_players = 4U;
        request.texture_skin = "classic";
        auto allocated = actor.identity->create_relay_lobby(request);
        require(static_cast<bool>(allocated), "Relay allocation failed: " + allocated.error);
        relay = std::move(allocated.lobby);
        platform::LocalServerLaunchConfig config;
        config.bundle_root = bundle;
        config.server_name = request.name;
        config.mode = "tdm";
        config.map_name = request.map;
        config.maximum_players = 4U;
        config.match_minutes = 10U;
        config.environment_overrides = {
            {"AOS_MASTER_URL", relay->master_url}, {"AOS_MASTER_WRITE_TOKEN", relay->server_token},
            {"AOS_PUBLIC_HOST", relay->relay_host}, {"AOS_PUBLIC_PORT", std::to_string(relay->relay_port)},
            {"AOS_PUBLIC_QUERY_PORT", std::to_string(relay->relay_port)}, {"AOS_SERVER_ID", relay->server_id},
            {"AOS_RELAY_LOBBY_ID", relay->lobby_id},
            {"AOS_MATCH_RESULTS_DIRECTORY", actor.identity->hosted_results_directory().string()},
        };
        std::string error;
        require(server.start(config, error), "Host launch failed: " + error);
        platform::RelayHostTunnelConfig tc;
        tc.allocation_id = relay->allocation_id;
        tc.relay_host = relay->relay_host;
        tc.relay_port = relay->relay_port;
        tc.host_key_base64url = relay->host_key;
        tc.local_server_port = server.port();
        tc.maximum_clients = 4U;
        tc.keepalive = std::chrono::seconds{relay->keepalive_seconds};
        require(tunnel.start(std::move(tc), error), "Relay tunnel failed: " + error);
        bool published{};
        const auto end = Clock::now() + 45s;
        while (!published && Clock::now() < end) {
            const auto reply = action(owner, "publish", {{"start_id", attempt}, {"relay_lobby_id", relay->lobby_id}, {"server_id", relay->server_id}}, -1);
            published = reply.http_status == 200;
            require(published || (reply.http_status == 409 && reply.error_code == "relay_not_ready"), "Publish failed: " + reply.error);
            if (!published) {
                const auto pause = Clock::now() + 1s;
                wait([&] { return Clock::now() >= pause; }, "relay readiness retry");
            }
        }
        require(published, "Server heartbeat never made relay publishable");
        std::cout << "Public relay published: " << relay->server_id << '\n';
        return attempt;
    }

    void connect_pair(std::size_t first, std::size_t second) {
        for (const auto i : {first, second}) {
            const auto ticket = actors[i].identity->game_ticket(relay->server_id);
            require(static_cast<bool>(ticket), "Game ticket failed: " + ticket.error);
            net::Protocol168SessionConfig session;
            session.player_name = ticket.join_code;
            session.team = 2U; // The assigned green player must be overridden by the server.
            actors[i].game = std::make_unique<net::LiveProtocol168Connection>();
            require(actors[i].game->start({relay->relay_host, relay->relay_port, 30'000U}, session), "Game worker failed to start");
        }
        wait([&] {
            bool ready = true;
            for (const auto i : {first, second}) {
                const auto status = actors[i].game->status();
                require(status.phase != net::LiveProtocol168Phase::failed && status.phase != net::LiveProtocol168Phase::disconnected,
                        "Protocol 168 actor " + std::to_string(i) + ": " + status.error);
                ready = ready && status.phase == net::LiveProtocol168Phase::ready;
            }
            return ready;
        }, "two simultaneous public relay game joins");
        for (const auto i : {first, second}) {
            const auto bootstrap = actors[i].game->take_bootstrap();
            require(bootstrap && bootstrap->map && bootstrap->map->solid_voxels() > 0U, "Missing authoritative map");
            const auto* player = bootstrap->roster.player(bootstrap->local_player_id);
            require(player != nullptr, "Missing own player");
            actors[i].player_id = bootstrap->local_player_id;
            actors[i].tool_id = player->loadout.empty() ? 0U : player->loadout.front();
            const auto lobby = current(i);
            for (const auto& member : lobby.members) if (member.legacy_id == actors[i].id && member.member_data.contains("assigned_team")) {
                require(player->team == member.member_data.at("assigned_team").get<unsigned>(), "Server ignored authoritative team assignment");
            }
            std::cout << "Public relay Protocol 168 actor " << i << ": map=" << bootstrap->initial_info.map_name
                      << " team=" << static_cast<unsigned>(player->team) << " voxels=" << bootstrap->map->solid_voxels() << '\n';
        }
    }
};
} // namespace

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        std::cerr << "usage: aos_social_lobby_live_smoke <portable-server-directory> <dedicated-fixture-state-directory> [delay-ms:0..12000|--progression|--recover-results]\n";
        return 2;
    }
    std::cout << std::unitbuf;
    try {
        Fixture f;
        const bool progression = argc == 4 && std::string_view{argv[3]} == "--progression";
        const bool recover_results = argc == 4 && std::string_view{argv[3]} == "--recover-results";
        if (argc == 4 && !progression && !recover_results) {
            const std::string_view text{argv[3]};
            unsigned milliseconds{};
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), milliseconds);
            require(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && milliseconds <= 12'000U, "Invalid injected delay");
            f.fault_delay = std::chrono::milliseconds{milliseconds};
        }
        std::cout << "Injected request/reply delay: " << f.fault_delay.count() << " ms\n";
        f.bundle = argv[1];
        f.initialize(argv[2]);
        if (recover_results) {
            const auto uploaded = f.actors[0].identity->flush_hosted_results();
            std::cout << "Recovered " << uploaded.uploaded << " queued reports: " << uploaded.error << '\n';
            require(uploaded.error.empty(), "Report recovery failed");
            for (std::size_t i = 1; i < 3; ++i) {
                const auto snapshot = f.actors[i].identity->inventory_request({});
                require(static_cast<bool>(snapshot), "Progression snapshot failed");
                std::cout << "Recovered fixture player " << i << " XP "
                          << snapshot.payload.at("progression").at("lifetime_xp").get<std::string>() << '\n';
            }
            return 0;
        }
        net::RevivalSocialRequest create;
        create.kind = net::RevivalSocialRequestKind::create_lobby;
        create.payload = {{"name", "AoS Lobby Stability Fixture"}, {"privacy", "open"}, {"max_members", 4}};
        const auto created = f.send(0, std::move(create), 201);
        require(created.snapshot.lobby.has_value(), "Create returned no lobby");
        f.lobby_id = created.snapshot.lobby->id;
        if (progression) {
            f.action(1, "join"); f.action(2, "join");
            f.converge(0, 3U, "forming");
            f.action(0, "assign_team", {{"target", f.actors[1].id}, {"team", 2}});
            f.action(0, "assign_team", {{"target", f.actors[2].id}, {"team", 3}});
            std::array<std::uint64_t,2> before{};
            for (std::size_t i=1; i<3; ++i) {
                const auto snapshot = f.actors[i].identity->inventory_request({});
                require(static_cast<bool>(snapshot), "Progression snapshot failed: " + snapshot.error);
                before[i-1] = std::stoull(snapshot.payload.at("progression").at("lifetime_xp").get<std::string>());
            }
            const auto attempt = f.launch(0);
            f.connect_pair(1,2);
            f.action(0,"in_game",{{"start_id",attempt}});
            std::cout << "Playing 75 seconds through the real relay before closing the host...\n";
            const auto end = Clock::now()+75s;
            while (Clock::now()<end) {
                f.pump();
                for (std::size_t i=1;i<3;++i) {
                    auto& actor=f.actors[i];
                    require(actor.game->status().phase==net::LiveProtocol168Phase::ready,"Game disconnected during activity");
                    net::ClientDataPacket input;
                    input.loop_count=actor.input_loop++;
                    input.player_id=actor.player_id; input.tool_id=actor.tool_id;
                    input.opaque_state=net::protocol168_client_data_opaque_state(input.loop_count);
                    const float angle=static_cast<float>(actor.input_loop)*0.03F;
                    input.orientation={std::cos(angle),std::sin(angle),0.0F};
                    require(actor.game->send(net::encode_packet(input)),"Activity send failed");
                }
                std::this_thread::sleep_for(33ms);
            }
            f.stop_host();
            std::array<std::uint64_t, 2> after = before;
            const auto recovery_deadline = Clock::now() + 60s;
            bool awarded{};
            do {
                const auto uploaded = f.actors[0].identity->flush_hosted_results();
                std::cout << "Owner recovered " << uploaded.uploaded << " result files after host shutdown: " << uploaded.error << '\n';
                awarded = true;
                for (std::size_t i = 1; i < 3; ++i) {
                    const auto snapshot = f.actors[i].identity->inventory_request({});
                    require(static_cast<bool>(snapshot), "Final snapshot failed");
                    after[i-1] = std::stoull(snapshot.payload.at("progression").at("lifetime_xp").get<std::string>());
                    awarded = awarded && after[i-1] > before[i-1];
                }
                if (!awarded) std::this_thread::sleep_for(2s);
            } while (!awarded && Clock::now() < recovery_deadline);
            require(awarded, "Hosted participation did not award XP after recovery deadline");
            for (std::size_t i = 1; i < 3; ++i)
                std::cout << "PASS relay player " << i << " XP " << before[i-1] << " -> " << after[i-1] << '\n';
            require(f.actors[0].identity->flush_hosted_results().uploaded==0U,"Acknowledged report retried");
            std::cout << "PASS public P2P participation, shutdown recovery and persistent XP\n";
            return 0;
        }
        f.actors[1].lose_reply = true;
        f.action(1, "join", Json::object(), 0);
        f.wait([&] { return f.actors[1].social->snapshot().lobby.has_value(); }, "committed join with lost response");
        f.action(1, "join");
        for (const auto& name : {"update", "start", "kick", "assign_team", "close"}) f.action(1, name, Json::object(), 403);
        f.wait([&] { return f.current(0).members.size() == 2U; }, "owner sees joined member before editing revision");
        f.action(0, "update", {{"revision", f.current(0).revision}, {"privacy", "invite"}});
        f.action(2, "join", Json::object(), 403);
        f.action(0, "update", {{"revision", f.current(0).revision}, {"privacy", "open"}});
        f.action(2, "join");
        f.converge(0, 3U, "forming");
        for (int round{}; round < 3; ++round) { f.action(2, "leave"); f.action(2, "join"); }
        f.action(0, "kick", {{"target", f.actors[2].id}});
        f.wait([&] { return !f.actors[2].social->snapshot().lobby; }, "kicked client clears lobby");
        f.action(2, "join");
        f.action(0, "assign_team", {{"target", f.actors[1].id}, {"team", 2}});
        f.action(0, "assign_team", {{"target", f.actors[2].id}, {"team", 3}});
        f.action(2, "member_update", {{"member_data", {{"assigned_team", 2}, {"ready", true}}}});
        f.converge(0, 3U, "forming");
        std::cout << "PASS delayed/lost join, retries, leave/rejoin, kick and permissions\n";
        const auto attempt = f.launch(0, true);
        f.connect_pair(1, 2);
        f.action(0, "in_game", {{"start_id", attempt}});
        f.converge(0, 3U, "in_game");
        f.action(0, "publish", {{"start_id", attempt}, {"relay_lobby_id", f.relay->lobby_id}, {"server_id", f.relay->server_id}});
        require(f.current(0).state == "in_game", "Duplicate publish rewound match state");
        f.stop_host();
        f.converge(0, 3U, "forming");
        std::cout << "PASS live relay loss recovers while owner remains online\n";
        std::cout << "Waiting for disconnected owner's 30-second grace and leadership handoff...\n";
        f.actors[0].social->shutdown(1500ms);
        f.actors[0].social.reset();
        f.converge(1, 2U, "forming", 1U);
        net::RevivalSocialRequest stale;
        stale.kind = net::RevivalSocialRequestKind::lobby_action;
        stale.lobby_id = f.lobby_id;
        stale.action = "close";
        require(f.actors[0].identity->social_request(stale).http_status == 403, "Old owner retained permissions");
        std::cout << "PASS disconnected owner transfers leadership; old permissions revoked\n";
        const auto restarted = f.launch(1);
        f.action(1, "start_failed", {{"start_id", attempt}}, 409);
        f.connect_pair(1, 2);
        f.action(1, "in_game", {{"start_id", restarted}});
        f.converge(1, 2U, "in_game", 1U);
        f.action(1, "close");
        f.wait([&] { return !f.actors[1].social->snapshot().lobby && !f.actors[2].social->snapshot().lobby; }, "close clears every member");
        std::cout << "PASS successor can host again; stale attempt rejected; closure converges\n";
        std::cout << "PASS all live lobby/relay scenarios\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL lobby live smoke: " << error.what() << '\n';
        return 1;
    }
}
