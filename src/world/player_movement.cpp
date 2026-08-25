#include "battlespades/world/player_movement.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace battlespades::world {
namespace {

constexpr std::array<MovementClassConfig, 18U> class_movement_profiles{{
    {0.7, 1.4, 0.5, 1.2, 8.0, true},
    {0.7, 1.45, 0.5, 1.5, 8.0, true},
    {0.7, 1.1, 0.5, 1.0, 12.0, true},
    {0.7, 1.4, 0.5, 1.2, 8.0, true},
    {0.5, 1.65, 0.5, 1.5, 4.0, true},
    {1.0, 1.33, 0.5, 1.0, 8.0, false},
    {0.7, 1.5, 0.5, 1.2, 8.0, true},
    {0.7, 1.5, 0.5, 1.2, 8.0, true},
    {0.7, 1.5, 0.5, 1.2, 8.0, true},
    {0.7, 1.5, 0.5, 1.2, 8.0, true},
    {0.7, 1.5, 0.5, 1.2, 8.0, true},
    {0.7, 1.5, 0.5, 1.2, 8.0, true},
    {0.7, 1.25, 0.5, 1.0, 8.0, true},
    {1.0, 3.0, 0.5, 1.0, 1.0, true},
    {1.1, 3.0, 0.25, 2.5, 12.0, true},
    {0.5, 1.0, 0.5, 3.0, 8.0, false},
    {0.85, 1.55, 0.5, 1.5, 8.0, true},
    {0.6, 1.35, 0.5, 1.2, 8.0, true},
}};

// Recovered movement constants (BattleSpades aoslib/world.pyx, oracle-
// calibrated against the live retail client).
constexpr double default_world_gravity{1.0};
constexpr double player_radius{0.45};
constexpr double standing_contact_offset{2.25};
constexpr double crouching_contact_offset{1.35};
constexpr double crouch_shift{0.9};
constexpr double fall_slow_down{0.24};
constexpr double severe_landing_velocity{0.8};
constexpr double ground_friction{4.0};
constexpr double air_friction{2.0};
constexpr double physics_scale{32.0};
constexpr double grounded_probe_epsilon{0.00875};
constexpr double map_edge{512.0};
constexpr double map_height{240.0};
// Tutorial parity uses the Soldier fall-damage curve; the session discards
// the damage but keeps the landing slowdown semantics.
constexpr double fall_min_distance{10.0};
constexpr double fall_max_distance{40.0};
constexpr double fall_max_damage{100.0};

[[nodiscard]] bool is_solid(const VxlMap* map, int x, int y, int z) noexcept {
    if (x < 0 || x >= static_cast<int>(map_edge) || y < 0 || y >= static_cast<int>(map_edge)) {
        return true;
    }
    if (z < 0 || z >= static_cast<int>(map_height) || map == nullptr) {
        return false;
    }
    return map->solid(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                      static_cast<std::uint32_t>(z));
}

/**
 * Retail clipbox @0x3c00 with allow_below_water=0. Truncation semantics at
 * block boundaries are load-bearing: a probe exactly on an integer face
 * belongs to the higher block, z in (-1,0) is empty, x/y outside the map
 * read solid, the open-water row 239 samples the bed at 238 and z>239 is
 * the solid world floor.
 */
[[nodiscard]] bool clip(const VxlMap* map, double x, double y, double z) noexcept {
    if (x < 0.0) {
        return true;
    }
    const int ix = static_cast<int>(x);
    if (ix > 511) {
        return true;
    }
    if (y < 0.0) {
        return true;
    }
    const int iy = static_cast<int>(y);
    if (iy > 511) {
        return true;
    }
    const int iz = static_cast<int>(z);
    if (iz < 0) {
        return false;
    }
    int sample_z = 238;
    if (iz != 239) {
        sample_z = iz;
        if (iz > 238) {
            return true;
        }
    }
    if (map == nullptr) {
        return false;
    }
    return map->solid(static_cast<std::uint32_t>(ix), static_cast<std::uint32_t>(iy),
                      static_cast<std::uint32_t>(sample_z));
}

/** The four AABB corners at radius 0.45 — retail's only lateral probe set. */
[[nodiscard]] bool clip_corners(const VxlMap* map, double cx, double cy, double z) noexcept {
    return clip(map, cx - 0.45, cy - 0.45, z) || clip(map, cx - 0.45, cy + 0.45, z) ||
           clip(map, cx + 0.45, cy - 0.45, z) || clip(map, cx + 0.45, cy + 0.45, z);
}

struct MoveBoxResult final {
    bool climbed{};
    bool landed{};
    bool airborne{};
    bool wade{};
    bool wade_valid{};
    bool crouch{};
};

/**
 * Single-pass port of the retail boxclipmove @0x3e90: X section, X glide,
 * Y section (with the X-glided z), Y glide, then the vertical move. All
 * intermediates are float32 like the compiled engine; double changes voxel
 * selection at exact contact planes and diverges collision branches.
 */
MoveBoxResult move_box(Vec3& position, Vec3& velocity, double dt, const VxlMap* map,
                       bool crouch, bool hover, bool sprint, bool can_uphill,
                       bool airborne, bool wade) {
    static_cast<void>(wade);
    const float px = static_cast<float>(position.x);
    const float py = static_cast<float>(position.y);
    const float pz = static_cast<float>(position.z);
    float vx = static_cast<float>(velocity.x);
    float vy = static_cast<float>(velocity.y);
    float vz = static_cast<float>(velocity.z);
    bool result_crouch = crouch;
    bool climbed = false;

    const float v35 = static_cast<float>(dt) * static_cast<float>(physics_scale);
    const float candidate_x = vx * v35 + px;
    const float candidate_y = vy * v35 + py;

    int lp{};
    float m{};
    float rad{};
    if (crouch && !hover) {
        lp = 2;
        m = 0.89999998F;
        rad = 0.44999999F;
    } else {
        lp = 3;
        m = 1.3499999F;
        rad = 0.89999998F;
    }
    const float v31 = rad;
    const float v36 = pz + rad;
    const float v40 = m + v36;
    float wx = px;
    float wy = py;

    // ---- X section ----
    {
        bool advanced = false;
        for (int level = 0; level < lp; ++level) {
            if (clip_corners(map, candidate_x, py, v40 - static_cast<float>(level))) {
                break;
            }
            if (level + 1 == lp) {
                wx = candidate_x;
                advanced = true;
            }
        }
        if (!advanced && !crouch && !hover && (!sprint || can_uphill)) {
            bool hit = false;
            for (int level = 0; level < lp; ++level) {
                if (clip_corners(map, candidate_x, py,
                                 (v40 - static_cast<float>(level)) - 1.0F)) {
                    vx = 0.0F;
                    hit = true;
                    break;
                }
            }
            if (!hit && !clip_corners(map, candidate_x, wy, (v36 - m) - 1.0F)) {
                wx = candidate_x;
                climbed = true;
            }
        }
    }

    // ---- X glide pass ----
    float glide = 0.0F;
    float glided_base = v36;
    {
        bool overlap = true;
        for (int level = 0; level < lp; ++level) {
            if (clip_corners(map, wx, wy, v40 - static_cast<float>(level))) {
                break;
            }
            if (level + 1 == lp) {
                overlap = false;
            }
        }
        if (overlap) {
            // The glide amount stays set even when the head revert fires:
            // retail still takes the frozen-z glide finalize that frame.
            glide = ((vx * vx * 4.0F) + 0.050000001F) * v35;
            const float head = std::floor(v36 - m);
            if (clip_corners(map, wx, wy, head)) {
                wx = px;
                vx = 0.0F;
            } else {
                glided_base = v36 - glide;
                vz = 0.0F;
            }
        }
    }
    float v37 = glided_base;
    const float v45 = m + glided_base;

    // ---- Y section (at the X-advanced x, with the glided z) ----
    {
        bool advanced = false;
        for (int level = 0; level < lp; ++level) {
            if (clip_corners(map, wx, candidate_y, v45 - static_cast<float>(level))) {
                break;
            }
            if (level + 1 == lp) {
                wy = candidate_y;
                advanced = true;
            }
        }
        if (!advanced && !crouch && !hover && (!sprint || can_uphill)) {
            bool hit = false;
            for (int level = 0; level < lp; ++level) {
                if (clip_corners(map, wx, candidate_y,
                                 (v45 - static_cast<float>(level)) - 1.0F)) {
                    vy = 0.0F;
                    hit = true;
                    break;
                }
            }
            if (!hit && !clip_corners(map, wx, candidate_y, (v37 - m) - 1.0F)) {
                wy = candidate_y;
                climbed = true;
            }
        }
    }

    // ---- Y glide pass ----
    {
        bool overlap = true;
        for (int level = 0; level < lp; ++level) {
            if (clip_corners(map, wx, wy, v45 - static_cast<float>(level))) {
                break;
            }
            if (level + 1 == lp) {
                overlap = false;
            }
        }
        if (overlap) {
            const float lift = ((vy * vy * 4.0F) + 0.050000001F) * v35;
            glide = lift;
            const float head = std::floor(v37 - m);
            if (clip_corners(map, wx, wy, head)) {
                wy = py;
                vy = 0.0F;
            } else {
                v37 = v37 - lift;
                vz = 0.0F;
            }
        }
    }

    // ---- finalize ----
    position.x = wx;
    position.y = wy;
    MoveBoxResult result;
    result.crouch = result_crouch;
    if (climbed) {
        velocity.x = vx;
        velocity.y = vy;
        velocity.z = vz;
        position.z = v37 - v31;
        result.climbed = true;
        result.airborne = false;
        return result;
    }
    if (glide != 0.0F) {
        // Glide frame: the vertical move is skipped entirely (z frozen at
        // the glided value, airborne untouched). Auto-crouch into a
        // one-and-a-fraction block gap.
        if (!result_crouch) {
            if (clip(map, wx, wy, std::floor(m + v37)) &&
                !clip(map, wx, wy, std::floor(v37)) &&
                !clip(map, wx, wy, std::floor(v37 - m))) {
                result.crouch = true;
            }
        }
        velocity.x = vx;
        velocity.y = vy;
        velocity.z = vz;
        position.z = v37 - v31;
        result.airborne = airborne;
        return result;
    }

    // Normal vertical move.
    const float direction = vz >= 0.0F ? m : -m;
    v37 = v37 + v35 * vz;
    const float probe = std::floor(direction + v37);
    if (!clip_corners(map, wx, wy, probe)) {
        velocity.x = vx;
        velocity.y = vy;
        velocity.z = vz;
        position.z = v37 - v31;
        result.airborne = true;
        return result;
    }
    // Vertical blocked: keep the exact frame-start z (no partial advance).
    position.z = pz;
    velocity.x = vx;
    velocity.y = vy;
    velocity.z = 0.0;
    if (vz >= 0.0F) {
        result.landed = true;
        result.airborne = false;
        result.wade = pz > 237.0F;
        result.wade_valid = true;
        return result;
    }
    result.airborne = true;
    return result;
}

/**
 * Port of check_for_ground_holes @0x2290: standing still on a ledge lip
 * (own column empty one block under the feet) drifts the player toward the
 * hole centre so client and server settle identically.
 */
void check_for_ground_holes(Vec3& position, Vec3& velocity, double dt, const VxlMap* map,
                            bool crouch, bool hover, const PlayerInputState& input) {
    const double feet =
        position.z + (crouch && !hover ? 1.3499999 : 2.25);
    if (input.forward || input.backward || input.left || input.right) {
        return;
    }
    const int column_x = static_cast<int>(std::floor(position.x));
    const int column_y = static_cast<int>(std::floor(position.y));
    const int below = static_cast<int>(std::floor(feet + 1.0));
    if (!(below > 239 || below < 0 || column_y > 511 || column_y < 0 || column_x > 511 ||
          !is_solid(map, column_x, column_y, below))) {
        return;
    }

    const double to_center_x = (static_cast<double>(column_x) + 0.5) - position.x;
    const double abs_x = std::fabs(to_center_x);
    const int neighbor_x = abs_x != 0.0
                               ? column_x - static_cast<int>(to_center_x / abs_x)
                               : std::numeric_limits<int>::min() / 2;
    bool x_neighbor_empty = true;
    if (below >= 0 && below <= 239 && column_y >= 0 && column_y <= 511 && neighbor_x >= 0 &&
        neighbor_x <= 511) {
        x_neighbor_empty = !is_solid(map, neighbor_x, column_y, below);
    }

    const double to_center_y = (static_cast<double>(column_y) + 0.5) - position.y;
    const double abs_y = std::fabs(to_center_y);
    const int neighbor_y = abs_y != 0.0
                               ? column_y - static_cast<int>(to_center_y / abs_y)
                               : std::numeric_limits<int>::min() / 2;
    bool y_neighbor_empty = true;
    if (below >= 0 && below <= 239 && neighbor_y >= 0 && neighbor_y <= 511 && column_x >= 0 &&
        column_x <= 511) {
        y_neighbor_empty = !is_solid(map, column_x, neighbor_y, below);
    }

    bool push_x = !x_neighbor_empty;
    bool push_y = !y_neighbor_empty;
    if (x_neighbor_empty && y_neighbor_empty && below >= 0 && below <= 239 &&
        neighbor_y >= 0 && neighbor_y <= 511 && neighbor_x >= 0 && neighbor_x <= 511 &&
        is_solid(map, neighbor_x, neighbor_y, below)) {
        // Both straight neighbours empty but the diagonal is solid: pick one
        // axis by signed dy <= dx.
        if (to_center_y <= to_center_x) {
            push_y = true;
        } else {
            push_x = true;
        }
    }

    if ((!push_x || abs_x <= 0.2) && (!push_y || abs_y <= 0.2)) {
        if (push_x) {
            velocity.x = (to_center_x * dt) * 5.0;
        }
        if (push_y) {
            velocity.y = (to_center_y * dt) * 5.0;
        }
    }
}

[[nodiscard]] double sign(double value) noexcept {
    return value < 0.0 ? -1.0 : (value > 0.0 ? 1.0 : 0.0);
}

/**
 * Retail `_collide_with_players`: run after horizontal friction and before
 * boxclipmove. The impulse resolves the predicted next horizontal position,
 * preferring vertical separation only when its overlap is the smaller axis.
 */
void collide_with_players(PlayerMovementState& state,
                          std::span<const PlayerCollisionBody> bodies,
                          double dt) noexcept {
    if (bodies.empty()) return;
    const double own_height = player_body_height(state.crouch, state.wade);
    const double own_center_z =
        state.position.z + ((own_height - player_radius) - 0.5 * own_height);
    const double scale = std::max(dt * physics_scale, 1.0e-6);
    for (const auto& body : bodies) {
        if (!std::isfinite(body.position.x) || !std::isfinite(body.position.y) ||
            !std::isfinite(body.position.z) || !std::isfinite(body.height) ||
            body.height <= 0.0) {
            continue;
        }
        const double dx =
            (state.position.x + state.velocity.x * scale) - body.position.x;
        const double dy =
            (state.position.y + state.velocity.y * scale) - body.position.y;
        const double distance_squared = dx * dx + dy * dy;
        const double distance = std::sqrt(distance_squared);
        const double push = std::max(0.0, 2.0 * player_radius - distance);
        if (push <= 0.0) continue;

        const double other_center_z =
            body.position.z +
            ((body.height - player_radius) - 0.5 * body.height);
        const double vertical_overlap = std::max(
            0.0, 0.5 * (body.height + own_height) -
                     std::abs(own_center_z - other_center_z));
        if (vertical_overlap <= 0.0) continue;

        if (vertical_overlap <= push) {
            state.velocity.z +=
                sign(own_center_z - other_center_z) * (vertical_overlap / scale);
        } else {
            const double nx = distance <= 0.0 ? 1.0 : dx / distance;
            const double ny = distance <= 0.0 ? 0.0 : dy / distance;
            state.velocity.x += nx * (push / scale);
            state.velocity.y += ny * (push / scale);
        }
    }
}

/** Retail AABB overlap used by the stand-up headroom check. */
[[nodiscard]] bool aabb_collides(const VxlMap* map, double x, double y, double z,
                                 double radius, double contact_offset) noexcept {
    const int min_x = static_cast<int>(std::floor(x - radius));
    const int max_x = static_cast<int>(std::floor(x + radius));
    const int min_y = static_cast<int>(std::floor(y - radius));
    const int max_y = static_cast<int>(std::floor(y + radius));
    const int min_z = static_cast<int>(std::floor((z - radius) + 1e-6));
    const int max_z = static_cast<int>(std::floor(z + contact_offset - 1e-6));
    if (max_x < 0 || max_y < 0 || min_x >= static_cast<int>(map_edge) ||
        min_y >= static_cast<int>(map_edge)) {
        return true;
    }
    for (int bx = min_x; bx <= max_x; ++bx) {
        for (int by = min_y; by <= max_y; ++by) {
            for (int bz = min_z; bz <= max_z; ++bz) {
                if (is_solid(map, bx, by, bz)) {
                    return true;
                }
            }
        }
    }
    return false;
}

} // namespace

bool constrain_player_to_bounds(PlayerMovementState& state,
                                const PlayerMovementBounds& bounds) noexcept {
    const auto finite = [](Vec3 value) {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
               std::isfinite(value.z);
    };
    if (!finite(state.position) || !finite(state.velocity) ||
        !finite(bounds.minimum) || !finite(bounds.maximum) ||
        bounds.minimum.x > bounds.maximum.x ||
        bounds.minimum.y > bounds.maximum.y ||
        bounds.minimum.z > bounds.maximum.z) {
        return false;
    }
    const auto constrain_axis = [](double& position,
                                   double& velocity,
                                   double minimum,
                                   double maximum) {
        if (position < minimum) {
            position = minimum;
            if (velocity < 0.0)
                velocity = 0.0;
        } else if (position > maximum) {
            position = maximum;
            if (velocity > 0.0)
                velocity = 0.0;
        }
    };
    constrain_axis(state.position.x, state.velocity.x,
                   bounds.minimum.x, bounds.maximum.x);
    constrain_axis(state.position.y, state.velocity.y,
                   bounds.minimum.y, bounds.maximum.y);
    constrain_axis(state.position.z, state.velocity.z,
                   bounds.minimum.z, bounds.maximum.z);
    return true;
}

MovementClassConfig movement_config_for_class(
    std::uint8_t class_id, double movement_speed_scale) noexcept {
    auto result = class_id < class_movement_profiles.size()
                      ? class_movement_profiles[class_id]
                      : class_movement_profiles[0U];
    if (!std::isfinite(movement_speed_scale) || movement_speed_scale <= 0.0) {
        movement_speed_scale = 1.0;
    }
    result.accel_multiplier *= movement_speed_scale;
    result.sprint_multiplier *= movement_speed_scale;
    result.crouch_sneak_multiplier *= movement_speed_scale;
    return result;
}

double player_contact_offset(bool crouch, bool wade) noexcept {
    return crouch && !wade ? crouching_contact_offset : standing_contact_offset;
}

double player_body_height(bool crouch, bool wade) noexcept {
    return player_contact_offset(crouch, wade) + player_radius;
}

bool clip_at(const VxlMap* map, double x, double y, double z) noexcept {
    return clip(map, x, y, z);
}

bool grounded(const VxlMap* map, const PlayerMovementState& state) noexcept {
    const double feet =
        state.position.z + player_contact_offset(state.crouch, state.wade);
    const int sample_z = static_cast<int>(std::floor(feet + grounded_probe_epsilon));
    const int west = static_cast<int>(std::floor(state.position.x - player_radius));
    const int east = static_cast<int>(std::floor(state.position.x + player_radius));
    const int north = static_cast<int>(std::floor(state.position.y - player_radius));
    const int south = static_cast<int>(std::floor(state.position.y + player_radius));
    for (const int bx : {west, east}) {
        for (const int by : {north, south}) {
            if (is_solid(map, bx, by, sample_z)) {
                return true;
            }
        }
    }
    return feet >= map_height;
}

void apply_crouch_request(PlayerMovementState& state, bool crouch, const VxlMap* map) {
    if (crouch == state.crouch) {
        return;
    }
    if (crouch) {
        if (!state.airborne) {
            state.position.z += crouch_shift;
        }
        state.crouch = true;
        return;
    }
    if (!aabb_collides(map, state.position.x, state.position.y,
                       state.position.z - crouch_shift, player_radius,
                       player_contact_offset(false, state.wade))) {
        state.position.z -= crouch_shift;
        state.crouch = false;
    }
}

MovementStepResult step_player(PlayerMovementState& state, const PlayerInputState& input,
                               const VxlMap* map, double dt,
                               const MovementClassConfig& movement_class,
                               std::span<const PlayerCollisionBody> collision_bodies,
                               double world_gravity) {
    const double gravity = std::isfinite(world_gravity) && world_gravity > 0.0 &&
                                   world_gravity <= 8.0
                               ? world_gravity
                               : default_world_gravity;
    MovementStepResult result;
    const bool was_airborne = state.airborne;
    bool jumped_this_frame = false;
    bool ordinary_jump_this_frame = false;
    // The session boundary has already combined WorldUpdate's advertised bit
    // with immediate physical key release. This core now follows world.pyd's
    // compact enum and active predicates literally.
    if (input.jump) {
        if (state.jetpack_active && state.jetpack >= 1U &&
            state.jetpack <= 4U) {
            double thrust{};
            switch (state.jetpack) {
            case 1U: thrust = 0.045; break;
            case 2U: thrust = 0.0125; break;
            case 3U: thrust = 0.020; break;
            case 4U: thrust = 0.025; break;
            default: break;
            }
            state.velocity.z -= ((gravity + 1.0) * thrust) * 0.5;
            state.fall_distance = 0.0;
            // Native jump_this_frame remains a grounded-contact marker even
            // though this is pack thrust rather than the ordinary impulse.
            jumped_this_frame = !was_airborne;
        } else if (!was_airborne) {
            // Retail assigns the impulse first and applies ordinary gravity
            // in the same frame; airborne requests are no-ops.
            state.velocity.z = -0.36 * movement_class.jump_multiplier;
            jumped_this_frame = true;
            ordinary_jump_this_frame = true;
        }
    } else if (input.hover && state.jetpack == 4U) {
        // UGC Builder hover is a distinct Z-key mode. Crouch descends; an
        // uncrouched frame pins vertical velocity before damping.
        if (state.crouch) {
            state.velocity.z -= ((gravity + 1.0) * -0.025) * 0.5;
        } else {
            state.velocity.z = 0.0;
        }
    }
    result.jumped = jumped_this_frame;

    // Class multiplier selection: crouch/sneak and sprint REPLACE the base
    // multiplier (no stacking, no extra base factor).
    double accel{};
    const bool effective_sprint = input.sprint && !state.burdened;
    if ((state.crouch && !input.hover) || input.sneak) {
        accel = movement_class.crouch_sneak_multiplier;
    } else if (effective_sprint) {
        accel = movement_class.sprint_multiplier;
    } else {
        accel = movement_class.accel_multiplier;
    }
    if (state.airborne) {
        accel *= state.jetpack_active && !state.wade &&
                         (state.jetpack == 3U || state.jetpack == 4U)
                     ? 0.1
                     : 0.5;
    }
    accel *= dt;
    if ((input.forward || input.backward) && (input.left || input.right)) {
        accel *= std::sqrt(0.5);
    }

    // Retail steers with the horizontal components of the unit look vector;
    // it does not renormalize them. Their length is cos(pitch), deliberately
    // slowing movement while the player looks toward their feet or straight
    // overhead. Renormalizing here erased that behavior and also diverged from
    // the authoritative server whenever the camera was pitched.
    const double ox = std::isfinite(state.orientation.x) ? state.orientation.x : 0.0;
    const double oy = std::isfinite(state.orientation.y) ? state.orientation.y : 0.0;
    const double sx = -oy;
    const double sy = ox;
    if (input.forward) {
        state.velocity.x += ox * accel;
        state.velocity.y += oy * accel;
    } else if (input.backward) {
        state.velocity.x -= ox * accel;
        state.velocity.y -= oy * accel;
    }
    if (input.left) {
        state.velocity.x -= sx * accel;
        state.velocity.y -= sy * accel;
    } else if (input.right) {
        state.velocity.x += sx * accel;
        state.velocity.y += sy * accel;
    }

    const double divisor = dt + 1.0;
    double gravity_step = dt * gravity;
    if (state.jetpack_passive) {
        gravity_step *= 0.75;
    } else if (state.parachute_active && state.parachute) {
        gravity_step *= 0.05;
    }
    // Hover skips the complete gravity-addition block. The wading crouch lane
    // uses the same recovered 0.025 force as the untouched server mover.
    if (!input.hover) {
        if (state.wade && state.crouch && !jumped_this_frame) {
            state.velocity.z += ((gravity + 1.0) * 0.025) * 0.5;
        } else {
            state.velocity.z += gravity_step;
        }
    }
    state.velocity.z /= divisor;
    if (state.parachute_active) {
        state.fall_distance = 0.0;
    }

    double horizontal_divisor{};
    if (state.airborne && (state.jetpack_active || state.jetpack_passive)) {
        horizontal_divisor = divisor;
    } else if (state.wade) {
        horizontal_divisor = (dt * movement_class.water_friction) + 1.0;
    } else if (!state.airborne) {
        horizontal_divisor = (dt * ground_friction) + 1.0;
    } else {
        horizontal_divisor = (dt * air_friction) + 1.0;
    }
    state.velocity.x /= horizontal_divisor;
    state.velocity.y /= horizontal_divisor;

    collide_with_players(state, collision_bodies, dt);

    const double landing_speed = state.velocity.z;
    const double move_start_z = state.position.z;
    const auto moved = move_box(state.position, state.velocity, dt, map, state.crouch,
                                input.hover, input.sprint,
                                movement_class.can_sprint_uphill,
                                state.airborne, state.wade);
    state.crouch = moved.crouch;
    result.climbed = moved.climbed;
    result.landed = moved.landed;

    // Fall accumulation tracks actual downward displacement after the move;
    // upward motion beyond 0.1 blocks clears it (slope climbs land softly).
    const double fall_delta = state.position.z - move_start_z;
    if (fall_delta > 0.0) {
        state.fall_distance += fall_delta;
    } else if (fall_delta < -0.1) {
        state.fall_distance = 0.0;
    }

    if (moved.climbed) {
        state.climb_timer = 0.1;
    } else {
        state.climb_timer = std::max(0.0, state.climb_timer - dt);
    }

    if (ordinary_jump_this_frame) {
        state.airborne = true;
    } else {
        state.airborne = moved.airborne;
    }
    if (moved.wade_valid) {
        state.wade = moved.wade;
    }

    // Defensive world-bottom clamp; the forced z=239 bed makes it unreachable.
    if (state.position.z > map_height) {
        state.position.z = map_height;
        state.velocity.z = 0.0;
    }

    // Shared post-move landing branch, gated on zero vertical velocity so
    // glide frames that retain the airborne flag still take it.
    if (state.velocity.z == 0.0) {
        check_for_ground_holes(state.position, state.velocity, dt, map, state.crouch,
                               input.hover, input);

        double fall = state.fall_distance * gravity;
        if (state.jetpack_passive && state.jetpack >= 1U && state.jetpack <= 4U) {
            fall *= 0.75;
        }
        const double span = fall_max_distance - fall_min_distance;
        double damage_ratio = span > 0.0 ? (fall - fall_min_distance) / span
                                         : (fall >= fall_max_distance ? 1.0 : 0.0);
        damage_ratio = std::clamp(damage_ratio, 0.0, 1.0);
        const auto damage = static_cast<int>(fall_max_damage * damage_ratio);
        state.fall_distance = 0.0;

        if (landing_speed <= fall_slow_down) {
            result.landing_damage = damage;
        } else {
            result.hard_landing = true;
            if (landing_speed > severe_landing_velocity / gravity) {
                state.velocity.x *= 0.5;
                state.velocity.y *= 0.5;
            }
            result.landing_damage = damage != 0 ? damage : -1;
        }
    }
    return result;
}

} // namespace battlespades::world
