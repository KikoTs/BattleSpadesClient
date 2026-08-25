#include "battlespades/network/live_protocol168_connection.hpp"
#include "battlespades/network/protocol168_players.hpp"
#include "battlespades/network/protocol168_runtime.hpp"
#include "battlespades/network/protocol168_session.hpp"
#include "battlespades/network/protocol168_weapon_action_adapter.hpp"
#include "battlespades/network/protocol168_weapons.hpp"
#include "battlespades/world/class_selection.hpp"
#include "battlespades/world/weapon_runtime.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace battlespades;

struct PlacementCase final {
    const char* name;
    std::uint8_t class_id;
    std::size_t equipment_index;
    std::uint8_t tool_id;
    std::uint8_t entity_type;
};

constexpr std::array cases{
    PlacementCase{"rocket_turret", 12U, 0U, 16U, 8U},
    PlacementCase{"landmine", 1U, 0U, 20U, 9U},
    PlacementCase{"dynamite", 3U, 0U, 21U, 10U},
    PlacementCase{"medpack", 17U, 0U, 51U, 30U},
    PlacementCase{"radar", 1U, 1U, 56U, 36U},
    PlacementCase{"c4", 3U, 1U, 59U, 38U},
};

[[nodiscard]] bool wait_for_bootstrap(
    network::LiveProtocol168Connection& connection,
    std::unique_ptr<network::Protocol168WorldBootstrap>& bootstrap) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds{35};
    while (std::chrono::steady_clock::now() < deadline && bootstrap == nullptr) {
        const auto status = connection.status();
        if (status.phase == network::LiveProtocol168Phase::failed ||
            status.phase == network::LiveProtocol168Phase::disconnected) {
            std::cerr << status.error << '\n';
            return false;
        }
        bootstrap = connection.take_bootstrap();
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return bootstrap != nullptr && bootstrap->map != nullptr;
}

[[nodiscard]] std::optional<std::array<std::int16_t, 3U>>
support_cell(const world::VxlMap& map,
             const network::CreatePlayerPacket& player) {
    const auto origin_x = static_cast<int>(std::lround(player.position[0U]));
    const auto origin_y = static_cast<int>(std::lround(player.position[1U]));
    const auto origin_z = static_cast<int>(std::floor(player.position[2U]));
    for (int radius{}; radius <= 2; ++radius) {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                const int x = origin_x + dx;
                const int y = origin_y + dy;
                if (x < 0 || y < 0 || x >= static_cast<int>(world::VxlMap::width) ||
                    y >= static_cast<int>(world::VxlMap::depth)) {
                    continue;
                }
                for (int z = std::max(0, origin_z); z < 239; ++z) {
                    if (!map.solid(static_cast<std::uint32_t>(x),
                                   static_cast<std::uint32_t>(y),
                                   static_cast<std::uint32_t>(z))) {
                        continue;
                    }
                    const double ddx = static_cast<double>(x) - player.position[0U];
                    const double ddy = static_cast<double>(y) - player.position[1U];
                    const double ddz = static_cast<double>(z) - player.position[2U];
                    if (ddx * ddx + ddy * ddy + ddz * ddz <= 25.0) {
                        return std::array<std::int16_t, 3U>{
                            static_cast<std::int16_t>(x),
                            static_cast<std::int16_t>(y),
                            static_cast<std::int16_t>(z)};
                    }
                    break;
                }
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool run_case(const char* host, std::uint16_t port,
                            const PlacementCase& test) {
    network::Protocol168SessionConfig config;
    config.player_name = std::string{"Place_"} + test.name;
    config.team = 2U;
    config.class_id = test.class_id;
    config.auto_join = false;

    network::LiveProtocol168Connection connection;
    if (!connection.start(network::EnetProtocol168Config{host, port, 30'000U},
                          config)) {
        std::cerr << test.name << ": " << connection.status().error << '\n';
        return false;
    }

    std::unique_ptr<network::Protocol168WorldBootstrap> bootstrap;
    if (!wait_for_bootstrap(connection, bootstrap)) {
        std::cerr << test.name << ": world bootstrap failed\n";
        connection.stop();
        return false;
    }

    std::array<std::size_t, 4U> option_indices{};
    option_indices[3U] = test.equipment_index;
    const auto selection = world::make_class_selection(
        test.class_id, option_indices, std::span<const std::string>{});
    if (std::ranges::find(selection.loadout, test.tool_id) ==
        selection.loadout.end()) {
        std::cerr << test.name << ": client class catalog omitted tool "
                  << static_cast<unsigned>(test.tool_id) << '\n';
        connection.stop();
        return false;
    }

    network::SetClassLoadoutPacket loadout;
    loadout.player_id = bootstrap->local_player_id;
    loadout.class_id = selection.class_id;
    loadout.instant = true;
    loadout.loadout = selection.loadout;
    loadout.prefabs = selection.prefabs;
    loadout.ugc_tools = selection.ugc_tools;
    if (!connection.send(network::encode_packet(loadout)) ||
        !connection.send(network::encode_protocol168_new_player_connection(config))) {
        std::cerr << test.name << ": failed to send join transaction\n";
        connection.stop();
        return false;
    }

    std::optional<network::CreatePlayerPacket> local_player;
    const auto join_deadline = std::chrono::steady_clock::now() +
                               std::chrono::seconds{8};
    while (std::chrono::steady_clock::now() < join_deadline &&
           !local_player.has_value()) {
        for (const auto& packet : connection.take_inbound()) {
            if (packet.empty() ||
                std::to_integer<std::uint8_t>(packet.front()) !=
                    network::CreatePlayerPacket::id) {
                continue;
            }
            const auto decoded = network::decode_create_player(packet);
            if (decoded && decoded.packet->player_id == bootstrap->local_player_id) {
                local_player = *decoded.packet;
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    if (!local_player.has_value() || local_player->class_id != test.class_id ||
        std::ranges::find(local_player->loadout, test.tool_id) ==
            local_player->loadout.end()) {
        std::cerr << test.name
                  << ": server did not spawn the requested class/loadout\n";
        connection.stop();
        return false;
    }

    const auto cell = support_cell(*bootstrap->map, *local_player);
    if (!cell.has_value()) {
        std::cerr << test.name << ": no in-range solid placement cell\n";
        connection.stop();
        return false;
    }

    std::int32_t loop{};
    bool sent{};
    bool acknowledged{};
    bool created_with_team_material{};
    bool turret_aim_seen = test.entity_type != 8U;
    std::optional<std::uint16_t> created_entity_id;
    std::optional<std::uint8_t> created_state;
    std::array<float, 3U> created_color{};
    const auto action_deadline = std::chrono::steady_clock::now() +
                                 std::chrono::seconds{4};
    while (std::chrono::steady_clock::now() < action_deadline && !acknowledged) {
        for (const auto& packet : connection.take_inbound()) {
            if (packet.empty()) {
                continue;
            }
            const auto id = std::to_integer<std::uint8_t>(packet.front());
            if (id == 2U && created_entity_id.has_value() &&
                test.entity_type == 8U) {
                const auto players = network::decode_world_update_weapon_rows(packet);
                const auto tail = players
                                      ? network::decode_world_update_tail(
                                            packet, players.consumed_bytes)
                                      : network::WorldUpdateTailResult{};
                if (tail && std::ranges::any_of(
                                tail.rocket_turrets,
                                [&](const auto& row) {
                                    return row.entity_id == *created_entity_id;
                                })) {
                    turret_aim_seen = true;
                }
                acknowledged = created_with_team_material && turret_aim_seen;
                continue;
            }
            if (id != network::CreateEntityPacket::id) {
                continue;
            }
            const auto decoded = network::decode_runtime_packet(packet);
            const auto* created = decoded
                                      ? std::get_if<network::CreateEntityPacket>(
                                            &*decoded.packet)
                                      : nullptr;
            if (created != nullptr && created->type == test.entity_type &&
                created->player_id == bootstrap->local_player_id) {
                created_entity_id = created->entity_id;
                created_state = created->state;
                created_color = created->color;
                created_with_team_material =
                    // Team balancing may override the requested team during
                    // join. Compare against the authoritative CreatePlayer,
                    // not the preference sent in the handshake.
                    created->state == local_player->team &&
                    (!created->has_explicit_color() ||
                     (test.entity_type != 8U && test.entity_type != 9U));
                acknowledged = created_with_team_material && turret_aim_seen;
            }
        }

        network::ClientDataPacket input;
        input.loop_count = loop++;
        input.player_id = bootstrap->local_player_id;
        input.tool_id = test.tool_id;
        input.orientation = {0.0F, 0.0F, 1.0F};
        input.opaque_state =
            network::protocol168_client_data_opaque_state(input.loop_count);
        input.action_flags = 0x10U;
        if (!connection.send(network::encode_packet(input))) {
            std::cerr << test.name << ": ClientData send failed\n";
            break;
        }

        if (!sent && loop >= 12) {
            world::WeaponAction action;
            action.kind = world::WeaponActionKind::deployable_place;
            action.tool_id = test.tool_id;
            network::WeaponActionWireContext wire;
            wire.loop_count = loop - 1;
            wire.player_id = bootstrap->local_player_id;
            wire.position = {static_cast<float>((*cell)[0U]),
                             static_cast<float>((*cell)[1U]),
                             static_cast<float>((*cell)[2U])};
            wire.orientation = input.orientation;
            wire.face = 4U;
            wire.yaw = 0.0F;
            const auto encoded = network::encode_weapon_action(action, wire);
            if (!encoded || encoded.packets.size() != 1U ||
                !connection.send(encoded.packets.front())) {
                std::cerr << test.name << ": placement packet send failed\n";
                break;
            }
            sent = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{16});
    }

    connection.stop();
    std::cout << test.name << " class=" << static_cast<unsigned>(test.class_id)
              << " tool=" << static_cast<unsigned>(test.tool_id)
              << " entity=" << static_cast<unsigned>(test.entity_type)
              << " cell=" << (*cell)[0U] << ',' << (*cell)[1U] << ','
              << (*cell)[2U]
              << " state="
              << (created_state.has_value()
                      ? std::to_string(static_cast<unsigned>(*created_state))
                      : std::string{"missing"})
              << " rgb=" << created_color[0U] << ',' << created_color[1U]
              << ',' << created_color[2U]
              << " team_material=" << (created_with_team_material ? "yes" : "no")
              << " turret_aim=" << (turret_aim_seen ? "yes" : "no") << ' '
              << (acknowledged ? "PASS" : "FAIL")
              << '\n';
    return acknowledged;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: aos_protocol168_deployable_live_smoke HOST PORT\n";
        return 2;
    }
    const auto parsed_port = std::strtoul(argv[2], nullptr, 10);
    if (parsed_port == 0UL || parsed_port > 65'535UL) {
        return 2;
    }
    bool passed{true};
    for (const auto& test : cases) {
        passed = run_case(argv[1], static_cast<std::uint16_t>(parsed_port), test) &&
                 passed;
    }
    return passed ? 0 : 1;
}
