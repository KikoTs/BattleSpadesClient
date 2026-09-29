#include "battlespades/network/protocol168_weapon_action_adapter.hpp"
#include "battlespades/network/protocol168_terrain.hpp"
#include "battlespades/network/protocol168_tool_actions.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

using battlespades::network::WeaponActionWireContext;
using battlespades::network::encode_weapon_action;
using battlespades::world::WeaponAction;
using battlespades::world::WeaponActionKind;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

std::uint8_t packet_id(const battlespades::network::EncodedWeaponAction& result) {
    expect(static_cast<bool>(result) && result.packets.size() == 1U &&
               !result.packets.front().empty(),
           "action must produce exactly one packet");
    return std::to_integer<std::uint8_t>(result.packets.front().front());
}

WeaponAction action(WeaponActionKind kind, std::uint8_t tool_id = 0U,
                    double value = 0.0) {
    return WeaponAction{kind, tool_id, 17U, 1U, false, value};
}

} // namespace

int main() {
    WeaponActionWireContext context;
    context.loop_count = 1234;
    context.shot_on_world_update = 1200;
    context.player_id = 7U;
    context.position = {10.0F, 20.0F, 30.0F};
    context.orientation = {0.0F, 1.0F, 0.0F};
    context.velocity = {1.0F, 2.0F, 3.0F};
    context.damage = 25;
    context.penetration = 2;
    context.face = 4U;
    context.yaw = 1.5F;
    context.color = 0x123456U;
    context.block_line_start = {1, 2, 3};
    context.block_line_end = {4, 5, 6};
    context.prefab_name = "prefab_bunker";
    context.prefab_yaw = 3U;
    context.prefab_to_block_index = 100;
    context.ugc_item_id = 4U;

    expect(packet_id(encode_weapon_action(action(WeaponActionKind::hitscan, 6U),
                                          context)) == 6U,
           "firearms must map to Shoot(6)");
    expect(packet_id(encode_weapon_action(action(WeaponActionKind::melee, 1U),
                                          context)) == 6U,
           "melee hit proposals must map to Shoot(6)");
    // Retail DiggingTool.use_spade sends seed 0; shoot_bullet sends its draw.
    expect(std::to_integer<std::uint8_t>(
               encode_weapon_action(action(WeaponActionKind::melee, 2U), context)
                   .packets.front().back()) == 0U,
           "melee Shoot(6) must carry the retail zero seed");
    expect(std::to_integer<std::uint8_t>(
               encode_weapon_action(action(WeaponActionKind::hitscan, 6U), context)
                   .packets.front().back()) == 17U,
           "firearm Shoot(6) must carry the runtime's shot seed");
    expect(packet_id(encode_weapon_action(
               action(WeaponActionKind::oriented_item, 12U, 75.0), context)) == 10U,
           "launchers and throwables must map to UseOrientedItem(10)");
    const auto block_line = encode_weapon_action(
        action(WeaponActionKind::block_line_commit, 5U), context);
    expect(packet_id(block_line) == 40U,
           "block drag commit must map to BlockLine(40)");
    const auto line_decoded =
        battlespades::network::decode_terrain_packet(block_line.packets.front());
    const auto* line_packet =
        line_decoded
            ? std::get_if<battlespades::network::BlockLinePacket>(
                  &*line_decoded.packet)
            : nullptr;
    expect(line_packet != nullptr &&
               line_packet->start == context.block_line_start &&
               line_packet->end == context.block_line_end,
           "BlockLine must preserve the distinct press and release endpoints");
    expect(packet_id(encode_weapon_action(
               action(WeaponActionKind::flare_place, 22U), context)) == 104U,
           "flare blocks must map to PlaceFlareBlock(104)");
    const auto prefab = encode_weapon_action(
        action(WeaponActionKind::prefab_place, 23U), context);
    expect(packet_id(prefab) == 30U,
           "prefabs must map to BuildPrefabAction(30)");
    const auto prefab_decoded =
        battlespades::network::decode_tool_action_packet(prefab.packets.front());
    const auto* prefab_packet =
        prefab_decoded
            ? std::get_if<battlespades::network::BuildPrefabActionPacket>(
                  &*prefab_decoded.packet)
            : nullptr;
    expect(prefab_packet != nullptr &&
               prefab_packet->prefab_name == context.prefab_name &&
               prefab_packet->yaw == 3U &&
               prefab_packet->position ==
                   std::array<std::int16_t, 3U>{10, 20, 30},
           "prefab packet must preserve selected name, rotation and raw origin");

    constexpr std::pair<std::uint8_t, std::uint8_t> deployables[]{
        {std::uint8_t{15U}, std::uint8_t{87U}},
        {std::uint8_t{16U}, std::uint8_t{88U}},
        {std::uint8_t{20U}, std::uint8_t{89U}},
        {std::uint8_t{21U}, std::uint8_t{1U}},
        {std::uint8_t{51U}, std::uint8_t{90U}},
        {std::uint8_t{56U}, std::uint8_t{91U}},
        {std::uint8_t{59U}, std::uint8_t{92U}}};
    for (const auto& [tool, expected_id] : deployables) {
        expect(packet_id(encode_weapon_action(
                   action(WeaponActionKind::deployable_place, tool), context)) ==
                   expected_id,
               "every deployable must select its distinct stock packet");
    }
    const auto mine_encoded = encode_weapon_action(
        action(WeaponActionKind::deployable_place, 20U), context);
    const auto mine_decoded = battlespades::network::decode_tool_action_packet(
        mine_encoded.packets.front());
    const auto* mine_packet =
        mine_decoded
            ? std::get_if<battlespades::network::PlaceLandminePacket>(
                  &*mine_decoded.packet)
            : nullptr;
    expect(mine_packet != nullptr &&
               mine_packet->position ==
                   std::array<std::int16_t, 3U>{10, 20, 30},
           "deployable adapter must encode literal voxel shorts, not 1/64 positions");
    const auto dynamite_encoded = encode_weapon_action(
        action(WeaponActionKind::deployable_place, 21U), context);
    const auto dynamite_decoded = battlespades::network::decode_tool_action_packet(
        dynamite_encoded.packets.front());
    const auto* dynamite_packet =
        dynamite_decoded
            ? std::get_if<battlespades::network::PlaceDynamitePacket>(
                  &*dynamite_decoded.packet)
            : nullptr;
    expect(dynamite_packet != nullptr && dynamite_packet->face == 4U &&
               dynamite_packet->position ==
                   std::array<std::int16_t, 3U>{10, 20, 30},
           "surface deployables must preserve both raw voxel and face");

    expect(packet_id(encode_weapon_action(
               action(WeaponActionKind::c4_detonate, 59U), context)) == 93U,
           "C4 secondary must map to DetonateC4(93)");
    expect(packet_id(encode_weapon_action(
               action(WeaponActionKind::objective_use, 25U), context)) == 71U,
           "bomb drop must map to DropPickup(71)");
    expect(packet_id(encode_weapon_action(
               action(WeaponActionKind::objective_use, 26U), context)) == 71U,
           "diamond drop must map to DropPickup(71)");
    expect(packet_id(encode_weapon_action(
               action(WeaponActionKind::objective_use, 30U), context)) == 71U,
           "intel drop must map to DropPickup(71)");
    expect(packet_id(encode_weapon_action(
               action(WeaponActionKind::objective_use, 15U), context)) == 86U,
           "mounted MG use toggle must map to UseCommand(86)");
    expect(packet_id(encode_weapon_action(
               action(WeaponActionKind::ugc_entity_use, 41U), context)) == 97U,
           "UGC entity tool must map to PlaceUGC(97)");

    context.paint_positions = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
    auto encoded = encode_weapon_action(
        action(WeaponActionKind::paint_area, 43U), context);
    expect(encoded && encoded.packets.size() == 3U,
           "paint area must encode every validated target voxel");
    encoded = encode_weapon_action(action(WeaponActionKind::paint_single, 43U),
                                   context);
    expect(encoded && encoded.packets.size() == 1U,
           "paint primary must encode only one target voxel");

    expect(packet_id(encode_weapon_action(
               action(WeaponActionKind::block_suck, 63U), context)) == 94U,
           "Block Sucker pulse must map to packet 94");
    expect(packet_id(encode_weapon_action(
               action(WeaponActionKind::block_sucker_state, 63U, 2.0), context)) ==
               94U,
           "Block Sucker state must map to packet 94");
    expect(packet_id(encode_weapon_action(
               action(WeaponActionKind::disguise_activate, 64U), context)) == 95U,
           "disguise activation must map to packet 95");
    expect(packet_id(encode_weapon_action(
               action(WeaponActionKind::reload_started, 7U), context)) == 76U,
           "reload start must map to WeaponReload(76)");
    expect(packet_id(encode_weapon_action(
               action(WeaponActionKind::reload_completed, 7U), context)) == 76U,
           "reload completion must map to WeaponReload(76)");

    for (const auto local : {WeaponActionKind::dry_fire,
                             WeaponActionKind::block_line_begin,
                             WeaponActionKind::block_line_cancel,
                             WeaponActionKind::prefab_rotate,
                             WeaponActionKind::color_pick}) {
        encoded = encode_weapon_action(action(local), context);
        expect(encoded && encoded.local_only && encoded.packets.empty(),
               "local presentation edge must not emit a wire packet");
    }

    context.prefab_name.clear();
    expect(!encode_weapon_action(action(WeaponActionKind::prefab_place, 23U),
                                 context),
           "prefab placement without a name must fail closed");
    expect(!encode_weapon_action(
               action(WeaponActionKind::deployable_place, 40U), context),
           "unknown deployable tool must fail closed");

    std::cout << "Weapon action packet-adapter tests passed\n";
    return EXIT_SUCCESS;
}
