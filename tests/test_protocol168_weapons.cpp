#include "battlespades/network/protocol168_weapons.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using namespace battlespades::network;

void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] std::vector<std::byte> bytes(std::string_view hex) {
    const auto nibble = [](char value) -> std::uint8_t {
        if (value >= '0' && value <= '9') {
            return static_cast<std::uint8_t>(value - '0');
        }
        if (value >= 'a' && value <= 'f') {
            return static_cast<std::uint8_t>(value - 'a' + 10);
        }
        throw std::runtime_error{"invalid test hex"};
    };
    expect(hex.size() % 2U == 0U, "test hex must contain whole bytes");
    std::vector<std::byte> result;
    result.reserve(hex.size() / 2U);
    for (std::size_t index{}; index < hex.size(); index += 2U) {
        result.push_back(static_cast<std::byte>(
            static_cast<std::uint8_t>((nibble(hex[index]) << 4U) |
                                      nibble(hex[index + 1U]))));
    }
    return result;
}

void expect_bytes(const std::vector<std::byte>& actual, std::string_view expected,
                  const char* message) {
    expect(actual == bytes(expected), message);
}

void outbound_packets_match_the_server_cython_oracle() {
    expect(protocol168_client_data_opaque_state(0) == 7U &&
               protocol168_client_data_opaque_state(8) == 15U &&
               protocol168_client_data_opaque_state(9) == 0U &&
               protocol168_client_data_opaque_state(448) == 7U,
           "ClientData opaque state must follow stock (loop + 7) modulo 16");

    ClientDataPacket client;
    client.loop_count = 0x01020304;
    client.player_id = 3U;
    client.palette_enabled = true;
    client.tool_id = 17U;
    client.orientation = {0.5F, -0.25F, 1.0F};
    client.opaque_state = protocol168_client_data_opaque_state(client.loop_count);
    client.movement_flags = 0x55U;
    client.action_flags = 0x55U;
    client.weapon_deployment_yaw = -1.25F;
    expect_bytes(encode_packet(client),
                 "040403020183110010008800400b55554f80",
                 "ClientData(4) differs from shared.packet.pyd");

    // Captured directly from the untouched retail Python 2 packet module.
    // These three raw shorts disambiguate stock sign-magnitude orientation
    // from ordinary signed two's-complement 3.13.
    client.loop_count = 123;
    client.player_id = 0U;
    client.palette_enabled = false;
    client.tool_id = 2U;
    client.orientation = {-1.0F, -0.25F, 0.5F};
    client.opaque_state = 0U;
    client.movement_flags = 0U;
    client.action_flags = 0U;
    client.weapon_deployment_yaw = 0.0F;
    const auto retail_orientation_packet = encode_packet(client);
    expect_bytes(retail_orientation_packet,
                 "047b000000000200c0008800100000000000",
                 "ClientData orientation differs from untouched retail");
    const auto retail_orientation_decode =
        decode_weapon_packet(retail_orientation_packet);
    expect(static_cast<bool>(retail_orientation_decode),
           "retail ClientData orientation must decode");
    const auto* decoded_client =
        std::get_if<ClientDataPacket>(&*retail_orientation_decode.packet);
    expect(decoded_client != nullptr &&
               decoded_client->orientation ==
                   std::array<float, 3U>{-1.0F, -0.25F, 0.5F},
           "retail ClientData orientation must round-trip exactly");

    ShootPacket shot;
    shot.loop_count = 0x01020304;
    shot.shooter_id = 3U;
    shot.shot_on_world_update = 0x11223344;
    shot.position = {1.0F, 2.0F, 3.0F};
    shot.orientation = {0.5F, -0.25F, 1.0F};
    shot.damage = 70;
    shot.penetration = -2;
    shot.affect_shooter = true;
    shot.secondary = true;
    shot.seed = 0x5AU;
    expect_bytes(encode_packet(shot),
                 "060403020103443322110000803f00000040000040400000003f000080be"
                 "0000803f4600feff035a",
                 "Shoot(6) differs from shared.packet.pyd");

    UseOrientedItemPacket oriented;
    oriented.loop_count = 0x01020304;
    oriented.player_id = 7U;
    oriented.tool_id = 33U;
    oriented.value = 2.5F;
    oriented.position = {1.5F, -2.25F, 3.0F};
    oriented.velocity = {0.5F, 0.25F, -0.75F};
    expect_bytes(encode_packet(oriented),
                 "0a040302010721a00060008f80c000200010002f80",
                 "UseOrientedItem(10) differs from shared.packet.pyd");

    SetClassLoadoutPacket loadout;
    loadout.player_id = 7U;
    loadout.class_id = 5U;
    loadout.instant = true;
    loadout.loadout = {2U, 17U, 11U};
    loadout.prefabs = {"wall", "ramp"};
    loadout.ugc_tools = {44U, 45U};
    expect_bytes(encode_packet(loadout),
                 "0d0705010302110b0277616c6c0072616d7000022c2d",
                 "SetClassLoadout(13) differs from shared.packet.pyd");
}

void inbound_feedback_reload_and_restock_round_trip() {
    const auto feedback_bytes = bytes("08feffffff073e04030201a5");
    auto decoded = decode_weapon_packet(feedback_bytes);
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& feedback = std::get<ShootFeedbackPacket>(*decoded.packet);
    expect(feedback.loop_count == -2 && feedback.shooter_id == 7U &&
               feedback.tool_id == 62U && feedback.seed == 0xA5U,
           "ShootFeedback must preserve tool, ordering stamp and seed");
    expect(encode_packet(feedback) == feedback_bytes,
           "ShootFeedback must round-trip byte exactly");

    const auto response_bytes = bytes("0907030160008f80c000");
    decoded = decode_weapon_packet(response_bytes);
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& response = std::get<ShootResponsePacket>(*decoded.packet);
    // Retail's negative fixed encoder adds +0.5 before truncation, so -2.25
    // intentionally quantizes to -143/64 rather than -144/64.
    expect(response.blood && response.position[0U] == 1.5F &&
               response.position[1U] == -143.0F / 64.0F &&
               response.position[2U] == 3.0F,
           "ShootResponse must decode retail's asymmetric fixed points");
    ShootResponsePacket outbound_response;
    outbound_response.damage_by = 7U;
    outbound_response.damaged = 3U;
    outbound_response.blood = true;
    outbound_response.position = {1.5F, -2.25F, 3.0F};
    expect(encode_packet(outbound_response) == response_bytes,
           "ShootResponse outbound fixed conversion must match shared.packet.pyd");

    for (const auto& packet_bytes : {bytes("4c073e01"), bytes("450703")}) {
        decoded = decode_weapon_packet(packet_bytes);
        expect(static_cast<bool>(decoded), decoded.error.c_str());
    }
    expect(encode_packet(std::get<WeaponReloadPacket>(*decode_weapon_packet(
               bytes("4c073e01")).packet)) == bytes("4c073e01"),
           "WeaponReload(76) must round-trip");
    expect(!decode_weapon_packet(bytes("4c073e02")),
           "WeaponReload must reject a noncanonical completion flag");
    expect(encode_packet(std::get<RestockPacket>(*decode_weapon_packet(
               bytes("450703")).packet)) == bytes("450703"),
           "Restock(69) must round-trip");
}

void world_update_keeps_state_flags_and_tool_in_separate_bytes() {
    std::vector<std::byte> payload = bytes("02040302010100");
    payload.push_back(std::byte{7U});
    payload.insert(payload.end(), 36U, std::byte{0U});
    payload.insert(payload.end(), 8U, std::byte{0U});
    payload.push_back(std::byte{0xAAU});
    payload.push_back(std::byte{0xBBU});
    payload.push_back(std::byte{0xCCU});
    payload.push_back(std::byte{17U});
    payload.push_back(std::byte{0xFFU});
    payload.insert(payload.end(), 4U, std::byte{0U});
    payload.push_back(std::byte{0x40U});
    payload.push_back(std::byte{0x00U});
    payload.push_back(std::byte{0U});
    payload.push_back(std::byte{0U});

    const auto decoded = decode_world_update_weapon_rows(payload);
    expect(static_cast<bool>(decoded) && decoded.rows.size() == 1U,
           "WorldUpdate weapon row must decode");
    const auto& row = decoded.rows.front();
    expect(row.state_flags == 0xCCU && row.tool_id == 17U &&
               row.pickup_id == 0xFFU && row.weapon_deployment_yaw == 1.0F,
           "WorldUpdate tool byte must never be confused with state flags");
    expect(row.position == std::array<float, 3U>{} &&
               row.orientation == std::array<float, 3U>{} &&
               row.velocity == std::array<float, 3U>{} && row.ping == 0 &&
               row.acknowledged_client_loop == 0 && row.health == 0 &&
               row.jetpack_fuel == 0.0F && row.spawn_protection == 0.0F,
           "WorldUpdate must publish its full authoritative transform/state prefix");
    expect(decoded.consumed_bytes == payload.size() - 2U,
           "WorldUpdate parser must stop exactly at entity_count");
}

void world_update_tail_carries_entity_team_and_turret_aim() {
    // No players, one team-2 landmine with the mandatory zero-RGB material
    // sentinel, followed by one authoritative turret-articulation row.
    auto payload = bytes("0278563412000001004523090207");
    const auto append = [&payload](std::string_view hex) {
        const auto value = bytes(hex);
        payload.insert(payload.end(), value.begin(), value.end());
    };
    append("40008080c000");                            // position 1,-2,3
    payload.insert(payload.end(), 6U, std::byte{0U});      // velocity
    append("8016");                                      // yaw 90
    payload.insert(payload.end(), 6U, std::byte{0U});      // color sentinel
    append("c000044080000000");
    append("01004523400b8082");

    const auto players = decode_world_update_weapon_rows(payload);
    expect(static_cast<bool>(players) && players.rows.empty(),
           "WorldUpdate player prefix must permit an empty roster");
    const auto tail = decode_world_update_tail(payload, players.consumed_bytes);
    expect(static_cast<bool>(tail), tail.error.c_str());
    expect(tail.entities.size() == 1U && tail.rocket_turrets.size() == 1U,
           "WorldUpdate suffix must retain entity and turret rows");
    const auto& mine = tail.entities.front();
    expect(mine.entity_id == 0x2345U && mine.type == 9U && mine.state == 2U &&
               mine.player_id == 7U && mine.position[0U] == 1.0F &&
               mine.position[1U] == -2.0F && mine.position[2U] == 3.0F &&
               !mine.has_explicit_color(),
           "entity state must remain the wire team and zero RGB must use it");
    const auto& turret = tail.rocket_turrets.front();
    expect(turret.entity_id == 0x2345U && turret.yaw == 45.0F &&
               turret.pitch == -10.0F,
           "turret aim must decode from the server-owned suffix");

    auto trailing = payload;
    trailing.push_back(std::byte{0U});
    expect(!decode_world_update_tail(trailing, players.consumed_bytes),
           "WorldUpdate suffix must reject trailing bytes");
}

void malformed_packets_fail_closed() {
    const auto valid = bytes("08feffffff073e04030201a5");
    auto truncated = valid;
    truncated.pop_back();
    expect(!decode_weapon_packet(truncated),
           "truncated ShootFeedback must fail closed");
    auto trailing = valid;
    trailing.push_back(std::byte{0U});
    expect(!decode_weapon_packet(trailing),
           "fixed-size weapon packet with trailing data must fail closed");
    expect(!decode_weapon_packet(bytes("ff")),
           "unknown packet must not be interpreted as a weapon packet");
}

} // namespace

int main() {
    try {
        expect(world_player_zoomed(0x40U),
               "WorldUpdate zoom must use the recovered server-to-client 0x40 bit");
        expect(!world_player_zoomed(0x04U),
               "WorldUpdate jetpack bit must never be mistaken for observer zoom");
        outbound_packets_match_the_server_cython_oracle();
        inbound_feedback_reload_and_restock_round_trip();
        world_update_keeps_state_flags_and_tool_in_separate_bytes();
        world_update_tail_carries_entity_team_and_turret_aim();
        malformed_packets_fail_closed();
        std::cout << "Protocol 168 weapon parity tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
