#include "battlespades/world/local_entity.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace battlespades::world {
namespace {

constexpr double degrees_per_turn{360.0};
constexpr double half_turn{180.0};
// GenericMovement gravity: 30 blocks/s^2, the same world gravity the retail
// crate drop was live-measured at (CRATES_CLASSES_RETAIL.md section 1).
constexpr double entity_gravity{30.0};
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

[[nodiscard]] constexpr bool is_supply_crate(std::uint8_t type) noexcept {
    return type >= 3U && type <= 6U;
}

[[nodiscard]] double bounce_restitution(const EntityDefinition& definition) noexcept {
    // Crate.initialize: set_bouncing(True). A dropped crate bounces twice on
    // the GenericMovement half-strength rebound before it rests.
    if (definition.type_id == 11U || is_supply_crate(definition.type_id))
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

bool retail_entity_spins(std::uint8_t type) noexcept {
    return is_supply_crate(type) || type == 16U;
}

void advance_entity_presentation(LocalEntity& entity, double dt) noexcept {
    if (!std::isfinite(dt) || dt <= 0.0 || !entity.alive) {
        return;
    }
    if (retail_entity_spins(entity.type)) {
        entity.spin_degrees = std::fmod(
            entity.spin_degrees + dt * retail_entity_spin_degrees_per_second, degrees_per_turn);
    }
    if (entity.type == 16U) {
        // intel.py:60-65: only while the pickup sits at the waterplane.
        if (std::isfinite(entity.position.z) &&
            entity.position.z >= retail_z_above_waterplane) {
            entity.floating_offset =
                std::min(retail_intel_floating_range,
                         entity.floating_offset + retail_intel_floating_speed * dt);
        } else {
            entity.floating_offset = 0.0;
        }
    }
}

EntityWorldLabel entity_world_label(const LocalEntity& entity,
                                    Vec3 display_position,
                                    std::optional<std::uint8_t> local_team,
                                    std::optional<Vec3> local_position) noexcept {
    EntityWorldLabel result;
    if (!entity.alive || !finite(display_position)) {
        return result;
    }
    const auto ceil_value = [](double value) {
        return static_cast<std::uint32_t>(
            std::clamp(std::ceil(value), 0.0, 99999.0));
    };
    const auto at = [&display_position](double lift) {
        return Vec3{display_position.x, display_position.y, display_position.z - lift};
    };
    const bool running_fuse = std::isfinite(entity.fuse) && entity.fuse > 0.0;
    switch (entity.type) {
    case 16U: // IntelPickup.set_fuse: the dropped intel's return timer.
        if (running_fuse) {
            result = {true, ceil_value(entity.fuse), at(1.3)};
        }
        break;
    case 15U: // DiamondPickup.initialize always creates the lifetime text.
        result = {true, ceil_value(std::isfinite(entity.fuse) ? std::max(entity.fuse, 0.0) : 0.0),
                  at(1.3)};
        break;
    case 14U: // BombPickup.set_fuse: an armed bomb's fuse.
        if (running_fuse) {
            result = {true, ceil_value(entity.fuse), at(1.5)};
        }
        break;
    case 36U: // RadarStationEntity lifetime (draw_lifetime; packet fuse).
        if (running_fuse) {
            result = {true, ceil_value(entity.fuse), at(1.0)};
        }
        break;
    case 8U: { // RocketTurret.update: own team within A1626 (20) blocks.
        constexpr double radius{20.0};
        if (!local_team.has_value() || !local_position.has_value() ||
            *local_team != entity.team) {
            break;
        }
        const double dx = local_position->x - entity.position.x;
        const double dy = local_position->y - entity.position.y;
        const double dz = local_position->z - entity.position.z;
        if (dx * dx + dy * dy + dz * dz < radius * radius) {
            result = {true, entity.ammo, at(1.0), EntityWorldLabelStyle::turret_ammo};
        }
        break;
    }
    default:
        break;
    }
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

    if (is_supply_crate(definition.type_id) && !entity.attached) {
        if (!entity.crate_drop.falling && !entity.grounded && near_zero(entity.velocity) &&
            !solid_at(map, entity.position)) {
            // Created (or re-created) in the air: the retail client falls and
            // parachutes it by itself; the server sends nothing during the fall.
            entity.crate_drop.falling = true;
            entity.crate_drop_clock = 0.0;
        } else if (!entity.crate_drop.falling && !entity.grounded &&
                   entity.velocity.z > 0.0 && !solid_at(map, entity.position) &&
                   !entity.crate_drop.parachute_deployed) {
            // A late joiner receives the current fall speed.
            entity.crate_drop.falling = true;
            entity.crate_drop_clock = 0.0;
        }
        if (entity.crate_drop.falling) {
            const auto x = std::floor(entity.position.x);
            const auto y = std::floor(entity.position.y);
            double support = static_cast<double>(VxlMap::height - 1U);
            if (x >= 0.0 && y >= 0.0 && x < static_cast<double>(VxlMap::width) &&
                y < static_cast<double>(VxlMap::depth)) {
                for (auto z = static_cast<std::uint32_t>(
                         std::clamp(std::floor(entity.position.z), 0.0,
                                    static_cast<double>(VxlMap::height - 1U)));
                     z < VxlMap::height; ++z) {
                    if (map.solid(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                                  z)) {
                        support = static_cast<double>(z);
                        break;
                    }
                }
            }
            entity.crate_drop_clock += dt;
            std::size_t steps{};
            while (entity.crate_drop_clock >= crate_drop_step && steps < 30U) {
                entity.crate_drop_clock -= crate_drop_step;
                ++steps;
                const auto step = step_crate_drop(entity.position.z, entity.velocity.z, support,
                                                  entity.crate_drop);
                result.moved = true;
                result.chute_opened = result.chute_opened || step.chute_opened;
                result.chute_released = result.chute_released || step.chute_released;
                if (step.landed) {
                    entity.crate_drop.falling = false;
                    entity.crate_drop.parachute_deployed = true;
                    entity.crate_drop.parachute_removed = true;
                    entity.crate_drop_clock = 0.0;
                    entity.face = 4U;
                    result.landed = true;
                    result.impact_speed = step.impact_speed;
                    const auto rebound = step.impact_speed * grave_bounce_restitution;
                    if (rebound >= minimum_bounce_speed) {
                        entity.velocity = {0.0, 0.0, -rebound};
                        entity.grounded = false;
                        result.bounced = true;
                    } else {
                        entity.velocity = {};
                        entity.grounded = true;
                    }
                    break;
                }
            }
            if (steps >= 30U) {
                entity.crate_drop_clock = 0.0;
            }
            return result;
        }
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
    // Finite input components can overflow when multiplied by dt. Bound the
    // floating-point result before integer conversion, including infinity.
    const auto steps = static_cast<std::size_t>(std::clamp(
        std::ceil(maximum_displacement / maximum_sweep_distance), 1.0,
        static_cast<double>(maximum_sweep_steps)));
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

EntityAimRotation entity_aim_rotation(std::uint8_t entity_type,
                                      double yaw,
                                      double pitch) noexcept {
    if (entity_type == 8U) {
        // rocketTurret.py sets GL yaw then adds local GL-X pitch. After the
        // KV6 conversion the barrel points along map +Y: pitch is map -X and
        // yaw is map -Z. Character-style Y pitch merely rolled this barrel.
        return {0U, -pitch, -yaw};
    }
    return {1U, -pitch, yaw};
}

void LauncherMuzzleTracker::remember(std::uint8_t tool, Vec3 position, Vec3 velocity,
                                    Clock::time_point now) noexcept {
    const auto type = static_cast<std::uint8_t>(
        tool == 12U ? 21U : (tool == 13U || tool == 46U) ? 22U : 0U);
    if (type == 0U || !finite(position) || !finite(velocity)) {
        return;
    }
    shots_[next_] = {position, velocity, now, type};
    next_ = (next_ + 1U) % shots_.size();
}

bool LauncherMuzzleTracker::consume(const LocalEntity& entity, Clock::time_point now) noexcept {
    // Packet 10/21 vectors use signed 1/64-block fixed point. Compare the
    // queued float origin with its round-tripped wire value, not the player's
    // current position or selected tool, which may change during the RTT.
    const auto matches = [](Vec3 sent, Vec3 received) noexcept {
        constexpr double quantum{1.0 / 64.0};
        return std::abs(sent.x - received.x) <= quantum &&
               std::abs(sent.y - received.y) <= quantum &&
               std::abs(sent.z - received.z) <= quantum;
    };
    for (auto& shot : shots_) {
        if (now < shot.created || now - shot.created >= std::chrono::seconds{2}) {
            shot.entity_type = 0U;
        }
        if (shot.entity_type != 0U && shot.entity_type == entity.type &&
            matches(shot.position, entity.position) && matches(shot.velocity, entity.velocity)) {
            shot.entity_type = 0U;
            return true;
        }
    }
    return false;
}

void LauncherMuzzleTracker::clear() noexcept {
    shots_ = {};
    next_ = 0U;
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

std::array<float, 16U> entity_presentation_transform(
    const LocalEntity& entity,
    const EntityDefinition& definition,
    const EntityModelPart& part,
    double contact_adjustment) noexcept {
    const float scale = definition.model_size * part.scale;
    std::array<std::array<float, 3U>, 3U> basis{{{scale, 0.0F, 0.0F},
                                               {0.0F, scale, 0.0F},
                                               {0.0F, 0.0F, scale}}};
    const auto rotate = [&basis](std::uint8_t axis, float degrees) {
        const auto radians = static_cast<float>(degrees * std::numbers::pi / 180.0);
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        for (auto& v : basis) {
            const auto before = v;
            if (axis == 0U) {
                v[1U] = before[1U] * c - before[2U] * s;
                v[2U] = before[1U] * s + before[2U] * c;
            } else if (axis == 1U) {
                v[0U] = before[0U] * c + before[2U] * s;
                v[2U] = -before[0U] * s + before[2U] * c;
            } else {
                v[0U] = before[0U] * c - before[1U] * s;
                v[1U] = before[0U] * s + before[1U] * c;
            }
        }
    };
    // Undo the KV6 loader's Rx(+90), then reproduce Entity.draw's face and
    // per-part articulation. Keeping this shared makes the ghost truthful.
    rotate(0U, -90.0F);
    const auto face = entity_face_rotation(entity.face);
    if (face.degrees != 0.0F) rotate(face.axis, face.degrees);
    // SpinningEntity turns about the display's up axis (map -Z).
    if (retail_entity_spins(entity.type) && entity.spin_degrees != 0.0)
        rotate(2U, static_cast<float>(-entity.spin_degrees));
    const auto aim = entity_aim_rotation(entity.type, entity.aim_yaw, entity.aim_pitch);
    if (part.rotation_mode >= 2U)
        rotate(aim.pitch_axis, static_cast<float>(aim.pitch_degrees));
    if (part.rotation_mode >= 1U)
        rotate(2U, static_cast<float>(aim.yaw_degrees));
    const auto origin = entity_presentation_position(entity, part);
    return {basis[0U][0U], basis[0U][1U], basis[0U][2U], 0.0F,
            basis[1U][0U], basis[1U][1U], basis[1U][2U], 0.0F,
            basis[2U][0U], basis[2U][1U], basis[2U][2U], 0.0F,
            static_cast<float>(origin.x), static_cast<float>(origin.y),
            static_cast<float>(origin.z + contact_adjustment - entity.floating_offset), 1.0F};
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
    // A server placement may point inside its supporting solid voxel. Use
    // that voxel's top surface after contact, not the fractional interior.
    const auto support_z = entity.grounded ? std::floor(entity.position.z) : entity.position.z;
    return support_z - visual_bottom_z;
}

double entity_rig_vertical_contact_adjustment(const LocalEntity& entity,
                                             const EntityDefinition& definition,
                                             std::span<const EntityModelPart> parts,
                                             std::span<const float> mesh_minimum_y) noexcept {
    std::optional<double> adjustment;
    for (std::size_t index = 0U; index < std::min(parts.size(), mesh_minimum_y.size()); ++index) {
        // Yaw preserves ground contact; a pitched barrel must never move the
        // entire machine as it tracks a target.
        if (parts[index].rotation_mode >= 2U || !std::isfinite(mesh_minimum_y[index])) {
            continue;
        }
        const double part = entity_vertical_contact_adjustment(entity, definition, parts[index], mesh_minimum_y[index]);
        adjustment = adjustment ? std::min(*adjustment, part) : part;
    }
    return adjustment.value_or(0.0);
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

std::optional<CharacterSpotShadow> character_spot_shadow(const Vec3& position,
                                                         const VxlMap& map) noexcept {
    // SPOT_SHADOW_RAY_CAST_CHARACTER_HEIGHT (constants.py) via
    // set_shadow_char_height; create_spot_shadows adds char_h - 1.
    constexpr double character_height{3.0};
    constexpr std::uint32_t search_cells{10U};
    constexpr double surface_bias{0.001};
    if (!finite(position) || position.x < 0.0 || position.y < 0.0 ||
        position.x >= static_cast<double>(VxlMap::width) ||
        position.y >= static_cast<double>(VxlMap::depth)) {
        return std::nullopt;
    }
    const double start =
        std::min(std::clamp(position.z, 0.0, static_cast<double>(VxlMap::height)) +
                     character_height - 1.0,
                 static_cast<double>(VxlMap::height - 1U));
    const auto cell_x = static_cast<std::uint32_t>(std::floor(position.x));
    const auto cell_y = static_cast<std::uint32_t>(std::floor(position.y));
    const auto first_z = static_cast<std::uint32_t>(std::floor(start));
    for (std::uint32_t step{}; step < search_cells; ++step) {
        const auto z = first_z + step;
        if (z >= VxlMap::height) {
            break;
        }
        if (map.solid(cell_x, cell_y, z)) {
            const double drop = std::max(0.0, static_cast<double>(z) - start);
            return CharacterSpotShadow{
                Vec3{position.x, position.y, static_cast<double>(z) - surface_bias},
                std::clamp(1.0 - drop / static_cast<double>(search_cells), 0.0, 1.0)};
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
