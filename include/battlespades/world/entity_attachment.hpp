#pragma once

#include "battlespades/world/player_movement.hpp"

#include <cstdint>

namespace battlespades::world {

/**
 * Where an entity sits on the player it is attached to, as
 * AttachedStickyGrenadeEntity.set_target (gameScene.pyd 0x100f81a0) stores it.
 */
struct EntityAttachment final {
    std::uint8_t player_id{};
    double horizontal_offset{};
    /** Retail's default when the player has no character yet. */
    double vertical_offset{0.5};
    double yaw_offset_degrees{};
};

/** aoslib.common.to_pitch_yaw: yaw = degrees(atan2(x, y)) of a view vector. */
[[nodiscard]] double retail_yaw_degrees(Vec3 orientation) noexcept;

/**
 * set_target: offset = entity - player; vert_offset = offset.z + 0.5; the flat
 * remainder gives horiz_offset and, through to_pitch_yaw, the yaw the grenade
 * sits at relative to the player's own yaw.
 */
[[nodiscard]] EntityAttachment attach_entity_to_player(std::uint8_t player_id,
                                                       Vec3 entity_position,
                                                       Vec3 player_position,
                                                       double player_yaw_degrees) noexcept;

/**
 * update: to_rotation_vector(player.yaw + yaw_offset, 0) * horiz_offset, added
 * to the player's position, with vert_offset on z.
 */
[[nodiscard]] Vec3 attached_entity_position(const EntityAttachment& attachment,
                                            Vec3 player_position,
                                            double player_yaw_degrees) noexcept;

/** MELEE_KILL (A423): RiotShieldEntity.hit picks the melee impact for it. */
inline constexpr std::uint8_t riot_shield_melee_hit_type{2U};

} // namespace battlespades::world
