#include "battlespades/world/entity_attachment.hpp"

#include <cmath>
#include <numbers>

namespace battlespades::world {
namespace {

constexpr double degrees_per_radian{180.0 / std::numbers::pi};

} // namespace

double retail_yaw_degrees(Vec3 orientation) noexcept {
    if (orientation.x == 0.0 && orientation.y == 0.0) return 0.0;
    return std::atan2(orientation.x, orientation.y) * degrees_per_radian;
}

EntityAttachment attach_entity_to_player(std::uint8_t player_id,
                                         Vec3 entity_position,
                                         Vec3 player_position,
                                         double player_yaw_degrees) noexcept {
    EntityAttachment attachment;
    attachment.player_id = player_id;
    const Vec3 offset{entity_position.x - player_position.x,
                      entity_position.y - player_position.y,
                      entity_position.z - player_position.z};
    attachment.vertical_offset = offset.z + 0.5;
    attachment.horizontal_offset = std::hypot(offset.x, offset.y);
    const double offset_yaw = retail_yaw_degrees({offset.x, offset.y, 0.0});
    attachment.yaw_offset_degrees = offset_yaw - player_yaw_degrees;
    return attachment;
}

Vec3 attached_entity_position(const EntityAttachment& attachment,
                              Vec3 player_position,
                              double player_yaw_degrees) noexcept {
    const double yaw = (player_yaw_degrees + attachment.yaw_offset_degrees) / degrees_per_radian;
    return {player_position.x + std::sin(yaw) * attachment.horizontal_offset,
            player_position.y + std::cos(yaw) * attachment.horizontal_offset,
            player_position.z + attachment.vertical_offset};
}

} // namespace battlespades::world
