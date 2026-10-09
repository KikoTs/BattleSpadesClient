#include "battlespades/network/classic_protocol.hpp"
#include "battlespades/network/protocol168_runtime.hpp"
#include "battlespades/network/protocol168_terrain.hpp"
#include "battlespades/network/protocol168_weapons.hpp"
#include "battlespades/network/server_discovery.hpp"
#include "battlespades/world/classic_combat.hpp"
#include "battlespades/world/tutorial_session.hpp"
#include <chrono>
#include <iostream>
#include <thread>

// Opt-in interoperability test against a locally owned piqueserver, or a
// handshake-only diagnostic for an explicitly supplied endpoint (--probe).
// --spawn-probe additionally joins once and observes post-spawn map redirects.
// No public server is contacted by CTest.
int main(int argc, char** argv) {
    using namespace battlespades::network;
    LiveProtocol168Connection connection;
    EnetProtocol168Config transport;
    const bool probe = argc > 2 && std::string_view{argv[1]} == "--probe";
    const bool spawn_probe = argc > 2 && std::string_view{argv[1]} == "--spawn-probe";
    const bool remote_probe = probe || spawn_probe;
    const bool rotation = !remote_probe && argc > 2 && std::string_view{argv[2]} == "--rotation";
    const bool base_refill = !remote_probe && argc > 2 && std::string_view{argv[2]} == "--base-refill";
    transport.host = "127.0.0.1";
    transport.port = !remote_probe && argc > 1 ? static_cast<std::uint16_t>(std::stoi(argv[1])) : 32887;
    transport.protocol = (rotation || base_refill) ? GameProtocol::classic075 : GameProtocol::automatic;
    if (remote_probe) {
        ServerEndpoint endpoint;
        std::string error;
        if (!parse_server_endpoint(argv[2], endpoint, error)) {
            std::cerr << error << '\n';
            return 1;
        }
        transport.host = endpoint.host;
        transport.port = endpoint.port;
        transport.protocol = endpoint.protocol;
    }
    Protocol168SessionConfig session;
    session.auto_join = false;
    session.player_name = rotation ? "BS rotation" : base_refill ? "BS base refill" : "BS local test";
    if (!connection.start(transport, session))
        return 1;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{45};
    std::unique_ptr<Protocol168WorldBootstrap> boot;
    bool spawned{};
    unsigned bootstraps{};
    std::uint64_t generation{};
    std::unique_ptr<battlespades::world::TutorialWorldSession> simulation;
    const bool exercise = !remote_probe && argc > 2 && std::string_view{argv[2]} == "--exercise";
    const bool require_refill = exercise && argc > 3 && std::string_view{argv[3]} == "--refill";
    bool refill_seen{}, line_confirmed{}, refill_verified{};
    std::optional<std::array<float,3>> friendly_base;
    auto previous_phase = LiveProtocol168Phase::idle;
    auto previous_protocol = GameProtocol::automatic;
    auto probe_ready_at = deadline;
    std::chrono::steady_clock::time_point spawned_at{}, next_tick{};
    bool shot{}, reloaded{}, colored{}, built{}, line_built{}, dug{}, grenade{}, color_confirmed{},
        score_confirmed{};
    unsigned corrections{}, reloads{};
    unsigned live_updates{};
    while (std::chrono::steady_clock::now() < deadline) {
        auto status = connection.status();
        if (remote_probe && (status.phase != previous_phase || status.protocol != previous_protocol)) {
            std::cout << protocol_name(status.protocol) << " phase=" << int(status.phase)
                      << " received=" << status.received_datagrams
                      << " sync=" << int(status.loading.sync_percent) << std::endl;
            previous_phase = status.phase;
            previous_protocol = status.protocol;
        }
        if (!status.error.empty()) {
            std::cerr << status.error << '\n';
            return 2;
        }
        if (auto next = connection.take_bootstrap()) {
            if (bootstraps && next->map_generation <= generation) {
                std::cerr << "Map generation did not advance\n";
                return 6;
            }
            generation = next->map_generation;
            ++bootstraps;
            spawned = false;
            live_updates = 0;
            boot = std::move(next);
            std::cout << "Bootstrap " << protocol_name(boot->protocol) << " player "
                      << int(boot->local_player_id) << " generation " << generation << std::endl;
            if (probe) {
                probe_ready_at = std::chrono::steady_clock::now();
                continue;
            }
            // A redirect's server-side CreatePlayer owns the resumed life.
            // Repeating ExistingPlayer here would conceal the frontend bug.
            if (spawn_probe && bootstraps > 1) continue;
            auto join = session;
            join.team = 2;
            join.class_id = 5;
            if (spawn_probe) {
                SetClassLoadoutPacket loadout;
                loadout.player_id = boot->local_player_id;
                loadout.class_id = 5;
                loadout.instant = true;
                loadout.loadout = {4, 5, 6, 31};
                if (!connection.send(encode_packet(loadout))) return 3;
                std::cout << "Submitting class selection and initial join" << std::endl;
            }
            if (!connection.send(encode_protocol168_new_player_connection(join)))
                return 3;
        }
        for (const auto& packet : connection.take_inbound()) {
            if (packet.empty())
                continue;
            if (spawn_probe && spawned && packet.front() == std::byte{2}) ++live_updates;
            if (const auto decoded = decode_weapon_packet(packet); decoded && boot && simulation) {
                if (const auto* refill = std::get_if<RestockPacket>(&*decoded.packet);
                    refill && refill->player_id == boot->local_player_id) {
                    if (refill->type == 0) simulation->restock_ammunition();
                    if (refill->type == 5) {
                        simulation->queue_classic_block_restock();
                        refill_seen = true;
                    }
                }
            }
            if (const auto decoded = decode_runtime_packet(packet); decoded && boot) {
                if (const auto* entity = std::get_if<CreateEntityPacket>(&*decoded.packet);
                    entity && entity->type == 1 && entity->state == 2)
                    friendly_base = entity->position;
                if (simulation) {
                    if (const auto* hp = std::get_if<SetHpPacket>(&*decoded.packet))
                        simulation->set_server_health(hp->health);
                }
                if (spawn_probe)
                    if (const auto* chat = std::get_if<ChatMessagePacket>(&*decoded.packet);
                        chat && chat->chat_type == 2)
                        std::cout << "Server: " << chat->value << std::endl;
                if (const auto* score = std::get_if<SetScorePacket>(&*decoded.packet))
                    if (score->type == 1 && score->specifier == boot->local_player_id &&
                        score->value == 1)
                        score_confirmed = true;
            }
            if (packet.front() == std::byte{BlockBuildColoredPacket::id}) {
                const auto decoded = decode_terrain_packet(packet);
                if (decoded) {
                    const auto& build = std::get<BlockBuildColoredPacket>(*decoded.packet);
                    if (boot && build.player_id == boot->local_player_id)
                        color_confirmed = build.color == 0xE05020;
                }
            }
            if (packet.front() == std::byte{CreatePlayerPacket::id}) {
                const auto decoded = decode_create_player(packet);
                if (decoded)
                    if (const auto* player = &*decoded.packet;
                        boot && player->player_id == boot->local_player_id) {
                        std::cout << "Spawn " << player->position[0] << ',' << player->position[1]
                                  << ',' << player->position[2] << '\n';
                        spawned = true;
                        if (spawn_probe) spawned_at = std::chrono::steady_clock::now();
                        if (exercise || base_refill) {
                            battlespades::world::TutorialSessionConfig settings;
                            settings.network_authoritative = true;
                            settings.classic_protocol = static_cast<std::uint8_t>(boot->protocol);
                            settings.initial_class_id = 5;
                            settings.initial_loadout = player->loadout;
                            settings.initial_tool = base_refill ? 31 : 6;
                            settings.initial_position = {
                                player->position[0], player->position[1], player->position[2]};
                            settings.initial_orientation = {1, 0, 0};
                            simulation =
                                std::make_unique<battlespades::world::TutorialWorldSession>(
                                    boot->map, settings);
                            if (base_refill) {
                                simulation->set_server_health(37);
                                static_cast<void>(simulation->spend_server_confirmed_blocks(43));
                                simulation->classic_reload_completed(4,11);
                                simulation->set_primary_held(true);
                                for (int n=0; n<30; ++n) simulation->tick();
                                simulation->set_primary_held(false);
                                simulation->tick();
                                if (!simulation->selected_ammo() || simulation->selected_ammo()->magazine != 2 ||
                                    !simulation->equip_inventory_slot(1) || simulation->selected_tool_id() != 6) return 9;
                            }
                            spawned_at = next_tick = std::chrono::steady_clock::now();
                        }
                    }
            }
            if (packet.front() == std::byte{254}) {
                ++corrections;
                if (simulation)
                    if (auto position = classic_correction(packet))
                        simulation->apply_classic_correction(
                            {(*position)[0], (*position)[1], (*position)[2]},
                            packet[1] == std::byte{1});
            }
            if (packet.front() == std::byte{253} && packet.size() == 3) {
                if (simulation) simulation->classic_reload_completed(
                    std::to_integer<std::uint8_t>(packet[1]), std::to_integer<std::uint8_t>(packet[2]));
                ++reloads;
                std::cout << "Server ammo " << int(std::to_integer<unsigned char>(packet[1])) << '/'
                          << int(std::to_integer<unsigned char>(packet[2])) << '\n';
            }
            if (simulation && packet.front() == std::byte{BlockLinePacket::id}) {
                const auto decoded = decode_terrain_packet(packet);
                const auto* line = decoded ? std::get_if<BlockLinePacket>(&*decoded.packet) : nullptr;
                if (line && line->player_id == boot->local_player_id) {
                    static_cast<void>(simulation->spend_server_confirmed_blocks(
                        static_cast<std::uint16_t>(cube_line_cells(*line).size())));
                    line_confirmed = true;
                }
            }
        }
        if (simulation) {
            simulation->finish_classic_packet_batch();
            if (require_refill && refill_seen && line_confirmed && !refill_verified) {
                const auto* ammo = simulation->selected_ammo();
                if (simulation->blocks_remaining() != 50 || simulation->health() != 64 ||
                    !ammo || ammo->magazine != 4 || ammo->reserve != 11) {
                    std::cerr << "Script refill/line ordering lost authoritative supplies\n";
                    return 8;
                }
                refill_verified = true;
                std::cout << "Script refill: 50 blocks, restored 64 HP and 4/11 ammunition\n";
            }
        }
        if (probe && boot &&
            std::chrono::steady_clock::now() - probe_ready_at > std::chrono::seconds{3}) {
            std::cout << "Map bootstrap and live updates accepted; no player join sent.\n";
            connection.stop();
            return 0;
        }
        if (spawned && !exercise && !base_refill && (!rotation || bootstraps >= 2)) {
            if (spawn_probe && std::chrono::steady_clock::now() - spawned_at < std::chrono::seconds{12}) {
                std::this_thread::sleep_for(std::chrono::milliseconds{10});
                continue;
            }
            if (spawn_probe) {
                std::cout << "Post-spawn world updates: " << live_updates << std::endl;
                if (!live_updates) return 7;
            }
            if (rotation)
                std::cout << "Map rotation downloaded, bootstrapped and respawned.\n";
            connection.stop();
            return 0;
        }
        if (simulation && std::chrono::steady_clock::now() >= next_tick) {
            namespace w = battlespades::world;
            const double elapsed =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - spawned_at)
                    .count();
            next_tick += std::chrono::microseconds{16667};
            if (base_refill) {
                simulation->set_action_held(w::TutorialAction::forward,
                    friendly_base && simulation->player().position.x < (*friendly_base)[0] - 0.5F);
                simulation->tick();
                const auto& player = simulation->player();
                ClassicMotion motion;
                motion.position = {static_cast<float>(player.position.x), static_cast<float>(player.position.y), static_cast<float>(player.position.z)};
                motion.orientation = {1,0,0};
                motion.alive = true;
                motion.tool = 6;
                motion.movement = simulation->movement_flags();
                connection.update_classic_motion(motion);
                if (refill_seen) {
                    const auto* ammo = simulation->selected_ammo();
                    const bool gun_ok = ammo && ammo->magazine == 4 && ammo->reserve == 50;
                    const bool grenades_ok = simulation->equip_inventory_slot(2) &&
                        simulation->selected_ammo() && simulation->selected_ammo()->magazine == 3;
                    if (corrections || !friendly_base || simulation->health() != 100 || simulation->blocks_remaining() != 50 ||
                        !gun_ok || !grenades_ok) {
                        std::cerr << "Base refill mismatch: corrections=" << corrections
                                  << " base=" << friendly_base.has_value() << " hp=" << simulation->health()
                                  << " blocks=" << simulation->blocks_remaining() << " gun=" << gun_ok
                                  << " grenades=" << grenades_ok << '\n';
                        return 10;
                    }
                    std::cout << "Base contact: checkpoint entity, 100 HP, 50 blocks, 3 grenades, 4/50 ammo, zero corrections\n";
                    connection.stop();
                    return 0;
                }
                if (elapsed > 10) { std::cerr << "Base contact did not refill supplies\n"; return 11; }
                continue;
            }
            simulation->set_action_held(w::TutorialAction::forward, elapsed < 2);
            simulation->set_action_held(w::TutorialAction::backward, elapsed >= 2 && elapsed < 4);
            simulation->set_action_held(w::TutorialAction::jump, elapsed >= 12.6 && elapsed < 13.9);
            simulation->tick();
            auto p = simulation->player();
            ClassicMotion motion;
            motion.position = {static_cast<float>(p.position.x),
                               static_cast<float>(p.position.y),
                               static_cast<float>(p.position.z)};
            motion.orientation = {1, 0, 0};
            motion.alive = true;
            motion.movement = simulation->movement_flags();
            motion.tool = 6;
            if (elapsed >= 5 && elapsed < 5.6)
                motion.actions = 1;
            if (elapsed >= 8 && elapsed < 10) {
                motion.tool = 5;
                motion.orientation = {0.6F, 0, 0.8F};
                if (elapsed >= 8.8 && elapsed < 9.3) motion.actions = 2;
            }
            if (elapsed >= 10 && elapsed < 12) {
                motion.tool = 4;
                motion.orientation = {0.6F, 0, 0.8F};
                motion.actions = 2;
            }
            if (elapsed >= 12)
                motion.tool = 31;
            connection.update_classic_motion(motion);
            if (elapsed > 5.05 && !shot) {
                // A local test peer may stand five blocks ahead. Hit reports use the
                // same fixed-size boxes and terrain occlusion as the interactive client.
                w::ClassicCombat combat;
                p.orientation = {1, 0, 0};
                const std::array targets{
                    w::ClassicHitTarget{1, {255.5, 250.5, 233.75}, {-1, 0, 0}, false, false}};
                const auto attack = combat.attack(simulation->map(),
                                                  p,
                                                  targets,
                                                  {w::WeaponActionKind::hitscan, 6, 3, 1},
                                                  3,
                                                  false,
                                                  elapsed);
                for (const auto hit : attack.hits)
                    static_cast<void>(
                        connection.send_classic(classic_hit_packet(hit.player, hit.part)));
                shot = true;
            }
            if (elapsed > 6 && !reloaded) {
                static_cast<void>(connection.send(
                    encode_packet(WeaponReloadPacket{boot->local_player_id, 6, false})));
                reloaded = true;
            }
            if (elapsed > 8.2 && !built) {
                p.orientation = {0.6, 0, 0.8};
                const auto cell = w::classic_build_target(simulation->map(), p, {});
                if (cell)
                    static_cast<void>(
                        connection.send_classic(classic_block_packet(boot->local_player_id,
                                                                     0,
                                                                     {static_cast<int>(cell->x),
                                                                      static_cast<int>(cell->y),
                                                                      static_cast<int>(cell->z)})));
                built = true;
            }
            if (elapsed > 7.8 && !colored) {
                static_cast<void>(connection.send(
                    encode_packet(SetColorPacket{boot->local_player_id, 0xE05020})));
                colored = true;
            }
            if (elapsed > 9.35 && !line_built) {
                const int x=static_cast<int>(p.position.x)+1, y=static_cast<int>(p.position.y)+1;
                static_cast<void>(connection.send_classic(classic_line_packet(boot->local_player_id,
                    {x,y,235},{x,y+1,235})));
                line_built = true;
            }
            if (elapsed > 11.2 && !dug) {
                p.orientation = {0.6, 0, 0.8};
                w::ClassicCombat combat;
                const auto attack = combat.attack(simulation->map(),
                                                  p,
                                                  {},
                                                  {w::WeaponActionKind::melee, 4, 0, 1, true},
                                                  3,
                                                  false,
                                                  elapsed);
                if (attack.destroy)
                    static_cast<void>(connection.send_classic(
                        classic_block_packet(boot->local_player_id,
                                             2,
                                             {static_cast<int>(attack.destroy->x),
                                              static_cast<int>(attack.destroy->y),
                                              static_cast<int>(attack.destroy->z)})));
                dug = true;
            }
            if (elapsed > 12.2 && !grenade) {
                static_cast<void>(connection.send_classic(classic_grenade_packet(
                    boot->local_player_id, 3, motion.position, {1, 0, -0.1F})));
                grenade = true;
            }
            if (elapsed > 14) {
                std::cout << "Exercise: " << corrections << " corrections, " << reloads
                          << " reload replies\n";
                connection.stop();
                return corrections == 0 && reloads > 0 && color_confirmed && score_confirmed &&
                       (!require_refill || refill_verified) ? 0
                                                                                             : 5;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    std::cerr << (probe ? "Timed out before map bootstrap\n" : "Timed out before local spawn\n");
    return 4;
}
