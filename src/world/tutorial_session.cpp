#include "battlespades/world/tutorial_session.hpp"
#include "battlespades/world/build_wallet.hpp"
#include "battlespades/world/retail_effects.hpp"
#include "battlespades/world/class_catalog.hpp"
#include "battlespades/world/weapon_catalog.hpp"
#include "battlespades/world/weapon_zoom.hpp"
#include "battlespades/world/retail_recoil.hpp"
#include "battlespades/world/retail_blast.hpp"
#include "battlespades/world/retail_random.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <numbers>
#include <utility>

namespace battlespades::world {
namespace {

// Recovered tutorial spawn: lane origin (0,0) + SPAWN_LOCAL. Yaw/pitch use
// retail's degree-based look model whose forward vector is
// (-cos yaw * cos pitch, -sin yaw * cos pitch, sin pitch): yaw 0 faces -x
// down the course, pitch positive looks down.
// Recovered from the retail character view clamp (89.90000000000001).
constexpr double pitch_limit_degrees{89.9};
constexpr double degrees_to_radians{std::numbers::pi / 180.0};

[[nodiscard]] double quantized_orientation_component(double value) noexcept {
    // ClientData uses retail's sign-magnitude orientation wire type, but its
    // decoded component grid is still 1/8192 within a unit look vector. Keep
    // the live mover on that grid so a sub-bit yaw difference cannot choose
    // the opposite side of a voxel corner hundreds of frames later.
    constexpr double scale{8192.0};
    constexpr double maximum{16383.0 / scale};
    return std::round(std::clamp(value, -maximum, maximum) * scale) / scale;
}

constexpr int default_block_health{5};
constexpr double melee_world_range{4.0};
// NONRETAIL: the tutorial server's block grant count was not recovered; 50
// covers building up the tower side (the CLIMB gate is the tower top).
constexpr int climb_block_grant{50};
constexpr std::uint8_t retail_block_tool_id{5U};
constexpr std::uint8_t retail_spade_tool_id{2U};
constexpr std::uint8_t retail_pistol_tool_id{17U};

[[nodiscard]] const WeaponDefinition& tool_definition(std::uint8_t id) noexcept {
    // All ids used by Tutorial are compile-time members of the generated
    // dense 0..64 server catalog.
    return weapon_catalog()[id];
}

[[nodiscard]] constexpr std::uint8_t retail_tool_id(TutorialTool tool) noexcept {
    switch (tool) {
    case TutorialTool::block:
        return retail_block_tool_id;
    case TutorialTool::spade:
        return retail_spade_tool_id;
    case TutorialTool::pistol:
        return retail_pistol_tool_id;
    }
    return retail_spade_tool_id;
}

[[nodiscard]] constexpr std::optional<TutorialTool>
tutorial_tool_from_id(std::uint8_t id) noexcept {
    switch (id) {
    case retail_block_tool_id:
        return TutorialTool::block;
    case retail_spade_tool_id:
        return TutorialTool::spade;
    case retail_pistol_tool_id:
        return TutorialTool::pistol;
    default:
        return std::nullopt;
    }
}

// Recovered gallery targets: five spheres of pure 0xE43334 voxels around
// these lane-local centers (the tutorial lane origin is (0,0), so they are
// world coordinates), spawn radius 4.
struct TargetCenter final {
    std::uint32_t x;
    std::uint32_t y;
    std::uint32_t z;
};
constexpr std::array<TargetCenter, 5U> target_centers{{
    {39U, 62U, 221U},
    {39U, 91U, 221U},
    {43U, 60U, 229U},
    {43U, 93U, 229U},
    {53U, 78U, 229U},
}};
constexpr std::uint32_t target_scan_radius{5U};
constexpr VxlColor target_red{228U, 51U, 52U, 255U};
constexpr VxlColor target_white{232U, 233U, 233U, 255U};

[[nodiscard]] constexpr bool is_target_red(VxlColor color) noexcept {
    return color.red == target_red.red && color.green == target_red.green &&
           color.blue == target_red.blue;
}

[[nodiscard]] constexpr bool is_target_face(VxlColor color) noexcept {
    return is_target_red(color) ||
           (color.red == target_white.red && color.green == target_white.green &&
            color.blue == target_white.blue);
}


[[nodiscard]] Vec3 normalized(Vec3 value) noexcept {
    const double length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (length <= 1e-9) {
        return {-1.0, 0.0, 0.0};
    }
    return {value.x / length, value.y / length, value.z / length};
}

[[nodiscard]] double seed_signed(std::uint8_t seed, std::uint32_t salt) noexcept {
    std::uint32_t value = static_cast<std::uint32_t>(seed) + salt * 0x9E3779B9U;
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    return static_cast<double>(value & 0xFFFFU) / 32767.5 - 1.0;
}

[[nodiscard]] double projectile_speed(const WeaponDefinition& weapon,
                                      const WeaponAction& action) noexcept {
    if (weapon.mechanism == WeaponMechanism::oriented_launcher) {
        return std::max(1.0, action.value);
    }

    const auto named = [&weapon](std::string_view name, double fallback) {
        const auto* value = find_retail_weapon_constant(weapon, name);
        return value != nullptr && value->value_count > 0U ? value->values.front() : fallback;
    };
    if (weapon.tool_id == 31U) {
        return named("CLASSIC_GRENADE_THROW_SPEED", 35.0);
    }
    if (weapon.mechanism == WeaponMechanism::cooked_throwable) {
        const double fuse =
            std::max(0.001, weapon.retail.use.maximum_fuse.value_or(weapon.fuse_time));
        const double cooked = std::clamp((fuse - action.value) / fuse, 0.0, 1.0);
        const bool antipersonnel = weapon.tool_id == 32U;
        const double minimum = named(antipersonnel ? "ANTIPERSONNEL_GRENADE_THROW_MIN_SPEED"
                                                   : "GRENADE_THROW_MIN_SPEED",
                                     25.0);
        const double added = named(
            antipersonnel ? "ANTIPERSONNEL_GRENADE_THROW_SPEED" : "GRENADE_THROW_SPEED", 50.0);
        return minimum + cooked * added;
    }
    if (weapon.mechanism == WeaponMechanism::charged_throwable) {
        struct ChargeNames final {
            std::string_view minimum;
            std::string_view added;
            double fallback_minimum;
            double fallback_added;
        };
        ChargeNames names{"", "", 25.0, 50.0};
        if (weapon.tool_id == 33U) {
            names = {"MOLOTOV_THROW_MIN_SPEED", "MOLOTOV_THROW_SPEED", 35.0, 40.0};
        }
        // The obfuscated Specialist aliases are A1664/A1663 (chemical) and
        // A1683/A1682 (sticky), recovered from Character.throw_*.
        if (weapon.tool_id == 54U) {
            names = {"A1664", "A1663", 25.0, 50.0};
        } else if (weapon.tool_id == 57U) {
            names = {"A1683", "A1682", 25.0, 50.0};
        }
        return named(names.minimum, names.fallback_minimum) +
               std::clamp(action.value, 0.0, 1.0) * named(names.added, names.fallback_added);
    }
    return 35.0;
}

[[nodiscard]] double projectile_gravity(std::uint8_t tool_id) noexcept {
    switch (tool_id) {
    case 12U:
        return 0.05;
    case 13U:
    case 46U:
        return 0.025;
    case 14U:
    case 47U:
        return 1.5;
    case 29U:
    case 48U:
        return 0.5;
    default:
        return 1.0;
    }
}

[[nodiscard]] double named_weapon_value(const WeaponDefinition& weapon,
                                        std::string_view suffix,
                                        double fallback) noexcept {
    if (const auto* exact = find_retail_weapon_constant(weapon, suffix);
        exact != nullptr && exact->value_count > 0U) {
        return exact->values.front();
    }
    for (const auto& constant : weapon.constants) {
        if (constant.value_count > 0U && constant.name.ends_with(suffix)) {
            return constant.values.front();
        }
    }
    return fallback;
}

[[nodiscard]] TutorialProjectileBehavior projectile_behavior(std::uint8_t tool_id) noexcept {
    switch (tool_id) {
    case 11U:
    case 31U:
    case 32U:
        return TutorialProjectileBehavior::bounce;
    case 57U:
        return TutorialProjectileBehavior::stick;
    case 58U:
        return TutorialProjectileBehavior::deploy;
    case 14U:
    case 47U:
        return TutorialProjectileBehavior::drill;
    default:
        return TutorialProjectileBehavior::contact;
    }
}

/**
 * CreateEntity type -> tool mapping for server-owned flying ordnance.
 *
 * Protocol 168 carries an ENTITY id in packet 21 but the client catalog,
 * sounds, crater radius and projectile model are keyed by TOOL id. Keeping the
 * conversion explicit avoids the old bug where entity 21 was accidentally
 * treated as tool 21 (Dynamite).
 */
[[nodiscard]] std::optional<std::uint8_t>
projectile_tool_for_entity(std::uint8_t entity_type) noexcept {
    switch (entity_type) {
    case 21U:
        return std::uint8_t{12U}; // ROCKET -> RPG
    case 22U:
        return std::uint8_t{13U}; // ROCKET2 -> RPG2
    case 23U:
        return std::uint8_t{14U}; // DRILL -> DRILLGUN
    case 24U:
        return std::uint8_t{29U}; // SNOWBALL -> SNOWBLOWER
    case 27U:
        return std::uint8_t{33U}; // MOLOTOV
    case 32U:
        return std::uint8_t{54U}; // CHEMICAL_BOMB
    case 33U:
        return std::uint8_t{55U}; // GL_GRENADE
    case 34U:
    case 35U:
        return std::uint8_t{57U}; // flying/stuck sticky grenade
    case 37U:
        return std::uint8_t{58U}; // PROJECTILE_MINE
    default:
        return std::nullopt;
    }
}

[[nodiscard]] bool is_server_projectile(const LocalEntity& entity,
                                        const EntityDefinition& definition) noexcept {
    if (definition.category == EntityCategory::projectile) {
        return true;
    }
    // Type 37 becomes a placed mine after landing, but CreateEntity supplies a
    // non-zero launch velocity while it is still the Mine Launcher's projectile.
    return entity.type == 37U &&
           (entity.velocity.x != 0.0 || entity.velocity.y != 0.0 || entity.velocity.z != 0.0);
}

[[nodiscard]] bool predicts_oriented_item_locally(std::uint8_t tool_id) noexcept {
    // Retail Character.throw_grenade creates these three on the firing client;
    // the server intentionally relays packet 10 only to observers. Launchers
    // and later grenade variants instead come back as CreateEntity(21).
    return tool_id == 11U || tool_id == 31U || tool_id == 32U;
}

[[nodiscard]] std::uint8_t projectile_crater_radius(const WeaponDefinition& weapon) noexcept {
    // The generated profile already resolves the primary explosion radius.
    // Suffix-searching here was unsafe for Molotov: the alphabetically earlier
    // MOLOTOV_BLOCKFIRE_EXPLOSION_RADIUS (2) shadowed the impact radius (4).
    const double radius = weapon.blast_radius > 0.0 ? weapon.blast_radius : 1.0;
    return static_cast<std::uint8_t>(std::clamp(std::lround(radius), 1L, 8L));
}

[[nodiscard]] std::vector<VoxelCell> radius_cells(VoxelCell center, std::uint8_t radius) {
    std::vector<VoxelCell> cells;
    const int r = static_cast<int>(radius);
    const double limit = static_cast<double>(r) + 0.5;
    const double limit_squared = limit * limit;
    cells.reserve(static_cast<std::size_t>((r * 2 + 1) * (r * 2 + 1) * (r * 2 + 1)));
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dz = -r; dz <= r; ++dz) {
                if (static_cast<double>(dx * dx + dy * dy + dz * dz) >= limit_squared) {
                    continue;
                }
                const auto x = static_cast<std::int64_t>(center.x) + dx;
                const auto y = static_cast<std::int64_t>(center.y) + dy;
                const auto z = static_cast<std::int64_t>(center.z) + dz;
                if (x >= 0 && y >= 0 && z >= 0 && x < static_cast<std::int64_t>(VxlMap::width) &&
                    y < static_cast<std::int64_t>(VxlMap::depth) &&
                    z < static_cast<std::int64_t>(VxlMap::height - 1U)) {
                    cells.push_back({static_cast<std::uint32_t>(x),
                                     static_cast<std::uint32_t>(y),
                                     static_cast<std::uint32_t>(z)});
                }
            }
        }
    }
    return cells;
}

struct RaycastHit final {
    std::uint32_t x{};
    std::uint32_t y{};
    std::uint32_t z{};
    /** Outward face normal of the struck cell (unit axis step, negated). */
    std::int32_t nx{};
    std::int32_t ny{};
    std::int32_t nz{};
};

/**
 * Amanatides-Woo voxel traversal from the eye along the look direction,
 * mirroring retail cube_line hit tests: the bounded map means leaving x/y
 * bounds or passing the bed misses, while sky travel keeps going as long as
 * the ray can still descend back into the map.
 */
[[nodiscard]] std::optional<RaycastHit>
raycast_voxels(const VxlMap& map, Vec3 origin, Vec3 direction, double max_distance) {
    constexpr double epsilon{1e-9};
    std::int64_t cell_x = static_cast<std::int64_t>(std::floor(origin.x));
    std::int64_t cell_y = static_cast<std::int64_t>(std::floor(origin.y));
    std::int64_t cell_z = static_cast<std::int64_t>(std::floor(origin.z));
    const std::array<double, 3U> dir{direction.x, direction.y, direction.z};
    const std::array<std::int64_t, 3U> step{
        dir[0U] > epsilon ? 1 : (dir[0U] < -epsilon ? -1 : 0),
        dir[1U] > epsilon ? 1 : (dir[1U] < -epsilon ? -1 : 0),
        dir[2U] > epsilon ? 1 : (dir[2U] < -epsilon ? -1 : 0),
    };
    const std::array<double, 3U> origin_axes{origin.x, origin.y, origin.z};
    std::array<double, 3U> t_max{};
    std::array<double, 3U> t_delta{};
    const std::array<std::int64_t, 3U> cells{cell_x, cell_y, cell_z};
    for (std::size_t axis{}; axis < 3U; ++axis) {
        if (step[axis] == 0) {
            t_max[axis] = std::numeric_limits<double>::infinity();
            t_delta[axis] = std::numeric_limits<double>::infinity();
            continue;
        }
        const double boundary = static_cast<double>(cells[axis]) + (step[axis] > 0 ? 1.0 : 0.0);
        t_max[axis] = (boundary - origin_axes[axis]) / dir[axis];
        t_delta[axis] = 1.0 / std::fabs(dir[axis]);
    }

    double travelled{};
    while (travelled <= max_distance) {
        std::size_t axis = 0U;
        if (t_max[1U] < t_max[0U]) {
            axis = 1U;
        }
        if (t_max[2U] < t_max[axis]) {
            axis = 2U;
        }
        travelled = t_max[axis];
        if (travelled > max_distance) {
            return std::nullopt;
        }
        t_max[axis] += t_delta[axis];
        if (axis == 0U) {
            cell_x += step[0U];
        } else if (axis == 1U) {
            cell_y += step[1U];
        } else {
            cell_z += step[2U];
        }
        if (cell_x < 0 || cell_x >= VxlMap::width || cell_y < 0 || cell_y >= VxlMap::depth) {
            return std::nullopt;
        }
        if (cell_z >= VxlMap::height) {
            return std::nullopt;
        }
        if (cell_z < 0) {
            if (step[2U] <= 0) {
                return std::nullopt;
            }
            continue;
        }
        if (map.solid(static_cast<std::uint32_t>(cell_x),
                      static_cast<std::uint32_t>(cell_y),
                      static_cast<std::uint32_t>(cell_z))) {
            RaycastHit hit;
            hit.x = static_cast<std::uint32_t>(cell_x);
            hit.y = static_cast<std::uint32_t>(cell_y);
            hit.z = static_cast<std::uint32_t>(cell_z);
            if (axis == 0U) {
                hit.nx = static_cast<std::int32_t>(-step[0U]);
            } else if (axis == 1U) {
                hit.ny = static_cast<std::int32_t>(-step[1U]);
            } else {
                hit.nz = static_cast<std::int32_t>(-step[2U]);
            }
            return hit;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool& slot(std::array<bool, static_cast<std::size_t>(TutorialAction::count)>& held,
                         TutorialAction action) noexcept {
    return held[static_cast<std::size_t>(action)];
}

} // namespace

TutorialWorldSession::TutorialWorldSession(std::shared_ptr<VxlMap> map,
                                           TutorialSessionConfig config)
    : map_{std::move(map)}, config_{config},
      movement_class_{
          movement_config_for_class(config.initial_class_id, config.movement_speed_scale,
                                    config.fall_on_water_damage)} {
    apply_flight_profile(movement_class_, config_.flight_profile);
    const auto& pistol = tool_definition(retail_pistol_tool_id);
    pistol_clip_ = static_cast<int>(pistol.clip_size);
    pistol_stock_ = static_cast<int>(pistol.reserve_ammo);
    player_.position = config_.initial_position;
    network_interpolated_position_ = player_.position;
    player_.orientation = config_.initial_orientation;
    // Look angles are the authority once the session runs: every frame rebuilds
    // the orientation vector from yaw_/pitch_. Seeding them from the configured
    // orientation has to happen for ALL sessions, not just authoritative ones --
    // otherwise `initial_orientation` appears to be honoured, then is silently
    // overwritten on the first update, and the camera faces the default -x.
    const auto horizontal = std::hypot(player_.orientation.x, player_.orientation.y);
    yaw_ = std::atan2(-player_.orientation.y, -player_.orientation.x) / degrees_to_radians;
    pitch_ = std::atan2(player_.orientation.z, horizontal) / degrees_to_radians;
    pitch_ = std::clamp(pitch_, -pitch_limit_degrees, pitch_limit_degrees);
    if (config_.network_authoritative) {
        last_authoritative_position_ = config_.initial_position;
        player_.velocity = config_.initial_velocity;
        player_.crouch = config_.initial_crouch;
        player_.wade = config_.initial_wade;
        // A native Player begins grounded even when the spawn anchor's feet
        // plane is still a fraction above the first solid voxel. Deriving
        // this flag from a voxel probe made our first live frames use air
        // friction while the authority used ground friction, leaving a
        // correction that survived into the first sprint/jump.
        player_.airborne = config_.initial_airborne.value_or(false);
        debug_full_loadout_ = true;
        auto loadout = config_.initial_loadout;
        if (loadout.empty()) {
            if (const auto* klass = find_class_definition(config_.initial_class_id);
                klass != nullptr) {
                for (const auto item : default_class_items(*klass)) {
                    if (item <= 255U) {
                        loadout.push_back(static_cast<std::uint8_t>(item));
                    }
                }
            }
        }
        config_.initial_loadout = loadout;
        sync_movement_equipment(loadout);
        sandbox_inventory_.set_block_wallet_multiplier(config_.block_wallet_multiplier);
        if (!sandbox_inventory_.spawn_with_selection(config_.initial_class_id,
                                                     loadout,
                                                     config_.initial_prefabs,
                                                     config_.initial_ugc_tools)) {
            static_cast<void>(sandbox_inventory_.spawn_as(config_.initial_class_id,
                                                          PlayerLoadoutScope::retail_default));
        }
        blocks_remaining_ = static_cast<int>(sandbox_inventory_.blocks());
        if (config_.initial_tool.has_value()) {
            static_cast<void>(sandbox_inventory_.select_tool(
                *config_.initial_tool, InventorySelectionOrigin::loadout_sync));
        }
        reset_tool_transition_state();
        return;
    }
    // Movement lessons start empty-handed: SHOOTING_1 "You now have a
    // pistol!" and CLIMB1 "You now have the block tool and spade!" grant the
    // kit later, exactly like the server's MOVEMENT_LOADOUT=().
    sync_inventory_slots(std::nullopt, InventorySelectionOrigin::loadout_sync);
}

void TutorialWorldSession::set_action_held(TutorialAction action, bool held) noexcept {
    if (action == TutorialAction::count) {
        return;
    }
    // Retail jump requests are edge-triggered on key-down and consumed even
    // while airborne, so holding SPACE never re-fires on landing.
    if (action == TutorialAction::jump && held && !slot(held_, action)) {
        jump_requested_ = true;
    }
    if (held && !slot(held_, action)) {
        slot(press_latch_, action) = true;
    }
    const bool was_held = slot(held_, action);
    slot(held_, action) = held;
    if (action == TutorialAction::sprint && was_held != held) {
        if (held) {
            // Character.set_sprint(True) exits ADS before changing the native
            // movement bit. Do this on the key edge so one sprint frame can
            // never retain the magnified sight.
            zoomed_ = false;
        } else if (const auto selected = selected_tool_id(); selected.has_value()) {
            const auto* weapon = find_weapon_definition(*selected);
            if (weapon != nullptr && weapon->mechanism != WeaponMechanism::melee) {
                // Character.set_sprint(False) assigns pullout=0.5 for tools
                // outside ALL_MELEE_WEAPONS. draw_fps then subtracts
                // pullout*5 from both X and Y as the hidden item returns.
                sprint_pullout_remaining_ = 0.5;
            }
        }
    }
    if (!held && ((action == TutorialAction::jump && player_.jetpack != 4U) ||
                  (action == TutorialAction::hover && player_.jetpack == 4U))) {
        // Character cancels physical pack state from key-up immediately;
        // do not wait for the reliable inactive WorldUpdate to arrive.
        player_.jetpack_active = false;
    }
}

bool TutorialWorldSession::action_held(TutorialAction action) const noexcept {
    return action != TutorialAction::count && held_[static_cast<std::size_t>(action)];
}

void TutorialWorldSession::clear_input() noexcept {
    reload_aim_tool_.reset();
    held_.fill(false);
    press_latch_.fill(false);
    tick_held_.fill(false);
    jump_requested_ = false;
    primary_held_ = false;
    primary_edge_ = false;
    primary_press_latch_ = false;
    primary_tick_held_ = false;
    secondary_held_ = false;
    custom_held_ = false;
    zoomed_ = false;
    zoom_level_ = 0.0;
    sprint_pullout_remaining_ = 0.0;
    player_.jetpack_active = false;
    movement_events_ = {};
    sandbox_inventory_.weapons().cancel_interaction();
}

void TutorialWorldSession::set_player_collision_bodies(
    std::span<const PlayerCollisionBody> bodies) {
    player_collision_bodies_.assign(bodies.begin(), bodies.end());
}

bool TutorialWorldSession::set_server_movement_bounds(
    PlayerMovementBounds bounds) noexcept {
    // world.pyd set_locked_to_box only stores the box; the next native
    // update clamps the position (never velocity). Validate, don't move.
    auto probe = player_;
    if (!constrain_player_to_bounds(probe, bounds))
        return false;
    server_movement_bounds_ = bounds;
    return true;
}

void TutorialWorldSession::set_look_preferences(double mouse_sensitivity,
                                                bool invert_mouse) noexcept {
    config_.mouse_sensitivity = std::clamp(mouse_sensitivity, 0.0, 1.0);
    config_.invert_mouse = invert_mouse;
}

void TutorialWorldSession::apply_look_delta(double delta_x, double delta_y) noexcept {
    // Retail look model in degrees: sensitivity is degrees per raw count
    // (default 0.1). The retail matrix construction was never decompiled, so
    // the yaw sign follows the sanctioned in-game validation: with the shared
    // camera basis, increasing yaw turns the view toward the strafe-right
    // vector, making mouse-right turn right. SDL deltas grow
    // rightward/downward, so pulling the mouse back looks down unless
    // inversion is on.
    //
    // Aimed look speed is gated on the INSTANTANEOUS aim state, not on the
    // ramp, so the slowdown lands on the press rather than fading in with the
    // sight. Retail does not compensate for magnification either: half speed
    // against a 2x view is still faster on screen than hip fire. That is
    // authentic, and is why aiming feels twitchy rather than steady.
    double sight_sensitivity{1.0};
    if (zoomed_) {
        const auto selected = selected_tool_id();
        const auto* weapon = selected.has_value() ? find_weapon_definition(*selected) : nullptr;
        if (weapon != nullptr) {
            // Retail's Tool base declares 0.5, so every catalog row carries a
            // value and this fallback should never be reached. It matches the
            // base default anyway: a missing row must not silently double the
            // aimed look speed, which is what a neutral 1.0 here would do.
            sight_sensitivity = weapon->retail.use.zoom_sensitivity_factor.value_or(0.5);
        }
    }
    yaw_ += delta_x * config_.mouse_sensitivity * sight_sensitivity;
    pitch_ += delta_y * config_.mouse_sensitivity * sight_sensitivity *
              (config_.invert_mouse ? -1.0 : 1.0);
    pitch_ = std::clamp(pitch_, -pitch_limit_degrees, pitch_limit_degrees);
    if (yaw_ > 180.0) {
        yaw_ -= 360.0;
    } else if (yaw_ < -180.0) {
        yaw_ += 360.0;
    }
}

void TutorialWorldSession::set_look_angles(double yaw_degrees, double pitch_degrees) noexcept {
    if (!std::isfinite(yaw_degrees) || !std::isfinite(pitch_degrees)) return;
    yaw_ = std::remainder(yaw_degrees, 360.0);
    pitch_ = std::clamp(pitch_degrees, -pitch_limit_degrees, pitch_limit_degrees);
}

void TutorialWorldSession::tick() {
    if (std::exchange(empty_magazine_unzoom_pending_, false) && zoomed_) {
        zoomed_ = false;
        attack_events_.zoom_dropped = true;
    }
    advance_machine_gun_deployment();
    update_reload_aim();
    sprint_pullout_remaining_ = std::max(0.0, sprint_pullout_remaining_ - config_.fixed_dt);
    // GameScene.update drives the sight ramp from the same fixed 60 Hz
    // schedule as the simulation, never from the render loop. Advancing it
    // here keeps the transition the same length on a 144 Hz display as on a
    // 60 Hz one, which running retail's dt-free recurrence per frame would not.
    if (const auto selected = selected_tool_id(); selected.has_value()) {
        zoom_level_ = advance_zoom_level(zoom_level_, zoom_target(), *selected, config_.fixed_dt);
    } else {
        zoom_level_ = 0.0;
    }

    const double yaw_radians = yaw_ * degrees_to_radians;
    const double pitch_radians = pitch_ * degrees_to_radians;
    player_.orientation = {-std::cos(yaw_radians) * std::cos(pitch_radians),
                           -std::sin(yaw_radians) * std::cos(pitch_radians),
                           std::sin(pitch_radians)};
    if (config_.network_authoritative) {
        player_.orientation = {quantized_orientation_component(player_.orientation.x),
                               quantized_orientation_component(player_.orientation.y),
                               quantized_orientation_component(player_.orientation.z)};
    }

    // Consume latched key-down edges: an action pressed since the previous
    // tick counts as held for this one tick even if already released.
    for (std::size_t index{}; index < held_.size(); ++index) {
        tick_held_[index] = held_[index] || press_latch_[index];
    }
    press_latch_.fill(false);
    primary_tick_held_ = primary_held_ || primary_press_latch_;
    primary_press_latch_ = false;
    if (machine_gun_.locks_movement()) {
        // Character.set_walk / set_jump / set_crouch return at once while
        // is_weapon_deployed or is_deploying_weapon is set.
        for (const auto action : {TutorialAction::forward, TutorialAction::backward,
                                  TutorialAction::left, TutorialAction::right,
                                  TutorialAction::jump, TutorialAction::crouch}) {
            tick_held_[static_cast<std::size_t>(action)] = false;
        }
        jump_requested_ = false;
    }
    const auto tick_held = [this](TutorialAction action) {
        return tick_held_[static_cast<std::size_t>(action)];
    };

    apply_crouch_request(player_, tick_held(TutorialAction::crouch), map_.get(),
                         player_collision_bodies_,
                         tick_held(TutorialAction::hover) && player_.jetpack == 4U);

    PlayerInputState sampled_input;
    sampled_input.forward = tick_held(TutorialAction::forward);
    sampled_input.backward = tick_held(TutorialAction::backward);
    sampled_input.left = tick_held(TutorialAction::left);
    sampled_input.right = tick_held(TutorialAction::right);
    // Combat packs consume sustained SPACE for ignition and thrust. Keeping
    // only the ordinary tutorial jump edge made offline packs stop after one
    // frame and prevented them from ever reaching their ignition delay.
    sampled_input.jump = (config_.network_authoritative || player_.jetpack != 0U)
        ? tick_held(TutorialAction::jump) : jump_requested_;
    sampled_input.crouch = tick_held(TutorialAction::crouch);
    sampled_input.sneak = tick_held(TutorialAction::sneak);
    sampled_input.sprint = tick_held(TutorialAction::sprint);
    sampled_input.hover = tick_held(TutorialAction::hover);
    jump_requested_ = false;
    auto input = sampled_input;
    const auto current_aim = player_.orientation;
    if (config_.network_authoritative) {
        // Character records/sends ClientData after its native movement frame.
        // Directional movement and jump use the observed packet-L-1 button
        // latch; crouch geometry and action-hover are already current in L.
        // This mixed phase is recovered from the retail Character update.
        input = network_latched_input_;
        input.crouch = sampled_input.crouch;
        input.hover = sampled_input.hover;
        network_latched_input_ = sampled_input;
        // Mouse look is immediate for rendering and shooting, while retail
        // movement consumes the preceding ClientData orientation with its
        // locomotion buttons. Preserve both phases in the replay journal.
        player_.orientation = network_latched_orientation_.value_or(current_aim);
        network_latched_orientation_ = current_aim;
    }
    last_simulated_input_ = input;
    last_simulated_orientation_ = player_.orientation;
    last_simulated_jetpack_damage_ = std::exchange(jetpack_damage_pending_, false);
    if (alive()) {
        advance_jetpack_prediction(jetpack_prediction_, player_.jetpack, input,
                                  config_.fixed_dt, last_simulated_jetpack_damage_,
                                  !player_.airborne || player_.wade);
    } else {
        const double fuel_at_death = jetpack_prediction_.fuel;
        jetpack_prediction_ = {};
        jetpack_prediction_.fuel = fuel_at_death;
    }
    player_.jetpack_active = jetpack_prediction_.physics_active;
    player_.jetpack_passive = player_.jetpack == 2U && player_.jetpack_active;
    last_simulated_jetpack_active_ = player_.jetpack_active;
    // Canopy state is server-owned (WorldUpdate state bit 0x01). Stock
    // world.pyd keeps the airborne SPACE request for parachute holders, so an
    // airborne SPACE press deploys exactly as in retail; the negotiated Z/hover
    // edge stays as an extra binding (decision D3). Both edges run through the
    // server's own rules (BS/server/player.py _update_parachute), so a refused
    // deploy never floats locally.
    const bool hover_pressed = input.hover && !parachute_deploy_last_held_;
    const bool jump_pressed = input.jump && !parachute_jump_last_held_;
    parachute_deploy_last_held_ = input.hover;
    parachute_jump_last_held_ = input.jump;
    last_simulated_parachute_pressed_ = hover_pressed || jump_pressed;
    advance_parachute_rules(player_, last_simulated_parachute_pressed_,
                            alive() && player_.parachute && player_.jetpack == 0U,
                            input.crouch, map_.get(), config_.fixed_dt);
    last_simulated_parachute_active_ = player_.parachute_active;
    const bool was_airborne = player_.airborne;
    const double pre_move_vz = player_.velocity.z;
    if (was_airborne && player_.parachute_active) parachute_fall_touched_ = true;
    auto movement = step_player(
        player_, input, map_.get(), config_.fixed_dt, movement_class_,
        player_collision_bodies_, config_.gravity,
        server_movement_bounds_.has_value() ? &*server_movement_bounds_ : nullptr);
    if (!player_.airborne || player_.wade) {
        // A canopy zeroes the native fall distance every frame, but the server
        // charges a late canopy the free fall that reaches the same landing
        // speed (Player._parachute_after_move). Predict it for the land cue.
        if (parachute_fall_touched_ && was_airborne) {
            const int canopy_damage = parachute_landing_damage(
                pre_move_vz, last_simulated_parachute_active_, config_.fixed_dt,
                config_.gravity, movement_class_, player_.position.z);
            if (canopy_damage > std::max(0, movement.landing_damage)) {
                movement.landing_damage = canopy_damage;
            }
        }
        parachute_fall_touched_ = false;
    }
    settle_parachute_after_move(player_);
    player_.orientation = current_aim;
    movement_events_.jumped = movement_events_.jumped || movement.jumped;
    movement_events_.climbed = movement_events_.climbed || movement.climbed;
    movement_events_.landed = movement_events_.landed || movement.landed;
    movement_events_.hard_landing = movement_events_.hard_landing || movement.hard_landing;
    if (movement.landing_damage > 0) {
        movement_events_.landing_damage =
            std::max(movement_events_.landing_damage, movement.landing_damage);
    } else if (movement.landing_damage < 0 && movement_events_.landing_damage == 0) {
        movement_events_.landing_damage = movement.landing_damage;
    }

    if (config_.network_authoritative) {
        // Generate the same semantic action edges as offline play; the network
        // adapter sends them to the authority. process_weapon_action returns
        // before any local hit/terrain mutation in this mode.
        update_weapon_sandbox();
    } else {
        update_combat();
    }
    update_reload_aim();

    // The spawn lane is the (0,0) origin, so lane-local x is world x.
    if (!config_.network_authoritative &&
        lessons_.tick(config_.fixed_dt,
                      player_.position.x,
                      input.jump || action_held(TutorialAction::jump),
                      action_held(TutorialAction::crouch))) {
        handle_stage_entered();
    }

    // External gates: five destroyed targets complete SHOOTING, standing on
    // the tower top completes CLIMB, exactly like the reconstructed server.
    if (!shooting_gate_reported_ && std::count(target_down_.begin(), target_down_.end(), true) ==
                                        static_cast<int>(target_down_.size())) {
        shooting_gate_reported_ = true;
        if (lessons_.advance_external(TutorialLessonStage::shooting)) {
            handle_stage_entered();
        }
    }
    if (!config_.network_authoritative && !climb_gate_reported_ &&
        lessons_.stage() == TutorialLessonStage::climb &&
        TutorialLessons::on_tower_top(player_.position.x, player_.position.y,
                                      player_.position.z)) {
        climb_gate_reported_ = true;
        if (lessons_.advance_external(TutorialLessonStage::climb)) {
            handle_stage_entered();
        }
    }
    update_entities();
    if (config_.network_authoritative) {
        if (position_lerp_timer_ > 0.0) {
            // Character.pyx lines 1498-1506: retain 90% of the prior
            // presentation, blend 10% of the corrected native position, then
            // extrapolate by half of the 32x native movement scale.
            constexpr double corrected_position_weight{0.1};
            constexpr double retained_position_weight{0.9};
            const double extrapolation = config_.fixed_dt * 16.0;
            network_interpolated_position_.x =
                player_.position.x * corrected_position_weight +
                network_interpolated_position_.x * retained_position_weight +
                player_.velocity.x * extrapolation;
            network_interpolated_position_.y =
                player_.position.y * corrected_position_weight +
                network_interpolated_position_.y * retained_position_weight +
                player_.velocity.y * extrapolation;
            network_interpolated_position_.z =
                player_.position.z * corrected_position_weight +
                network_interpolated_position_.z * retained_position_weight +
                player_.velocity.z * extrapolation;
            position_lerp_timer_ = std::max(0.0, position_lerp_timer_ - config_.fixed_dt);
        } else {
            network_interpolated_position_ = player_.position;
        }
    }
    ++ticks_;
}

void TutorialWorldSession::handle_stage_entered() {
    entered_stage_ = lessons_.stage();
    // SetClassLoadout instant=1 equips the final item of the granted list:
    // the pistol at the gallery, the spade of (pistol, block, spade) at the
    // climb (block supply granted alongside).
    if (lessons_.stage() == TutorialLessonStage::shooting) {
        sync_inventory_slots(TutorialTool::pistol, InventorySelectionOrigin::loadout_sync);
    } else if (lessons_.stage() == TutorialLessonStage::climb) {
        sync_inventory_slots(TutorialTool::spade, InventorySelectionOrigin::loadout_sync);
        blocks_remaining_ = climb_block_grant;
    }
}

void TutorialWorldSession::set_primary_held(bool held) noexcept {
    if (held && !primary_held_) {
        primary_edge_ = true;
        primary_press_latch_ = true;
    }
    primary_held_ = held;
}

void TutorialWorldSession::set_secondary_held(bool held) noexcept {
    const bool pressed = held && !secondary_held_;
    secondary_held_ = held;
    const auto selected = selected_tool_id();
    const auto* weapon = selected.has_value() ? find_weapon_definition(*selected) : nullptr;
    const auto behavior =
        weapon != nullptr ? weapon_secondary_behavior(*weapon) : WeaponSecondaryBehavior::none;
    // Character.use_weapon_secondary calls set_zoom(not self.zoom) on the
    // press edge. Retail ADS is therefore a toggle, not a hold-to-aim state.
    // Iron sights and magnified scopes take the identical path: they differ in
    // magnification and transition rate, not in how they are entered.
    if (weapon_reload_remaining()>0.0) {
        update_reload_aim();
    } else if (action_held(TutorialAction::sprint)) {
        // Starting sprint already exits ADS. Also reject subsequent aim
        // presses until sprint ends, without losing the physical button state
        // or bypassing the tool-owned secondary actions below.
        zoomed_ = false;
    } else if (pressed && aims_down_sights(behavior)) {
        zoomed_ = !zoomed_;
    } else if (!aims_down_sights(behavior)) {
        zoomed_ = false;
    }

    if (debug_full_loadout_) {
        const bool routes_to_tool = behavior == WeaponSecondaryBehavior::spin_up ||
                                    behavior == WeaponSecondaryBehavior::tool_action ||
                                    behavior == WeaponSecondaryBehavior::deploy_machine_gun;
        // Always forward release. This prevents a tool action from sticking
        // when selection or class changes while RMB is still physically held.
        sandbox_inventory_.weapons().set_secondary(held && routes_to_tool);
    }
}

void TutorialWorldSession::trigger_weapon_custom() noexcept {
    if (debug_full_loadout_) {
        custom_edge_ = true;
    }
}

void TutorialWorldSession::set_weapon_custom_held(bool held) noexcept {
    if (held && !custom_held_) {
        trigger_weapon_custom();
    }
    custom_held_ = held;
    if (debug_full_loadout_) {
        sandbox_inventory_.weapons().set_custom(held);
    }
}

WeaponStateResult TutorialWorldSession::request_reload() noexcept {
    const auto result=debug_full_loadout_ ? sandbox_inventory_.weapons().request_reload()
                                        : WeaponStateResult::invalid_tool;
    if(result==WeaponStateResult::accepted)update_reload_aim();
    return result;
}

void TutorialWorldSession::update_reload_aim() noexcept {
    const auto selected=selected_tool_id();
    if(weapon_reload_remaining()>0.0){
        reload_aim_tool_=selected;
        zoomed_=false;
    }else if(reload_aim_tool_){
        const auto* weapon=selected?find_weapon_definition(*selected):nullptr;
        zoomed_=selected==reload_aim_tool_&&secondary_held_&&!action_held(TutorialAction::sprint)&&
            weapon&&aims_down_sights(weapon_secondary_behavior(*weapon));
        reload_aim_tool_.reset();
    }
}

void TutorialWorldSession::restock_ammunition() noexcept {
    if (debug_full_loadout_) {
        sandbox_inventory_.restock_ammunition();
    }
}

void TutorialWorldSession::restock_from_ammo_crate() noexcept {
    if (debug_full_loadout_) {
        static_cast<void>(sandbox_inventory_.restock_from_ammo_crate());
    }
}

void TutorialWorldSession::restock_blocks() noexcept {
    if (debug_full_loadout_) {
        sandbox_inventory_.restock_blocks();
        blocks_remaining_ = static_cast<int>(sandbox_inventory_.blocks());
    }
}

void TutorialWorldSession::restock_jetpack_fuel() noexcept {
    jetpack_prediction_.fuel = 100.0;
    if (!network_predictions_.empty()) {
        last_jetpack_fuel_loop_ = network_predictions_.back().loop;
        network_predictions_.back().jetpack.fuel = 100.0;
        network_predictions_.back().jetpack_restocked = true;
    }
}

void TutorialWorldSession::grant_blocks(std::uint16_t amount) noexcept {
    if (!debug_full_loadout_ || amount == 0U) {
        return;
    }
    sandbox_inventory_.add_blocks(amount);
    blocks_remaining_ = static_cast<int>(sandbox_inventory_.blocks());
}

std::uint16_t TutorialWorldSession::spend_server_confirmed_blocks(std::uint16_t amount) noexcept {
    if (!config_.network_authoritative || !debug_full_loadout_ || amount == 0U ||
        infinite_blocks_) {
        return 0U;
    }
    const auto debit = std::min(amount, sandbox_inventory_.blocks());
    if (debit != 0U) {
        static_cast<void>(sandbox_inventory_.spend_blocks(debit));
    }
    blocks_remaining_ = static_cast<int>(sandbox_inventory_.blocks());
    return debit;
}

void TutorialWorldSession::set_block_color(VxlColor color) noexcept {
    color.alpha = 255U;
    block_color_ = color;
}

void TutorialWorldSession::update_combat() {
    if (debug_full_loadout_) {
        update_weapon_sandbox();
        return;
    }
    const double dt = config_.fixed_dt;
    since_primary_ += dt;
    inventory_.tick(dt);
    tool_cooldown_ = std::max(0.0, tool_cooldown_ - dt);
    if (reload_remaining_ > 0.0) {
        reload_remaining_ -= dt;
        if (reload_remaining_ <= 0.0) {
            reload_remaining_ = 0.0;
            const int clip_size =
                static_cast<int>(tool_definition(retail_pistol_tool_id).clip_size);
            const int take = std::min(clip_size - pistol_clip_, pistol_stock_);
            pistol_clip_ += take;
            pistol_stock_ -= take;
        }
    }

    const bool attack_edge = primary_edge_;
    primary_edge_ = false;
    const auto equipped = equipped_tool();
    if (!equipped.has_value()) {
        return;
    }
    switch (*equipped) {
    case TutorialTool::pistol:
        if (attack_edge && tool_cooldown_ <= 0.0) {
            if (reload_remaining_ > 0.0) {
                break;
            }
            if (pistol_clip_ > 0) {
                fire_pistol();
            } else {
                attack_events_.pistol_dry = true;
            }
        }
        break;
    case TutorialTool::spade:
        // Retail DiggingTool repeats while the button is held.
        if ((attack_edge || primary_held_) && tool_cooldown_ <= 0.0) {
            swing_spade();
        }
        break;
    case TutorialTool::block:
        if (attack_edge && tool_cooldown_ <= 0.0 && blocks_remaining_ > 0) {
            place_block();
        }
        break;
    }
}

void TutorialWorldSession::update_weapon_sandbox() {
    since_primary_ += config_.fixed_dt;
    auto& runtime = sandbox_inventory_.weapons();
    bool deployable_target_valid = true;
    if (const auto selected = runtime.replication().selected_tool(); selected.has_value()) {
        const auto& weapon = weapon_catalog()[*selected];
        if (weapon.mechanism == WeaponMechanism::deployable ||
            weapon.mechanism == WeaponMechanism::c4 ||
            weapon.mechanism == WeaponMechanism::deployed_machine_gun) {
            deployable_target_valid = deployable_target(*selected).has_value();
        }
    }
    runtime.set_context({action_held(TutorialAction::sprint),
                         machine_gun_.deployed(),
                         disguise_active_,
                         deployable_target_valid});
    // MGWeapon.update: set_primary_shoot(False) while either timer counts.
    runtime.set_primary((primary_held_ || primary_tick_held_) && !machine_gun_.blocks_primary());
    runtime.set_custom(custom_held_);
    sandbox_inventory_.tick(config_.fixed_dt);
    const auto actions = runtime.take_actions();
    for (const auto& action : actions) {
        process_weapon_action(action);
    }
    if (custom_edge_) {
        // Weapon-custom and RMB are distinct retail inputs. Selection owns
        // cancellation so this edge cannot leak into the next equipped tool.
        custom_edge_ = false;
    }
    // Live sessions also own cosmetic projectiles. Their terrain mutation is
    // suppressed in explode_projectile(), but their flight must continue every
    // fixed tick or server-relayed grenades freeze at the packet-10 origin.
    update_projectiles();
}

void TutorialWorldSession::process_weapon_action(const WeaponAction& action) {
    const auto* weapon = find_weapon_definition(action.tool_id);
    if (weapon == nullptr) {
        return;
    }
    if (config_.network_authoritative && action.kind == WeaponActionKind::oriented_item &&
        (action.tool_id == 29U || action.tool_id == 48U)) {
        // SnowBlowerWeapon.use_an_ammo consumes the shared block wallet when
        // fired, even if the later impact cannot place a voxel. Every other
        // oriented launcher uses its own magazine, so this cannot live in the
        // generic WeaponRuntime ammunition path.
        if (!infinite_blocks_ && !sandbox_inventory_.spend_blocks()) {
            return;
        }
        if (!infinite_blocks_) {
            blocks_remaining_ = static_cast<int>(sandbox_inventory_.blocks());
        }
    }
    weapon_actions_.push_back(action);
    if (action.kind == WeaponActionKind::placement_rejected) {
        // Audio-only edge: no wire action, no sequence, no local placement.
        return;
    }
    if (config_.network_authoritative) {
        if (action.kind != WeaponActionKind::reload_started &&
            action.kind != WeaponActionKind::reload_completed &&
            action.kind != WeaponActionKind::dry_fire &&
            action.kind != WeaponActionKind::throwable_primed) {
            since_primary_ = 0.0;
            ++weapon_action_sequence_;
        }
        // The retail server excludes the thrower when rebroadcasting the three
        // legacy grenade variants because Character.throw_grenade already
        // created the local object. Reproduce that prediction boundary here.
        // Entity-backed launchers must NOT be predicted or the subsequent
        // CreateEntity would produce a duplicate rocket.
        if (action.kind == WeaponActionKind::oriented_item &&
            predicts_oriented_item_locally(action.tool_id)) {
            spawn_projectile(action, *weapon);
        }
        // The server owns hit validation, ammo and terrain, but recoil is a
        // local camera response in the retail client. Returning before this
        // call made every live weapon perfectly still while the same weapon
        // kicked correctly in the offline parity lab.
        if (action.kind == WeaponActionKind::hitscan ||
            action.kind == WeaponActionKind::melee ||
            action.kind == WeaponActionKind::oriented_item) {
            apply_weapon_recoil(action, *weapon);
        }
        return;
    }
    if (action.kind == WeaponActionKind::reload_started ||
        action.kind == WeaponActionKind::reload_completed ||
        action.kind == WeaponActionKind::dry_fire) {
        return;
    }
    if (weapon->mechanism == WeaponMechanism::deployed_machine_gun &&
        (action.kind == WeaponActionKind::deployable_place ||
         action.kind == WeaponActionKind::objective_use)) {
        // The mounted gun unfolds where its carrier stands (MGWeapon); the
        // two actions only tell a server about it.
        return;
    }
    // Deployables and objectives are what turn a tool into a thing in the
    // world. Without these three cases every one of them was a no-op: the
    // weapon fired, spent its ammo and produced nothing.
    if (action.kind == WeaponActionKind::deployable_place) {
        static_cast<void>(place_deployable(action));
        return;
    }
    if (action.kind == WeaponActionKind::objective_use) {
        static_cast<void>(drop_carried_objective(action));
        return;
    }
    if (action.kind == WeaponActionKind::c4_detonate) {
        static_cast<void>(detonate_all_c4());
        return;
    }
    if (action.kind == WeaponActionKind::block_line_begin &&
        action.tool_id == retail_block_tool_id) {
        place_block(false);
        return;
    }
    // The flare block arrives as its own action kind, not as a block line, and
    // was previously dropped by the early-out below -- pressing fire with tool
    // 22 placed nothing at all. It is an ordinary destructible voxel that also
    // registers a static point light, exactly as retail's FlareBlockEntity did.
    if (action.kind == WeaponActionKind::flare_place) {
        if (!place_block(true)) {
            // flareBlockTool.py use_primary: a refused cube plays
            // BUILD_ERROR_SOUND (presentation only; nothing is placed).
            weapon_actions_.push_back(WeaponAction{WeaponActionKind::placement_rejected,
                                                   action.tool_id, 0U, 1U, false, 0.0, false,
                                                   0.0});
        }
        return;
    }
    if (action.kind != WeaponActionKind::hitscan && action.kind != WeaponActionKind::melee &&
        action.kind != WeaponActionKind::oriented_item) {
        return;
    }

    since_primary_ = 0.0;
    ++weapon_action_sequence_;
    if (action.kind == WeaponActionKind::hitscan && action.tool_id == retail_pistol_tool_id) {
        attack_events_.pistol_fired = true;
    }
    if (action.kind == WeaponActionKind::melee && action.tool_id == retail_spade_tool_id) {
        attack_events_.spade_swung = true;
    }
    apply_weapon_recoil(action, *weapon);
    if (action.kind == WeaponActionKind::oriented_item) {
        spawn_projectile(action, *weapon);
        return;
    }

    const auto pellets =
        action.kind == WeaponActionKind::melee ? 1U : std::max<std::uint8_t>(1U, action.pellets);
    for (std::uint8_t pellet{}; pellet < pellets; ++pellet) {
        Vec3 direction = player_.orientation;
        if (action.kind == WeaponActionKind::hitscan) {
            const double raw_accuracy = zoomed_ && weapon->retail.aim.accuracy_zoom.has_value()
                                            ? *weapon->retail.aim.accuracy_zoom
                                            : sandbox_inventory_.weapons().current_accuracy();
            const double accuracy = raw_accuracy * (zoomed_ ? 1.0 : 2.0);
            const Vec3 right = normalized({-direction.y, direction.x, 0.0});
            const double side = seed_signed(action.seed, pellet * 2U + 1U) * accuracy;
            const double vertical = seed_signed(action.seed, pellet * 2U + 2U) * accuracy;
            direction = normalized({direction.x + right.x * side,
                                    direction.y + right.y * side,
                                    direction.z + vertical});
        }
        const double range =
            action.kind == WeaponActionKind::melee ? melee_world_range : weapon->maximum_range;
        const auto hit = raycast_voxels(*map_, player_.position, direction, range);
        if (!hit.has_value()) {
            continue;
        }
        const auto color = map_->color(hit->x, hit->y, hit->z);
        if (action.kind == WeaponActionKind::melee) {
            if (color.has_value()) {
                const TerrainImpactEvent impact{TerrainImpactKind::melee,
                                                {hit->x, hit->y, hit->z},
                                                *color,
                                                {hit->nx, hit->ny, hit->nz},
                                                false,
                                                1.0F,
                                                action.tool_id};
                apply_melee_terrain(action, *weapon, impact.cell, impact);
                if (action.tool_id == retail_spade_tool_id) {
                    attack_events_.spade_hit_block = true;
                }
            }
            continue;
        }
        const double raw_damage = action.kind == WeaponActionKind::melee
                                      ? weapon->block_damage
                                      : weapon->retail.damage.block.value_or(weapon->block_damage);
        const bool destroyed = damage_voxel(hit->x, hit->y, hit->z, std::max(0.0, raw_damage));
        if (color.has_value()) {
            terrain_impacts_.push_back({action.kind == WeaponActionKind::melee
                                            ? TerrainImpactKind::melee
                                            : TerrainImpactKind::bullet,
                                        {hit->x, hit->y, hit->z},
                                        *color,
                                        {hit->nx, hit->ny, hit->nz},
                                        destroyed,
                                        1.0F,
                                        action.tool_id});
            if (action.kind == WeaponActionKind::melee && action.tool_id == retail_spade_tool_id) {
                attack_events_.spade_hit_block = true;
            }
        }
    }
}

void TutorialWorldSession::spawn_projectile(const WeaponAction& action,
                                            const WeaponDefinition& weapon) {
    const double speed = projectile_speed(weapon, action);
    const Vec3 direction = normalized(player_.orientation);
    TutorialProjectile projectile;
    projectile.id = next_projectile_id_++;
    projectile.tool_id = action.tool_id;
    // Character.throw_* sends world_object.position and adds the thrower's
    // live velocity. Starting ahead of the eye made close throws tunnel.
    projectile.position = player_.position;
    projectile.spawn_position = projectile.position;
    projectile.presentation_age = -config_.fixed_dt;
    projectile.velocity = {player_.velocity.x + direction.x * speed,
                           player_.velocity.y + direction.y * speed,
                           player_.velocity.z + direction.z * speed};
    projectile.behavior = projectile_behavior(action.tool_id);
    // Same rule as radius: use the generator's primary explosion damage, not
    // a suffix match that can accidentally select Molotov block-fire damage.
    projectile.block_damage = weapon.block_damage;
    projectile.crater_radius = projectile_crater_radius(weapon);
    if (weapon.mechanism == WeaponMechanism::cooked_throwable) {
        projectile.remaining = std::max(0.05, action.value);
    } else if (action.tool_id == 55U) {
        projectile.remaining =
            named_weapon_value(weapon, "GRENADE_LAUNCHER_PROJECTILE_LIFESPAN", 3.0);
    } else if (projectile.behavior == TutorialProjectileBehavior::stick) {
        projectile.remaining = 10.0; // flight failsafe; impact arms a 5 s fuse
    } else {
        projectile.remaining = weapon.fuse_time > 0.0 ? weapon.fuse_time : 10.0;
    }
    projectile.gravity_multiplier = projectile_gravity(action.tool_id);
    projectiles_.push_back(projectile);
}

bool TutorialWorldSession::apply_server_oriented_item(
    std::uint8_t player_id, std::uint8_t tool_id, double value, Vec3 position, Vec3 velocity) {
    const auto* weapon = find_weapon_definition(tool_id);
    if (weapon == nullptr || !weapon->projectile || !std::isfinite(value) ||
        !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) ||
        !std::isfinite(velocity.x) || !std::isfinite(velocity.y) || !std::isfinite(velocity.z)) {
        return false;
    }

    TutorialProjectile projectile;
    projectile.id = next_projectile_id_++;
    projectile.tool_id = tool_id;
    projectile.position = position;
    projectile.spawn_position = position;
    projectile.presentation_age = -config_.fixed_dt;
    projectile.velocity = velocity;
    projectile.behavior = projectile_behavior(tool_id);
    projectile.block_damage = weapon->block_damage;
    projectile.crater_radius = projectile_crater_radius(*weapon);
    projectile.gravity_multiplier = projectile_gravity(tool_id);
    projectile.source_player = player_id;

    if (weapon->mechanism == WeaponMechanism::cooked_throwable) {
        projectile.remaining = std::max(0.05, value);
    } else if (tool_id == 55U) {
        projectile.remaining =
            named_weapon_value(*weapon, "GRENADE_LAUNCHER_PROJECTILE_LIFESPAN", 3.0);
    } else if (projectile.behavior == TutorialProjectileBehavior::stick) {
        projectile.remaining = 10.0;
    } else {
        projectile.remaining = weapon->fuse_time > 0.0 ? weapon->fuse_time : 10.0;
    }
    projectiles_.push_back(projectile);
    return true;
}

bool TutorialWorldSession::apply_server_drill_contact(
    std::int16_t causer_id, std::uint8_t player_id, Vec3 position) noexcept {
    if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
        !std::isfinite(position.z)) {
        return false;
    }

    constexpr double contact_loop_seconds{0.5}; // retail A1509
    if (causer_id >= 0) {
        const auto entity = std::ranges::find(
            entities_, static_cast<std::uint64_t>(causer_id), &LocalEntity::id);
        if (entity != entities_.end() && entity->type == 23U) {
            entity->drilling_audio_remaining = contact_loop_seconds;
            return true;
        }
    }

    // A few compatible relays lose the signed entity id but retain the owner.
    // Pick the nearest live Drill deterministically instead of opening a
    // position-only global loop that can survive its projectile.
    const auto distance_squared = [&position](const Vec3& candidate) noexcept {
        const auto dx = candidate.x - position.x;
        const auto dy = candidate.y - position.y;
        const auto dz = candidate.z - position.z;
        return dx * dx + dy * dy + dz * dz;
    };
    auto nearest_entity = entities_.end();
    double nearest_entity_distance = std::numeric_limits<double>::max();
    for (auto entity = entities_.begin(); entity != entities_.end(); ++entity) {
        if (entity->type != 23U || entity->owner != player_id) {
            continue;
        }
        const auto distance = distance_squared(entity->position);
        if (distance < nearest_entity_distance) {
            nearest_entity = entity;
            nearest_entity_distance = distance;
        }
    }
    if (nearest_entity != entities_.end()) {
        nearest_entity->drilling_audio_remaining = contact_loop_seconds;
        return true;
    }

    auto nearest_projectile = projectiles_.end();
    double nearest_projectile_distance = std::numeric_limits<double>::max();
    for (auto projectile = projectiles_.begin(); projectile != projectiles_.end(); ++projectile) {
        const bool is_drill = projectile->tool_id == 14U || projectile->tool_id == 47U;
        if (!is_drill || projectile->source_player != player_id) {
            continue;
        }
        const auto distance = distance_squared(projectile->position);
        if (distance < nearest_projectile_distance) {
            nearest_projectile = projectile;
            nearest_projectile_distance = distance;
        }
    }
    if (nearest_projectile == projectiles_.end()) {
        return false;
    }
    nearest_projectile->drilling_audio_remaining = contact_loop_seconds;
    return true;
}

void TutorialWorldSession::update_projectiles() {
    const double dt = config_.fixed_dt;
    for (auto iterator = projectiles_.begin(); iterator != projectiles_.end();) {
        iterator->presentation_age += dt;
        iterator->remaining -= dt;
        iterator->drilling_audio_remaining =
            std::max(0.0, iterator->drilling_audio_remaining - dt);
        if (iterator->stuck) {
            if (iterator->remaining <= 0.0) {
                explode_projectile(*iterator, std::nullopt);
                iterator = projectiles_.erase(iterator);
            } else {
                ++iterator;
            }
            continue;
        }
        const Vec3 old = iterator->position;
        // Projectile/grenade simulation uses the recovered 30 units/s^2
        // gravity, independently of character gravity.
        iterator->velocity.z += 30.0 * iterator->gravity_multiplier * dt;
        const double speed = std::sqrt(iterator->velocity.x * iterator->velocity.x +
                                       iterator->velocity.y * iterator->velocity.y +
                                       iterator->velocity.z * iterator->velocity.z);
        if (speed > 511.98999) {
            const double scale = 511.98999 / speed;
            iterator->velocity.x *= scale;
            iterator->velocity.y *= scale;
            iterator->velocity.z *= scale;
        }
        if (iterator->behavior == TutorialProjectileBehavior::drill) {
            // Retail advances this per UPDATE rather than per second, so the
            // spin rate is tied to the fixed 60 Hz step, not to wall time.
            iterator->roll += 10.0;
        }
        const Vec3 delta{
            iterator->velocity.x * dt, iterator->velocity.y * dt, iterator->velocity.z * dt};
        const double distance =
            std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        const auto hit = distance > 1e-9 ? raycast_voxels(*map_, old, normalized(delta), distance)
                                         : std::nullopt;
        if (hit.has_value()) {
            const VoxelCell cell{hit->x, hit->y, hit->z};
            if (iterator->behavior == TutorialProjectileBehavior::bounce) {
                // GRENADE_BOUNCE_SOUND is presentation for the frontend; the
                // pre-reflection speed lets it ignore a grenade at rest.
                projectile_bounces_.push_back(
                    {iterator->id,
                     iterator->tool_id,
                     old,
                     std::max({std::abs(iterator->velocity.x),
                               std::abs(iterator->velocity.y),
                               std::abs(iterator->velocity.z)})});
                if (hit->nx != 0)
                    iterator->velocity.x = -iterator->velocity.x;
                if (hit->ny != 0)
                    iterator->velocity.y = -iterator->velocity.y;
                if (hit->nz != 0)
                    iterator->velocity.z = -iterator->velocity.z;
                iterator->velocity.x *= 0.36;
                iterator->velocity.y *= 0.36;
                iterator->velocity.z *= 0.36;
                // Keep the grenade on the free side of the contact plane.
                iterator->position = {
                    old.x + hit->nx * 0.01, old.y + hit->ny * 0.01, old.z + hit->nz * 0.01};
            } else if (iterator->behavior == TutorialProjectileBehavior::stick) {
                iterator->stuck = true;
                iterator->velocity = {};
                iterator->position = old;
                iterator->remaining = 5.0;
            } else if (iterator->behavior == TutorialProjectileBehavior::drill) {
                // Bore rather than stop. Retail's server owns which voxels
                // die; locally we carve the same radius-3 sphere retail's
                // BlockManager.handle_drill_damage expands each packet into,
                // and the velocity is deliberately NOT cleared so the drill
                // keeps grinding forward through the hole it just made.
                if (config_.network_authoritative) {
                    // In a live match only packet 32/33/22 terrain mutations
                    // may alter the map. The projectile remains visible while
                    // the authoritative server emits its drill footprint.
                    iterator->position = {old.x + delta.x, old.y + delta.y, old.z + delta.z};
                    ++iterator;
                    continue;
                }
                bool carved{};
                std::vector<VoxelCell> removed;
                for (const auto& bore : radius_cells(cell, 3U)) {
                    if (!map_->color(bore.x, bore.y, bore.z).has_value()) {
                        continue;
                    }
                    if (damage_voxel(bore.x, bore.y, bore.z, 5.0, false)) {
                        removed.push_back(bore);
                        carved = true;
                    }
                }
                if (carved) {
                    iterator->drilling_audio_remaining = 0.5; // retail A1509
                    auto falling = collapse_unsupported_components(*map_, removed);
                    for (auto& component : falling) {
                        for (const auto& voxel : component) {
                            mark_dirty(voxel.cell.x, voxel.cell.y);
                        }
                        falling_components_.push_back(std::move(component));
                    }
                    terrain_impacts_.push_back({TerrainImpactKind::drill,
                                                cell,
                                                map_->color(cell.x, cell.y, cell.z)
                                                    .value_or(VxlColor{150U, 150U, 150U, 255U}),
                                                {0, 0, -1},
                                                true,
                                                1.5F,
                                                iterator->tool_id});
                }
                iterator->position = {old.x + delta.x, old.y + delta.y, old.z + delta.z};
            } else if (iterator->behavior == TutorialProjectileBehavior::deploy) {
                iterator = projectiles_.erase(iterator);
                continue;
            } else {
                explode_projectile(*iterator, cell);
                iterator = projectiles_.erase(iterator);
                continue;
            }
        }
        if (iterator->remaining <= 0.0) {
            explode_projectile(*iterator, std::nullopt);
            iterator = projectiles_.erase(iterator);
            continue;
        }
        if (iterator->position.x < 0.0 || iterator->position.y < 0.0 ||
            iterator->position.x >= VxlMap::width || iterator->position.y >= VxlMap::depth ||
            iterator->position.z >= VxlMap::height) {
            iterator = projectiles_.erase(iterator);
            continue;
        }
        if (!hit.has_value()) {
            iterator->position = {old.x + delta.x, old.y + delta.y, old.z + delta.z};
        }
        ++iterator;
    }
}

void TutorialWorldSession::explode_projectile(const TutorialProjectile& projectile,
                                              std::optional<VoxelCell> contact) {
    const auto clamp_axis = [](double value, std::uint32_t maximum) {
        return static_cast<std::uint32_t>(
            std::clamp(std::floor(value), 0.0, static_cast<double>(maximum)));
    };
    const VoxelCell center =
        contact.value_or(VoxelCell{clamp_axis(projectile.position.x, VxlMap::width - 1U),
                                   clamp_axis(projectile.position.y, VxlMap::depth - 1U),
                                   clamp_axis(projectile.position.z, VxlMap::height - 2U)});
    const std::array<float, 3U> presentation_position{static_cast<float>(projectile.position.x),
                                                      static_cast<float>(projectile.position.y),
                                                      static_cast<float>(projectile.position.z)};
    const std::array<float, 3U> presentation_velocity{static_cast<float>(projectile.velocity.x),
                                                      static_cast<float>(projectile.velocity.y),
                                                      static_cast<float>(projectile.velocity.z)};
    if (projectile.tool_id == 29U || projectile.tool_id == 48U) {
        // Snowballs die on contact, but do not explode. In a live match the
        // subsequent BlockBuildColored(33) owns the actual voxel; this event
        // is presentation-only and deliberately carries no destruction.
        const auto color = map_->color(center.x, center.y, center.z).value_or(block_color_);
        TerrainImpactEvent impact{TerrainImpactKind::block_cannon,
                                  center,
                                  color,
                                  {0, 0, -1},
                                  false,
                                  1.0F,
                                  projectile.tool_id};
        impact.position = presentation_position;
        impact.source_velocity = presentation_velocity;
        terrain_impacts_.push_back(impact);
        return;
    }
    bool destroyed{};
    VxlColor effect_color = projectile.tool_id == 54U ? VxlColor{82U, 210U, 72U, 255U}
                                                      : VxlColor{238U, 139U, 55U, 255U};
    const bool authored_particle_color = projectile.tool_id == 33U || projectile.tool_id == 54U;
    if (config_.network_authoritative) {
        // Flight and delete VFX are client presentation, but the server owns
        // every changed voxel. Mutating here caused the predicted crater to
        // race BlockDamage/BlockBuild catch-up packets and manufacture holes
        // that existed on only one client.
        if (!authored_particle_color) {
            // Grenade/Rocket explode: map.get_point(x, y, z + 1), the block
            // BELOW the blast (z grows downward). map.get_point on an air
            // cell returns colour 0, so an air burst's debris is black in
            // retail, not grey.
            effect_color =
                map_->color(center.x, center.y,
                            std::min<std::uint32_t>(center.z + 1U, VxlMap::height - 1U))
                    .value_or(VxlColor{0U, 0U, 0U, 255U});
        }
        TerrainImpactEvent impact{projectile.tool_id == 33U   ? TerrainImpactKind::fire
                                  : projectile.tool_id == 54U ? TerrainImpactKind::chemical
                                                              : TerrainImpactKind::explosion,
                                  center,
                                  effect_color,
                                  {0, 0, -1},
                                  false,
                                  static_cast<float>(projectile.crater_radius),
                                  projectile.explosion_sound_tool != 0U
                                      ? projectile.explosion_sound_tool
                                      : projectile.tool_id};
        impact.position = presentation_position;
        impact.source_velocity = presentation_velocity;
        terrain_impacts_.push_back(impact);
        return;
    }
    std::vector<VoxelCell> destroyed_cells;
    for (const auto& cell : radius_cells(center, projectile.crater_radius)) {
        if (const auto color = map_->color(cell.x, cell.y, cell.z); color.has_value()) {
            if (!authored_particle_color)
                effect_color = *color;
            const bool cell_destroyed =
                damage_voxel(cell.x, cell.y, cell.z, projectile.block_damage, false);
            destroyed = cell_destroyed || destroyed;
            if (cell_destroyed)
                destroyed_cells.push_back(cell);
        }
    }
    if (!destroyed_cells.empty()) {
        auto falling = collapse_unsupported_components(*map_, destroyed_cells);
        for (auto& component : falling) {
            attack_events_.blocks_collapsed += static_cast<int>(component.size());
            for (const auto& voxel : component)
                mark_dirty(voxel.cell.x, voxel.cell.y);
            falling_components_.push_back(std::move(component));
        }
    }
    TerrainImpactEvent impact{projectile.tool_id == 33U   ? TerrainImpactKind::fire
                              : projectile.tool_id == 54U ? TerrainImpactKind::chemical
                                                          : TerrainImpactKind::explosion,
                              center,
                              effect_color,
                              {0, 0, -1},
                              destroyed,
                              static_cast<float>(projectile.crater_radius),
                              projectile.explosion_sound_tool != 0U
                                  ? projectile.explosion_sound_tool
                                  : projectile.tool_id};
    impact.position = presentation_position;
    impact.source_velocity = presentation_velocity;
    terrain_impacts_.push_back(impact);
}

namespace {

/**
 * Retail selects the `_water` explosion variant by absolute depth, not by
 * whether the voxel is wet: everything at or below two layers above the
 * indestructible bed counts as submerged.
 */
[[nodiscard]] bool blast_in_water(double z) noexcept {
    return z >= static_cast<double>(VxlMap::height) - 2.0;
}

/** Rocket-turret rocket blast, alias block A1621/A1622/A1623. */
constexpr double turret_rocket_crater_radius{3.0};
constexpr double turret_rocket_block_damage{10.0};
/** A1605 tracking range, A1606 detection range, A1608 aiming speed. */
constexpr double turret_tracking_range{50.0};
constexpr double turret_detection_range{30.0};
constexpr double turret_aiming_speed{180.0};
constexpr double turret_shoot_interval{1.5};
/**
 * A1607 = 0.1, used here as an angular ERROR gate on firing.
 *
 * NOT retail's use of it. The retail client reads this constant only as an
 * angular RATE threshold, multiplied by ten, to decide when the aiming loop
 * should play (rocketTurret.py:65 -> 1.0 deg/s). Retail's server-side firing
 * rule was never recovered, so this borrows BattleSpades' interpretation.
 * Whoever wires the aim audio must use the rate form and NOT reuse this.
 */
constexpr double turret_aim_tolerance{0.1};
/** NOT RETAIL: BattleSpades' rocket muzzle speed. Retail's was not recovered. */
constexpr double turret_rocket_speed{75.0};

} // namespace

void TutorialWorldSession::step_entity_timers(LocalEntity& entity,
                                              const EntityDefinition& definition) {
    const double dt = config_.fixed_dt;
    entity.presentation_age += dt;
    entity.drilling_audio_remaining = std::max(0.0, entity.drilling_audio_remaining - dt);

    if (!entity.alive) {
        entity.respawn_remaining -= dt;
        if (entity.respawn_remaining <= 0.0) {
            entity.alive = true;
            entity.position = entity.home;
            entity.velocity = {};
            entity.grounded = false;
            entity.attached = false;
            entity.health = definition.health;
            entity.uses = definition.uses;
            entity.ammo = definition.ammo;
            entity_events_.push_back(
                {EntityEventKind::respawned, entity.id, entity.type, entity.position});
        }
        return;
    }

    if (is_server_projectile(entity, definition)) {
        // CreateEntity projectiles are deterministic client-side movers. Their
        // authoritative lifetime edge is DestroyEntity(19); consuming the fuse
        // here made rockets disappear early and then made the real destroy edge
        // silent. Each type uses its owning tool's recovered gravity.
        const auto tool = projectile_tool_for_entity(entity.type);
        const auto gravity_multiplier = tool.has_value() ? projectile_gravity(*tool) : 1.0;
        entity.velocity.z += 30.0 * gravity_multiplier * dt;
        const double speed = std::sqrt(entity.velocity.x * entity.velocity.x +
                                       entity.velocity.y * entity.velocity.y +
                                       entity.velocity.z * entity.velocity.z);
        if (speed > 511.98999) {
            const double scale = 511.98999 / speed;
            entity.velocity.x *= scale;
            entity.velocity.y *= scale;
            entity.velocity.z *= scale;
        }
        entity.position = {entity.position.x + entity.velocity.x * dt,
                           entity.position.y + entity.velocity.y * dt,
                           entity.position.z + entity.velocity.z * dt};
        // ExplodeOnImpactEntity spins its displayed round as it flies. Rockets
        // keep a stable roll while the drill deliberately spins much faster.
        if (entity.type == 23U) {
            entity.accumulators[0U] = std::fmod(entity.accumulators[0U] + 10.0, 360.0);
        } else if (entity.type == 27U || entity.type == 32U || entity.type == 33U ||
                   entity.type == 34U || entity.type == 37U) {
            entity.accumulators[0U] =
                std::fmod(entity.accumulators[0U] + std::min(speed * speed, 1000.0) * dt, 360.0);
        }
        return;
    }

    // SpinningEntity turn and the intel water float are client presentation.
    advance_entity_presentation(entity, dt);

    if (config_.network_authoritative) {
        // Reliable Create/Change/Destroy packets and WorldUpdate own every
        // gameplay transition in a live match. The client only supplies the
        // retail presentation gravity between snapshots. In particular it
        // must never arm a friendly mine or make a turret target its observer.
        if ((entity.type == 14U || entity.type == 15U || entity.type == 16U ||
             entity.type == 35U || entity.type == 36U) &&
            entity.fuse > 0.0) {
            // BombPickup / DiamondPickup / IntelPickup / RadarStationEntity /
            // AttachedStickyGrenadeEntity count their packet fuse down
            // locally between server updates (their 3D label reads it); a
            // later packet simply overwrites it.
            entity.fuse = std::max(0.0, entity.fuse - dt);
        }
        const auto physics = step_entity_terrain_physics(entity, definition, *map_, dt);
        if (physics.landed && !physics.bounced &&
            definition.category == EntityCategory::objective) {
            entity.home = entity.position;
        }
        if (physics.chute_opened) {
            entity_events_.push_back(
                {EntityEventKind::parachute_opened, entity.id, entity.type, entity.position});
        }
        if (physics.landed && entity.type >= 3U && entity.type <= 6U) {
            entity_events_.push_back({EntityEventKind::landed, entity.id, entity.type,
                                      entity.position, physics.impact_speed});
        }
        if (entity.type == 28U || entity.type == 31U) {
            // BlockFire/BlockGoo colour ramps read the remaining fuse. The
            // server owns the lifetime (DestroyEntity); this is presentation.
            entity.fuse = std::max(0.0, entity.fuse - dt);
        }
        return;
    }

    if (entity.arm_remaining > 0.0) {
        entity.arm_remaining -= dt;
        if (entity.arm_remaining <= 0.0) {
            entity.armed = true;
            entity_events_.push_back(
                {EntityEventKind::armed, entity.id, entity.type, entity.position});
        }
    }

    if (entity.fuse >= 0.0) {
        entity.fuse -= dt;
        if (entity.fuse <= 0.0) {
            entity.detonating = true;
        }
    }

    if (entity.lifetime_remaining >= 0.0) {
        entity.lifetime_remaining -= dt;
        if (entity.lifetime_remaining <= 0.0) {
            entity_events_.push_back(
                {EntityEventKind::expired, entity.id, entity.type, entity.position});
            entity.retired = true;
            return;
        }
    }

    if (entity.shoot_cooldown > 0.0) {
        entity.shoot_cooldown -= dt;
    }

    const auto physics = step_entity_terrain_physics(entity, definition, *map_, dt);
    if (physics.landed && !physics.bounced && definition.category == EntityCategory::objective) {
        // A dropped objective returns to where it actually landed, never to a
        // stale point in mid-air if it is later consumed and respawned.
        entity.home = entity.position;
    }
}

void TutorialWorldSession::step_entity_behaviour(LocalEntity& entity,
                                                 const EntityDefinition& definition) {
    // Test against the body centre rather than the eye: crates sit on the floor
    // and a sphere hung off the camera would miss a crate the player is
    // standing right on top of.
    const auto feet = player_.position.z + player_contact_offset(player_.crouch, player_.wade);
    const Vec3 body{player_.position.x, player_.position.y, (player_.position.z + feet) * 0.5};

    switch (definition.category) {
    case EntityCategory::pickup: {
        if (!within_touch_radius(entity, body, definition.touch_radius)) {
            break;
        }
        // Every branch consumes: retail's crate is taken even by a player who
        // needs nothing from it, which is what stops one walk-through banking
        // two crates.
        switch (entity.type) {
        case 3U: // AMMO_CRATE
            // Unconditional: retail consumes the crate even at full ammo.
            static_cast<void>(sandbox_inventory_.restock_from_ammo_crate());
            break;
        case 4U: // HEALTH_CRATE
            // Retail ships NO heal amount. 20-and-only-below-full is the
            // reference server's literal; our own server heals to maximum
            // unconditionally. The attested number wins, and `heal()` already
            // no-ops at full health. The catalog row is marked non-alias so the
            // debug readout shows this is a decision, not recovered parity.
            static_cast<void>(heal(static_cast<double>(definition.uses)));
            break;
        case 5U: // BLOCK_CRATE
            sandbox_inventory_.restock_blocks();
            blocks_remaining_ = static_cast<int>(sandbox_inventory_.blocks());
            break;
        case 6U: // JETPACK_CRATE
            restock_jetpack_fuel();
            break;
        default:
            break;
        }
        entity.alive = false;
        entity.respawn_remaining = definition.respawn_delay;
        entity_events_.push_back(
            {EntityEventKind::collected, entity.id, entity.type, entity.position});
        if (definition.respawn_delay <= 0.0F) {
            entity.retired = true;
        }
        break;
    }
    case EntityCategory::deployable: {
        if (entity.type == 8U) { // ROCKET_TURRET
            step_turret(entity, definition);
            break;
        }
        if (entity.type == 30U) { // MEDPACK
            if (within_touch_radius(entity, body, definition.touch_radius) &&
                health_ < maximum_player_health && entity.uses > 0U) {
                // Heal amount and use count are BattleSpades inventions; the
                // readout marks the row so this is never mistaken for parity.
                static_cast<void>(heal(25.0));
                --entity.uses;
                // Retail authored exactly two medpack cues -- place and
                // EXHAUSTED -- and no per-heal one. So healing is silent and
                // the pack only speaks when its last charge goes. Emitting
                // `collected` on every use would fire the exhausted sample
                // repeatedly, which is both wrong and irritating.
                if (entity.uses == 0U) {
                    entity_events_.push_back(
                        {EntityEventKind::collected, entity.id, entity.type, entity.position});
                    entity.retired = true;
                }
            }
            break;
        }
        // Landmine and projectile mine: a cylinder trip test, no line of sight.
        if (entity.armed && definition.trip_radius > 0.0F &&
            within_trip_volume(entity, body, definition.trip_radius, definition.trip_height)) {
            entity.detonating = true;
        }
        break;
    }
    case EntityCategory::objective: {
        // A just-thrown objective must not be caught again on the next tick:
        // the thrower is still standing inside its own pickup sphere.
        if (objective_pickup_lockout_ > 0.0) {
            break;
        }
        if (within_touch_radius(entity, body, definition.touch_radius)) {
            // Carrying is not a separate visual system in retail: the ground
            // entity is destroyed and the carrier EQUIPS the matching tool,
            // which is what puts the intel in the player's hands on screen.
            // PICKUPS = {BOMB: BOMB_TOOL, DIAMOND: DIAMOND_TOOL, INTEL: INTEL_TOOL}
            const std::uint8_t carry_tool = entity.type == 14U   ? 25U
                                            : entity.type == 15U ? 26U
                                                                 : 30U;
            if (debug_full_loadout_) {
                static_cast<void>(sandbox_inventory_.select_tool(carry_tool));
            }
            carried_objective_ = entity.type;
            entity_events_.push_back(
                {EntityEventKind::collected, entity.id, entity.type, entity.position});
            // Suppresses sprint without clearing the key, exactly as the
            // network burden byte does.
            player_.burdened = true;
            entity.retired = true;
        }
        break;
    }
    case EntityCategory::structure: {
        // The only executable retail behaviour on a capture point is a restock
        // that resolves to set_hp(100); the distance and refill-time constants
        // are read by no code in either tree.
        if (within_touch_radius(entity, body, definition.touch_radius)) {
            static_cast<void>(heal(maximum_player_health));
        }
        break;
    }
    case EntityCategory::hazard: {
        if (entity.type == 28U) { // BLOCKFIRE
            // Three INDEPENDENT retail timers -- player damage every 0.3 s,
            // block damage every 0.4 s, spread every 0.5 s. Modelled as
            // separate accumulators because folding them into one shared clock
            // silently locks them into a single cadence.
            for (auto& accumulator : entity.accumulators) {
                accumulator += config_.fixed_dt;
            }
            if (entity.accumulators[0U] >= 0.3) {
                entity.accumulators[0U] = 0.0;
                if (within_touch_radius(entity, body, 1.5F)) {
                    static_cast<void>(apply_damage(static_cast<double>(definition.blast_damage)));
                }
            }
            if (entity.accumulators[1U] >= 0.4) {
                entity.accumulators[1U] = 0.0;
                const auto cell = VoxelCell{
                    static_cast<std::uint32_t>(std::clamp(std::floor(entity.position.x),
                                                          0.0,
                                                          static_cast<double>(VxlMap::width - 1U))),
                    static_cast<std::uint32_t>(std::clamp(std::floor(entity.position.y),
                                                          0.0,
                                                          static_cast<double>(VxlMap::depth - 1U))),
                    static_cast<std::uint32_t>(
                        std::clamp(std::floor(entity.position.z) + 1.0,
                                   0.0,
                                   static_cast<double>(VxlMap::height - 2U)))};
                static_cast<void>(damage_voxel(cell.x, cell.y, cell.z, definition.block_damage));
            }
            // Presentation (colour-ramped patch, light, BLOCKFIRE smoke and
            // the looping sound) is owned by the frontend for both offline
            // and networked fire, exactly like retail's BlockFireEntity; a
            // fire tick is not an explosion.
        }
        break;
    }
    case EntityCategory::marker:
    case EntityCategory::projectile:
    case EntityCategory::unportable:
        break;
    }
}

void TutorialWorldSession::step_turret(LocalEntity& entity, const EntityDefinition& definition) {
    if (definition.parts.size() < 3U) {
        return;
    }
    const auto muzzle = entity_presentation_position(entity, definition.parts[2U]);
    const auto dx = player_.position.x - muzzle.x;
    const auto dy = player_.position.y - muzzle.y;
    const auto dz = player_.position.z - muzzle.z;
    const auto distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (distance <= 1e-9) {
        return;
    }

    // A sticky target is held out to the tracking range but only ACQUIRED
    // inside the shorter detection range, so a turret does not snap onto
    // someone who merely walked past at the edge of its reach.
    const bool had_target = entity.target.has_value();
    const bool in_range =
        had_target ? distance <= turret_tracking_range : distance <= turret_detection_range;
    if (in_range != had_target) {
        entity.target = in_range ? std::optional<std::uint8_t>{std::uint8_t{0U}} : std::nullopt;
        entity_events_.push_back({EntityEventKind::target_changed,
                                  entity.id,
                                  entity.type,
                                  entity.position,
                                  in_range ? 1.0 : 0.0});
    }
    if (!in_range) {
        return;
    }

    const auto horizontal = std::hypot(dx, dy);
    const auto desired_yaw = std::atan2(dx, dy) / degrees_to_radians;
    const auto desired_pitch = -std::atan2(dz, horizontal) / degrees_to_radians;
    const auto aim = step_turret_aim(entity.aim_yaw,
                                     entity.aim_pitch,
                                     desired_yaw,
                                     desired_pitch,
                                     turret_aiming_speed,
                                     config_.fixed_dt,
                                     turret_aim_tolerance);
    entity.aim_yaw = aim.yaw;
    entity.aim_pitch = aim.pitch;
    entity.yaw = desired_yaw;
    entity.pitch = desired_pitch;

    if (!aim.on_target || entity.shoot_cooldown > 0.0 || entity.ammo == 0U) {
        return;
    }
    entity.shoot_cooldown = turret_shoot_interval;
    --entity.ammo;

    // Fire through the existing projectile path so the rocket's flight,
    // contact test, crater and VFX are the ones already proven in the sandbox.
    TutorialProjectile rocket;
    rocket.id = next_projectile_id_++;
    rocket.tool_id = 12U;
    rocket.position = {muzzle.x + dx / distance, muzzle.y + dy / distance,
                       muzzle.z + dz / distance};
    rocket.spawn_position = rocket.position;
    rocket.autonomous_source = true;
    rocket.velocity = {dx / distance * turret_rocket_speed,
                       dy / distance * turret_rocket_speed,
                       dz / distance * turret_rocket_speed};
    rocket.remaining = 5.0;
    rocket.gravity_multiplier = 0.0;
    rocket.behavior = TutorialProjectileBehavior::contact;
    rocket.block_damage = turret_rocket_block_damage;
    rocket.crater_radius = static_cast<std::uint8_t>(turret_rocket_crater_radius);
    projectiles_.push_back(rocket);
    entity_events_.push_back({EntityEventKind::fired, entity.id, entity.type, muzzle});
    static_cast<void>(definition);
}

void TutorialWorldSession::detonate_entity(LocalEntity& entity,
                                           const EntityDefinition& definition) {
    entity.detonating = false;
    entity.retired = true;

    // Terrain: reuse the projectile crater path verbatim, so craters, falling
    // structures and explosion VFX all come free and stay consistent.
    TutorialProjectile blast;
    blast.tool_id = 12U;
    blast.position = entity.position;
    blast.block_damage = definition.block_damage;
    blast.crater_radius = definition.crater_radius;
    explode_projectile(blast, std::nullopt);
    if (entity.type == 11U && !terrain_impacts_.empty()) {
        // Grave detonation owns a dedicated retail death effect. The crater
        // remains on the shared authoritative path, but presenting its result
        // as tool 12 incorrectly added RPG fire, smoke and rocket audio.
        auto& impact = terrain_impacts_.back();
        impact.kind = TerrainImpactKind::grave_explosion;
        if (entity.has_color) {
            impact.color = {entity.color[0U], entity.color[1U], entity.color[2U], 255U};
        } else {
            impact.color = VxlColor{104U, 94U, 88U, 255U};
        }
        impact.source_tool = 0U;
    }

    // Players: live HP belongs only to SetHP/WorldUpdate. Offline training
    // still owns its complete local blast simulation.
    if (!config_.network_authoritative && definition.blast_damage > 0.0F &&
        definition.blast_radius > 0.0F) {
        const auto feet = player_.position.z + player_contact_offset(player_.crouch, player_.wade);
        const Vec3 body{player_.position.x, player_.position.y, (player_.position.z + feet) * 0.5};
        const auto dx = body.x - entity.position.x;
        const auto dy = body.y - entity.position.y;
        const auto dz = body.z - entity.position.z;
        const auto distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        const auto falloff = blast_falloff(distance, definition.blast_radius);
        if (falloff > 0.0) {
            static_cast<void>(apply_damage(static_cast<double>(definition.blast_damage) * falloff));
        }
    }

    entity_events_.push_back({EntityEventKind::detonated,
                              entity.id,
                              entity.type,
                              entity.position,
                              definition.blast_radius,
                              blast_in_water(entity.position.z)});
}

bool TutorialWorldSession::place_deployable(const WeaponAction& action) {
    const auto type = entity_placed_by_tool(action.tool_id);
    if (type == 0U) {
        return false;
    }
    const auto* definition = find_entity_definition(type);
    if (definition == nullptr) {
        return false;
    }

    // C4 is capped at two LIVE charges, which is a different rule from its
    // ammo stock: shooting one off a wall frees a slot even though no ammo
    // came back. Counting live entities is the only way to model that.
    if (type == 38U) {
        const auto live = std::ranges::count_if(
            entities_, [](const LocalEntity& entity) { return entity.type == 38U; });
        if (live >= 2) {
            return false;
        }
    }

    const auto placement = deployable_target(action.tool_id);
    if (!placement.has_value()) {
        return false;
    }

    // The entity sits AT the solid voxel; the renderer's half-block standoff
    // along the face normal is what lifts it onto the surface. Retail sends
    // the solid cube too -- unlike the block tool, which steps out to the
    // adjacent air cube first.
    const Vec3 target{static_cast<double>(placement->cell[0U]),
                      static_cast<double>(placement->cell[1U]),
                      static_cast<double>(placement->cell[2U])};
    const auto id = spawn_entity(type, target, 0U, placement->face);
    if (id == 0U) {
        return false;
    }
    // Attached, so the 10 Hz settle must not drag it down off a wall.
    entities_.back().attached = true;
    since_primary_ = 0.0;
    ++weapon_action_sequence_;
    return true;
}

std::size_t TutorialWorldSession::detonate_all_c4() {
    std::size_t fired{};
    for (auto& entity : entities_) {
        if (entity.type == 38U && entity.alive) {
            entity.detonating = true;
            ++fired;
        }
    }
    return fired;
}

bool TutorialWorldSession::drop_carried_objective(const WeaponAction& action) {
    const auto type = entity_placed_by_tool(action.tool_id);
    if (type == 0U || tool_carrying_entity(type) == 0U) {
        return false;
    }
    // Retail throw speeds: BOMB 10, DIAMOND and INTEL 15, each added to the
    // player's own velocity so a running drop travels further.
    const double speed = type == 14U ? 10.0 : 15.0;
    const auto direction = normalized(player_.orientation);
    const auto id = spawn_entity(type, player_.position);
    if (id == 0U) {
        return false;
    }
    auto& dropped = entities_.back();
    dropped.velocity = {player_.velocity.x + direction.x * speed,
                        player_.velocity.y + direction.y * speed,
                        player_.velocity.z + direction.z * speed};
    carried_objective_ = 0U;
    player_.burdened = false;
    // Retail's NO_PICKUP_AFTER_DROP_TIME. Without it the objective is caught
    // again on the very next tick, because the thrower is still inside its
    // pickup sphere.
    objective_pickup_lockout_ = 2.5;
    return true;
}

void TutorialWorldSession::handle_player_death() {
    death_pending_ = false;
    player_.parachute_active = false;
    player_.parachute_pending = false;
    parachute_deploy_last_held_ = false;
    parachute_jump_last_held_ = false;
    parachute_fall_touched_ = false;
    player_.parachute_used_this_fall = false;
    player_.parachute_open_frames = 0U;
    // Character.set_dead calls the selected tool's on_unset before the corpse
    // lifecycle continues. A soft input release is not enough for minigun
    // spin, reload ownership, cooked throws, or queued burst edges.
    clear_input();
    sandbox_inventory_.weapons().on_unset();
    // Anything carried falls where the carrier did, rather than vanishing with
    // them -- otherwise an intel carrier dying would delete the objective.
    if (carried_objective_ != 0U) {
        static_cast<void>(spawn_entity(carried_objective_, player_.position));
        carried_objective_ = 0U;
        player_.burdened = false;
        objective_pickup_lockout_ = 2.5;
    }
    // The gravestone: a 7 s fuse, then a small blast. Retail gates this behind
    // a gravestones rule; locally it always spawns so it can be tested.
    static_cast<void>(spawn_entity(11U, player_.position));
}

void TutorialWorldSession::update_entities() {
    if (objective_pickup_lockout_ > 0.0) {
        objective_pickup_lockout_ -= config_.fixed_dt;
    }
    if (death_pending_) {
        handle_player_death();
    }
    if (!server_flares_.empty()) {
        refresh_server_flares();
    }
    refresh_patch_lights();
    if (entities_.empty()) {
        return;
    }
    for (auto& entity : entities_) {
        const auto* definition = find_entity_definition(entity.type);
        if (definition == nullptr) {
            entity.retired = true;
            continue;
        }
        step_entity_timers(entity, *definition);
        if (!config_.network_authoritative && entity.alive && !entity.retired) {
            step_entity_behaviour(entity, *definition);
        }
    }
    // Detonation runs as a SEPARATE pass over indices rather than inside the
    // loop above, because a blast pushes projectiles and events and may in
    // future chain into neighbouring charges -- all of which would invalidate
    // an iterator mid-traversal.
    for (std::size_t index = 0U; !config_.network_authoritative && index < entities_.size();
         ++index) {
        if (!entities_[index].detonating) {
            continue;
        }
        const auto* definition = find_entity_definition(entities_[index].type);
        if (definition == nullptr) {
            entities_[index].detonating = false;
            continue;
        }
        detonate_entity(entities_[index], *definition);
    }
    std::erase_if(entities_, [](const LocalEntity& entity) { return entity.retired; });
}

void TutorialWorldSession::apply_melee_terrain(const WeaponAction& action,
                                               const WeaponDefinition& weapon,
                                               const VoxelCell& center,
                                               const TerrainImpactEvent& impact) {
    std::vector<VoxelCell> cells;
    std::vector<double> cell_damage;
    double damage = weapon.block_damage;
    const auto add = [&](int dx, int dy, int dz, std::optional<double> amount = std::nullopt) {
        const auto x = static_cast<std::int64_t>(center.x) + dx;
        const auto y = static_cast<std::int64_t>(center.y) + dy;
        const auto z = static_cast<std::int64_t>(center.z) + dz;
        if (x >= 0 && y >= 0 && z >= 0 && x < VxlMap::width && y < VxlMap::depth &&
            z < VxlMap::height - 1U) {
            cells.push_back({static_cast<std::uint32_t>(x),
                             static_cast<std::uint32_t>(y),
                             static_cast<std::uint32_t>(z)});
            cell_damage.push_back(amount.value_or(-1.0));
        }
    };

    const bool cube = action.tool_id == 3U || action.tool_id == 24U ||
                      (action.tool_id == 45U && action.secondary);
    const bool column = action.tool_id == 2U || action.tool_id == 4U;
    if (cube) {
        // Retail BlockManager.handle_damage cube types (BS
        // server/block_damage_model.py FOOTPRINTS, fitted live; applied the
        // same way by server/combat_runtime.py _apply_native_dig): each cell
        // of the centred 3x3x3 takes ceil4(amount + E * random()) and breaks
        // only when its accumulated damage reaches its health (map 5, built
        // 9). E = 5 Super Spade (SUPERSPADE_DAMAGE 3), 8 Zombie hands
        // (ZOMBIE_DAMAGE 17), 0 UGC Super Spade RMB (type 31). The amount is
        // the stock DiggingTool block_damage (BS server/dig_profiles.py:
        // Zombie hands 2, Super Spade 7.5) or
        // UGC_SUPERSPADE_SECONDARY_DAMAGE_AMOUNT 7.5 (combat_runtime.py
        // _spade_profile_for_packet). Zombie hands therefore crack a map
        // voxel with 2..10 per swing and never erase the whole cube at once.
        const double amount = action.tool_id == 45U ? 7.5 : weapon.block_damage;
        const double extra = action.tool_id == 24U ? 8.0 : action.tool_id == 3U ? 5.0 : 0.0;
        // Python 2 random.Random(Damage.seed): one draw per footprint cell in
        // x-major order, solid or not (block_damage_model.footprint).
        RetailRandom random{action.seed};
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dz = -1; dz <= 1; ++dz)
                    add(dx, dy, dz, std::ceil((amount + extra * random.random()) * 4.0) / 4.0);
    } else if (column) {
        add(0, 0, -1);
        add(0, 0, 0);
        add(0, 0, 1);
        damage = default_block_health;
    } else if (action.tool_id == 50U) {
        add(0, 0, 0);
        add(0, 0, 1);
        damage = 2.0;
    } else {
        add(0, 0, 0);
    }

    bool center_destroyed{};
    std::vector<VoxelCell> destroyed_cells;
    for (std::size_t index{}; index < cells.size(); ++index) {
        const auto& cell = cells[index];
        if (!map_->solid(cell.x, cell.y, cell.z))
            continue;
        const double applied = cell_damage[index] >= 0.0 ? cell_damage[index] : damage;
        const bool destroyed = damage_voxel(cell.x, cell.y, cell.z, applied, false);
        if (destroyed)
            destroyed_cells.push_back(cell);
        if (cell.x == center.x && cell.y == center.y && cell.z == center.z) {
            center_destroyed = destroyed;
        }
    }
    if (!destroyed_cells.empty()) {
        auto falling = collapse_unsupported_components(*map_, destroyed_cells);
        for (auto& component : falling) {
            attack_events_.blocks_collapsed += static_cast<int>(component.size());
            for (const auto& voxel : component)
                mark_dirty(voxel.cell.x, voxel.cell.y);
            falling_components_.push_back(std::move(component));
        }
    }
    auto emitted = impact;
    emitted.destroyed = center_destroyed;
    terrain_impacts_.push_back(emitted);
}

void TutorialWorldSession::apply_weapon_recoil(const WeaponAction& action,
                                               const WeaponDefinition& weapon) noexcept {
    const double up = weapon.retail.aim.recoil_up.value_or(0.0);
    const double side = weapon.retail.aim.recoil_side.value_or(0.0);
    // Character.shoot (character.pyd 0x10049db0): a deterministic sawtooth
    // side kick on the scene's millisecond clock, the walking/airborne/crouch
    // stance multipliers and set_view(pitch + up*60, yaw + side*60). Negative
    // recoil-up raises the muzzle because positive pitch looks downward.
    const bool walking = action_held(TutorialAction::forward) ||
                         action_held(TutorialAction::backward) ||
                         action_held(TutorialAction::left) ||
                         action_held(TutorialAction::right);
    const auto kick = retail_recoil_kick(up, side, retail_scene_timer_ms(ticks_), walking,
                                         player_.crouch, player_.airborne);
    pitch_ += kick.pitch_degrees;
    yaw_ += kick.yaw_degrees;
    pitch_ = std::clamp(pitch_, -pitch_limit_degrees, pitch_limit_degrees);
    // The tail of Character.shoot: `if shot and self.main and not
    // weapon.get_has_enough_ammo(): self.set_zoom(0)` -- the round that empties
    // the magazine drops the sight (and set_zoom plays zoom_out). There is no
    // can_zoom test. Deferred to the next tick so this shot's own presentation
    // still reads the aimed accuracy it was fired with.
    if (action.kind == WeaponActionKind::hitscan || action.kind == WeaponActionKind::oriented_item) {
        if (const auto* ammo = selected_ammo(); ammo != nullptr && ammo->magazine == 0U) {
            empty_magazine_unzoom_pending_ = true;
        }
    }
}

void TutorialWorldSession::fire_pistol() {
    const auto& pistol = tool_definition(retail_pistol_tool_id);
    --pistol_clip_;
    tool_cooldown_ = pistol.fire_interval;
    since_primary_ = 0.0;
    attack_events_.pistol_fired = true;
    const auto hit =
        raycast_voxels(*map_, player_.position, player_.orientation, pistol.maximum_range);
    if (hit.has_value()) {
        const auto color = map_->color(hit->x, hit->y, hit->z);
        const bool destroyed =
            damage_voxel(hit->x, hit->y, hit->z, static_cast<int>(pistol.block_damage));
        if (color.has_value()) {
            terrain_impacts_.push_back({TerrainImpactKind::bullet,
                                        {hit->x, hit->y, hit->z},
                                        *color,
                                        {hit->nx, hit->ny, hit->nz},
                                        destroyed});
        }
    }
    // Retail auto-reloads the emptied clip without a keypress.
    if (pistol_clip_ == 0 && pistol_stock_ > 0) {
        reload_remaining_ = pistol.reload_time;
        attack_events_.reload_started = true;
    }
}

void TutorialWorldSession::swing_spade() {
    const auto& spade = tool_definition(retail_spade_tool_id);
    tool_cooldown_ = spade.fire_interval;
    since_primary_ = 0.0;
    attack_events_.spade_swung = true;
    const auto hit =
        raycast_voxels(*map_, player_.position, player_.orientation, melee_world_range);
    if (hit.has_value()) {
        const auto color = map_->color(hit->x, hit->y, hit->z);
        const bool destroyed =
            damage_voxel(hit->x, hit->y, hit->z, static_cast<int>(spade.block_damage));
        if (color.has_value()) {
            terrain_impacts_.push_back({TerrainImpactKind::melee,
                                        {hit->x, hit->y, hit->z},
                                        *color,
                                        {hit->nx, hit->ny, hit->nz},
                                        destroyed,
                                        1.0F,
                                        retail_spade_tool_id});
            // DIG_HIT_BLOCK_SOUND accompanies contact, not only destruction.
            attack_events_.spade_hit_block = true;
        }
    }
}

bool TutorialWorldSession::place_block(bool emits_light) {
    // BlockTool costs one block, FlareBlockTool FLAREBLOCK_COST (10); both
    // refuse when the wallet cannot pay (get_has_enough_ammo).
    const int cost = emits_light ? retail_flare_block_cost : 1;
    const int wallet = debug_full_loadout_ ? static_cast<int>(sandbox_inventory_.blocks())
                                           : blocks_remaining_;
    if (!infinite_blocks_ && wallet < cost) {
        return false;
    }
    std::int64_t place_x{};
    std::int64_t place_y{};
    std::int64_t place_z{};
    if (emits_light) {
        // flareBlockTool.py use_primary places at BlockToolCommon's hit_cube
        // -- the cube its ghost draws, found out to MAX_BLOCK_DISTANCE with
        // the bridge fallback -- and only while `valid_placement and
        // hit_cube and get_has_enough_ammo()`. The four-block melee ray used
        // before refused every cube past arm's length while the ghost said
        // it was placeable, so the click did nothing.
        auto orientation = player_.orientation;
        const double length = std::sqrt(orientation.x * orientation.x +
                                        orientation.y * orientation.y +
                                        orientation.z * orientation.z);
        if (length > 1.0e-9) {
            orientation = {orientation.x / length, orientation.y / length,
                           orientation.z / length};
        }
        const auto body = player_.position;
        const BlockOccupiedPredicate occupied = [body](const BlockTargetCell& cell) {
            return block_cell_overlaps_body(cell, body);
        };
        const auto target = resolve_block_target(*map_, player_.position, orientation,
                                                 retail_max_block_distance, occupied);
        const auto ghost =
            evaluate_flare_block_ghost(*map_, target, wallet, infinite_blocks_, true, occupied);
        if (!target.cell.has_value() ||
            !flare_block_placement_allowed(ghost, wallet, infinite_blocks_)) {
            return false;
        }
        place_x = (*target.cell)[0U];
        place_y = (*target.cell)[1U];
        place_z = (*target.cell)[2U];
    } else {
        const auto hit =
            raycast_voxels(*map_, player_.position, player_.orientation, melee_world_range);
        if (!hit.has_value()) {
            return false;
        }
        place_x = static_cast<std::int64_t>(hit->x) + hit->nx;
        place_y = static_cast<std::int64_t>(hit->y) + hit->ny;
        place_z = static_cast<std::int64_t>(hit->z) + hit->nz;
    }
    if (place_x < 0 || place_x >= VxlMap::width || place_y < 0 || place_y >= VxlMap::depth ||
        place_z < 0 || place_z >= VxlMap::height) {
        return false;
    }
    const auto x = static_cast<std::uint32_t>(place_x);
    const auto y = static_cast<std::uint32_t>(place_y);
    const auto z = static_cast<std::uint32_t>(place_z);
    if (map_->solid(x, y, z)) {
        return false;
    }
    // Reject cells overlapping the player: half extent 0.45 around the eye
    // column, from just above the head to the contact feet.
    const double half{0.45};
    const double body_top = player_.position.z - 0.6;
    const double body_bottom = player_.position.z + (player_.crouch ? 1.35 : 2.25);
    const bool overlaps_x =
        static_cast<double>(x)<player_.position.x + half&& static_cast<double>(x) + 1.0> player_
            .position.x -
        half;
    const bool overlaps_y =
        static_cast<double>(y)<player_.position.y + half&& static_cast<double>(y) + 1.0> player_
            .position.y -
        half;
    const bool overlaps_z =
        static_cast<double>(z)<body_bottom&& static_cast<double>(z) + 1.0> body_top;
    if (overlaps_x && overlaps_y && overlaps_z) {
        return false;
    }
    if (!map_->set_voxel(x, y, z, block_color_)) {
        return false;
    }
    tool_cooldown_ = tool_definition(retail_block_tool_id).fire_interval;
    since_primary_ = 0.0;
    if (debug_full_loadout_ && !infinite_blocks_) {
        static_cast<void>(sandbox_inventory_.spend_blocks(static_cast<std::uint16_t>(cost)));
        blocks_remaining_ = static_cast<int>(sandbox_inventory_.blocks());
    } else if (!debug_full_loadout_ && !infinite_blocks_) {
        blocks_remaining_ -= cost;
    }
    ++blocks_built_;
    attack_events_.block_placed = true;
    mark_dirty(x, y);

    // The flare block is an ordinary destructible voxel that also registers a
    // static point light, which is exactly what retail's FlareBlockEntity did:
    // add_user_block followed by add_static_point_light at the placed block's
    // own colour. Because the light is baked into vertex colours at mesh time,
    // every chunk it reaches has to be re-meshed, not just the one it sits in.
    if (emits_light) {
        StaticLight light;
        light.cell = {x, y, z};
        // Retail takes the light colour from the same SetColor palette entry
        // used by the placed voxel.
        light.color = block_color_;
        light.radius = StaticLightField::flare_block_radius;
        if (static_lights_.add(light)) {
            const auto reach = static_cast<std::int64_t>(StaticLightField::flare_block_radius) + 1;
            for (std::int64_t offset_y = -reach; offset_y <= reach; ++offset_y) {
                for (std::int64_t offset_x = -reach; offset_x <= reach; ++offset_x) {
                    const auto near_x = static_cast<std::int64_t>(x) + offset_x;
                    const auto near_y = static_cast<std::int64_t>(y) + offset_y;
                    if (near_x < 0 || near_x >= VxlMap::width || near_y < 0 ||
                        near_y >= VxlMap::depth) {
                        continue;
                    }
                    mark_dirty(static_cast<std::uint32_t>(near_x),
                               static_cast<std::uint32_t>(near_y));
                }
            }
        }
    }
    return true;
}

bool TutorialWorldSession::damage_voxel(
    std::uint32_t x, std::uint32_t y, std::uint32_t z, double damage, bool collapse) {
    // The z=239 bed is indestructible in every retail mode.
    if (z >= VxlMap::height - 1U) {
        return false;
    }
    const auto color = map_->color(x, y, z);
    if (color.has_value() && is_target_red(*color)) {
        for (std::size_t index{}; index < target_centers.size(); ++index) {
            const auto& center = target_centers[index];
            if (target_down_[index]) {
                continue;
            }
            const auto within = [](std::uint32_t a, std::uint32_t b) {
                return a + target_scan_radius >= b && b + target_scan_radius >= a;
            };
            if (within(x, center.x) && within(y, center.y) && within(z, center.z)) {
                destroy_target(index);
                return true;
            }
        }
        // Red voxels outside every live target fall through to block damage.
    }
    // Retail BlockManager.add_damage: per-cell health (map 5, built 9) and
    // the compounding shared.common.dim darkening live in the map itself.
    const auto outcome = map_->add_damage(x, y, z, static_cast<float>(damage));
    if (outcome == BlockDamageOutcome::damaged) {
        mark_dirty(x, y);
    }
    if (outcome != BlockDamageOutcome::destroyed) {
        return false;
    }
    mark_dirty(x, y);
    // Destroying a flare block must take its light with it, and every chunk the
    // light reached has to re-mesh or the glow outlives the lamp.
    if (static_lights_.remove_at(x, y, z)) {
        const auto reach = static_cast<std::int64_t>(StaticLightField::flare_block_radius) + 1;
        for (std::int64_t offset_y = -reach; offset_y <= reach; ++offset_y) {
            for (std::int64_t offset_x = -reach; offset_x <= reach; ++offset_x) {
                const auto near_x = static_cast<std::int64_t>(x) + offset_x;
                const auto near_y = static_cast<std::int64_t>(y) + offset_y;
                if (near_x < 0 || near_x >= VxlMap::width || near_y < 0 ||
                    near_y >= VxlMap::depth) {
                    continue;
                }
                mark_dirty(static_cast<std::uint32_t>(near_x), static_cast<std::uint32_t>(near_y));
            }
        }
    }
    if (!collapse) {
        return true;
    }
    auto falling = collapse_unsupported_components(*map_, {{x, y, z}});
    for (auto& component : falling) {
        attack_events_.blocks_collapsed += static_cast<int>(component.size());
        for (const auto& voxel : component) {
            mark_dirty(voxel.cell.x, voxel.cell.y);
        }
        falling_components_.push_back(std::move(component));
    }
    return true;
}

void TutorialWorldSession::destroy_target(std::size_t index) {
    // Training.vxl authors each bullseye as one 21-voxel red/white disc
    // (13 red + 8 white) in front of a separately coloured metal stand.
    // Any hit on a live target's red cell detaches the complete disc and
    // counts it once (target_down_ guards repeats). The retail rule is
    // unrecoverable (the tutorial server never shipped); this one-hit rule is
    // a reconstruction shared with BattleSpades modes/tutorial.py. Keeping
    // only the white cells in the map creates the conspicuous floating ring
    // that the old colour-only cleanup produced.
    const auto& center = target_centers[index];
    FallingComponent target_face;
    const auto low = [](std::uint32_t value) {
        return value > target_scan_radius ? value - target_scan_radius : 0U;
    };
    for (std::uint32_t x = low(center.x);
         x <= std::min(center.x + target_scan_radius, VxlMap::width - 1U);
         ++x) {
        for (std::uint32_t y = low(center.y);
             y <= std::min(center.y + target_scan_radius, VxlMap::depth - 1U);
             ++y) {
            for (std::uint32_t z = low(center.z);
                 z <= std::min(center.z + target_scan_radius, VxlMap::height - 2U);
                 ++z) {
                const auto color = map_->color(x, y, z);
                if (color.has_value() && is_target_face(*color) && map_->clear_voxel(x, y, z)) {
                    target_face.push_back({{x, y, z}, *color});
                    mark_dirty(x, y);
                }
            }
        }
    }
    if (!target_face.empty()) {
        attack_events_.blocks_collapsed += static_cast<int>(target_face.size());
        falling_components_.push_back(std::move(target_face));
    }
    target_down_[index] = true;
    ++attack_events_.targets_destroyed;
}

void TutorialWorldSession::mark_dirty(std::uint32_t x, std::uint32_t y) {
    // Chunk faces sample neighbor solidity, so edits on a 16-block seam must
    // also re-mesh the adjacent chunk.
    const auto push = [this](std::uint32_t chunk_x, std::uint32_t chunk_y) {
        const auto index = chunk_y * dirty_chunk_columns + chunk_x;
        if (dirty_chunk_membership_[index]) return;
        dirty_chunks_.push_back(ChunkKey{chunk_x, chunk_y});
        dirty_chunk_membership_.set(index);
    };
    if (x >= VxlMap::width || y >= VxlMap::depth) return;
    const std::uint32_t chunk_x = x / dirty_chunk_edge;
    const std::uint32_t chunk_y = y / dirty_chunk_edge;
    push(chunk_x, chunk_y);
    if (x % dirty_chunk_edge == 0U && x > 0U) {
        push(chunk_x - 1U, chunk_y);
    }
    if (x % dirty_chunk_edge == dirty_chunk_edge - 1U && x + 1U < VxlMap::width) {
        push(chunk_x + 1U, chunk_y);
    }
    if (y % dirty_chunk_edge == 0U && y > 0U) {
        push(chunk_x, chunk_y - 1U);
    }
    if (y % dirty_chunk_edge == dirty_chunk_edge - 1U && y + 1U < VxlMap::depth) {
        push(chunk_x, chunk_y + 1U);
    }
}

TutorialAttackEvents TutorialWorldSession::take_attack_events() noexcept {
    const auto events = attack_events_;
    attack_events_ = {};
    return events;
}

std::vector<ChunkKey> TutorialWorldSession::take_dirty_chunks() {
    dirty_chunk_membership_.reset();
    return std::exchange(dirty_chunks_, {});
}

std::vector<FallingComponent> TutorialWorldSession::take_falling_components() {
    return std::exchange(falling_components_, {});
}

std::vector<ProjectileBounceEvent> TutorialWorldSession::take_projectile_bounces() {
    return std::exchange(projectile_bounces_, {});
}

std::vector<TerrainImpactEvent> TutorialWorldSession::take_terrain_impacts() {
    return std::exchange(terrain_impacts_, {});
}

int TutorialWorldSession::pistol_clip() const noexcept {
    if (debug_full_loadout_) {
        if (const auto* ammo = sandbox_inventory_.ammo(retail_pistol_tool_id); ammo != nullptr) {
            return static_cast<int>(ammo->magazine);
        }
    }
    return pistol_clip_;
}

int TutorialWorldSession::pistol_stock() const noexcept {
    if (debug_full_loadout_) {
        if (const auto* ammo = sandbox_inventory_.ammo(retail_pistol_tool_id); ammo != nullptr) {
            return static_cast<int>(ammo->reserve);
        }
    }
    return pistol_stock_;
}

bool TutorialWorldSession::pistol_reloading() const noexcept {
    return debug_full_loadout_ ? sandbox_inventory_.weapons().reload_remaining() > 0.0
                               : reload_remaining_ > 0.0;
}

int TutorialWorldSession::blocks_remaining() const noexcept {
    return blocks_remaining_;
}

int TutorialWorldSession::targets_destroyed() const noexcept {
    return static_cast<int>(std::count(target_down_.begin(), target_down_.end(), true));
}

double TutorialWorldSession::seconds_since_primary() const noexcept {
    if (debug_full_loadout_) {
        const auto selected = selected_tool_id();
        const auto* weapon = selected.has_value() ? find_weapon_definition(*selected) : nullptr;
        if (weapon != nullptr && (weapon->mechanism == WeaponMechanism::cooked_throwable ||
                                  weapon->mechanism == WeaponMechanism::charged_throwable)) {
            const double held = sandbox_inventory_.weapons().interaction_elapsed();
            // AnimThrowGrenade exists only while the throw interaction is
            // active. A released grenade must not replay an arbitrary recoil.
            return held >= 0.0 ? held : 1.0e9;
        }
        if (weapon != nullptr && weapon->tool_id == 4U) {
            const double windup = sandbox_inventory_.weapons().interaction_elapsed();
            if (windup >= 0.0)
                return windup;
        }
    }
    return since_primary_;
}

double TutorialWorldSession::pullout_remaining() const noexcept {
    if (debug_full_loadout_) {
        // Retail Character.reload reuses Character.pullout rather than a
        // weapon-only animation. The selected weapon and both class hands
        // must therefore descend through the same root transform.
        return std::max({sandbox_inventory_.toolbar().pullout_remaining(),
                         sandbox_inventory_.weapons().reload_remaining(),
                         sprint_pullout_remaining_});
    }
    return std::max({inventory_.pullout_remaining(), reload_remaining_, sprint_pullout_remaining_});
}

std::vector<TutorialTool> TutorialWorldSession::unlocked_tools() const {
    std::vector<TutorialTool> tools;
    const auto stage = lessons_.stage();
    // Nothing before SHOOTING, the pistol at SHOOTING, then the server's
    // CLIMB_LOADOUT order (pistol, block, spade).
    const auto granted = TutorialLessons::loadout(
        debug_full_loadout_ ? TutorialLessonStage::climb : stage);
    for (const auto tool_id : granted) {
        if (const auto tool = tutorial_tool_from_id(tool_id); tool.has_value()) {
            tools.push_back(*tool);
        }
    }
    return tools;
}

std::optional<TutorialTool> TutorialWorldSession::equipped_tool() const noexcept {
    const auto id = debug_full_loadout_ ? sandbox_inventory_.toolbar().selected_tool_id()
                                        : inventory_.selected_tool_id();
    return id.has_value() ? tutorial_tool_from_id(*id) : std::nullopt;
}

void TutorialWorldSession::equip_tool(TutorialTool tool, InventorySelectionOrigin origin) noexcept {
    set_secondary_held(false);
    const auto id = retail_tool_id(tool);
    if (debug_full_loadout_) {
        if (sandbox_inventory_.select_tool(id, origin)) {
            reset_tool_transition_state();
        }
        return;
    }
    const auto& slots = inventory_.slots();
    for (std::size_t index{}; index < slots.size(); ++index) {
        if (slots[index].tool_id == id) {
            if (inventory_.select_slot(index, origin)) {
                reset_tool_transition_state();
            }
            break;
        }
    }
}

bool TutorialWorldSession::equip_inventory_slot(std::size_t index) noexcept {
    // MGWeapon.can_swap: not while the gun is deployed or unfolding.
    if (machine_gun_.blocks_swap()) return false;
    set_secondary_held(false);
    const bool selected =
        debug_full_loadout_
            ? sandbox_inventory_.select_slot(index, InventorySelectionOrigin::direct_slot)
            : inventory_.select_slot(index, InventorySelectionOrigin::direct_slot);
    if (selected) {
        reset_tool_transition_state();
    }
    return selected;
}

void TutorialWorldSession::cycle_tool(int direction) noexcept {
    if (machine_gun_.blocks_swap()) return;
    set_secondary_held(false);
    const bool selected =
        debug_full_loadout_ ? sandbox_inventory_.cycle(direction) : inventory_.cycle(direction);
    if (selected) {
        reset_tool_transition_state();
    }
}

const RetailInventory& TutorialWorldSession::inventory() const noexcept {
    return debug_full_loadout_ ? sandbox_inventory_.toolbar() : inventory_;
}

std::span<const std::string> TutorialWorldSession::inventory_prefabs() const noexcept {
    return debug_full_loadout_ ? sandbox_inventory_.prefabs() : std::span<const std::string>{};
}

std::optional<InventorySelectionEvent> TutorialWorldSession::take_inventory_event() noexcept {
    return debug_full_loadout_ ? sandbox_inventory_.toolbar().take_selection_event()
                               : inventory_.take_selection_event();
}

void TutorialWorldSession::debug_grant_full_loadout() noexcept {
    // A live Protocol 168 session may only use the server-normalized loadout.
    // Keep this guard here as well as in the frontend so a future debug route
    // cannot turn a local presentation helper into an illegal tool selector.
    if (config_.network_authoritative) {
        return;
    }
    if (debug_full_loadout_) {
        restock_ammunition();
        return;
    }
    debug_full_loadout_ = true;
    static_cast<void>(sandbox_inventory_.spawn_as(0U, PlayerLoadoutScope::all_weapons));
    static_cast<void>(sandbox_inventory_.select_tool(retail_pistol_tool_id,
                                                     InventorySelectionOrigin::loadout_sync));
    reset_tool_transition_state();
    blocks_remaining_ = static_cast<int>(sandbox_inventory_.blocks());
}

void TutorialWorldSession::debug_cycle_class(int direction) noexcept {
    if (config_.network_authoritative) {
        return;
    }
    if (!debug_full_loadout_) {
        debug_grant_full_loadout();
    }
    const auto classes = class_catalog();
    if (classes.empty() || direction == 0) {
        return;
    }
    const auto current = sandbox_inventory_.class_id();
    const auto found = std::ranges::find(classes, current, &ClassDefinition::class_id);
    std::size_t index =
        found == classes.end() ? 0U : static_cast<std::size_t>(found - classes.begin());
    const auto count = static_cast<std::ptrdiff_t>(classes.size());
    const auto step = direction > 0 ? 1 : -1;
    index = static_cast<std::size_t>((static_cast<std::ptrdiff_t>(index) + step + count) % count);
    const auto selected = selected_tool_id();
    set_secondary_held(false);
    zoomed_ = false;
    static_cast<void>(
        sandbox_inventory_.spawn_as(classes[index].class_id, PlayerLoadoutScope::all_weapons));
    movement_class_ = movement_config_for_class(classes[index].class_id,
                                               config_.movement_speed_scale,
                                               config_.fall_on_water_damage);
    apply_flight_profile(movement_class_, config_.flight_profile);
    // The developer arsenal contains handheld tools only. Movement equipment
    // still comes from the selected class's original default equipment slot.
    std::vector<std::uint8_t> equipment;
    for (const auto item : default_class_items(classes[index])) {
        if (item <= 255U) equipment.push_back(static_cast<std::uint8_t>(item));
    }
    sync_movement_equipment(equipment);
    blocks_remaining_ = static_cast<int>(sandbox_inventory_.blocks());
    if (selected.has_value()) {
        static_cast<void>(
            sandbox_inventory_.select_tool(*selected, InventorySelectionOrigin::loadout_sync));
    }
    reset_tool_transition_state();
}

void TutorialWorldSession::reset_tool_transition_state() noexcept {
    reload_aim_tool_.reset();
    // Character.on_unset stops the old tool's animations and held inputs.
    // Reset the shared presentation clock as well, otherwise F4/wheel/number
    // changes replay the outgoing recoil, melee or throw pose on new hands.
    primary_held_ = false;
    primary_edge_ = false;
    primary_press_latch_ = false;
    primary_tick_held_ = false;
    secondary_held_ = false;
    custom_held_ = false;
    custom_edge_ = false;
    zoomed_ = false;
    // Snap rather than ramp: new hands must arrive at hip fire, exactly as
    // they arrive without the outgoing tool's recoil or throw pose.
    zoom_level_ = 0.0;
    sprint_pullout_remaining_ = 0.0;
    since_primary_ = 1.0e9;
    weapon_action_sequence_ = 0U;
    sandbox_inventory_.weapons().cancel_interaction();
    static_cast<void>(sandbox_inventory_.weapons().take_actions());
}

std::uint8_t TutorialWorldSession::debug_class_id() const noexcept {
    return debug_full_loadout_ ? sandbox_inventory_.class_id() : 0U;
}

bool TutorialWorldSession::weapon_sandbox_enabled() const noexcept {
    return debug_full_loadout_;
}

std::optional<std::uint8_t> TutorialWorldSession::selected_tool_id() const noexcept {
    return inventory().selected_tool_id();
}

double TutorialWorldSession::weapon_crosshair_radius_pixels(double viewport_height) const noexcept {
    if (!debug_full_loadout_) {
        // The ordinary tutorial's spade/block/pistol milestone uses the same
        // fixed tool reticle until it enters the full WeaponRuntime path.
        // Plain Tools (spade, block) have no get_accuracy and keep retail's
        // one-pixel small square; see WeaponRuntime::crosshair_radius_pixels.
        const auto selected = selected_tool_id();
        const auto* weapon =
            selected.has_value() ? find_weapon_definition(*selected) : nullptr;
        if (weapon != nullptr && !weapon->retail.class_name.ends_with("Weapon")) {
            return 1.0;
        }
        return 6.0;
    }
    return sandbox_inventory_.weapons().crosshair_radius_pixels(
        viewport_height, zoom_fov_y_degrees(zoom_level_), zoomed_);
}

double TutorialWorldSession::weapon_spin_fraction() const noexcept {
    return sandbox_inventory_.weapons().spin_fraction();
}

bool TutorialWorldSession::weapon_trigger_live() const noexcept {
    if (!primary_held_ || pullout_remaining() > 0.0) {
        return false;
    }
    const auto selected = selected_tool_id();
    const auto* weapon = selected.has_value() ? find_weapon_definition(*selected) : nullptr;
    if (weapon == nullptr || (action_held(TutorialAction::sprint) &&
                              !WeaponRuntime::can_use_while_sprinting(*weapon, false))) {
        return false;
    }
    const auto* ammo = selected_ammo();
    return ammo != nullptr && ammo->magazine > 0U && !ammo->reloading;
}

bool TutorialWorldSession::weapon_view_model_visible() const noexcept {
    if (view_model_suppressed_) {
        return false;
    }
    if (!action_held(TutorialAction::sprint)) {
        return true;
    }
    const auto selected = selected_tool_id();
    const auto* weapon = selected.has_value() ? find_weapon_definition(*selected) : nullptr;
    // GameScene keeps ALL_MELEE_WEAPONS visible while sprinting; every other
    // held item is put away and returns through Character.pullout on release.
    return weapon != nullptr && weapon->mechanism == WeaponMechanism::melee;
}

FootstepInput TutorialWorldSession::footstep_input() const noexcept {
    const auto info = diagnostics();
    FootstepInput input;
    // "Walking" is horizontal motion, not input intent: a player sliding to a
    // halt is still making noise, and one shoving against a wall is not.
    const double speed = std::hypot(info.velocity.x, info.velocity.y);
    input.walking = speed > 0.02;
    input.airborne = info.airborne;
    input.crouch = info.crouch;
    input.wade = info.wade;
    input.sprint = action_held(TutorialAction::sprint);
    // Retail's sneak is a separate walk modifier we do not bind today; crouch
    // already covers the quiet case.
    input.sneak = false;
    return input;
}

MovementStepResult TutorialWorldSession::take_movement_events() noexcept {
    const auto events = movement_events_;
    movement_events_ = {};
    return events;
}

bool TutorialWorldSession::machine_gun_deployed() const noexcept {
    return machine_gun_.deployed();
}

bool TutorialWorldSession::machine_gun_deploying() const noexcept {
    return machine_gun_.deploying();
}

std::optional<double> TutorialWorldSession::machine_gun_deployment_progress() const noexcept {
    if (!machine_gun_.timer_running()) return std::nullopt;
    const auto progress = machine_gun_.progress();
    // draw_weapon_deployment_hud returns while the progress is still 1.
    return progress < 1.0 ? std::optional<double>{progress} : std::nullopt;
}

double TutorialWorldSession::weapon_deployment_yaw() const noexcept {
    if (!machine_gun_.locks_movement()) return 0.0;
    // to_pitch_yaw: yaw = degrees(atan2(x, y)) of the view vector, which is
    // (-cos a, -sin a) for the session's own yaw a.
    const double radians = machine_gun_.deployment_yaw() * degrees_to_radians;
    return std::atan2(-std::cos(radians), -std::sin(radians)) / degrees_to_radians;
}

void TutorialWorldSession::fold_machine_gun() {
    const bool was_deployed = machine_gun_.reset();
    if (!was_deployed) return;
    zoomed_ = false;
    if (config_.network_authoritative) {
        process_weapon_action(WeaponAction{WeaponActionKind::objective_use, 15U});
    }
}

void TutorialWorldSession::advance_machine_gun_deployment() {
    const auto selected = selected_tool_id();
    const auto* weapon = selected.has_value() ? find_weapon_definition(*selected) : nullptr;
    if (weapon == nullptr || weapon->mechanism != WeaponMechanism::deployed_machine_gun ||
        !alive()) {
        fold_machine_gun();
        return;
    }
    MachineGunDeployInput input;
    input.trigger_held = secondary_held_ || custom_held_;
    input.crouching = player_.crouch;
    input.yaw_degrees = yaw_;
    input.position = player_.position;
    input.position_tolerance = config_.network_authoritative ? 1.0 / 32.0 : 0.0;
    input.space_available =
        machine_gun_.deployed() || machine_gun_deployment_space(*map_, player_.position, yaw_);
    const auto event = machine_gun_.tick(input, config_.fixed_dt);

    if (machine_gun_.timer_running()) {
        // MGWeapon.update: world_object.velocity.set(0, 0, 0) while it counts.
        player_.velocity = {};
    }
    yaw_ = machine_gun_.constrained_yaw(yaw_);
    pitch_ = machine_gun_.constrained_pitch(pitch_);
    if (!machine_gun_.timer_running()) {
        // The deployed gunner always looks down the sight; the carried gun
        // has none.
        zoomed_ = machine_gun_.deployed();
    }

    if (event == MachineGunDeployEvent::none) return;
    // Completion clears Character.weapon_custom and shoot_secondary.
    custom_held_ = false;
    sandbox_inventory_.weapons().set_secondary(false);
    if (!config_.network_authoritative) return;
    // The BattleSpades server keeps the gun as an entity its carrier is
    // mounted on: PlaceMG(87) then UseCommand(86) to man it, UseCommand to
    // leave it.
    if (event == MachineGunDeployEvent::deployed) {
        process_weapon_action(WeaponAction{WeaponActionKind::deployable_place, 15U});
    }
    process_weapon_action(WeaponAction{WeaponActionKind::objective_use, 15U});
}

const ToolAmmoState* TutorialWorldSession::selected_ammo() const noexcept {
    const auto selected = selected_tool_id();
    return debug_full_loadout_ && selected.has_value() ? sandbox_inventory_.ammo(*selected)
                                                       : nullptr;
}

bool TutorialWorldSession::zoomed() const noexcept {
    return zoomed_;
}

double TutorialWorldSession::weapon_reload_remaining() const noexcept {
    return debug_full_loadout_ ? sandbox_inventory_.weapons().reload_remaining() : reload_remaining_;
}

bool TutorialWorldSession::magnified_scope() const noexcept {
    if (!zoomed_) {
        return false;
    }
    const auto selected = selected_tool_id();
    const auto* weapon = selected.has_value() ? find_weapon_definition(*selected) : nullptr;
    return weapon != nullptr &&
           weapon_secondary_behavior(*weapon) == WeaponSecondaryBehavior::magnified_scope;
}

double TutorialWorldSession::zoom_target() const noexcept {
    const auto selected = selected_tool_id();
    if(zoomed_&&selected&&skin_zoom_&&skin_zoom_->first==*selected)return skin_zoom_->second;
    return selected.has_value() ? zoom_target_multiplier(*selected, zoomed_) : 0.0;
}

void TutorialWorldSession::set_skin_zoom(std::uint8_t tool,std::optional<double> target) noexcept {
    if(target&&std::isfinite(*target))skin_zoom_=std::pair{tool,std::clamp(*target,0.,1.6)};
    else skin_zoom_.reset();
}

double TutorialWorldSession::zoom_level() const noexcept {
    return zoom_level_;
}

double TutorialWorldSession::weapon_mechanism_phase() const noexcept {
    return debug_full_loadout_ ? sandbox_inventory_.weapons().spin_rotation_fraction() : 0.0;
}

std::array<double, 3U> TutorialWorldSession::weapon_view_model_shake() const noexcept {
    return debug_full_loadout_ ? sandbox_inventory_.weapons().block_sucker_shake()
                               : std::array<double, 3U>{};
}

std::uint8_t TutorialWorldSession::weapon_block_sucker_state() const noexcept {
    return debug_full_loadout_ ? sandbox_inventory_.weapons().block_sucker_state() : 0U;
}

std::span<const TutorialProjectile> TutorialWorldSession::projectiles() const noexcept {
    return projectiles_;
}

// -- Local entities ---------------------------------------------------------

std::span<const LocalEntity> TutorialWorldSession::entities() const noexcept {
    return entities_;
}

std::vector<EntityEvent> TutorialWorldSession::take_entity_events() {
    return std::exchange(entity_events_, {});
}

std::size_t TutorialWorldSession::entity_part_budget() const noexcept {
    return entity_part_budget_;
}

void TutorialWorldSession::set_entity_part_budget(std::size_t parts) noexcept {
    entity_part_budget_ = parts;
}

std::size_t TutorialWorldSession::entity_parts_in_use() const noexcept {
    std::size_t parts{};
    for (const auto& entity : entities_) {
        const auto* definition = find_entity_definition(entity.type);
        const auto model_parts =
            entity.type == 29U ? ugc_entity_model_parts(entity.ugc_item_id)
                               : (definition != nullptr ? definition->parts
                                                        : std::span<const EntityModelPart>{});
        parts += std::max<std::size_t>(1U, model_parts.size());
    }
    return parts;
}

std::uint64_t TutorialWorldSession::spawn_entity(std::uint8_t type,
                                                 Vec3 position,
                                                 std::uint8_t team,
                                                 std::uint8_t face) {
    const auto* definition = find_entity_definition(type);
    if (definition == nullptr || definition->category == EntityCategory::unportable ||
        face > 5U || !std::isfinite(position.x) || !std::isfinite(position.y) ||
        !std::isfinite(position.z)) {
        return 0U;
    }
    // Budget in PARTS: a turret costs three slots and a UGC marker two, so a
    // per-entity count would overrun the renderer band without ever tripping.
    const auto cost = std::max<std::size_t>(1U, definition->parts.size());
    if (entity_parts_in_use() + cost > entity_part_budget_) {
        // Refuse rather than spawn-and-hide. A simulating entity with no mesh
        // looks exactly like a broken renderer, and the HUD reports this count.
        return 0U;
    }

    // The flare block is the one entity that is TERRAIN rather than a model:
    // it places a real destructible voxel that also registers a static point
    // light. Handled at spawn and never entered into `entities_`, because a
    // simulated entity holding a renderer slot for a mesh that does not exist
    // would look exactly like a broken model path.
    if (type == 13U) {
        const auto x = static_cast<std::uint32_t>(
            std::clamp(std::floor(position.x), 0.0, static_cast<double>(VxlMap::width - 1U)));
        const auto y = static_cast<std::uint32_t>(
            std::clamp(std::floor(position.y), 0.0, static_cast<double>(VxlMap::depth - 1U)));
        const auto z = static_cast<std::uint32_t>(
            std::clamp(std::floor(position.z), 0.0, static_cast<double>(VxlMap::height - 2U)));
        if (map_->solid(x, y, z) || !map_->set_voxel(x, y, z, VxlColor{255U, 255U, 82U, 255U})) {
            return 0U;
        }
        mark_dirty(x, y);
        StaticLight light;
        light.cell = {x, y, z};
        light.color = VxlColor{255U, 255U, 82U, 255U};
        light.radius = definition->light_radius > 0.0F ? definition->light_radius
                                                       : StaticLightField::flare_block_radius;
        if (static_lights_.add(light)) {
            // The light bakes into vertex colours at mesh time, so every chunk
            // it reaches has to re-mesh -- not just the one it sits in.
            const auto reach = static_cast<std::int64_t>(light.radius) + 1;
            for (std::int64_t offset_y = -reach; offset_y <= reach; ++offset_y) {
                for (std::int64_t offset_x = -reach; offset_x <= reach; ++offset_x) {
                    const auto lit_x = static_cast<std::int64_t>(x) + offset_x;
                    const auto lit_y = static_cast<std::int64_t>(y) + offset_y;
                    if (lit_x < 0 || lit_y < 0 ||
                        lit_x >= static_cast<std::int64_t>(VxlMap::width) ||
                        lit_y >= static_cast<std::int64_t>(VxlMap::depth)) {
                        continue;
                    }
                    mark_dirty(static_cast<std::uint32_t>(lit_x),
                               static_cast<std::uint32_t>(lit_y));
                }
            }
        }
        entity_events_.push_back({EntityEventKind::spawned, 0U, type, position});
        // A non-zero id so the caller sees success, with no entity behind it.
        return next_entity_id_++;
    }

    LocalEntity entity;
    entity.id = next_entity_id_++;
    entity.type = type;
    entity.position = position;
    entity.home = position;
    entity.team = team;
    entity.face = face;
    entity.health = definition->health;
    entity.ammo = definition->ammo;
    entity.uses = definition->uses;
    entity.fuse = definition->fuse > 0.0F ? definition->fuse : -1.0;
    entity.arm_remaining = definition->arm_delay;
    entity.armed = definition->arm_delay <= 0.0F;
    entity.lifetime_remaining = definition->lifetime > 0.0F ? definition->lifetime : -1.0;
    entities_.push_back(entity);
    entity_events_.push_back({EntityEventKind::spawned, entity.id, type, position});
    return entity.id;
}

bool TutorialWorldSession::apply_server_entity(LocalEntity entity) {
    const auto* definition = find_entity_definition(entity.type);
    if (definition == nullptr || entity.face > 5U || !std::isfinite(entity.position.x) ||
        !std::isfinite(entity.position.y) || !std::isfinite(entity.position.z) ||
        !std::isfinite(entity.velocity.x) || !std::isfinite(entity.velocity.y) ||
        !std::isfinite(entity.velocity.z) || !std::isfinite(entity.yaw)) {
        return false;
    }
    // Retail ignores a duplicate CreateEntity rather than mutating the object
    // already registered under that id. The server relies on this invariant.
    if (std::ranges::find(entities_, entity.id, &LocalEntity::id) != entities_.end() ||
        std::ranges::find(server_flares_, entity.id, &ServerFlare::id) != server_flares_.end()) {
        return true;
    }
    if (entity.type == 13U) {
        return apply_server_flare(entity);
    }
    const auto model_parts =
        entity.type == 29U ? ugc_entity_model_parts(entity.ugc_item_id) : definition->parts;
    if (entity.type == 29U && model_parts.empty()) {
        return false;
    }
    const auto cost = std::max<std::size_t>(1U, model_parts.size());
    if (entity_parts_in_use() + cost > entity_part_budget_) {
        return false;
    }

    entity.home = entity.position;
    entity.presentation_age = -config_.fixed_dt;
    entity.health = definition->health;
    entity.ammo = definition->ammo;
    if (entity.type != 29U) {
        entity.uses = definition->uses;
        entity.ugc_item_id = 0xFFU;
    }
    if (entity.fuse <= 0.0) {
        entity.fuse = definition->fuse > 0.0F ? definition->fuse : -1.0;
    }
    entity.arm_remaining = definition->arm_delay;
    entity.armed = definition->arm_delay <= 0.0F;
    entity.lifetime_remaining = definition->lifetime > 0.0F ? definition->lifetime : -1.0;
    entity.grounded = false;
    // Dynamite and C4 may arrive attached to walls or ceilings. Ground rows
    // keep face 4 and enter the normal VXL gravity path.
    if (!entity.attached && entity.face != 4U && (entity.type == 10U || entity.type == 38U)) {
        entity.attached = true;
    }
    entity.alive = true;
    entity.retired = false;
    entities_.push_back(entity);
    next_entity_id_ = std::max(next_entity_id_, entity.id + 1U);
    entity_events_.push_back({EntityEventKind::spawned, entity.id, entity.type, entity.position});
    return true;
}

void TutorialWorldSession::mark_light_dirty(const StaticLight& light) {
    const auto reach = static_cast<std::int64_t>(std::ceil(light.radius)) + 1;
    for (std::int64_t offset_y = -reach; offset_y <= reach; ++offset_y) {
        for (std::int64_t offset_x = -reach; offset_x <= reach; ++offset_x) {
            const auto lit_x = static_cast<std::int64_t>(light.cell[0U]) + offset_x;
            const auto lit_y = static_cast<std::int64_t>(light.cell[1U]) + offset_y;
            if (lit_x < 0 || lit_y < 0 || lit_x >= static_cast<std::int64_t>(VxlMap::width) ||
                lit_y >= static_cast<std::int64_t>(VxlMap::depth)) {
                continue;
            }
            mark_dirty(static_cast<std::uint32_t>(lit_x), static_cast<std::uint32_t>(lit_y));
        }
    }
}

bool TutorialWorldSession::apply_server_flare(const LocalEntity& entity) {
    // Retail FlareBlockEntity.post_initialize: add_user_block(x, y, z, RGB, 5)
    // then add_static_point_light(x, y, z, RGB, FLAREBLOCK_LIGHT_RADIUS). Both
    // player flares (packet 104) and the map's chroma-marker lights arrive
    // here; the server always sends the colour, and the stock palette slot 0
    // is only a defensive fallback.
    if (entity.position.x < 0.0 || entity.position.y < 0.0 || entity.position.z < 0.0) {
        return false;
    }
    const auto x = static_cast<std::uint32_t>(std::floor(entity.position.x));
    const auto y = static_cast<std::uint32_t>(std::floor(entity.position.y));
    const auto z = static_cast<std::uint32_t>(std::floor(entity.position.z));
    // z=239 is the indestructible bed; nothing can be placed in it.
    if (x >= VxlMap::width || y >= VxlMap::depth || z >= VxlMap::height - 1U) {
        return false;
    }
    const VxlColor color = entity.has_color
                               ? VxlColor{entity.color[0U], entity.color[1U], entity.color[2U], 255U}
                               : VxlColor{255U, 255U, 82U, 255U};
    // A marker cell may still hold the raw chroma voxel (the load-time
    // cleanup removes only exposed markers); retail's add_user_block simply
    // overwrites it with the static-light colour.
    if (!map_->set_voxel(x, y, z, color)) {
        return false;
    }
    mark_dirty(x, y);
    StaticLight light;
    light.cell = {x, y, z};
    light.color = color;
    light.radius = StaticLightField::flare_block_radius;
    const bool lit = static_lights_.add(light);
    if (lit) {
        mark_light_dirty(light);
    }
    server_flares_.push_back(ServerFlare{entity.id, light.cell, lit});
    next_entity_id_ = std::max(next_entity_id_, entity.id + 1U);
    // No spawn event: retail's placement cue (build_light) belongs to the
    // placing FlareBlockTool, and map lights arrive silently on join.
    return true;
}

void TutorialWorldSession::refresh_server_flares() {
    for (auto& flare : server_flares_) {
        if (!flare.lit || map_->solid(flare.cell[0U], flare.cell[1U], flare.cell[2U])) {
            continue;
        }
        // The terrain replica destroyed the lamp's voxel; its light goes with
        // it even before the server's DestroyEntity arrives.
        if (static_lights_.remove_at(flare.cell[0U], flare.cell[1U], flare.cell[2U])) {
            StaticLight light;
            light.cell = flare.cell;
            light.radius = StaticLightField::flare_block_radius;
            mark_light_dirty(light);
        }
        flare.lit = false;
    }
}

void TutorialWorldSession::refresh_patch_lights() {
    constexpr double recolour_period{0.25};
    for (const auto& entity : entities_) {
        if ((entity.type != 28U && entity.type != 31U) || !entity.alive || entity.retired ||
            !std::isfinite(entity.position.x) || !std::isfinite(entity.position.y) ||
            !std::isfinite(entity.position.z)) {
            continue;
        }
        const std::array<std::uint32_t, 3U> cell{
            static_cast<std::uint32_t>(std::clamp(std::floor(entity.position.x), 0.0,
                                                  static_cast<double>(VxlMap::width - 1U))),
            static_cast<std::uint32_t>(std::clamp(std::floor(entity.position.y), 0.0,
                                                  static_cast<double>(VxlMap::depth - 1U))),
            static_cast<std::uint32_t>(std::clamp(std::floor(entity.position.z), 0.0,
                                                  static_cast<double>(VxlMap::height - 1U)))};
        auto [found, inserted] = patch_lights_.try_emplace(entity.id, PatchLight{cell, 0.0});
        found->second.clock -= config_.fixed_dt;
        if (!inserted && found->second.clock > 0.0 && found->second.cell == cell) {
            continue;
        }
        if (!inserted && found->second.cell != cell) {
            // A patch whose voxel went drops onto the voxel below.
            static_cast<void>(static_lights_.remove_at(
                found->second.cell[0U], found->second.cell[1U], found->second.cell[2U]));
            StaticLight old_light;
            old_light.cell = found->second.cell;
            old_light.radius = StaticLightField::block_fire_radius;
            mark_light_dirty(old_light);
        }
        found->second.cell = cell;
        found->second.clock = recolour_period;
        const auto ramp = entity.type == 28U ? block_fire_colour(entity.fuse)
                                             : block_goo_colour(entity.fuse);
        const auto channel = [](float value) {
            return static_cast<std::uint8_t>(std::clamp(std::lround(value * 255.0F), 0L, 255L));
        };
        StaticLight light;
        light.cell = cell;
        light.color = VxlColor{channel(ramp[0U]), channel(ramp[1U]), channel(ramp[2U]), 255U};
        light.radius = StaticLightField::block_fire_radius;
        if (static_lights_.add(light)) {
            mark_light_dirty(light);
        }
    }
    for (auto iterator = patch_lights_.begin(); iterator != patch_lights_.end();) {
        const auto live = std::ranges::find_if(entities_, [&](const LocalEntity& entity) {
            return entity.id == iterator->first && entity.alive && !entity.retired;
        });
        if (live != entities_.end()) {
            ++iterator;
            continue;
        }
        const auto [x, y, z] = iterator->second.cell;
        if (static_lights_.remove_at(x, y, z)) {
            StaticLight light;
            light.cell = iterator->second.cell;
            light.radius = StaticLightField::block_fire_radius;
            mark_light_dirty(light);
        }
        iterator = patch_lights_.erase(iterator);
    }
}

bool TutorialWorldSession::apply_server_entity_snapshot(
    const LocalEntity& entity, std::optional<std::int32_t> world_loop) {
    const auto found = std::ranges::find(entities_, entity.id, &LocalEntity::id);
    if (found == entities_.end() || found->type != entity.type || entity.face > 5U ||
        !std::isfinite(entity.position.x) || !std::isfinite(entity.position.y) ||
        !std::isfinite(entity.position.z) || !std::isfinite(entity.velocity.x) ||
        !std::isfinite(entity.velocity.y) || !std::isfinite(entity.velocity.z) ||
        !std::isfinite(entity.yaw) || !std::isfinite(entity.fuse)) {
        return false;
    }
    if (world_loop.has_value()) {
        if (*world_loop < 0 || (found->world_update_loop.has_value() &&
                               *world_loop <= *found->world_update_loop)) return false;
        found->world_update_loop = world_loop;
    }
    const auto* definition = find_entity_definition(found->type);
    const auto same_server_anchor = [](const Vec3& left, const Vec3& right) noexcept {
        constexpr double epsilon{1.0e-6};
        return std::abs(left.x - right.x) <= epsilon &&
               std::abs(left.y - right.y) <= epsilon &&
               std::abs(left.z - right.z) <= epsilon;
    };
    const bool presentation_owns_gravity =
        definition != nullptr && uses_entity_terrain_gravity(*definition);
    const bool server_relocated = !same_server_anchor(found->home, entity.position);

    // WorldUpdate repeats an entity's last server anchor. A grave (and other
    // terrain-owned props) is expected to fall and bounce locally between
    // those snapshots. Comparing the repeated anchor with the locally moved
    // body rewound gravity ten times per second and left it levitating forever.
    // `home` is the last accepted server anchor, so duplicate snapshots update
    // gameplay properties without touching the presentation integrator. A
    // genuinely new anchor, or any row without local gravity, remains fully
    // authoritative.
    if (!presentation_owns_gravity || server_relocated) {
        const bool moved = !same_server_anchor(found->position, entity.position);
        found->position = entity.position;
        found->velocity = entity.velocity;
        if (presentation_owns_gravity) {
            found->home = entity.position;
        }
        if (moved) {
            found->grounded = false;
        }
    }
    if (!world_loop.has_value() || !found->turret_update_loop.has_value() ||
        *world_loop > *found->turret_update_loop) {
        found->yaw = entity.yaw;
        found->aim_yaw = entity.yaw;
    }
    found->team = entity.team;
    found->owner = entity.owner;
    found->face = entity.face;
    found->fuse = entity.fuse;
    found->color = entity.color;
    found->has_color = entity.has_color;
    return true;
}

bool TutorialWorldSession::apply_server_turret_aim(std::uint64_t id,
                                                   double yaw,
                                                   double pitch,
                                                   std::optional<std::int32_t> world_loop) noexcept {
    if (!std::isfinite(yaw) || !std::isfinite(pitch)) {
        return false;
    }
    const auto found = std::ranges::find(entities_, id, &LocalEntity::id);
    if (found == entities_.end() || found->type != 8U) {
        return false;
    }
    if (world_loop.has_value()) {
        if (*world_loop < 0 ||
            (found->turret_update_loop.has_value() && *world_loop <= *found->turret_update_loop) ||
            (found->world_update_loop.has_value() && *world_loop < *found->world_update_loop)) {
            return false;
        }
        found->turret_update_loop = world_loop;
    }
    // These are already retail degrees from WorldUpdate. Do not derive them
    // from the observing player's camera: that was the friendly-target bug.
    found->yaw = yaw;
    found->pitch = pitch;
    found->aim_yaw = yaw;
    found->aim_pitch = pitch;
    return true;
}

bool TutorialWorldSession::apply_server_entity_update(const ServerEntityMutation& mutation) {
    const auto found = std::ranges::find(entities_, mutation.entity_id, &LocalEntity::id);
    if (found == entities_.end()) {
        return false;
    }
    const auto finite_vector = [](const Vec3& value) noexcept {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    switch (mutation.property) {
    case ServerEntityProperty::state:
        if (mutation.integer < 0 || mutation.integer > 255)
            return false;
        found->team = static_cast<std::uint8_t>(mutation.integer);
        return true;
    case ServerEntityProperty::position:
        if (!finite_vector(mutation.vector))
            return false;
        found->position = mutation.vector;
        // Static pickups return to the most recently authored server position.
        found->home = mutation.vector;
        found->grounded = false;
        return true;
    case ServerEntityProperty::velocity:
        if (!finite_vector(mutation.vector))
            return false;
        found->velocity = mutation.vector;
        if (mutation.vector.x != 0.0 || mutation.vector.y != 0.0 || mutation.vector.z != 0.0) {
            found->grounded = false;
            found->attached = false;
        }
        return true;
    case ServerEntityProperty::owner:
        if (mutation.integer < 0 || mutation.integer > 127)
            return false;
        found->owner = static_cast<std::uint8_t>(mutation.integer);
        return true;
    case ServerEntityProperty::forward: {
        if (!finite_vector(mutation.vector))
            return false;
        const auto length = std::sqrt(mutation.vector.x * mutation.vector.x +
                                      mutation.vector.y * mutation.vector.y +
                                      mutation.vector.z * mutation.vector.z);
        if (length <= 1e-9)
            return false;
        const auto x = mutation.vector.x / length;
        const auto y = mutation.vector.y / length;
        const auto z = mutation.vector.z / length;
        const auto horizontal = std::hypot(x, y);
        found->yaw = std::atan2(-y, -x) / degrees_to_radians;
        found->pitch = std::atan2(z, horizontal) / degrees_to_radians;
        found->aim_yaw = found->yaw;
        found->aim_pitch = found->pitch;
        return true;
    }
    case ServerEntityProperty::target: {
        if (mutation.integer < -1 || mutation.integer > 127)
            return false;
        const auto previous = found->target;
        found->target =
            mutation.integer < 0
                ? std::nullopt
                : std::optional<std::uint8_t>{static_cast<std::uint8_t>(mutation.integer)};
        if (found->target != previous) {
            entity_events_.push_back({EntityEventKind::target_changed,
                                      found->id,
                                      found->type,
                                      found->position,
                                      found->target.has_value() ? 1.0 : 0.0});
        }
        return true;
    }
    case ServerEntityProperty::fuse:
        if (!std::isfinite(mutation.scalar))
            return false;
        found->fuse = mutation.scalar;
        // AttachedStickyGrenadeEntity.set_fuse: draw_fuse = True.
        if (found->type == 35U) found->fuse_label = true;
        return true;
    case ServerEntityProperty::ammo:
        if (!std::isfinite(mutation.scalar) || mutation.scalar < 0.0 ||
            mutation.scalar > static_cast<double>(std::numeric_limits<std::uint16_t>::max())) {
            return false;
        }
        found->ammo = static_cast<std::uint16_t>(std::lround(mutation.scalar));
        return true;
    }
    return false;
}

bool TutorialWorldSession::despawn_entity(std::uint64_t id) noexcept {
    const auto found = std::ranges::find(entities_, id, &LocalEntity::id);
    if (found == entities_.end()) {
        return false;
    }
    entity_events_.push_back({EntityEventKind::expired, found->id, found->type, found->position});
    entities_.erase(found);
    return true;
}

void TutorialWorldSession::present_projectile_blast(
    const LocalEntity& entity, std::optional<std::uint8_t> explosion_sound_tool) {
    const auto tool = projectile_tool_for_entity(entity.type);
    if (!tool.has_value()) return;
    const auto* weapon = find_weapon_definition(*tool);
    TutorialProjectile blast;
    blast.id = next_projectile_id_++;
    blast.tool_id = *tool;
    blast.position = entity.position;
    blast.velocity = entity.velocity;
    blast.block_damage = weapon != nullptr ? weapon->block_damage : 0.0;
    blast.crater_radius = weapon != nullptr ? projectile_crater_radius(*weapon) : 3U;
    blast.explosion_sound_tool = explosion_sound_tool.value_or(std::uint8_t{0U});
    // explode_projectile is presentation-only in a network session.
    explode_projectile(blast, std::nullopt);
}

void TutorialWorldSession::present_server_entity_blast(const LocalEntity& entity) {
    present_projectile_blast(entity, std::nullopt);
}

std::optional<LocalEntity> TutorialWorldSession::take_server_entity(std::uint64_t id) {
    const auto found = std::ranges::find(entities_, id, &LocalEntity::id);
    if (found == entities_.end()) return std::nullopt;
    auto entity = *found;
    entity_events_.push_back({EntityEventKind::expired, found->id, found->type, found->position});
    entities_.erase(found);
    return entity;
}

bool TutorialWorldSession::carry_server_entity(std::uint64_t id, Vec3 position) noexcept {
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
        return false;
    }
    const auto found = std::ranges::find(entities_, id, &LocalEntity::id);
    if (found == entities_.end()) return false;
    found->position = position;
    found->velocity = {};
    return true;
}

bool TutorialWorldSession::destroy_server_entity(
    std::uint64_t id, std::optional<std::uint8_t> explosion_sound_tool) {
    if (const auto flare = std::ranges::find(server_flares_, id, &ServerFlare::id);
        flare != server_flares_.end()) {
        // FlareBlockEntity.delete takes its point light and its user block.
        const auto [x, y, z] = flare->cell;
        StaticLight light;
        light.cell = flare->cell;
        light.radius = StaticLightField::flare_block_radius;
        if (static_lights_.remove_at(x, y, z)) {
            mark_light_dirty(light);
        }
        if (map_->solid(x, y, z) && map_->clear_voxel(x, y, z)) {
            mark_dirty(x, y);
        }
        server_flares_.erase(flare);
        return true;
    }
    const auto found = std::ranges::find(entities_, id, &LocalEntity::id);
    if (found == entities_.end()) {
        return false;
    }
    const auto* definition = find_entity_definition(found->type);
    // AttachedStickyGrenadeEntity.on_delete is the sticky grenade's explosion:
    // the stuck grenade (35) blasts exactly like the projectile it came from.
    if (definition != nullptr &&
        (is_server_projectile(*found, *definition) || found->type == 35U)) {
        present_projectile_blast(*found, explosion_sound_tool);
    }
    if (found->type == 11U) {
        // Entity 11 is the retained grave created after ExplodeCorpse(36).
        // Its final DestroyEntity is the authoritative seven-second detonation
        // edge; without this presentation event the stone simply vanished.
        const auto clamp_axis = [](double value, std::uint32_t maximum) {
            return static_cast<std::uint32_t>(
                std::clamp(std::floor(value), 0.0, static_cast<double>(maximum)));
        };
        TerrainImpactEvent impact;
        impact.kind = TerrainImpactKind::grave_explosion;
        impact.cell = {clamp_axis(found->position.x, VxlMap::width - 1U),
                       clamp_axis(found->position.y, VxlMap::depth - 1U),
                       clamp_axis(found->position.z, VxlMap::height - 1U)};
        impact.color = found->has_color
                           ? VxlColor{found->color[0U], found->color[1U], found->color[2U], 255U}
                           : VxlColor{104U, 94U, 88U, 255U};
        impact.destroyed = true;
        impact.radius =
            definition != nullptr ? static_cast<float>(definition->crater_radius) : 3.0F;
        impact.position = {std::array<float, 3U>{static_cast<float>(found->position.x),
                                                 static_cast<float>(found->position.y),
                                                 static_cast<float>(found->position.z)}};
        impact.source_velocity = {static_cast<float>(found->velocity.x),
                                  static_cast<float>(found->velocity.y),
                                  static_cast<float>(found->velocity.z)};
        terrain_impacts_.push_back(impact);
    }
    entity_events_.push_back({EntityEventKind::expired, found->id, found->type, found->position});
    entities_.erase(found);
    return true;
}

void TutorialWorldSession::clear_entities() noexcept {
    // F9 is an offline entity-lab reset. A live session may remove entities
    // only through authoritative DestroyEntity/map-transition processing.
    if (config_.network_authoritative) {
        return;
    }
    entities_.clear();
    // The queue goes with them: an event referring to an entity that no longer
    // exists would have the frontend lease a slot it can never release.
    entity_events_.clear();
}

std::uint8_t TutorialWorldSession::debug_selected_entity_type() const noexcept {
    const auto menu = spawnable_entities();
    if (menu.empty()) {
        return 0U;
    }
    return menu[std::min(debug_entity_index_, menu.size() - 1U)]->type_id;
}

std::size_t TutorialWorldSession::debug_selected_entity_index() const noexcept {
    return debug_entity_index_;
}

void TutorialWorldSession::debug_select_entity_index(std::size_t index) noexcept {
    const auto menu = spawnable_entities();
    if (menu.empty()) {
        return;
    }
    debug_entity_index_ = std::min(index, menu.size() - 1U);
}

void TutorialWorldSession::debug_cycle_entity_type(int direction) noexcept {
    const auto menu = spawnable_entities();
    if (menu.empty() || direction == 0) {
        return;
    }
    const auto count = static_cast<std::int64_t>(menu.size());
    auto next = static_cast<std::int64_t>(debug_entity_index_) + direction;
    // Wrap rather than clamp: the menu is a ring, and clamping at the ends
    // makes the last few rows feel broken.
    next = ((next % count) + count) % count;
    debug_entity_index_ = static_cast<std::size_t>(next);
}

std::size_t TutorialWorldSession::debug_spawn_every_entity() {
    if (config_.network_authoritative) {
        return 0U;
    }
    const auto menu = spawnable_entities();
    if (menu.empty()) {
        return 0U;
    }
    const auto yaw_radians = yaw_ * degrees_to_radians;
    const Vec3 right{-std::sin(yaw_radians), std::cos(yaw_radians), 0.0};
    const Vec3 forward{-std::cos(yaw_radians), -std::sin(yaw_radians), 0.0};
    const auto origin = player_.position;

    // A GRID, not a row. Twenty-odd entities in a line is over 80 units wide --
    // far past the fog distance and impossible to see at once, which defeats
    // the point of a spawn-everything key. A near-square grid keeps the whole
    // set inside one screenful.
    constexpr double spacing{3.5};
    const auto columns =
        static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(menu.size()))));
    std::size_t spawned{};
    for (std::size_t index = 0U; index < menu.size(); ++index) {
        const auto column = static_cast<double>(index % columns);
        const auto row = static_cast<double>(index / columns);
        const auto lateral = (column - static_cast<double>(columns - 1U) * 0.5) * spacing;
        // First rank sits far enough ahead not to clip the camera.
        const auto depth = 7.0 + row * spacing;
        Vec3 position{origin.x + right.x * lateral + forward.x * depth,
                      origin.y + right.y * lateral + forward.y * depth,
                      origin.z};
        if (spawn_entity(menu[index]->type_id, position) != 0U) {
            ++spawned;
        }
    }
    return spawned;
}

// -- Local player health ----------------------------------------------------

double TutorialWorldSession::health() const noexcept {
    return health_;
}

bool TutorialWorldSession::alive() const noexcept {
    return health_ > 0.0;
}

void TutorialWorldSession::reset_health() noexcept {
    health_ = maximum_player_health;
    death_pending_ = false;
    entity_events_.push_back({EntityEventKind::health_changed, 0U, 0U, player_.position, health_});
}

void TutorialWorldSession::set_server_health(double health) noexcept {
    if (!std::isfinite(health)) {
        return;
    }
    const bool was_alive = alive();
    if (health < health_) jetpack_damage_pending_ = true;
    // WorldUpdate carries a signed 16-bit pool and SetHP carries an unsigned
    // byte. Both are server authority; capping them at the tutorial's 100 HP
    // erased custom-mode health before the HUD could apply class durability.
    health_ = std::max(health, 0.0);
    if (was_alive && !alive()) {
        // A live match receives its grave through CreateEntity(21). Retaining
        // local attack/movement latches here both leaks actions past death and
        // risks the offline handle_player_death path creating a duplicate.
        clear_input();
        // A corpse has its own presentation; live fuel and ignition clocks
        // must not keep running or be revived by a delayed active owner row.
        const double fuel_at_death = jetpack_prediction_.fuel;
        jetpack_prediction_ = {};
        jetpack_prediction_.fuel = fuel_at_death;
        player_.jetpack_passive = false;
        player_.parachute_active = false;
        player_.parachute_pending = false;
        parachute_deploy_last_held_ = false;
        parachute_jump_last_held_ = false;
        parachute_fall_touched_ = false;
        player_.parachute_used_this_fall = false;
        player_.parachute_open_frames = 0U;
        sandbox_inventory_.weapons().on_unset();
        network_latched_input_ = {};
        network_latched_orientation_.reset();
        last_simulated_input_ = {};
        death_pending_ = false;
        disguise_active_ = false;
    } else if (!was_alive && alive()) {
        death_pending_ = false;
        network_latched_input_ = {};
        network_latched_orientation_.reset();
        last_simulated_input_ = {};
        movement_events_ = {};
    }
    entity_events_.push_back({EntityEventKind::health_changed, 0U, 0U, player_.position, health_});
}

double TutorialWorldSession::heal(double amount) {
    if (amount <= 0.0 || health_ >= maximum_player_health) {
        return 0.0;
    }
    const auto before = health_;
    health_ = std::min(maximum_player_health, health_ + amount);
    entity_events_.push_back({EntityEventKind::health_changed, 0U, 0U, player_.position, health_});
    return health_ - before;
}

double TutorialWorldSession::apply_damage(double amount, std::uint8_t kill_type) {
    static_cast<void>(kill_type);
    if (amount <= 0.0 || health_ <= 0.0) {
        return 0.0;
    }
    const auto before = health_;
    health_ = std::max(0.0, health_ - amount);
    entity_events_.push_back({EntityEventKind::health_changed, 0U, 0U, player_.position, health_});
    if (health_ <= 0.0 && before > 0.0) {
        // Latched rather than handled inline: death spawns entities, and doing
        // that from inside a blast that is itself iterating the entity vector
        // would invalidate the iteration. The entity pass picks it up.
        death_pending_ = true;
    }
    return before - health_;
}

std::vector<WeaponAction> TutorialWorldSession::take_weapon_actions() {
    return std::exchange(weapon_actions_, {});
}

std::string_view TutorialWorldSession::selected_prefab() const noexcept {
    return debug_full_loadout_ ? sandbox_inventory_.selected_prefab() : std::string_view{};
}

std::optional<std::uint8_t> TutorialWorldSession::selected_ugc_item() const noexcept {
    return debug_full_loadout_ ? sandbox_inventory_.selected_ugc_item() : std::nullopt;
}

bool TutorialWorldSession::cycle_selected_ugc_item_variant() noexcept {
    if (!debug_full_loadout_ || !sandbox_inventory_.cycle_selected_ugc_item_variant()) {
        return false;
    }
    reset_tool_transition_state();
    return true;
}

std::optional<std::array<std::int16_t, 3U>>
TutorialWorldSession::placement_cell(double range) const noexcept {
    const auto hit =
        raycast_voxels(*map_, player_.position, normalized(player_.orientation), range);
    if (!hit.has_value()) {
        return std::nullopt;
    }
    const auto x = static_cast<std::int32_t>(hit->x) + hit->nx;
    const auto y = static_cast<std::int32_t>(hit->y) + hit->ny;
    const auto z = static_cast<std::int32_t>(hit->z) + hit->nz;
    if (x < 0 || y < 0 || z < 0 || x >= static_cast<std::int32_t>(VxlMap::width) ||
        y >= static_cast<std::int32_t>(VxlMap::depth) ||
        z >= static_cast<std::int32_t>(VxlMap::height)) {
        return std::nullopt;
    }
    return std::array<std::int16_t, 3U>{
        static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), static_cast<std::int16_t>(z)};
}

std::optional<std::array<std::int16_t, 3U>>
TutorialWorldSession::ugc_marker_cell(double range) const noexcept {
    const auto hit =
        raycast_voxels(*map_, player_.position, normalized(player_.orientation), range);
    // Top face only: the marker sits in the air cell directly above (AoS z
    // grows downward, so the up-facing normal is nz == -1).
    if (!hit.has_value() || hit->nx != 0 || hit->ny != 0 || hit->nz != -1) {
        return std::nullopt;
    }
    return placement_cell(range);
}

std::optional<VxlColor> TutorialWorldSession::looked_at_block_color(double range) const noexcept {
    const auto hit =
        raycast_voxels(*map_, player_.position, normalized(player_.orientation), range);
    if (!hit.has_value()) {
        return std::nullopt;
    }
    return map_->color(hit->x, hit->y, hit->z);
}

std::optional<DeployableTarget>
TutorialWorldSession::deployable_target(std::uint8_t tool_id) const noexcept {
    if (tool_id == 15U) {
        // MGWeapon.check_available_space_for_deployment unfolds from the
        // character and checks a two-block horizontal prism. PlaceMG then
        // stores the floor voxel under that prism rather than a crosshair hit.
        const auto x = static_cast<long>(std::lround(player_.position.x));
        const auto y = static_cast<long>(std::lround(player_.position.y));
        const auto z = static_cast<long>(std::floor(player_.position.z + 3.0));
        if (x < 0 || y < 0 || z < 0 || x >= static_cast<long>(VxlMap::width) ||
            y >= static_cast<long>(VxlMap::depth) || z >= static_cast<long>(VxlMap::height)) {
            return std::nullopt;
        }
        return DeployableTarget{{static_cast<std::int16_t>(x),
                                 static_cast<std::int16_t>(y),
                                 static_cast<std::int16_t>(z)},
                                4U};
    }

    const auto* rules = deployable_placement(tool_id);
    if (rules == nullptr) {
        return std::nullopt;
    }

    // Retail can_place_object first traces without the placement radius as a
    // ray limit. It then applies the radius as a shell around the integer hit
    // corner, so a distant visible surface is a clean refusal rather than a
    // nearer fabricated target.
    const auto hit =
        raycast_voxels(*map_, player_.position, normalized(player_.orientation), 128.0);
    if (!hit.has_value()) {
        return std::nullopt;
    }
    const auto face = entity_face_from_normal(hit->nx, hit->ny, hit->nz);
    if (!rules->can_place_vertical && face != 4U) {
        return std::nullopt;
    }

    const auto dx = static_cast<double>(hit->x) - player_.position.x;
    const auto dy = static_cast<double>(hit->y) - player_.position.y;
    const auto dz = static_cast<double>(hit->z) - player_.position.z;
    const auto distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (distance > rules->far_radius || distance < rules->player_min_radius) {
        return std::nullopt;
    }

    const Vec3 target{
        static_cast<double>(hit->x), static_cast<double>(hit->y), static_cast<double>(hit->z)};
    const auto crowded = std::ranges::any_of(
        entities_, [&target, limit = rules->entity_min_radius](const LocalEntity& other) {
            if (!other.alive) {
                return false;
            }
            const auto ox = other.position.x - target.x;
            const auto oy = other.position.y - target.y;
            const auto oz = other.position.z - target.z;
            return ox * ox + oy * oy + oz * oz <
                   static_cast<double>(limit) * static_cast<double>(limit);
        });
    if (crowded) {
        return std::nullopt;
    }

    return DeployableTarget{{static_cast<std::int16_t>(hit->x),
                             static_cast<std::int16_t>(hit->y),
                             static_cast<std::int16_t>(hit->z)},
                            face};
}

Vec3 TutorialWorldSession::placement_position(double range) const noexcept {
    const auto cell = placement_cell(range);
    if (!cell.has_value())
        return player_.position;
    return {static_cast<double>((*cell)[0U]),
            static_cast<double>((*cell)[1U]),
            static_cast<double>((*cell)[2U])};
}

Vec3 TutorialWorldSession::action_velocity(const WeaponAction& action) const noexcept {
    const auto* weapon = find_weapon_definition(action.tool_id);
    if (weapon == nullptr)
        return player_.velocity;
    const auto direction = normalized(player_.orientation);
    const auto speed = projectile_speed(*weapon, action);
    return {player_.velocity.x + direction.x * speed,
            player_.velocity.y + direction.y * speed,
            player_.velocity.z + direction.z * speed};
}

std::uint64_t TutorialWorldSession::weapon_action_sequence() const noexcept {
    return weapon_action_sequence_;
}

void TutorialWorldSession::sync_inventory_slots(std::optional<TutorialTool> preferred,
                                                InventorySelectionOrigin origin) {
    std::vector<InventorySlot> slots;
    for (const auto tool : unlocked_tools()) {
        const auto tool_id = retail_tool_id(tool);
        const auto& definition = tool_definition(tool_id);
        slots.push_back(InventorySlot{
            tool_id, InventorySlotKind::loadout, 0U, true, true, definition.selectable_when_empty});
    }
    inventory_.set_slots(std::move(slots));
    if (preferred.has_value()) {
        equip_tool(*preferred, origin);
    }
}

const TutorialLessons& TutorialWorldSession::lessons() const noexcept {
    return lessons_;
}

std::optional<TutorialLessonStage> TutorialWorldSession::take_entered_stage() noexcept {
    auto entered = entered_stage_;
    entered_stage_.reset();
    return entered;
}

const VxlMap& TutorialWorldSession::map() const noexcept {
    return *map_;
}

const PlayerMovementState& TutorialWorldSession::player() const noexcept {
    return player_;
}

double TutorialWorldSession::yaw() const noexcept {
    return yaw_;
}

double TutorialWorldSession::pitch() const noexcept {
    return pitch_;
}

std::array<double, 3U> TutorialWorldSession::eye_position() const noexcept {
    if (config_.network_authoritative) {
        return {network_interpolated_position_.x,
                network_interpolated_position_.y,
                network_interpolated_position_.z};
    }
    return {player_.position.x, player_.position.y, player_.position.z};
}

TutorialDiagnostics TutorialWorldSession::diagnostics() const noexcept {
    TutorialDiagnostics result;
    result.position = player_.position;
    result.velocity = player_.velocity;
    result.yaw = yaw_;
    result.pitch = pitch_;
    result.airborne = player_.airborne;
    result.grounded = grounded(map_.get(), player_);
    result.wade = player_.wade;
    result.crouch = player_.crouch;
    const auto clamp_axis = [](double value) {
        return static_cast<std::uint32_t>(std::clamp(value, 0.0, 511.0));
    };
    result.chunk_x = clamp_axis(player_.position.x) / 16U;
    result.chunk_y = clamp_axis(player_.position.y) / 16U;
    return result;
}

std::uint64_t TutorialWorldSession::ticks_simulated() const noexcept {
    return ticks_;
}

bool TutorialWorldSession::network_authoritative() const noexcept {
    return config_.network_authoritative;
}

bool TutorialWorldSession::sent_held(TutorialAction action) const noexcept {
    // ClientData carries what the tick simulated: a tap latched into the
    // tick is sent even when its release arrived in the same event poll.
    return action_held(action) ||
           (action != TutorialAction::count && tick_held_[static_cast<std::size_t>(action)]);
}

std::uint8_t TutorialWorldSession::movement_flags() const noexcept {
    std::uint8_t flags{};
    if (sent_held(TutorialAction::forward))
        flags |= 0x01U;
    if (sent_held(TutorialAction::backward))
        flags |= 0x02U;
    if (sent_held(TutorialAction::left))
        flags |= 0x04U;
    if (sent_held(TutorialAction::right))
        flags |= 0x08U;
    if (sent_held(TutorialAction::jump))
        flags |= 0x10U;
    if (sent_held(TutorialAction::crouch))
        flags |= 0x20U;
    if (sent_held(TutorialAction::sneak))
        flags |= 0x40U;
    if (sent_held(TutorialAction::sprint))
        flags |= 0x80U;
    // A deploying or deployed gunner sends no movement: Character.set_walk,
    // set_jump and set_crouch ignore the keys, and any movement bit makes the
    // server take the gunner off the gun.
    if (machine_gun_.locks_movement())
        flags = 0U;
    return flags;
}

std::uint8_t TutorialWorldSession::action_flags() const noexcept {
    std::uint8_t flags{0x10U}; // can_display_weapon, required for observers
    if ((primary_held_ || primary_tick_held_) && !machine_gun_.blocks_primary())
        flags |= 0x01U;
    if (secondary_held_)
        flags |= 0x02U;
    if (zoomed_)
        flags |= 0x04U;
    if (machine_gun_.deployed())
        flags |= 0x40U;
    if (sent_held(TutorialAction::hover))
        flags |= 0x80U;
    return flags;
}

void TutorialWorldSession::apply_authoritative_transform(Vec3 position,
                                                         Vec3 orientation,
                                                         Vec3 velocity) noexcept {
    player_.position = position;
    player_.orientation = orientation;
    player_.velocity = velocity;
    // CreatePlayer starts a new movement-history generation. None of the
    // collision/latch state from the previous life may leak into its first
    // ClientData frame: the server also begins that frame from an uncrouched,
    // dry, idle movement latch.
    player_.crouch = false;
    player_.wade = false;
    player_.fall_distance = 0.0;
    player_.climb_timer = 0.0;
    player_.burdened = false;
    player_.jetpack_active = false;
    player_.jetpack_passive = false;
    jetpack_prediction_ = {};
    jetpack_damage_pending_ = false;
    jetpack_replay_required_ = false;
    last_simulated_jetpack_active_ = false;
    parachute_deploy_last_held_ = false;
    parachute_jump_last_held_ = false;
    parachute_fall_touched_ = false;
    player_.parachute_used_this_fall = false;
    player_.parachute_open_frames = 0U;
    last_simulated_parachute_active_ = false;
    last_simulated_parachute_pressed_ = false;
    last_jetpack_fuel_loop_ = std::numeric_limits<std::int32_t>::min();
    last_movement_state_loop_ = std::numeric_limits<std::int32_t>::min();
    player_.parachute_active = false;
    player_.parachute_pending = false;
    disguise_active_ = false;
    // CreatePlayer starts a fresh native Player generation. Its initialized
    // airborne bit is false; boxclipmove owns the first transition even for a
    // deliberately elevated spawn.
    player_.airborne = false;
    network_latched_input_ = {};
    network_latched_orientation_.reset();
    last_simulated_input_ = {};
    last_simulated_orientation_ = orientation;
    network_predictions_.clear();
    jump_requested_ = false;
    movement_events_ = {};
    last_authoritative_position_ = position;
    last_authoritative_position_loop_ = std::numeric_limits<std::int32_t>::min();
    last_reconciled_loop_ = std::numeric_limits<std::int32_t>::min();
    network_interpolated_position_ = position;
    position_lerp_timer_ = 0.0;
    const auto horizontal = std::hypot(orientation.x, orientation.y);
    yaw_ = std::atan2(-orientation.y, -orientation.x) / degrees_to_radians;
    pitch_ = std::atan2(orientation.z, horizontal) / degrees_to_radians;
}

void TutorialWorldSession::note_authoritative_snapshot(std::int32_t acknowledged_loop,
                                                       Vec3 position) noexcept {
    // Self WorldUpdates are sequenced but unreliable. A duplicate row can be
    // rebuilt after the authority has advanced while retaining the same pong
    // stamp; retain only a strictly newer diagnostic authority position.
    // Movement never rewinds to this cached row during a jump.
    if (acknowledged_loop <= last_authoritative_position_loop_)
        return;
    last_authoritative_position_loop_ = acknowledged_loop;
    last_authoritative_position_ = position;
}

void TutorialWorldSession::record_network_prediction(std::int32_t client_loop) {
    if (!config_.network_authoritative)
        return;
    NetworkPredictionSample sample;
    sample.loop = client_loop;
    sample.state = player_;
    sample.consumed_input = last_simulated_input_;
    sample.consumed_orientation = last_simulated_orientation_;
    sample.consumed_jetpack_active = last_simulated_jetpack_active_;
    sample.consumed_jetpack_damage = last_simulated_jetpack_damage_;
    sample.consumed_parachute_active = last_simulated_parachute_active_;
    sample.parachute_deploy_pressed = last_simulated_parachute_pressed_;
    sample.jetpack = jetpack_prediction_;
    sample.movement_class = movement_class_;
    sample.collision_bodies = player_collision_bodies_;
    if (!network_predictions_.empty() && network_predictions_.back().loop == client_loop) {
        network_predictions_.back() = std::move(sample);
    } else {
        network_predictions_.push_back(std::move(sample));
    }
    constexpr std::size_t prediction_capacity{256U};
    while (network_predictions_.size() > prediction_capacity) {
        network_predictions_.pop_front();
    }
}

void TutorialWorldSession::conceal_authoritative_correction(Vec3 position_delta) noexcept {
    const double correction_length =
        std::hypot(position_delta.x, position_delta.y, position_delta.z);
    if (correction_length <= 4.0) {
        // Preserve exactly what was visible immediately before physics moves.
        // A correction received while already smoothing starts a fresh retail
        // 0.1-second window from the current interpolated presentation.
        const auto visible = eye_position();
        network_interpolated_position_ = {visible[0U], visible[1U], visible[2U]};
        position_lerp_timer_ = 0.1;
    } else {
        network_interpolated_position_ = player_.position;
        position_lerp_timer_ = 0.0;
    }
}

bool TutorialWorldSession::reconcile_authoritative(std::int32_t acknowledged_loop,
                                                   Vec3 position,
                                                   Vec3 velocity) {
    // Duplicate/out-of-order owner rows are presentation snapshots, not new
    // correction events. Applying the same ACK twice compounds drift and is
    // the characteristic intermittent local teleport seen under packet
    // reordering.
    if (acknowledged_loop <= last_reconciled_loop_)
        return true;
    const auto found = std::find_if(network_predictions_.begin(),
                                    network_predictions_.end(),
                                    [acknowledged_loop](const NetworkPredictionSample& sample) {
                                        return sample.loop == acknowledged_loop;
                                    });
    if (found == network_predictions_.end())
        return false;
    last_reconciled_loop_ = acknowledged_loop;

    const Vec3 error{position.x - found->state.position.x,
                     position.y - found->state.position.y,
                     position.z - found->state.position.z};
    const double error_squared = error.x * error.x + error.y * error.y + error.z * error.z;

    while (!network_predictions_.empty() && network_predictions_.front().loop < acknowledged_loop) {
        network_predictions_.pop_front();
    }

    // Character.apply_player_network_correction tests the complete position
    // distance against 0.01 squared. It does not independently reconcile a
    // velocity wobble, nor discard small components of an accepted vector.
    if (error_squared <= 0.01 && !jetpack_replay_required_)
        return true;
    jetpack_replay_required_ = false;

    const auto current_before = player_;
    auto replayed = found->state;
    replayed.position = position;
    replayed.velocity = velocity;

    // An owner row describes this old ACK, not every later movement frame.
    // Replaying today's active pack across a historical release/activation
    // fabricates thrust and turns a small correction into a vertical snap.
    found->state = replayed;

    // Retail snaps errors over four blocks and drops the history. A semantic
    // teleport must never be replayed through walls or concealed by the eye.
    if (error_squared > 16.0) {
        // Owner WorldUpdate corrects position/velocity, not mouse look. The
        // acknowledged sample can be many frames older than the current aim.
        replayed.orientation = current_before.orientation;
        replayed.jetpack_active = current_before.jetpack_active;
        player_ = replayed;
        network_interpolated_position_ = replayed.position;
        position_lerp_timer_ = 0.0;
        network_predictions_.clear();
        return true;
    }

    for (auto iterator = std::next(found); iterator != network_predictions_.end(); ++iterator) {
        const auto recorded_aim = iterator->state.orientation;
        const auto recorded_state = iterator->state;
        replayed.burdened = recorded_state.burdened;
        replayed.jetpack = recorded_state.jetpack;
        replayed.jetpack_active = iterator->consumed_jetpack_active;
        replayed.jetpack_passive = recorded_state.jetpack_passive;
        replayed.parachute = recorded_state.parachute;
        replayed.parachute_active = iterator->consumed_parachute_active;
        replayed.orientation = iterator->consumed_orientation;
        apply_crouch_request(replayed, iterator->consumed_input.crouch, map_.get(),
                             iterator->collision_bodies,
                             iterator->consumed_input.hover && replayed.jetpack == 4U);
        static_cast<void>(step_player(replayed,
                                      iterator->consumed_input,
                                      map_.get(),
                                      config_.fixed_dt,
                                      iterator->movement_class,
                                      iterator->collision_bodies,
                                      config_.gravity,
                                      server_movement_bounds_.has_value()
                                          ? &*server_movement_bounds_
                                          : nullptr));
        replayed.orientation = recorded_aim;
        replayed.jetpack_active = recorded_state.jetpack_active;
        replayed.parachute_active = recorded_state.parachute_active;
        replayed.parachute_pending = recorded_state.parachute_pending;
        replayed.parachute_used_this_fall = recorded_state.parachute_used_this_fall;
        replayed.parachute_open_frames = recorded_state.parachute_open_frames;
        iterator->state = replayed;
    }

    const Vec3 position_delta{replayed.position.x - current_before.position.x,
                              replayed.position.y - current_before.position.y,
                              replayed.position.z - current_before.position.z};
    conceal_authoritative_correction(position_delta);
    replayed.orientation = current_before.orientation;
    replayed.burdened = current_before.burdened;
    replayed.jetpack = current_before.jetpack;
    replayed.jetpack_active = current_before.jetpack_active;
    replayed.jetpack_passive = current_before.jetpack_passive;
    replayed.parachute = current_before.parachute;
    replayed.parachute_active = current_before.parachute_active;
    replayed.parachute_pending = current_before.parachute_pending;
    replayed.parachute_used_this_fall = current_before.parachute_used_this_fall;
    replayed.parachute_open_frames = current_before.parachute_open_frames;
    player_ = replayed;
    return true;
}

std::optional<Vec3> TutorialWorldSession::apply_blast_push(std::uint8_t damage_type,
                                                          Vec3 explosion) noexcept {
    if (!alive()) return std::nullopt;
    const auto spec = retail_blast_for_damage_type(damage_type);
    if (!spec.has_value()) return std::nullopt;
    const auto impulse =
        retail_blast_impulse(map_.get(), explosion, player_.position, player_.crouch, *spec);
    if (!impulse.has_value()) return std::nullopt;
    player_.velocity.x += impulse->x;
    player_.velocity.y += impulse->y;
    player_.velocity.z += impulse->z;
    return impulse;
}

void TutorialWorldSession::apply_authoritative_delta(Vec3 position_delta,
                                                     Vec3 velocity_delta) noexcept {
    conceal_authoritative_correction(position_delta);
    player_.position.x += position_delta.x;
    player_.position.y += position_delta.y;
    player_.position.z += position_delta.z;
    player_.velocity.x += velocity_delta.x;
    player_.velocity.y += velocity_delta.y;
    player_.velocity.z += velocity_delta.z;
    if (position_lerp_timer_ <= 0.0) {
        network_interpolated_position_ = player_.position;
    }
}

bool TutorialWorldSession::apply_server_selection(std::uint8_t class_id,
                                                  std::span<const std::uint8_t> loadout,
                                                  std::span<const std::string> prefabs,
                                                  std::span<const std::uint8_t> ugc_tools,
                                                  std::optional<std::uint8_t> selected_tool) {
    if (!config_.network_authoritative ||
        !sandbox_inventory_.spawn_with_selection(class_id, loadout, prefabs, ugc_tools)) {
        return false;
    }
    config_.initial_class_id = class_id;
    config_.initial_loadout.assign(loadout.begin(), loadout.end());
    config_.initial_prefabs.assign(prefabs.begin(), prefabs.end());
    config_.initial_ugc_tools.assign(ugc_tools.begin(), ugc_tools.end());
    sync_movement_equipment(loadout);
    blocks_remaining_ = static_cast<int>(sandbox_inventory_.blocks());
    if (selected_tool.has_value()) {
        static_cast<void>(
            sandbox_inventory_.select_tool(*selected_tool, InventorySelectionOrigin::loadout_sync));
    }
    reset_tool_transition_state();
    return true;
}

bool TutorialWorldSession::apply_server_tool(std::uint8_t tool_id) noexcept {
    if (!config_.network_authoritative)
        return false;
    if (selected_tool_id() == tool_id)
        return true;

    const auto slots = sandbox_inventory_.toolbar().slots();
    const bool visible = std::ranges::any_of(
        slots, [tool_id](const InventorySlot& item) { return item.tool_id == tool_id; });
    if (!visible) {
        return false;
    }

    // A local player's ordinary held tool is input-owned. BattleSpades emits
    // 0xFF in self WorldUpdate rows, but older/third-party endpoints may echo
    // a delayed real byte. Treating that byte as a command produces the
    // reported weapon changes with no key press. Objective pickup tools are
    // the exception: their visibility was just authorized by the pickup byte
    // in the same row, and retail equips them automatically.
    constexpr std::array<std::uint8_t, 3U> objective_tools{25U, 26U, 30U};
    const bool objective =
        std::ranges::find(objective_tools, tool_id) != objective_tools.end();
    if (selected_tool_id().has_value() && !objective) {
        return true;
    }

    set_secondary_held(false);
    if (!sandbox_inventory_.select_tool(tool_id, InventorySelectionOrigin::loadout_sync)) {
        return false;
    }
    reset_tool_transition_state();
    return true;
}

void TutorialWorldSession::apply_server_movement_state(std::uint8_t action_flags,
                                                       std::uint8_t state_flags,
                                                       std::uint8_t pickup_id,
                                                       std::optional<std::int32_t> acknowledged_loop) noexcept {
    if (!config_.network_authoritative)
        return;
    if (acknowledged_loop.has_value()) {
        if (*acknowledged_loop <= last_movement_state_loop_) return;
        last_movement_state_loop_ = *acknowledged_loop;
    }
    const bool active = alive() && (action_flags & 0x04U) != 0U;
    const auto found = acknowledged_loop.has_value() ? std::ranges::find(
        network_predictions_, *acknowledged_loop, &NetworkPredictionSample::loop) : network_predictions_.end();
    if (alive() && found != network_predictions_.end()) {
        // The advertised bit precedes native thrust by three recurrences.
        // Compare it to the same ACK's resource state, never today's key.
        if (found->jetpack.advertised_active != active) {
            found->jetpack.advertised_active = active;
            found->jetpack.activation_defer =
                active ? jetpack_activation_defer_frames : std::uint8_t{};
            found->jetpack.physics_active = false;
            found->jetpack.exhaustion_tail = 0U;
            found->jetpack.requires_release = !active && found->jetpack.fuel <= 0.0;
            replay_jetpack_prediction(*acknowledged_loop);
        }
    } else if (alive() && (!acknowledged_loop.has_value() || network_predictions_.empty())) {
        // Bootstrap has no input journal. An already-active row represents
        // existing flight, rather than a locally observed activation edge.
        const bool held = player_.jetpack == 4U ? action_held(TutorialAction::hover)
                                               : action_held(TutorialAction::jump);
        jetpack_prediction_.advertised_active = active && held && jetpack_prediction_.fuel > 0.0 &&
                                                !jetpack_prediction_.requires_release;
        jetpack_prediction_.physics_active = jetpack_prediction_.advertised_active;
        jetpack_prediction_.activation_defer = 0U;
        player_.jetpack_active = jetpack_prediction_.physics_active;
        player_.jetpack_passive = player_.jetpack == 2U && player_.jetpack_active;
    }
    const bool parachute_active = alive() && player_.parachute && (state_flags & 0x01U) != 0U;
    if (found != network_predictions_.end()) {
        // An older closed row must not cancel a newer local deployment. Start
        // at its ACK and replay the subsequent key edges and landing states.
        replay_parachute_prediction(*acknowledged_loop, parachute_active);
    } else if (!acknowledged_loop.has_value() || network_predictions_.empty()) {
        player_.parachute_active = parachute_active;
    }
    disguise_active_ = (state_flags & 0x02U) != 0U;
    // Player pickup setter: the carrier equips PICKUPS[pickup] and cannot
    // switch away, except the Classic CTF intel (can_shoot_holding_intel).
    constexpr std::uint8_t intel_pickup{16U};
    const auto held_before = sandbox_inventory_.toolbar().selected_tool_id();
    sandbox_inventory_.set_carried_pickup(
        pickup_id, !(pickup_id == intel_pickup && can_shoot_holding_intel_));
    if (sandbox_inventory_.toolbar().selected_tool_id() != held_before) {
        set_secondary_held(false);
        reset_tool_transition_state();
    }
    if (pickup_id == 0xFFU)
        player_.burdened = false;
}

void TutorialWorldSession::apply_server_jetpack_fuel(
    double fuel, std::optional<std::int32_t> acknowledged_loop) noexcept {
    if (!config_.network_authoritative || !alive() || !std::isfinite(fuel)) return;
    if (acknowledged_loop.has_value()) {
        if (*acknowledged_loop <= last_jetpack_fuel_loop_) return;
        last_jetpack_fuel_loop_ = *acknowledged_loop;
    }
    fuel = std::clamp(fuel, 0.0, 100.0);
    const auto found = acknowledged_loop.has_value() ? std::ranges::find(
        network_predictions_, *acknowledged_loop, &NetworkPredictionSample::loop) : network_predictions_.end();
    if (found != network_predictions_.end()) {
        // Preserve the exact recurrence when the only difference is the
        // packet's 1/64 quantization; rounding must not move fuel exhaustion.
        if (std::abs(found->jetpack.fuel - fuel) > 1.0 / 64.0) {
            found->jetpack.fuel = fuel;
            replay_jetpack_prediction(*acknowledged_loop);
        }
    } else if (!acknowledged_loop.has_value() || network_predictions_.empty()) {
        jetpack_prediction_.fuel = fuel;
    }
}

void TutorialWorldSession::advance_jetpack_prediction(
    JetpackPredictionState& state, std::uint8_t pack,
    const PlayerInputState& input, double dt, bool damaged, bool grounded) noexcept {
    // Same consumed-input recurrence as server.Player._update_jetpack. The
    // owner predicts it without waiting for an active WorldUpdate to travel
    // back: wire action 0x04 advertises ignition before native thrust begins.
    if (pack == 0U || pack > 4U) { state = {}; return; }
    const auto& profile = config_.flight_profile;
    const auto& props = jetpack_properties(pack);
    const double refill_delay = props.refill_delay_due_damage;
    state.refill_delay_remaining = damaged ? refill_delay :
        std::max(0.0, state.refill_delay_remaining - dt);
    const bool held = pack == 4U ? input.hover : input.jump;
    state.idle_seconds = held || state.advertised_active || state.physics_active
        ? 0.0 : state.idle_seconds + dt;
    if (held && state.requires_release && state.exhaustion_tail > 0U) {
        state.advertised_active = false;
        state.physics_active = true;
        state.activation_defer = 0U;
        --state.exhaustion_tail;
        state.fuel = 0.0;
        return;
    }
    const bool previously_active = state.advertised_active;
    if (previously_active && state.activation_defer > 0U) {
        state.physics_active = false;
        --state.activation_defer;
    } else {
        state.physics_active = previously_active;
    }
    bool newly_activated{};
    if (held) {
        state.held_seconds += dt;
        const double delay = props.start_delay;
        const double cost = props.activation_cost;
        if (!previously_active && !state.requires_release &&
            state.held_seconds >= delay && state.fuel >= std::max(cost, 1.0)) {
            state.advertised_active = true;
            state.fuel -= cost;
            state.physics_active = false;
            state.activation_defer = jetpack_activation_defer_frames;
            state.exhaustion_tail = 0U;
            newly_activated = true;
        }
    } else {
        state.held_seconds = 0.0;
        state.advertised_active = false;
        state.physics_active = false;
        state.activation_defer = 0U;
        state.exhaustion_tail = 0U;
        state.requires_release = false;
    }
    if (!newly_activated && (state.advertised_active || state.physics_active)) {
        state.fuel = std::max(0.0, state.fuel - profile.drain[pack] * dt);
        if (state.fuel <= 0.0) {
            state.advertised_active = false;
            state.requires_release = true;
            state.activation_defer = 0U;
            state.exhaustion_tail =
                state.physics_active ? jetpack_exhaustion_tail_frames : std::uint8_t{};
        }
    }
    if (!state.advertised_active && !state.physics_active && state.refill_delay_remaining <= 0.0 &&
        (pack == 4U || !profile.grounded_refill_only ||
         (grounded && state.idle_seconds + 1e-9 >= profile.refill_idle_seconds))) {
        state.fuel = std::min(props.max_fuel, state.fuel + profile.refill[pack] * dt);
    }
}

void TutorialWorldSession::replay_jetpack_prediction(std::int32_t acknowledged_loop) noexcept {
    const auto found = std::ranges::find(network_predictions_, acknowledged_loop,
                                        &NetworkPredictionSample::loop);
    if (found == network_predictions_.end()) return;
    auto state = found->jetpack;
    found->state.jetpack_active = state.physics_active;
    found->state.jetpack_passive = found->state.jetpack == 2U && state.physics_active;
    for (auto iterator = std::next(found); iterator != network_predictions_.end(); ++iterator) {
        advance_jetpack_prediction(state, iterator->state.jetpack, iterator->consumed_input,
                                  config_.fixed_dt, iterator->consumed_jetpack_damage,
                                  !std::prev(iterator)->state.airborne || std::prev(iterator)->state.wade);
        if (iterator->jetpack_restocked) state.fuel = 100.0;
        if (iterator->consumed_jetpack_active != state.physics_active) {
            jetpack_replay_required_ = true;
        }
        iterator->consumed_jetpack_active = state.physics_active;
        iterator->state.jetpack_active = state.physics_active;
        iterator->state.jetpack_passive = iterator->state.jetpack == 2U && state.physics_active;
        iterator->jetpack = state;
    }
    jetpack_prediction_ = state;
    player_.jetpack_active = state.physics_active;
    player_.jetpack_passive = player_.jetpack == 2U && state.physics_active;
}

void TutorialWorldSession::apply_server_pickup_burden(bool burdened) noexcept {
    if (config_.network_authoritative)
        player_.burdened = burdened;
}

void TutorialWorldSession::replay_parachute_prediction(
    std::int32_t acknowledged_loop, bool active) noexcept {
    const auto found = std::ranges::find(network_predictions_, acknowledged_loop,
                                        &NetworkPredictionSample::loop);
    if (found == network_predictions_.end()) return;
    // The ACKed row's bit 0x01 is authority for that frame. Adopt it, keeping
    // the local fall bookkeeping when the two sides already agree. The same
    // rules close a canopy on both sides in the same frame, so a closed row
    // against a local canopy means the server never opened it: its one
    // deploy for this fall is still unused.
    auto& acknowledged = found->state;
    const bool server_active = active && acknowledged.parachute;
    if (acknowledged.parachute_active != server_active) {
        acknowledged.parachute_active = server_active;
        acknowledged.parachute_pending = false;
        acknowledged.parachute_used_this_fall = server_active;
        acknowledged.parachute_open_frames = 0U;
    }
    // Re-run the server rules over every later frame from its own pre-move
    // state (the previous sample) and consumed edges.
    auto previous = found;
    for (auto next = std::next(found); next != network_predictions_.end(); ++next) {
        auto probe = previous->state;
        probe.parachute = next->state.parachute;
        probe.jetpack = next->state.jetpack;
        advance_parachute_rules(probe, next->parachute_deploy_pressed,
                                probe.parachute && probe.jetpack == 0U,
                                next->consumed_input.crouch, map_.get(), config_.fixed_dt);
        if (next->consumed_parachute_active != probe.parachute_active) {
            jetpack_replay_required_ = true;
        }
        next->consumed_parachute_active = probe.parachute_active;
        auto& state = next->state;
        state.parachute_active = probe.parachute_active;
        state.parachute_pending = probe.parachute_pending;
        state.parachute_used_this_fall = probe.parachute_used_this_fall;
        state.parachute_open_frames = probe.parachute_open_frames;
        settle_parachute_after_move(state);
        previous = next;
    }
    const auto& latest = previous->state;
    const bool holds = alive() && player_.parachute;
    player_.parachute_active = holds && latest.parachute_active;
    player_.parachute_pending = holds && latest.parachute_pending;
    player_.parachute_used_this_fall = latest.parachute_used_this_fall;
    player_.parachute_open_frames = latest.parachute_open_frames;
}

void TutorialWorldSession::sync_movement_equipment(std::span<const std::uint8_t> loadout) noexcept {
    // Protocol ids 66..69 map to world.pyd's compact 1..4 pack enum.
    const auto previous_pack = player_.jetpack;
    player_.jetpack = 0U;
    player_.parachute = false;
    for (const auto item : loadout) {
        if (item >= 66U && item <= 69U && player_.jetpack == 0U) {
            player_.jetpack = static_cast<std::uint8_t>(item - 65U);
        } else if (item == 72U) {
            player_.parachute = true;
        }
    }
    if (player_.jetpack == 0U) {
        player_.jetpack_active = false;
        player_.jetpack_passive = false;
    }
    if (player_.jetpack != previous_pack) {
        player_.jetpack_active = false;
        player_.jetpack_passive = false;
        jetpack_prediction_ = {};
        jetpack_replay_required_ = false;
        last_jetpack_fuel_loop_ = std::numeric_limits<std::int32_t>::min();
        last_movement_state_loop_ = std::numeric_limits<std::int32_t>::min();
    }
    if (!player_.parachute) {
        player_.parachute_active = false;
        player_.parachute_pending = false;
    }
}

void TutorialWorldSession::apply_server_class(std::uint8_t class_id,
                                              double movement_speed_scale) noexcept {
    config_.initial_class_id = class_id;
    if (std::isfinite(movement_speed_scale) && movement_speed_scale > 0.0) {
        config_.movement_speed_scale = movement_speed_scale;
    }
    movement_class_ = movement_config_for_class(class_id, config_.movement_speed_scale,
                                                config_.fall_on_water_damage);
    apply_flight_profile(movement_class_, config_.flight_profile);
}

} // namespace battlespades::world
