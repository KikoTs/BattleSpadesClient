#include "battlespades/network/protocol168_tool_actions.hpp"

#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using namespace battlespades::network;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

std::vector<std::byte> bytes(std::initializer_list<unsigned int> values) {
    std::vector<std::byte> result;
    result.reserve(values.size());
    for (const auto value : values) {
        result.push_back(static_cast<std::byte>(value));
    }
    return result;
}

template <typename Packet>
void expect_round_trip(const Packet& packet, std::string_view label) {
    const auto encoded = encode_packet(packet);
    const auto decoded = decode_tool_action_packet(encoded);
    expect(static_cast<bool>(decoded), label);
    const auto reencoded = std::visit(
        [](const auto& value) { return encode_packet(value); }, *decoded.packet);
    expect(reencoded == encoded, label);
}

} // namespace

int main() {
    expect(encode_packet(UseCommandPacket{}) == bytes({86U}),
           "UseCommand(86) must contain only its id");
    expect_round_trip(
        DropPickupPacket{39, 2U, 14U, {1.0F, 2.0F, 3.0F},
                         {4.0F, 5.0F, 6.0F}},
        "DropPickup fixed position and velocity must round-trip");
    expect_round_trip(BlockBuildPacket{40, 2U, {100, 101, 102}, 3U},
                      "BlockBuild raw voxel layout must round-trip");
    expect_round_trip(BlockLiberatePacket{41, 2U, {100, 101, 102}},
                      "BlockLiberate raw voxel layout must round-trip");

    PlaceDynamitePacket dynamite;
    dynamite.loop_count = 0x000057C6;
    dynamite.position = {177, 263, 226};
    dynamite.face = 4U;
    expect(encode_packet(dynamite) ==
               bytes({1U, 0xC6U, 0x57U, 0U, 0U, 0xB1U, 0U, 0x07U,
                      0x01U, 0xE2U, 0U, 4U}),
           "PlaceDynamite must match the captured retail raw-voxel layout");
    expect_round_trip(dynamite, "PlaceDynamite must round-trip");

    expect_round_trip(PlaceC4Packet{42, {4, 5, 6}, 2U},
                      "PlaceC4 must round-trip");
    expect_round_trip(PlaceFlareBlockPacket{43, {7, 8, 9}},
                      "PlaceFlareBlock must round-trip");
    expect(encode_packet(PlaceLandminePacket{80, 0U, {339, 167, 227}}) ==
               bytes({89U, 0x50U, 0U, 0U, 0U, 0U, 0x53U, 0x01U,
                      0xA7U, 0U, 0xE3U, 0U}),
           "PlaceLandmine must preserve captured raw voxel coordinates");
    expect_round_trip(PlaceLandminePacket{44, 3U, {10, 11, 12}},
                      "PlaceLandmine must round-trip");
    expect_round_trip(
        PlaceMachineGunPacket{45, 4U, {13, 14, 15}, 1.25F},
        "PlaceMG must round-trip");
    expect_round_trip(PlaceMedPackPacket{46, 5U, {16, 17, 18}, 4U},
                      "PlaceMedPack must round-trip");
    expect_round_trip(
        PlaceRadarStationPacket{47, 6U, {19, 20, 21}},
        "PlaceRadarStation must round-trip");
    expect_round_trip(
        PlaceRocketTurretPacket{48, 7U, {22, 23, 24}, 2.5F},
        "PlaceRocketTurret must round-trip");
    expect_round_trip(DetonateC4Packet{49}, "DetonateC4 must round-trip");
    expect_round_trip(DisguisePacket{50, true}, "Disguise must round-trip");
    expect_round_trip(BlockSuckerPacket{51, 8U, 2U, true},
                      "BlockSucker must round-trip");
    expect_round_trip(PlaceUgcPacket{52, {123, -45, 67}, 9U, true},
                      "PlaceUGC raw voxel coordinates must round-trip");

    PaintBlockPacket paint{53, {100, 101, 102}, 0x112233U};
    const auto paint_bytes = encode_packet(paint);
    expect(paint_bytes.size() == 14U && paint_bytes[11U] == std::byte{0x33U} &&
               paint_bytes[12U] == std::byte{0x22U} &&
               paint_bytes[13U] == std::byte{0x11U},
           "PaintBlock must encode conventional RGB in retail BGR byte order");
    expect_round_trip(paint, "PaintBlock must round-trip");

    BuildPrefabActionPacket build;
    build.loop_count = 54;
    build.prefab_name = "prefab_bunker";
    build.player_id = 10U;
    build.yaw = 1U;
    build.pitch = 2U;
    build.roll = 3U;
    build.from_block_index = 4;
    build.to_block_index = 99;
    build.position = {200, 201, 202};
    build.color = 0xA1B2C3U;
    build.add_to_user_blocks = true;
    expect_round_trip(build, "BuildPrefabAction raw voxel layout must round-trip");

    ErasePrefabActionPacket erase;
    erase.loop_count = 55;
    erase.prefab_name = "prefab_bunker";
    erase.player_id = 10U;
    erase.yaw = 1U;
    erase.pitch = 2U;
    erase.roll = 3U;
    erase.from_block_index = 4;
    erase.to_block_index = 99;
    erase.position = {3.5F, 4.25F, 5.75F};
    expect_round_trip(erase,
                      "ErasePrefabAction fixed-point layout must round-trip");

    const auto malformed = encode_packet(dynamite);
    auto trailing = malformed;
    trailing.push_back(std::byte{0U});
    expect(!decode_tool_action_packet(trailing),
           "special-tool decoder must reject trailing bytes");
    expect(!decode_tool_action_packet(
               bytes({94U, 0U, 0U, 0U, 0U, 1U, 3U, 0U})),
           "BlockSucker must reject states outside off/warm/full");
    expect(!decode_tool_action_packet(
               bytes({97U, 0U, 0U, 0U, 0U, 1U, 0U, 2U, 0U,
                      3U, 0U, 9U, 2U})),
           "PlaceUGC must reject a noncanonical placing flag");
    expect(!decode_tool_action_packet(std::span<const std::byte>{}),
           "special-tool decoder must reject an empty packet");

    std::cout << "Protocol 168 special-tool packet tests passed\n";
    return EXIT_SUCCESS;
}
