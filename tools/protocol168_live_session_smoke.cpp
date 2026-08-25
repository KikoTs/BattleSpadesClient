#include "battlespades/network/live_protocol168_connection.hpp"
#include "battlespades/network/protocol168_terrain.hpp"
#include "battlespades/network/protocol168_tool_actions.hpp"
#include "battlespades/network/protocol168_weapons.hpp"
#include "battlespades/world/class_selection.hpp"
#include "battlespades/world/weapon_catalog.hpp"

#include <algorithm>
#include <chrono>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3 || argc > 5) {
        std::cerr << "usage: aos_protocol168_live_session_smoke HOST PORT "
                     "[SECONDS] [--prefab]\n";
        return 2;
    }
    const auto parsed_port = std::strtoul(argv[2], nullptr, 10);
    const auto seconds = argc >= 4 ? std::strtoul(argv[3], nullptr, 10) : 5UL;
    const bool test_prefab = argc == 5 && std::string_view{argv[4]} == "--prefab";
    if (argc == 5 && !test_prefab) return 2;
    if (parsed_port == 0UL || parsed_port > 65'535UL || seconds == 0UL) return 2;

    using namespace battlespades::network;
    LiveProtocol168Connection connection;
    if (!connection.start(
            EnetProtocol168Config{argv[1], static_cast<std::uint16_t>(parsed_port), 30'000U},
            Protocol168SessionConfig{"NativeLiveSmoke"})) {
        std::cerr << connection.status().error << '\n';
        return 1;
    }

    const auto handshake_deadline = std::chrono::steady_clock::now() +
                                    std::chrono::seconds{35};
    std::unique_ptr<Protocol168WorldBootstrap> bootstrap;
    while (std::chrono::steady_clock::now() < handshake_deadline && bootstrap == nullptr) {
        const auto status = connection.status();
        if (status.phase == LiveProtocol168Phase::failed ||
            status.phase == LiveProtocol168Phase::disconnected) {
            std::cerr << status.error << '\n';
            return 1;
        }
        bootstrap = connection.take_bootstrap();
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    if (bootstrap == nullptr || bootstrap->map == nullptr) {
        std::cerr << "live session never published a world bootstrap\n";
        return 1;
    }

    LiveProtocol168Connection observer;
    if (!observer.start(
            EnetProtocol168Config{argv[1], static_cast<std::uint16_t>(parsed_port),
                                  30'000U},
            Protocol168SessionConfig{"NativeObserver"})) {
        std::cerr << observer.status().error << '\n';
        return 1;
    }
    const auto observer_deadline = std::chrono::steady_clock::now() +
                                   std::chrono::seconds{35};
    std::unique_ptr<Protocol168WorldBootstrap> observer_bootstrap;
    while (std::chrono::steady_clock::now() < observer_deadline &&
           observer_bootstrap == nullptr) {
        const auto observer_status = observer.status();
        if (observer_status.phase == LiveProtocol168Phase::failed ||
            observer_status.phase == LiveProtocol168Phase::disconnected) {
            std::cerr << observer_status.error << '\n';
            return 1;
        }
        observer_bootstrap = observer.take_bootstrap();
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    if (observer_bootstrap == nullptr) {
        std::cerr << "observer never published a world bootstrap\n";
        return 1;
    }

    std::size_t world_updates{};
    std::size_t runtime_packets{};
    std::array<std::size_t, 256U> packet_counts{};
    std::size_t own_shot_feedback{};
    std::size_t own_prefab_cells{};
    std::int32_t loop{};
    const auto* local = bootstrap->roster.player(bootstrap->local_player_id);
    if (local == nullptr) {
        std::cerr << "bootstrap omitted the local player\n";
        return 1;
    }
    std::vector<std::string> requested_prefabs;
    if (test_prefab && local->class_id == 1U) {
        requested_prefabs = {"prefab_caltrop", "prefab_supertower",
                             "prefab_superbridge"};
    }
    const auto selection = requested_prefabs.empty()
                               ? battlespades::world::default_class_selection(
                                     local->class_id)
                               : battlespades::world::make_class_selection(
                                     local->class_id, {}, requested_prefabs);
    SetClassLoadoutPacket loadout;
    loadout.player_id = bootstrap->local_player_id;
    loadout.class_id = selection.class_id;
    loadout.instant = true;
    loadout.loadout = selection.loadout;
    loadout.prefabs = selection.prefabs;
    loadout.ugc_tools = selection.ugc_tools;
    if (!connection.send(encode_packet(loadout))) {
        std::cerr << "failed to send class transaction\n";
        return 1;
    }
    auto active_tool = selection.loadout.empty() ? std::uint8_t{}
                                                  : selection.loadout.front();
    for (const auto tool : selection.loadout) {
        const auto* weapon = battlespades::world::find_weapon_definition(tool);
        if (weapon != nullptr &&
            (weapon->category == battlespades::world::WeaponCategory::rifle ||
             weapon->category == battlespades::world::WeaponCategory::smg ||
             weapon->category == battlespades::world::WeaponCategory::shotgun ||
             weapon->category == battlespades::world::WeaponCategory::sniper ||
             weapon->category == battlespades::world::WeaponCategory::pistol ||
             weapon->category == battlespades::world::WeaponCategory::machine_gun)) {
            active_tool = tool;
            break;
        }
    }
    bool shot_sent{};
    bool prefab_sent{};
    const auto spawn_x = static_cast<std::int32_t>(std::lround(local->position.x));
    const auto spawn_y = static_cast<std::int32_t>(std::lround(local->position.y));
    const auto prefab_x = std::clamp(spawn_x <= 498 ? spawn_x + 8 : spawn_x - 12,
                                     1, 506);
    const auto prefab_y = std::clamp(spawn_y, 1, 506);
    const auto surface = bootstrap->map->surface_z(
        static_cast<std::uint32_t>(prefab_x),
        static_cast<std::uint32_t>(prefab_y));
    const auto prefab_z = static_cast<std::int16_t>(surface >= 3U ? surface - 3U
                                                                  : surface);
    std::int32_t observer_loop{};
    const auto* observer_local =
        observer_bootstrap->roster.player(observer_bootstrap->local_player_id);
    const auto until = std::chrono::steady_clock::now() +
                       std::chrono::seconds{seconds};
    while (std::chrono::steady_clock::now() < until) {
        for (const auto& packet : observer.take_inbound()) {
            if (packet.empty()) {
                continue;
            }
            const auto id = std::to_integer<std::uint8_t>(packet.front());
            if (id == ShootFeedbackPacket::id) {
                const auto decoded = decode_weapon_packet(packet);
                if (decoded) {
                    if (const auto* feedback = std::get_if<ShootFeedbackPacket>(
                            &*decoded.packet);
                        feedback != nullptr &&
                        feedback->shooter_id == bootstrap->local_player_id) {
                        ++own_shot_feedback;
                    }
                }
            } else if (id == BlockBuildColoredPacket::id) {
                const auto decoded = decode_terrain_packet(packet);
                if (decoded) {
                    if (const auto* build = std::get_if<BlockBuildColoredPacket>(
                            &*decoded.packet);
                        build != nullptr &&
                        build->player_id == bootstrap->local_player_id) {
                        ++own_prefab_cells;
                    }
                }
            }
        }
        for (const auto& packet : connection.take_inbound()) {
            ++runtime_packets;
            if (!packet.empty()) {
                ++packet_counts[std::to_integer<std::uint8_t>(packet.front())];
            }
            if (!packet.empty() && std::to_integer<std::uint8_t>(packet.front()) == 2U) {
                const auto update = decode_world_update_weapon_rows(packet);
                if (!update) {
                    std::cerr << update.error << '\n';
                    return 1;
                }
                ++world_updates;
            } else if (!packet.empty() &&
                       std::to_integer<std::uint8_t>(packet.front()) ==
                           ShootFeedbackPacket::id) {
                const auto decoded = decode_weapon_packet(packet);
                if (decoded) {
                    if (const auto* feedback = std::get_if<ShootFeedbackPacket>(
                            &*decoded.packet);
                        feedback != nullptr &&
                        feedback->shooter_id == bootstrap->local_player_id) {
                        ++own_shot_feedback;
                    }
                }
            }
        }
        ClientDataPacket input;
        input.loop_count = loop++;
        input.player_id = bootstrap->local_player_id;
        input.tool_id = test_prefab && loop >= 60 ? 23U : active_tool;
        input.opaque_state =
            protocol168_client_data_opaque_state(input.loop_count);
        // Shoot validation compares packet 6 against the latest ClientData
        // orientation. Keep both pointed harmlessly upward so this integration
        // smoke proves replication without damaging players or terrain.
        input.orientation = {0.0F, 0.0F, -1.0F};
        input.action_flags = 0x10U;
        if (!connection.send(encode_packet(input))) {
            std::cerr << connection.status().error << '\n';
            return 1;
        }
        ClientDataPacket observer_input;
        observer_input.loop_count = observer_loop++;
        observer_input.player_id = observer_bootstrap->local_player_id;
        observer_input.opaque_state =
            protocol168_client_data_opaque_state(observer_input.loop_count);
        observer_input.tool_id = observer_local == nullptr ||
                                         observer_local->loadout.empty()
                                     ? 0U
                                     : observer_local->loadout.front();
        observer_input.orientation = {-1.0F, 0.0F, 0.0F};
        if (!observer.send(encode_packet(observer_input))) {
            std::cerr << observer.status().error << '\n';
            return 1;
        }
        if (!shot_sent && loop >= 30) {
            const auto* weapon = battlespades::world::find_weapon_definition(active_tool);
            ShootPacket shot;
            shot.loop_count = loop - 1;
            shot.shooter_id = bootstrap->local_player_id;
            shot.position = {static_cast<float>(local->position.x),
                             static_cast<float>(local->position.y),
                             static_cast<float>(local->position.z)};
            // Fire harmlessly into the sky; the smoke only proves that the
            // authoritative server recognizes our selected tool/action.
            shot.orientation = {0.0F, 0.0F, -1.0F};
            shot.damage = weapon == nullptr
                              ? 0
                              : static_cast<std::int16_t>(weapon->base_damage);
            shot.penetration = weapon == nullptr
                                   ? 0
                                   : static_cast<std::int16_t>(weapon->block_damage);
            shot.seed = 47U;
            if (!connection.send(encode_packet(shot))) {
                std::cerr << "failed to send safe weapon smoke shot\n";
                return 1;
            }
            shot_sent = true;
        }
        if (test_prefab && !prefab_sent && loop >= 90) {
            BuildPrefabActionPacket prefab;
            prefab.loop_count = loop - 1;
            prefab.prefab_name = "prefab_caltrop";
            prefab.player_id = bootstrap->local_player_id;
            prefab.to_block_index = 11;
            prefab.position = {static_cast<std::int16_t>(prefab_x),
                               static_cast<std::int16_t>(prefab_y), prefab_z};
            prefab.color = 0x006E6E6EU;
            if (!connection.send(encode_packet(prefab))) {
                std::cerr << "failed to send safe prefab smoke action\n";
                return 1;
            }
            prefab_sent = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{16});
    }
    const auto status = connection.status();
    const auto observer_status = observer.status();
    connection.stop();
    observer.stop();
    std::cout << "map=" << bootstrap->initial_info.map_name
              << " local_player=" << static_cast<unsigned>(bootstrap->local_player_id)
              << " roster=" << bootstrap->roster.players().size()
              << " team1_classes=" << bootstrap->state_info.team1_classes.size()
              << " team2_classes=" << bootstrap->state_info.team2_classes.size()
              << " advertised_prefabs=" << bootstrap->state_info.prefabs.size()
              << " runtime_packets=" << runtime_packets
              << " world_updates=" << world_updates
              << " own_shot_feedback=" << own_shot_feedback
              << " own_prefab_cells=" << own_prefab_cells
              << " selected_class=" << static_cast<unsigned>(selection.class_id)
              << " selected_tool=" << static_cast<unsigned>(active_tool)
              << " sent=" << status.sent_datagrams
              << " received=" << status.received_datagrams << '\n';
    std::cout << "packet_ids=";
    bool first{true};
    for (std::size_t id{}; id < packet_counts.size(); ++id) {
        if (packet_counts[id] == 0U) continue;
        if (!first) std::cout << ',';
        first = false;
        std::cout << id << ':' << packet_counts[id];
    }
    std::cout << '\n';
    if (status.phase != LiveProtocol168Phase::ready ||
        observer_status.phase != LiveProtocol168Phase::ready ||
        world_updates == 0U ||
        own_shot_feedback == 0U || (test_prefab && own_prefab_cells == 0U)) {
        std::cerr << (status.error.empty() ? "live weapon feedback was not observed" : status.error)
                  << '\n';
        return 1;
    }
    return 0;
}
