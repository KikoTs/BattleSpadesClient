#include "battlespades/world/local_entity.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace battlespades::world {
namespace {

constexpr double degrees_per_turn{360.0};
constexpr double half_turn{180.0};
constexpr double entity_gravity{32.0};
constexpr double maximum_entity_speed{511.98999};
constexpr double maximum_sweep_distance{0.2};
// Retail GenericMovement reverses a bouncing object's vertical velocity at
// half strength. Corpse fragments use their separately recovered 0.1 value.
constexpr double grave_bounce_restitution{0.5};
constexpr double corpse_bounce_restitution{0.1};
// Without the native engine's float/contact damping, infinitesimal rebounds
// can chatter forever on one voxel. Retail's collision audio uses 2 as its
// meaningful-impact threshold, so sub-threshold rebounds settle silently.
constexpr double minimum_bounce_speed{2.0};
constexpr std::size_t maximum_sweep_steps{4096U};

[[nodiscard]] bool finite(Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool near_zero(Vec3 value) noexcept {
    constexpr double epsilon{1.0e-9};
    return std::abs(value.x) <= epsilon && std::abs(value.y) <= epsilon &&
           std::abs(value.z) <= epsilon;
}

/** Point collision for an entity transform in map coordinates. */
[[nodiscard]] bool solid_at(const VxlMap& map, Vec3 position) noexcept {
    if (!finite(position) || position.x < 0.0 || position.y < 0.0 ||
        position.x >= static_cast<double>(VxlMap::width) ||
        position.y >= static_cast<double>(VxlMap::depth) ||
        position.z >= static_cast<double>(VxlMap::height)) {
        return true;
    }
    // The sky is open. This also lets an entity created just above z=0 fall
    // into the authored map instead of colliding with an imaginary ceiling.
    if (position.z < 0.0) {
        return false;
    }
    return map.solid(static_cast<std::uint32_t>(std::floor(position.x)),
                     static_cast<std::uint32_t>(std::floor(position.y)),
                     static_cast<std::uint32_t>(std::floor(position.z)));
}

[[nodiscard]] double support_voxel_z(double value) noexcept {
    return std::clamp(std::floor(value), 0.0, static_cast<double>(VxlMap::height - 1U));
}

[[nodiscard]] double bounce_restitution(const EntityDefinition& definition) noexcept {
    if (definition.type_id == 11U)
        return grave_bounce_restitution;
    if (definition.type_id == 12U)
        return corpse_bounce_restitution;
    return 0.0;
}

} // namespace

bool uses_entity_terrain_gravity(const EntityDefinition& definition) noexcept {
    switch (definition.category) {
    case EntityCategory::pickup:
    case EntityCategory::objective:
        return true;
    case EntityCategory::deployable:
        // The riot shield is parented to its player. Every other portable
        // deployable is terrain-owned once it is no longer a flying projectile.
        return definition.type_id != 39U;
    case EntityCategory::hazard:
        // Flare blocks and block fire are voxel effects; type 35 is parented to
        // the player it hit. Graves, corpses and airstrike shells are physical.
        return definition.type_id == 11U || definition.type_id == 12U || definition.type_id == 17U;
    case EntityCategory::marker:
    case EntityCategory::structure:
    case EntityCategory::projectile:
    case EntityCategory::unportable:
        return false;
    }
    return false;
}

TimedExplosivePresentation timed_explosive_presentation(
    const LocalEntity& entity) noexcept {
    TimedExplosivePresentation result;
    // Type 10 is dynamite. Type 38 is C4 and has no retail fuse: secondary
    // fire remotely detonates every placed charge, so it must never acquire a
    // fabricated countdown merely because a malformed packet supplied fuse.
    if (!entity.alive || entity.type != 10U || !std::isfinite(entity.fuse) ||
        entity.fuse <= 0.0) {
        return result;
    }
    result.visible = true;
    result.seconds = static_cast<std::uint8_t>(std::clamp(
        std::ceil(entity.fuse), 1.0,
        static_cast<double>(std::numeric_limits<std::uint8_t>::max())));
    // Lift the canvas above the supporting voxel. XY is centred because packet
    // 90 stores the support cell, not an already-centred render coordinate.
    result.label_position = {entity.position.x + 0.5, entity.position.y + 0.5,
                             entity.position.z - 1.0};
    result.ticking_sound = "dynamite_tick";
    return result;
}

EntityPhysicsStep step_entity_terrain_physics(LocalEntity& entity,
                                              const EntityDefinition& definition,
                                              const VxlMap& map,
                                              double dt) noexcept {
    EntityPhysicsStep result;
    if (!uses_entity_terrain_gravity(definition) || !entity.alive || !std::isfinite(dt) ||
        dt <= 0.0 || !finite(entity.position) || !finite(entity.velocity)) {
        return result;
    }

    if (entity.attached) {
        if (solid_at(map, entity.position)) {
            entity.velocity = {};
            return result;
        }
        entity.attached = false;
        entity.grounded = false;
        entity.face = 4U;
        result.support_lost = true;
    }

    if (entity.grounded) {
        if (solid_at(map, entity.position)) {
            entity.velocity = {};
            return result;
        }
        entity.grounded = false;
        result.support_lost = true;
    } else if (near_zero(entity.velocity) && solid_at(map, entity.position)) {
        // Network and local placement both use the supporting SOLID voxel as
        // the transform anchor. Recognise it without applying a second drop.
        entity.grounded = true;
        entity.face = 4U;
        return result;
    }

    const auto predicted_vertical_speed =
        std::min(entity.velocity.z + entity_gravity * dt, maximum_entity_speed);
    const auto maximum_displacement = std::max({std::abs(entity.velocity.x * dt),
                                                std::abs(entity.velocity.y * dt),
                                                std::abs(predicted_vertical_speed * dt)});
    const auto requested_steps = static_cast<std::size_t>(
        std::max(1.0, std::ceil(maximum_displacement / maximum_sweep_distance)));
    const auto steps = std::min(requested_steps, maximum_sweep_steps);
    const auto step_dt = dt / static_cast<double>(steps);

    for (std::size_t step = 0U; step < steps; ++step) {
        entity.velocity.z =
            std::min(entity.velocity.z + entity_gravity * step_dt, maximum_entity_speed);

        Vec3 candidate = entity.position;
        candidate.x += entity.velocity.x * step_dt;
        if (solid_at(map, candidate)) {
            entity.velocity.x = 0.0;
        } else {
            result.moved = result.moved || candidate.x != entity.position.x;
            entity.position.x = candidate.x;
        }

        candidate = entity.position;
        candidate.y += entity.velocity.y * step_dt;
        if (solid_at(map, candidate)) {
            entity.velocity.y = 0.0;
        } else {
            result.moved = result.moved || candidate.y != entity.position.y;
            entity.position.y = candidate.y;
        }

        candidate = entity.position;
        candidate.z += entity.velocity.z * step_dt;
        if (!solid_at(map, candidate)) {
            result.moved = result.moved || candidate.z != entity.position.z;
            entity.position.z = candidate.z;
            continue;
        }

        if (entity.velocity.z >= 0.0) {
            // Face-4 rendering subtracts half a block. Store the SOLID cell,
            // not the last air cell, or the model visibly levitates by one.
            entity.position.z = support_voxel_z(candidate.z);
            entity.face = 4U;
            result.moved = true;
            result.landed = true;
            result.impact_speed = entity.velocity.z;
            const auto rebound = result.impact_speed * bounce_restitution(definition);
            if (rebound >= minimum_bounce_speed) {
                // GenericMovement changes only vertical speed on a bounce.
                // Preserve lateral momentum so a falling grave can tumble
                // away from the ledge that launched it.
                entity.velocity.z = -rebound;
                entity.grounded = false;
                result.bounced = true;
            } else {
                entity.velocity = {};
                entity.grounded = true;
            }
            break;
        }
        // Head contact while moving upward: keep the last safe position and
        // allow gravity to pull the entity back down on subsequent substeps.
        entity.velocity.z = 0.0;
    }

    return result;
}

bool within_touch_radius(const LocalEntity& entity, const Vec3& player, float radius) noexcept {
    if (!entity.alive || radius <= 0.0F) {
        return false;
    }
    const auto dx = player.x - entity.position.x;
    const auto dy = player.y - entity.position.y;
    const auto dz = player.z - entity.position.z;
    const auto limit = static_cast<double>(radius);
    return dx * dx + dy * dy + dz * dz <= limit * limit;
}

bool within_trip_volume(const LocalEntity& entity,
                        const Vec3& player,
                        float radius,
                        float layers) noexcept {
    if (!entity.alive || radius <= 0.0F) {
        return false;
    }
    const auto dx = player.x - entity.position.x;
    const auto dy = player.y - entity.position.y;
    const auto limit = static_cast<double>(radius);
    if (dx * dx + dy * dy > limit * limit) {
        return false;
    }
    // Vertical extent is counted in whole voxel layers rather than distance,
    // which is why this is not simply folded into the sphere test above.
    return std::abs(player.z - entity.position.z) <= static_cast<double>(layers);
}

Vec3 entity_face_normal(std::uint8_t face) noexcept {
    switch (face) {
    case 0U:
        return {-1.0, 0.0, 0.0};
    case 1U:
        return {1.0, 0.0, 0.0};
    case 2U:
        return {0.0, -1.0, 0.0};
    case 3U:
        return {0.0, 1.0, 0.0};
    case 5U:
        return {0.0, 0.0, 1.0};
    case 4U:
    default:
        // The block's TOP, because z grows downward. Also the default face, so
        // an unset or out-of-range value lands on the ground case rather than
        // silently burying the entity in a ceiling.
        return {0.0, 0.0, -1.0};
    }
}

EntityFaceRotation entity_face_rotation(std::uint8_t face) noexcept {
    // Retail expresses these in GL axes, where GL_X = map +X, GL_Y = map -Z and
    // GL_Z = map +Y. Converted to map axes here so the renderer never has to
    // hold two conventions at once: a GL Z-rotation is a map Y-rotation.
    switch (face) {
    case 0U:
        return {1U, 90.0F};
    case 1U:
        return {1U, -90.0F};
    case 2U:
        return {0U, -90.0F};
    case 3U:
        return {0U, 90.0F};
    case 5U:
        return {0U, 180.0F};
    case 4U:
    default:
        return {0U, 0.0F};
    }
}

Vec3 entity_presentation_position(const LocalEntity& entity, const EntityModelPart& part) noexcept {
    auto x = entity.position.x;
    auto y = entity.position.y;
    const auto z = entity.position.z;

    // rocketTurret.py centres the entire assembly before Entity.set_position.
    if (entity.type == 8U) {
        x -= 0.5;
        y -= 0.5;
    }
    // GraveEntity.update owns a GenericMovement object and copies its position
    // to the display with this exact centring, bypassing Entity.set_position.
    if (entity.type == 11U) {
        return {x - 0.5, y - 0.5, z - 0.5};
    }

    // LandmineWeapon.draw_ghosting places the mine at the centre of the hit
    // voxel's top face: (x + 0.5, y + 0.5, z).  PlaceLandmine(89) and the
    // resulting CreateEntity retain the integer support voxel, so feeding the
    // generic face standoff below an uncentred (x,y) draws the mine on the
    // shared corner of four blocks.  Keep this correction presentation-only;
    // trip distance and the authoritative blast continue to use packet space.
    if (entity.type == 9U && entity.face == 4U) {
        return {x + 0.5 + static_cast<double>(part.offset[0U]),
                y + 0.5 + static_cast<double>(part.offset[1U]),
                z - 0.5 + static_cast<double>(part.offset[2U])};
    }

    const auto ox = static_cast<double>(part.offset[0U]);
    const auto oy = static_cast<double>(part.offset[1U]);
    const auto oz = static_cast<double>(part.offset[2U]);
    switch (entity.face) {
    case 0U:
        return {x - 0.5 + oz, y + ox, z - oy};
    case 1U:
        return {x + 0.5 - oz, y + ox, z - oy};
    case 2U:
        return {x + oy, y - 0.5 + oz, z - ox};
    case 3U:
        return {x + oy, y + 0.5 - oz, z - ox};
    case 5U:
        return {x + ox, y + oy, z + 0.5 - oz};
    case 4U:
    default:
        return {x + ox, y + oy, z - 0.5 + oz};
    }
}

double entity_vertical_contact_adjustment(const LocalEntity& entity,
                                          const EntityDefinition& definition,
                                          const EntityModelPart& contact_part,
                                          float mesh_minimum_y) noexcept {
    if (entity.face != 4U || !uses_entity_terrain_gravity(definition) ||
        !std::isfinite(mesh_minimum_y) || !std::isfinite(definition.model_size) ||
        !std::isfinite(contact_part.scale) || definition.model_size <= 0.0F ||
        contact_part.scale <= 0.0F) {
        return 0.0;
    }

    // Kv6Model::mesh stores authored axes as (x, -z, y). The entity draw path
    // applies Rx(-90), converting the mesh's minimum Y into its greatest map Z
    // (the physical bottom because map Z grows downward). This relationship is
    // constant for the whole fall: grounded is a collision state, not a change
    // in the KV6 pivot. Keeping the adjustment continuous removes the final
    // half-block correction strike after a grave's diminishing rebounds.
    const auto contact_origin = entity_presentation_position(entity, contact_part);
    const auto model_scale =
        static_cast<double>(definition.model_size) * static_cast<double>(contact_part.scale);
    const auto visual_bottom_z =
        contact_origin.z - static_cast<double>(mesh_minimum_y) * model_scale;
    return entity.position.z - visual_bottom_z;
}

std::optional<Vec3> health_crate_spot_shadow_position(const LocalEntity& entity,
                                                      const VxlMap& map) noexcept {
    constexpr std::uint8_t health_crate_type{4U};
    constexpr double surface_bias{0.002};
    if (!entity.alive || entity.type != health_crate_type || !finite(entity.position)) {
        return std::nullopt;
    }

    // get_spot_shadow_pos adds this exact half-cell XY offset before the
    // retail world renderer projects onto VXL. Using that point for the column
    // lookup also handles a falling crate whose packet coordinate is not
    // perfectly integral.
    const auto x = entity.position.x + 0.5;
    const auto y = entity.position.y + 0.5;
    if (x < 0.0 || y < 0.0 || x >= static_cast<double>(VxlMap::width) ||
        y >= static_cast<double>(VxlMap::depth)) {
        return std::nullopt;
    }
    const auto cell_x = static_cast<std::uint32_t>(std::floor(x));
    const auto cell_y = static_cast<std::uint32_t>(std::floor(y));
    const auto first_z = static_cast<std::uint32_t>(
        std::clamp(std::floor(entity.position.z), 0.0,
                   static_cast<double>(VxlMap::height - 1U)));
    for (auto z = first_z; z < VxlMap::height; ++z) {
        if (map.solid(cell_x, cell_y, z)) {
            // Map Z grows downward. A tiny upward bias prevents depth fighting
            // without making the decal visibly float above the voxel face.
            return Vec3{x, y, static_cast<double>(z) - surface_bias};
        }
    }
    return std::nullopt;
}

double blast_falloff(double distance, double radius) noexcept {
    if (radius <= 0.0) {
        return 0.0;
    }
    if (distance >= radius) {
        // Exactly zero past the edge, so slack can never leak damage outward.
        return 0.0;
    }
    return std::clamp(1.0 - distance / radius, 0.0, 1.0);
}

double shortest_angle_delta(double from, double to) noexcept {
    auto delta = std::fmod(to - from, degrees_per_turn);
    if (delta > half_turn) {
        delta -= degrees_per_turn;
    } else if (delta <= -half_turn) {
        delta += degrees_per_turn;
    }
    return delta;
}

TurretAim step_turret_aim(double current_yaw,
                          double current_pitch,
                          double desired_yaw,
                          double desired_pitch,
                          double degrees_per_second,
                          double dt,
                          double tolerance) noexcept {
    TurretAim aim{current_yaw, current_pitch, false, 0.0};
    if (dt <= 0.0) {
        return aim;
    }
    const auto budget = std::max(0.0, degrees_per_second * dt);
    const auto yaw_error = shortest_angle_delta(current_yaw, desired_yaw);
    // Pitch does not wrap: a turret that took the short way around in pitch
    // would flip itself upside down to look slightly further down.
    const auto pitch_error = desired_pitch - current_pitch;

    const auto yaw_step = std::clamp(yaw_error, -budget, budget);
    const auto pitch_step = std::clamp(pitch_error, -budget, budget);
    aim.yaw = current_yaw + yaw_step;
    aim.pitch = current_pitch + pitch_step;
    if (aim.yaw >= degrees_per_turn) {
        aim.yaw -= degrees_per_turn;
    } else if (aim.yaw < 0.0) {
        aim.yaw += degrees_per_turn;
    }
    aim.angular_rate = (std::abs(yaw_step) + std::abs(pitch_step)) / dt;
    aim.on_target = std::abs(yaw_error) <= tolerance && std::abs(pitch_error) <= tolerance;
    return aim;
}

} // namespace battlespades::world
