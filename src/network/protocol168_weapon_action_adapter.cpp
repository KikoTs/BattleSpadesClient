#include "battlespades/network/protocol168_weapon_action_adapter.hpp"

#include "battlespades/network/protocol168_terrain.hpp"
#include "battlespades/network/protocol168_tool_actions.hpp"
#include "battlespades/network/protocol168_weapons.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace battlespades::network {
namespace {

template <typename Packet>
void append(EncodedWeaponAction& result, const Packet& packet) {
    result.packets.push_back(encode_packet(packet));
}

[[nodiscard]] std::array<std::int16_t, 3U>
raw_position(const std::array<float, 3U>& position) noexcept {
    std::array<std::int16_t, 3U> result{};
    for (std::size_t axis{}; axis < result.size(); ++axis) {
        if (!std::isfinite(position[axis])) {
            continue;
        }
        result[axis] = static_cast<std::int16_t>(std::clamp(
            std::lround(position[axis]),
            static_cast<long>(std::numeric_limits<std::int16_t>::min()),
            static_cast<long>(std::numeric_limits<std::int16_t>::max())));
    }
    return result;
}

} // namespace

EncodedWeaponAction encode_weapon_action(
    const world::WeaponAction& action,
    const WeaponActionWireContext& context) {
    using world::WeaponActionKind;
    EncodedWeaponAction result;
    switch (action.kind) {
    case WeaponActionKind::dry_fire:
    case WeaponActionKind::block_line_begin:
    case WeaponActionKind::block_line_cancel:
    case WeaponActionKind::prefab_rotate:
    case WeaponActionKind::color_pick:
    case WeaponActionKind::throwable_primed:
    case WeaponActionKind::placement_rejected:
        result.local_only = true;
        return result;

    case WeaponActionKind::hitscan:
    case WeaponActionKind::melee:
        append(result,
               ShootPacket{context.loop_count,
                           context.player_id,
                           context.shot_on_world_update,
                           context.position,
                           context.orientation,
                           context.damage,
                           context.penetration,
                           context.affect_shooter,
                           action.secondary,
                           // DiggingTool.use_spade passes seed 0; only
                           // shoot_bullet draws randint(1, 255).
                           action.kind == WeaponActionKind::melee
                               ? std::uint8_t{0U}
                               : action.seed});
        return result;

    case WeaponActionKind::oriented_item:
        append(result,
               UseOrientedItemPacket{context.loop_count,
                                     context.player_id,
                                     action.tool_id,
                                     static_cast<float>(action.value),
                                     context.position,
                                     context.velocity});
        return result;

    case WeaponActionKind::block_line_commit:
        append(result, BlockLinePacket{context.loop_count, context.player_id,
                                       context.block_line_start,
                                       context.block_line_end});
        return result;

    case WeaponActionKind::flare_place:
        append(result,
               PlaceFlareBlockPacket{context.loop_count,
                                     raw_position(context.position)});
        return result;

    case WeaponActionKind::prefab_place:
        if (context.prefab_name.empty()) {
            result.error = "prefab placement requires a prefab name";
            return result;
        }
        append(result,
               BuildPrefabActionPacket{
                   context.loop_count,
                   context.prefab_name,
                   context.player_id,
                   context.prefab_yaw,
                   context.prefab_pitch,
                   context.prefab_roll,
                   context.prefab_from_block_index,
                   context.prefab_to_block_index,
                   raw_position(context.position),
                   context.color,
                   context.prefab_add_to_user_blocks});
        return result;

    case WeaponActionKind::deployable_place:
        switch (action.tool_id) {
        case 15U:
            append(result, PlaceMachineGunPacket{context.loop_count,
                                                  context.player_id,
                                                  raw_position(context.position),
                                                  context.yaw});
            return result;
        case 16U:
            append(result, PlaceRocketTurretPacket{context.loop_count,
                                                    context.player_id,
                                                    raw_position(context.position),
                                                    context.yaw});
            return result;
        case 20U:
            append(result, PlaceLandminePacket{context.loop_count,
                                                context.player_id,
                                                raw_position(context.position)});
            return result;
        case 21U:
            append(result, PlaceDynamitePacket{context.loop_count,
                                                raw_position(context.position),
                                                context.face});
            return result;
        case 51U:
            append(result, PlaceMedPackPacket{context.loop_count,
                                               context.player_id,
                                               raw_position(context.position),
                                               context.face});
            return result;
        case 56U:
            append(result, PlaceRadarStationPacket{context.loop_count,
                                                    context.player_id,
                                                    raw_position(context.position)});
            return result;
        case 59U:
            append(result, PlaceC4Packet{context.loop_count,
                                         raw_position(context.position),
                                         context.face});
            return result;
        default:
            result.error = "tool has no Protocol 168 deployable packet";
            return result;
        }

    case WeaponActionKind::c4_detonate:
        append(result, DetonateC4Packet{context.loop_count});
        return result;

    case WeaponActionKind::objective_use:
        if (action.tool_id == 15U) {
            append(result, UseCommandPacket{});
            return result;
        }
        if (action.tool_id == 25U || action.tool_id == 26U ||
            action.tool_id == 30U) {
            const auto pickup_id = static_cast<std::uint8_t>(
                action.tool_id == 25U ? 14U : action.tool_id == 26U ? 15U : 16U);
            append(result,
                   DropPickupPacket{context.loop_count, context.player_id,
                                    pickup_id, context.position,
                                    context.velocity});
            return result;
        }
        result.error = "objective tool has no Protocol 168 pickup mapping";
        return result;

    case WeaponActionKind::ugc_entity_use:
        append(result, PlaceUgcPacket{context.loop_count,
                                      raw_position(context.position),
                                      context.ugc_item_id,
                                      context.ugc_placing});
        return result;

    case WeaponActionKind::paint_single:
    case WeaponActionKind::paint_area: {
        auto positions = context.paint_positions;
        if (positions.empty()) {
            positions.push_back(raw_position(context.position));
        }
        if (action.kind == WeaponActionKind::paint_single && positions.size() > 1U) {
            positions.resize(1U);
        }
        for (const auto& position : positions) {
            append(result, PaintBlockPacket{context.loop_count, position,
                                             context.color});
        }
        return result;
    }

    case WeaponActionKind::block_suck:
        append(result, BlockSuckerPacket{context.loop_count, context.player_id,
                                         2U, true});
        return result;

    case WeaponActionKind::block_sucker_state:
        append(result,
               BlockSuckerPacket{
                   context.loop_count,
                   context.player_id,
                   static_cast<std::uint8_t>(std::clamp(action.value, 0.0, 2.0)),
                   false});
        return result;

    case WeaponActionKind::disguise_activate:
        append(result, DisguisePacket{context.loop_count, true});
        return result;

    case WeaponActionKind::reload_started:
    case WeaponActionKind::reload_completed:
        append(result,
               WeaponReloadPacket{
                   context.player_id,
                   action.tool_id,
                   action.kind == WeaponActionKind::reload_completed});
        return result;
    }
    result.error = "unknown weapon action";
    return result;
}

} // namespace battlespades::network
