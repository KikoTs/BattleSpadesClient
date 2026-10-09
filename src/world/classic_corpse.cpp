#include "battlespades/world/classic_corpse.hpp"
#include "battlespades/world/retail_character_pose.hpp"
#include "battlespades/world/voxel_raycast.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

// Rigid parts joined by position-based constraints (XPBD, Mueller et al. 2020,
// "Detailed Rigid Body Simulation with Extended Position Based Dynamics").
// Every correction is weighted by mass and inertia, so nothing here invents
// momentum: a joint limit that pushes a forearm one way turns the upper arm
// the other, and a part pressed into the ground is moved out, never launched.

namespace battlespades::world {
namespace {
using corpse_detail::Ball;
using corpse_detail::Quaternion;
using corpse_detail::RigidBody;
using C = ClassicCorpse;

// The small helpers below are declared inline on purpose: the developer
// configuration only expands functions that are, and the solver's inner loops
// are made of them.
constexpr double step_seconds = 1.0 / 120.0;
constexpr int substeps = 6;
constexpr double radians = std::numbers::pi / 180.0;
constexpr double gravity_blocks = 32.0;
constexpr double static_friction = .9, kinetic_friction = .65;
// Past these a value is a solver fault, not motion.
constexpr double maximum_speed = 36.0, maximum_spin = 18.0;
// The most one substep's corrections may add to a part's speed and spin.
constexpr double correction_speed = 8.0, correction_spin = 9.0;
// Parts pushed out of one another part at no more than this, and lose this
// share of their slide along one another each substep.
constexpr double parting_speed = 1.0, self_grip = .35;
// The momentum of one blow of unit speed, as kilograms moved at that speed,
// and how much faster than the blow the struck point may be driven.
constexpr double blow_kilograms = 12.0, blow_follow = 2.0;
// The links, in the order they are built: parents before their children.
enum : std::size_t {
    neck_link,
    shoulder_left_link,
    shoulder_right_link,
    elbow_left_link,
    elbow_right_link,
    hip_left_link,
    hip_right_link,
    knee_left_link,
    knee_right_link
};
// Muscle. Stiffness is torque per radian, in kg blocks^2 / s^2: the legs' is
// what it takes to carry the body on a bent knee, the rest what holds a limb
// against its own weight. `limp_*` is what is left when the strength is gone:
// enough that a limb lies loosely bent instead of folding flat on itself, far
// too little to hold anything up.
constexpr double leg_strength = 1500, trailing_strength = 900, neck_strength = 180,
                 shoulder_strength = 70, elbow_strength = 45, upright_strength = 1700;
// While it staggers its legs still carry it: a share of its weight that keeps
// the hips at a height, lower and lower, until there is none. The legs of the
// model step under it; they are not asked to balance a body, which no limp
// rig can.
constexpr double carry_base = .9, carry_sink = .5, carry_spring = 6, carry_damping = .6,
                 carry_most = 1.6;
// How hard the hips go after a leaning trunk: a share of the body's weight for
// each block the neck is out over them, and the most it can be.
constexpr double catch_gain = 1.4, catch_most = .45;
constexpr double limp_hip = 60, limp_knee = 70, limp_neck = 12, limp_shoulder = 14,
                 limp_elbow = 14;
// The slack goes once the body is down and slowing, or this long after the
// stagger whatever it is doing; and it takes this long to go.
constexpr double limp_seconds = 1.6, limp_fade = .45;
// A body left kneeling, hips held up on its own folded legs, keels over: a
// sideways pull on the hips that grows to this share of its weight, eases off
// whenever the hips are already going over at keel_speed, stops once the
// trunk leans keel_lean (the sine of the angle), and is given up after
// keel_budget steps.
constexpr double keel_share = .9, keel_clearance = .52, keel_sitting = .7, keel_lean = .55,
                 keel_speed = 1.0;
constexpr unsigned keel_ramp = 70, keel_budget = 420;
// A body that has gone nowhere for this long is asleep, whatever its parts
// still trade among themselves: a creep of a twentieth of a block a second, a
// limb rolling a few degrees a second where it is pinned.
constexpr unsigned still_limit = 150;
constexpr double still_distance = .06, still_turn = .2;

inline Vec3 add(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline Vec3 sub(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vec3 mul(Vec3 a, double s) {
    return {a.x * s, a.y * s, a.z * s};
}
inline double dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double length(Vec3 a) {
    return std::sqrt(dot(a, a));
}
inline Vec3 unit(Vec3 a, Vec3 fallback = {0, 0, 1}) {
    const auto n = length(a);
    return n > 1e-8 ? mul(a, 1 / n) : fallback;
}
inline bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
/** `a` without its component along the unit vector `n`. */
inline Vec3 reject(Vec3 a, Vec3 n) {
    return sub(a, mul(n, dot(a, n)));
}

inline Quaternion multiply(Quaternion a, Quaternion b) {
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}
inline Quaternion conjugate(Quaternion q) {
    return {q.w, -q.x, -q.y, -q.z};
}
inline Quaternion normalized(Quaternion q) {
    const double n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (!(n > 1e-12))
        return {};
    return {q.w / n, q.x / n, q.y / n, q.z / n};
}
inline Vec3 rotate(Quaternion q, Vec3 v) {
    const Vec3 u{q.x, q.y, q.z};
    const auto t = mul(cross(u, v), 2);
    return add(add(v, mul(t, q.w)), cross(u, t));
}
inline Vec3 unrotate(Quaternion q, Vec3 v) {
    return rotate(conjugate(q), v);
}
/** A small world-space rotation vector applied to `q`. */
inline Quaternion turned(Quaternion q, Vec3 rotation) {
    const auto d = multiply({0, rotation.x, rotation.y, rotation.z}, q);
    return normalized({q.w + .5 * d.w, q.x + .5 * d.x, q.y + .5 * d.y, q.z + .5 * d.z});
}
/** The rotation whose columns are the images of the local x, y and z axes. */
Quaternion from_axes(Vec3 ex, Vec3 ey, Vec3 ez) {
    const double m00 = ex.x, m01 = ey.x, m02 = ez.x;
    const double m10 = ex.y, m11 = ey.y, m12 = ez.y;
    const double m20 = ex.z, m21 = ey.z, m22 = ez.z;
    const double trace = m00 + m11 + m22;
    Quaternion q;
    if (trace > 0) {
        const double s = std::sqrt(trace + 1) * 2;
        q = {.25 * s, (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s};
    } else if (m00 > m11 && m00 > m22) {
        const double s = std::sqrt(1 + m00 - m11 - m22) * 2;
        q = {(m21 - m12) / s, .25 * s, (m01 + m10) / s, (m02 + m20) / s};
    } else if (m11 > m22) {
        const double s = std::sqrt(1 + m11 - m00 - m22) * 2;
        q = {(m02 - m20) / s, (m01 + m10) / s, .25 * s, (m12 + m21) / s};
    } else {
        const double s = std::sqrt(1 + m22 - m00 - m11) * 2;
        q = {(m10 - m01) / s, (m02 + m20) / s, (m12 + m21) / s, .25 * s};
    }
    return normalized(q);
}
/** `v` carried by the shortest rotation that takes the unit vector `from` to `to`. */
Vec3 carried(Vec3 v, Vec3 from, Vec3 to, Vec3 fallback_axis) {
    const double c = dot(from, to);
    if (c < -.9999) // opposite: half a turn about any perpendicular
        return sub(mul(fallback_axis, 2 * dot(fallback_axis, v)), v);
    const auto k = cross(from, to);
    return add(add(v, cross(k, v)), mul(cross(k, cross(k, v)), 1 / (1 + c)));
}

inline Vec3 world_inverse_inertia(const RigidBody& body, Vec3 v) {
    const auto local = unrotate(body.orientation, v);
    return rotate(body.orientation,
                  {local.x * body.inverse_inertia.x,
                   local.y * body.inverse_inertia.y,
                   local.z * body.inverse_inertia.z});
}
/** How readily a point at `offset` from the centre moves along the unit `direction`. */
inline double compliance_at(const RigidBody& body, Vec3 offset, Vec3 direction) {
    const auto lever = cross(offset, direction);
    return body.inverse_mass + dot(lever, world_inverse_inertia(body, lever));
}
/** A positional impulse at `offset`. */
inline void push(RigidBody& body, Vec3 offset, Vec3 impulse) {
    body.position = add(body.position, mul(impulse, body.inverse_mass));
    body.orientation =
        turned(body.orientation, world_inverse_inertia(body, cross(offset, impulse)));
}
/** A velocity impulse at `offset`. */
inline void kick(RigidBody& body, Vec3 offset, Vec3 impulse) {
    body.velocity = add(body.velocity, mul(impulse, body.inverse_mass));
    body.angular_velocity =
        add(body.angular_velocity, world_inverse_inertia(body, cross(offset, impulse)));
}
inline Vec3 velocity_at(const RigidBody& body, Vec3 offset) {
    return add(body.velocity, cross(body.angular_velocity, offset));
}
void set_box_inertia(RigidBody& body, double kilograms, Vec3 size) {
    body.inverse_mass = 1 / kilograms;
    const double k = kilograms / 12;
    body.inverse_inertia = {1 / (k * (size.y * size.y + size.z * size.z)),
                            1 / (k * (size.x * size.x + size.z * size.z)),
                            1 / (k * (size.x * size.x + size.y * size.y))};
}
/** Turn `child` by `rotation` relative to `parent`, shared by their inertia. */
void turn_apart(RigidBody& parent, RigidBody& child, Vec3 rotation, double softness = 0) {
    double angle = length(rotation);
    if (angle < 1e-9)
        return;
    const auto axis = mul(rotation, 1 / angle);
    // The update below is first order; larger errors close over a few substeps.
    angle = std::min(angle, .35);
    const auto of_parent = world_inverse_inertia(parent, axis);
    const auto of_child = world_inverse_inertia(child, axis);
    const double share = angle / (dot(axis, of_parent) + dot(axis, of_child) + softness);
    child.orientation = turned(child.orientation, mul(of_child, share));
    parent.orientation = turned(parent.orientation, mul(of_parent, -share));
}

void begin_substep(RigidBody& body, double gravity, double h) {
    body.velocity.z += gravity_blocks * gravity * h;
    // Air and tissue: without it a limb would swing for ever.
    body.velocity = mul(body.velocity, 1 - .15 * h);
    body.angular_velocity = mul(body.angular_velocity, 1 - 1.2 * h);
    body.position_before = body.position;
    body.orientation_before = body.orientation;
    body.position = add(body.position, mul(body.velocity, h));
    body.orientation = turned(body.orientation, mul(body.angular_velocity, h));
}
/** The velocity the substep's corrections leave the part with. */
void end_substep(RigidBody& body, double h) {
    // Still the speed it was carried at: the corrections change it by the
    // difference, and a large correction (a block built into a body, a pose
    // no joint allows) is a change of place, not that much speed.
    const auto bounded = [](Vec3 before, Vec3 after, double limit) {
        const auto change = sub(after, before);
        const double size = length(change);
        return size > limit ? add(before, mul(change, limit / size)) : after;
    };
    body.velocity = bounded(body.velocity,
                            mul(sub(body.position, body.position_before), 1 / h),
                            correction_speed);
    const auto turn = multiply(body.orientation, conjugate(body.orientation_before));
    const double sign = turn.w < 0 ? -2 / h : 2 / h;
    body.angular_velocity = bounded(body.angular_velocity,
                                    {turn.x * sign, turn.y * sign, turn.z * sign},
                                    correction_spin);
}
void limit_rates(RigidBody& body) {
    if (const double speed = length(body.velocity); speed > maximum_speed)
        body.velocity = mul(body.velocity, maximum_speed / speed);
    if (const double spin = length(body.angular_velocity); spin > maximum_spin)
        body.angular_velocity = mul(body.angular_velocity, maximum_spin / spin);
}

struct Penetration {
    Vec3 normal;
    double depth;
};

inline bool solid_at(const VxlMap& map, int x, int y, int z) {
    if (x < 0 || y < 0 || x >= static_cast<int>(VxlMap::width) ||
        y >= static_cast<int>(VxlMap::depth))
        return true;
    if (z < 0)
        return false;
    if (z >= static_cast<int>(VxlMap::height) - 1)
        return true;
    return map.solid(static_cast<std::uint32_t>(x),
                     static_cast<std::uint32_t>(y),
                     static_cast<std::uint32_t>(z));
}

/** How far the first block under `point` is, looking two blocks down. */
double clearance_below(const VxlMap& map, Vec3 point) {
    const int x = static_cast<int>(std::floor(point.x)), y = static_cast<int>(std::floor(point.y));
    const int top = static_cast<int>(std::floor(point.z));
    for (int z = top; z <= top + 2; ++z)
        if (solid_at(map, x, y, z))
            return std::max(0.0, static_cast<double>(z) - point.z);
    return 9;
}

/** The deepest overlap of a ball with the terrain, and the way out of it. */
std::optional<Penetration> terrain_contact(const VxlMap& map, Vec3 center, double radius) {
    std::optional<Penetration> deepest;
    const auto offer = [&](Vec3 normal, double depth) {
        if (depth > 0 && (!deepest || depth > deepest->depth))
            deepest = Penetration{normal, depth};
    };
    for (int x = static_cast<int>(std::floor(center.x - radius));
         x <= static_cast<int>(std::floor(center.x + radius));
         ++x)
        for (int y = static_cast<int>(std::floor(center.y - radius));
             y <= static_cast<int>(std::floor(center.y + radius));
             ++y)
            for (int z = static_cast<int>(std::floor(center.z - radius));
                 z <= static_cast<int>(std::floor(center.z + radius));
                 ++z) {
                if (!solid_at(map, x, y, z))
                    continue;
                const Vec3 closest{std::clamp(center.x, static_cast<double>(x), x + 1.0),
                                   std::clamp(center.y, static_cast<double>(y), y + 1.0),
                                   std::clamp(center.z, static_cast<double>(z), z + 1.0)};
                const auto delta = sub(center, closest);
                const double distance = length(delta);
                if (distance >= radius)
                    continue;
                if (distance > 1e-9) {
                    offer(mul(delta, 1 / distance), radius - distance);
                    continue;
                }
                // The centre is inside this block: leave by the nearest face
                // that opens onto air, never deeper into the ground.
                struct Face {
                    Vec3 normal;
                    double exit;
                    int x, y, z;
                };
                const std::array<Face, 6> faces{{{{0, 0, -1}, center.z - z, x, y, z - 1},
                                                 {{-1, 0, 0}, center.x - x, x - 1, y, z},
                                                 {{1, 0, 0}, x + 1 - center.x, x + 1, y, z},
                                                 {{0, -1, 0}, center.y - y, x, y - 1, z},
                                                 {{0, 1, 0}, y + 1 - center.y, x, y + 1, z},
                                                 {{0, 0, 1}, z + 1 - center.z, x, y, z + 1}}};
                const Face* best{};
                for (const auto& face : faces)
                    if (!solid_at(map, face.x, face.y, face.z) && (!best || face.exit < best->exit))
                        best = &face;
                if (best)
                    offer(best->normal, best->exit + radius);
                else
                    offer({0, 0, -1}, center.z - z + 1 + radius); // buried: up
            }
    return deepest;
}

struct TerrainContact {
    RigidBody* body;
    Vec3 offset, normal;
    double pushed;
};

/** Move one ball out of the terrain, holding it by static friction if that can. */
std::optional<TerrainContact>
resolve_ball(RigidBody& body, const Ball& ball, const VxlMap& map) {
    const auto arm = rotate(body.orientation, ball.local);
    const auto hit = terrain_contact(map, add(body.position, arm), ball.radius);
    if (!hit)
        return std::nullopt;
    const auto offset = sub(arm, mul(hit->normal, ball.radius));
    const double pushed = hit->depth / compliance_at(body, offset, hit->normal);
    push(body, offset, mul(hit->normal, pushed));
    // Where that point of the body was at the start of the substep.
    const auto before = add(body.position_before,
                            sub(rotate(body.orientation_before, ball.local),
                                mul(hit->normal, ball.radius)));
    const auto now = add(body.position,
                         sub(rotate(body.orientation, ball.local), mul(hit->normal, ball.radius)));
    const auto slip = reject(sub(now, before), hit->normal);
    if (const double slid = length(slip); slid > 1e-12) {
        const auto along = mul(slip, 1 / slid);
        const double needed = slid / compliance_at(body, offset, along);
        if (needed <= static_friction * pushed)
            push(body, offset, mul(along, -needed));
    }
    return TerrainContact{&body, offset, hit->normal, pushed};
}

/** Sliding friction, and no bounce: the ground returns none of the approach. */
void finish_contact(const TerrainContact& contact, double h) {
    auto& body = *contact.body;
    const auto relative = velocity_at(body, contact.offset);
    const auto sliding = reject(relative, contact.normal);
    if (const double speed = length(sliding); speed > 1e-9) {
        const auto along = mul(sliding, 1 / speed);
        const double stop = speed / compliance_at(body, contact.offset, along);
        kick(body, contact.offset,
             mul(along, -std::min(stop, kinetic_friction * contact.pushed / h)));
    }
    const double approach = dot(velocity_at(body, contact.offset), contact.normal);
    kick(body, contact.offset,
         mul(contact.normal, -approach / compliance_at(body, contact.offset, contact.normal)));
}

ClassicCorpse::Matrix matrix_of(Vec3 ex, Vec3 ey, Vec3 ez, Vec3 origin) {
    return {static_cast<float>(ex.x),     static_cast<float>(ex.y),
            static_cast<float>(ex.z),     0,
            static_cast<float>(ey.x),     static_cast<float>(ey.y),
            static_cast<float>(ey.z),     0,
            static_cast<float>(ez.x),     static_cast<float>(ez.y),
            static_cast<float>(ez.z),     0,
            static_cast<float>(origin.x), static_cast<float>(origin.y),
            static_cast<float>(origin.z), 1};
}
/** Rest space to world for a body: how a mesh authored at rest is drawn. */
ClassicCorpse::Matrix rest_to_world(const RigidBody& body) {
    const auto image = [&](Vec3 axis) {
        return rotate(body.orientation, unrotate(body.rest_orientation, axis));
    };
    return matrix_of(image({1, 0, 0}),
                     image({0, 1, 0}),
                     image({0, 0, 1}),
                     sub(body.position, image(body.rest_position)));
}

// --- The two skeletons -----------------------------------------------------

struct RigBall {
    C::Part part;
    Vec3 center;
    double radius;
};
// The standing character every class shares, in the space its part meshes are
// authored in: x to the left hand, y forward, z down from the eye. Retail puts
// the head pivot 0.3 under the eye and the hips at 1.1; the leg models are cut
// at their middle, 1.675, for the knee.
constexpr std::array<Vec3, C::joint_count> rest_joints{{{0, 0, 1.1},     // pelvis
                                                        {0, 0, .3},      // neck
                                                        {0, 0, -.05},    // head
                                                        {.45, 0, .4},    // shoulders
                                                        {-.45, 0, .4},
                                                        {.58, .1, .88},  // elbows
                                                        {-.58, .1, .88},
                                                        {.6, .16, 1.36}, // hands
                                                        {-.6, .16, 1.36},
                                                        {.25, 0, 2.2},   // feet: the ankles
                                                        {-.25, 0, 2.2},
                                                        {.25, 0, 1.1},   // hips
                                                        {-.25, 0, 1.1},
                                                        {.25, 0, 1.675}, // knees
                                                        {-.25, 0, 1.675}}};
// Measured from the Classic soldier's meshes: trunk 0.9 wide, 0.6 deep and 1.0
// tall, head a 0.7 block, legs 0.3 by 0.4 with deeper boots. Each ball is as
// thick as the art it stands for, so a part comes to rest on the ground where
// its voxels do, on its back, its front or its side.
constexpr std::array rest_balls{
    RigBall{C::torso, {.175, -.1, .525}, .3},
    RigBall{C::torso, {-.125, -.1, .525}, .3},
    RigBall{C::torso, {.175, -.1, .925}, .3},
    RigBall{C::torso, {-.125, -.1, .925}, .3},
    RigBall{C::torso, {0, -.05, .36}, .17}, // the neck
    // The head is a block: eight balls let it rest on a face, not roll.
    RigBall{C::skull, {.155, .105, -.13}, .17},
    RigBall{C::skull, {-.205, .105, -.13}, .17},
    RigBall{C::skull, {.155, -.255, -.13}, .17},
    RigBall{C::skull, {-.205, -.255, -.13}, .17},
    RigBall{C::skull, {.155, .105, .13}, .17},
    RigBall{C::skull, {-.205, .105, .13}, .17},
    RigBall{C::skull, {.155, -.255, .13}, .17},
    RigBall{C::skull, {-.205, -.255, .13}, .17},
    // Shoulder, middle and elbow: no joint is left bare to pass through a wall.
    RigBall{C::upper_arm_left, {.47, .02, .47}, .12},
    RigBall{C::upper_arm_left, {.515, .05, .64}, .12},
    RigBall{C::upper_arm_left, {.575, .095, .86}, .12},
    RigBall{C::upper_arm_right, {-.47, .02, .47}, .12},
    RigBall{C::upper_arm_right, {-.515, .05, .64}, .12},
    RigBall{C::upper_arm_right, {-.575, .095, .86}, .12},
    RigBall{C::forearm_left, {.585, .115, 1.0}, .12},
    RigBall{C::forearm_left, {.595, .145, 1.2}, .12},
    RigBall{C::forearm_left, {.6, .16, 1.34}, .12},
    RigBall{C::forearm_right, {-.585, .115, 1.0}, .12},
    RigBall{C::forearm_right, {-.595, .145, 1.2}, .12},
    RigBall{C::forearm_right, {-.6, .16, 1.34}, .12},
    // Hip to knee: the knee is solid too, or the thigh levers the bare joint
    // into the ground and the shin see-saws on its calf, foot in the air.
    RigBall{C::thigh_left, {.225, -.075, 1.27}, .18},
    RigBall{C::thigh_left, {.225, -.075, 1.5}, .18},
    RigBall{C::thigh_left, {.225, -.075, 1.675}, .18},
    RigBall{C::thigh_right, {-.275, -.075, 1.27}, .18},
    RigBall{C::thigh_right, {-.275, -.075, 1.5}, .18},
    RigBall{C::thigh_right, {-.275, -.075, 1.675}, .18},
    RigBall{C::shin_left, {.225, -.075, 1.83}, .2},
    RigBall{C::shin_left, {.225, -.07, 2.11}, .16}, // heel
    RigBall{C::shin_left, {.225, .07, 2.11}, .16},  // toe
    RigBall{C::shin_right, {-.275, -.075, 1.83}, .2},
    RigBall{C::shin_right, {-.275, -.07, 2.11}, .16},
    RigBall{C::shin_right, {-.275, .07, 2.11}, .16},
};

// A 74 kg soldier, shared out as bodies are: half of it is the trunk.
constexpr std::array<double, C::part_count> kilograms{34, 6, 2.2, 2.2, 1.9, 1.9, 8, 8, 5, 5};

struct Bone {
    C::Part part;
    C::Joint from, to;
    double radius;
};
// Where a bullet can strike each part.
constexpr std::array<Bone, C::part_count> bones{{{C::torso, C::neck, C::pelvis, .36},
                                                 {C::skull, C::neck, C::head, .32},
                                                 {C::upper_arm_left, C::shoulder_left, C::elbow_left, .15},
                                                 {C::upper_arm_right, C::shoulder_right, C::elbow_right, .15},
                                                 {C::forearm_left, C::elbow_left, C::hand_left, .14},
                                                 {C::forearm_right, C::elbow_right, C::hand_right, .14},
                                                 {C::thigh_left, C::hip_left, C::knee_left, .19},
                                                 {C::thigh_right, C::hip_right, C::knee_right, .19},
                                                 {C::shin_left, C::knee_left, C::foot_left, .17},
                                                 {C::shin_right, C::knee_right, C::foot_right, .17}}};
// The part that carries each skeleton joint.
constexpr std::array<C::Part, C::joint_count> joint_owner{
    C::torso,          C::torso,           C::skull,        C::torso,         C::torso,
    C::upper_arm_left, C::upper_arm_right, C::forearm_left, C::forearm_right, C::shin_left,
    C::shin_right,     C::torso,           C::torso,        C::thigh_left,    C::thigh_right};

/** Parts that may not pass through one another. Jointed neighbours may overlap. */
bool blocks(std::uint8_t a, std::uint8_t b) {
    const auto arm = [](std::uint8_t p) { return p >= C::upper_arm_left && p <= C::forearm_right; };
    const auto leg_left = [](std::uint8_t p) { return p == C::thigh_left || p == C::shin_left; };
    const auto leg_right = [](std::uint8_t p) { return p == C::thigh_right || p == C::shin_right; };
    if (a > b)
        std::swap(a, b);
    // An upper arm and a thigh are jointed to the trunk and lie against it,
    // and the hip and knee stops bring a heel to the seat and no further.
    if (a == C::torso)
        return b == C::forearm_left || b == C::forearm_right;
    if (a == C::skull)
        return b == C::forearm_left || b == C::forearm_right;
    if (arm(a) && arm(b)) // the two arms, not an arm and its own forearm
        return ((a - C::upper_arm_left) % 2) != ((b - C::upper_arm_left) % 2);
    return (leg_left(a) && leg_right(b)) || (leg_right(a) && leg_left(b));
}

std::optional<double> ray_capsule(Vec3 origin, Vec3 ray, Vec3 a, Vec3 b, double radius) {
    const auto axis = sub(b, a), offset = sub(origin, a);
    const double axis2 = dot(axis, axis), along = dot(axis, ray), start = dot(axis, offset);
    const auto closest = add(a, mul(axis, std::clamp(start / std::max(axis2, 1e-12), 0.0, 1.0)));
    if (length(sub(origin, closest)) <= radius)
        return 0;
    std::optional<double> nearest;
    const auto accept = [&](double t) {
        if (t >= 0 && (!nearest || t < *nearest))
            nearest = t;
    };
    // Infinite cylinder, restricted to the segment, plus its spherical caps.
    const double qa = axis2 - along * along;
    const double qb = axis2 * dot(offset, ray) - start * along;
    const double qc = axis2 * (dot(offset, offset) - radius * radius) - start * start;
    const double discriminant = qb * qb - qa * qc;
    if (qa > 1e-10 && discriminant >= 0) {
        const double t = (-qb - std::sqrt(discriminant)) / qa;
        const double y = start + t * along;
        if (y >= 0 && y <= axis2)
            accept(t);
    }
    for (const auto center : {a, b}) {
        const auto delta = sub(origin, center);
        const double projection = dot(delta, ray);
        const double d = projection * projection - dot(delta, delta) + radius * radius;
        if (d >= 0)
            accept(-projection - std::sqrt(d));
    }
    return nearest;
}
} // namespace

double ClassicCorpse::part_mass(Part part) noexcept {
    return part < part_count ? kilograms[part] : 0;
}

ClassicCorpse::ClassicCorpse(Vec3 position,
                             double yaw_degrees,
                             Vec3 velocity,
                             std::uint32_t seed,
                             bool ragdoll,
                             bool crouched,
                             std::uint64_t animation_timer_ms)
    : ragdoll_(ragdoll) {
    if (!finite(position))
        position = {256, 256, 230};
    if (!finite(velocity))
        velocity = {};
    if (!std::isfinite(yaw_degrees))
        yaw_degrees = 0;
    const double yaw = yaw_degrees * radians;
    right_ = {std::cos(yaw), std::sin(yaw), 0};
    const Vec3 forward{-right_.y, right_.x, 0};
    if (const double speed = length(velocity); speed > 12)
        velocity = mul(velocity, 12 / speed);
    if (!ragdoll_) {
        // The retail corpse: the authored model set down where the player
        // died. One falling point carries it.
        auto& body = bodies_[torso];
        body.position = add(position, {0, 0, 1.7});
        body.velocity = velocity;
        body.inverse_mass = 1 / 74.0;
        return;
    }

    joint_rest_ = rest_joints;
    const auto& j = joint_rest_;

    // Each part's own frame in rest space: z along its bone, x the axis its
    // far joint hinges on. The trunk and head share the body's frame: x to
    // the left shoulder, y to the front, z down the spine.
    struct Frame {
        Vec3 origin, x, y, z;
    };
    std::array<Frame, part_count> frames{};
    const auto down = unit(sub(j[pelvis], j[neck]));
    const auto left = unit(reject(sub(j[shoulder_left], j[shoulder_right]), down), {1, 0, 0});
    const auto front = cross(down, left);
    frames[torso] = {mul(add(j[neck], j[pelvis]), .5), left, front, down};
    frames[skull] = {j[head], left, front, down};
    const auto limb = [&](Part upper, Part lower, Joint a, Joint b, Joint c, Vec3 bend) {
        const auto first = unit(sub(j[b], j[a])), second = unit(sub(j[c], j[b]));
        // A limb drawn bent shows its own hinge; a straight one takes `bend`.
        auto hinge = cross(first, second);
        if (length(hinge) < .2 || dot(reject(second, first), bend) < 0)
            hinge = cross(first, bend);
        const auto upper_x = unit(reject(hinge, first), left);
        const auto lower_x = unit(reject(hinge, second), left);
        frames[upper] = {mul(add(j[a], j[b]), .5), upper_x, cross(first, upper_x), first};
        frames[lower] = {mul(add(j[b], j[c]), .5), lower_x, cross(second, lower_x), second};
    };
    // Arms fold toward the head in the model's plane (the death soldier's
    // raised arm shows it); a straight arm mirrors that. Knees fold back.
    const auto arm_bend = [&](Joint shoulder, Joint elbow, Joint hand) {
        const auto first = unit(sub(j[elbow], j[shoulder]));
        const auto drawn = reject(unit(sub(j[hand], j[elbow])), first);
        return length(drawn) > .2 ? unit(drawn) : front;
    };
    auto bend_left = arm_bend(shoulder_left, elbow_left, hand_left);
    auto bend_right = arm_bend(shoulder_right, elbow_right, hand_right);
    const auto mirrored = [&](Vec3 v) { return sub(v, mul(left, 2 * dot(v, left))); };
    const auto drawn_bent = [&](Joint shoulder, Joint elbow, Joint hand) {
        return length(reject(unit(sub(j[hand], j[elbow])), unit(sub(j[elbow], j[shoulder])))) > .2;
    };
    if (!drawn_bent(shoulder_right, elbow_right, hand_right) &&
        drawn_bent(shoulder_left, elbow_left, hand_left))
        bend_right = mirrored(bend_left);
    else if (!drawn_bent(shoulder_left, elbow_left, hand_left) &&
             drawn_bent(shoulder_right, elbow_right, hand_right))
        bend_left = mirrored(bend_right);
    limb(upper_arm_left, forearm_left, shoulder_left, elbow_left, hand_left, bend_left);
    limb(upper_arm_right, forearm_right, shoulder_right, elbow_right, hand_right, bend_right);
    limb(thigh_left, shin_left, hip_left, knee_left, foot_left, mul(front, -1));
    limb(thigh_right, shin_right, hip_right, knee_right, foot_right, mul(front, -1));

    const auto bone_length = [&](Joint a, Joint b) { return length(sub(j[b], j[a])); };
    const std::array<Vec3, part_count> sizes{
        {{.8, .5, .85},
         {.65, .65, .6},
         {.25, .25, bone_length(shoulder_left, elbow_left)},
         {.25, .25, bone_length(shoulder_right, elbow_right)},
         {.25, .25, bone_length(elbow_left, hand_left)},
         {.25, .25, bone_length(elbow_right, hand_right)},
         {.3, .33, bone_length(hip_left, knee_left)},
         {.3, .33, bone_length(hip_right, knee_right)},
         {.3, .33, bone_length(knee_left, foot_left)},
         {.3, .33, bone_length(knee_right, foot_right)}}};

    // Rest space to the world: the spine upright under the eye, turned to the
    // player's yaw.
    const auto stand = [&](Vec3 direction) {
        const Vec3 upright{dot(direction, left), dot(direction, front), dot(direction, down)};
        return add(add(mul(right_, upright.x), mul(forward, upright.y)), {0, 0, upright.z});
    };
    const auto place = [&](Vec3 rest_point) {
        return add(add(position, {0, 0, .3}), stand(sub(rest_point, j[neck])));
    };
    for (std::size_t part = 0; part < part_count; ++part) {
        auto& body = bodies_[part];
        const auto& frame = frames[part];
        set_box_inertia(body, kilograms[part], sizes[part]);
        body.rest_position = frame.origin;
        body.rest_orientation = from_axes(frame.x, frame.y, frame.z);
        body.position = place(frame.origin);
        body.orientation = from_axes(stand(frame.x), stand(frame.y), stand(frame.z));
    }
    origin_rest_ = {};

    const auto local = [&](Part part, Vec3 rest_point) {
        return unrotate(bodies_[part].rest_orientation,
                        sub(rest_point, bodies_[part].rest_position));
    };
    for (const auto& ball : rest_balls)
        balls_[ball_count_++] = {static_cast<std::uint8_t>(ball.part),
                                 local(ball.part, ball.center),
                                 ball.radius};

    const auto ball_joint = [&](Part parent, Part child, Joint anchor, Vec3 axis, Vec3 side,
                                Vec3 neutral_hinge, Vec3 child_bone, double pitch_low,
                                double pitch_high, double side_low, double side_high, double twist) {
        Link link;
        link.parent = static_cast<std::uint8_t>(parent);
        link.child = static_cast<std::uint8_t>(child);
        link.parent_anchor = local(parent, j[anchor]);
        link.child_anchor = local(child, j[anchor]);
        link.axis = axis;
        link.front = {0, 1, 0};
        link.side = side;
        link.neutral_hinge = neutral_hinge;
        link.child_bone = child_bone;
        link.pitch_minimum = pitch_low * radians;
        link.pitch_maximum = pitch_high * radians;
        link.side_minimum = side_low * radians;
        link.side_maximum = side_high * radians;
        link.twist = twist * radians;
        return link;
    };
    const auto hinge_joint = [&](Part parent, Part child, Joint anchor, double low, double high) {
        Link link;
        link.parent = static_cast<std::uint8_t>(parent);
        link.child = static_cast<std::uint8_t>(child);
        link.parent_anchor = local(parent, j[anchor]);
        link.child_anchor = local(child, j[anchor]);
        link.hinge = true;
        link.flex_minimum = low * radians;
        link.flex_maximum = high * radians;
        return link;
    };
    // The head's bone runs from the neck to its centre, in the head's frame.
    const auto head_bone = unrotate(bodies_[skull].rest_orientation, unit(sub(j[head], j[neck])));
    // An arm swings out from the side no further than 75 degrees: at a stop
    // past the vertical, the upper arm of a body lying on its side would lean
    // on it and stay pointing at the sky.
    // Parents come before their children: place_chain() relies on it.
    links_ = {ball_joint(torso, skull, neck, {0, 0, -1}, {1, 0, 0}, {1, 0, 0}, head_bone,
                         -35, 50, -30, 30, 50),
              ball_joint(torso, upper_arm_left, shoulder_left, {0, 0, 1}, {1, 0, 0}, {-1, 0, 0},
                         {0, 0, 1}, -50, 150, -20, 75, 70),
              ball_joint(torso, upper_arm_right, shoulder_right, {0, 0, 1}, {-1, 0, 0}, {-1, 0, 0},
                         {0, 0, 1}, -50, 150, -20, 75, 70),
              hinge_joint(upper_arm_left, forearm_left, elbow_left, 3, 125),
              hinge_joint(upper_arm_right, forearm_right, elbow_right, 3, 125),
              ball_joint(torso, thigh_left, hip_left, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}, {0, 0, 1},
                         -20, 130, -12, 45, 35),
              ball_joint(torso, thigh_right, hip_right, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, 0, 1},
                         -20, 130, -12, 45, 35),
              hinge_joint(thigh_left, shin_left, knee_left, 0, 150),
              hinge_joint(thigh_right, shin_right, knee_right, 0, 150)};

    // The pose at the moment of death. The arms hang as the model draws them
    // (set_arm_pose replaces that with the live weapon pose); the legs keep
    // their stride or their crouch.
    const Vec3 world_down{0, 0, 1};
    const auto leaning = [&](double angle) {
        return add(mul(world_down, std::cos(angle)), mul(forward, std::sin(angle)));
    };
    if (crouched) {
        // A crouch is the trunk upright over folded legs, as the game draws
        // it, under an eye only 1.35 blocks up: thighs to the chest and
        // calves to the thighs, both at their stops, is the one pose that
        // fits. Its weight is behind its feet: let go, it sits back and lies
        // down, as a limp squat does, where one leaning over its knees would
        // kneel on them and stay up.
        const double lean = 1 * radians;
        const auto spine = leaning(-lean), chest = leaning(90 * radians - lean);
        auto& trunk = bodies_[torso];
        trunk.orientation = from_axes(right_, chest, spine);
        trunk.position = sub(add(position, {0, 0, .3}),
                             rotate(trunk.orientation, local(torso, j[neck])));
        for (const auto arm : {upper_arm_left, upper_arm_right, forearm_left, forearm_right}) {
            const auto& frame = frames[arm];
            const auto turn = [&](Vec3 axis) {
                return add(add(mul(right_, dot(axis, left)), mul(chest, dot(axis, front))),
                           mul(spine, dot(axis, down)));
            };
            bodies_[arm].orientation = from_axes(turn(frame.x), turn(frame.y), turn(frame.z));
        }
    }
    const auto walking = evaluate_retail_walk_pose(
        animation_timer_ms, {velocity.x / 32, velocity.y / 32, velocity.z / 32},
        {forward.x, forward.y, forward.z}, crouched);
    // Nobody squats square: one knee is a little lower than the other, so
    // the body goes over to one side as it sits back.
    const bool left_lower = ((seed >> 3U) & 1U) != 0;
    // The crouched eye is 1.35 above the soles.
    const double hip_above_ground =
        position.z + 1.35 - joint_rest_to_world(torso, j[hip_left]).z;
    for (const bool is_left : {true, false}) {
        const double stride =
            std::clamp((is_left ? walking.left : walking.right).rotation_x_degrees, -18.0, 32.0) *
            radians;
        // Standing, a hair of bend so the knees give way forward, as they do.
        double hip = stride + 4 * radians, knee = 9 * radians;
        if (crouched) {
            // Knees up in front, heels under the seat, and each knee bent
            // as far as leaves its ankle just above the ground.
            hip = (is_left == left_lower ? 125.5 : 128.5) * radians;
            const double thigh = length(sub(j[knee_left], j[hip_left]));
            const double shin = length(sub(j[foot_left], j[knee_left]));
            const double reach = (hip_above_ground - thigh * std::cos(hip) - .13) / shin;
            knee = std::min(hip + std::acos(std::clamp(reach, -1.0, 1.0)), 149.5 * radians);
        }
        pose_limb(is_left ? thigh_left : thigh_right, is_left ? shin_left : shin_right,
                  leaning(hip), leaning(hip - knee), mul(forward, -1));
    }
    place_chain();

    // A body does not fall like a felled tree: give the trunk a lean of its
    // own, so a standing death picks a side instead of balancing.
    const double bearing = static_cast<double>(seed % 360U) * radians;
    const Vec3 lean{std::cos(bearing), std::sin(bearing), 0};
    for (auto& body : bodies_)
        body.velocity = velocity;
    // A squat sits on its own heels; it takes more to tip it off them.
    bodies_[torso].angular_velocity = mul(lean, crouched ? 2.6 : .9);
    keel_left_ = ((seed >> 5U) & 1U) != 0;

    // How long it keeps its feet: longest when it was already moving, a
    // moment when it stood, hardly at all from a squat. Never the same twice.
    crouched_ = crouched;
    standing_height_ = position.z + 2.25 - joint(pelvis).z;
    const double chance = static_cast<double>((seed >> 8U) & 255U) / 255.0;
    const double pace = std::hypot(velocity.x, velocity.y);
    stagger_ = crouched ? .25 + .2 * chance : pace > 1.5 ? .8 + .45 * chance : .55 + .35 * chance;
    gait_velocity_ = {velocity.x, velocity.y, 0};
    gait_forward_ = forward;
    gait_left_ = right_;
    // The step it was in the middle of.
    const double stride_at_death =
        std::clamp(walking.left.rotation_x_degrees, -18.0, 32.0) * radians;
    gait_phase_ = std::asin(std::clamp(stride_at_death / .6, -1.0, 1.0));
    capture_pose();
}

double ClassicCorpse::flex_of(const Link& link) const noexcept {
    const auto& parent = bodies_[link.parent];
    const auto& child = bodies_[link.child];
    const auto parent_z = rotate(parent.orientation, {0, 0, 1});
    const auto child_z = rotate(child.orientation, {0, 0, 1});
    const auto axis = rotate(parent.orientation, {1, 0, 0});
    return std::atan2(dot(cross(parent_z, child_z), axis), dot(parent_z, child_z));
}

void ClassicCorpse::capture_pose() {
    for (std::size_t index = 0; index < link_count; ++index) {
        const auto& link = links_[index];
        auto& held = held_[index];
        if (link.hinge) {
            held.flex = flex_of(link);
            continue;
        }
        const auto& parent = bodies_[link.parent];
        const auto bone = rotate(bodies_[link.child].orientation, link.child_bone);
        const bool hip = index == hip_left_link || index == hip_right_link;
        // A hip is held against the world: the legs stay under the body
        // whatever a shot does to the trunk above them.
        const auto axis = hip ? Vec3{0, 0, 1} : rotate(parent.orientation, link.axis);
        const auto front = hip ? gait_forward_ : rotate(parent.orientation, link.front);
        const auto side =
            hip ? mul(gait_left_, link.side.x) : rotate(parent.orientation, link.side);
        held.pitch = std::atan2(dot(bone, front), dot(bone, axis));
        held.lateral = std::asin(std::clamp(dot(bone, side), -1.0, 1.0));
        held.world = hip;
    }
}

void ClassicCorpse::plan_drives() {
    const auto& trunk = bodies_[torso];
    // How much strength is left. It holds for the first part of the stagger
    // and then drains.
    const double spent = stagger_ > 0 ? std::clamp((age_ / stagger_ - .3) / .7, 0.0, 1.0) : 1.0;
    const double fade = 1 - spent * spent * (3 - 2 * spent);
    // How upright it still is. A body going over stops walking: its legs
    // trail in line with it and it comes down full length, not onto its knees.
    const double spine_down = rotate(trunk.orientation, {0, 0, 1}).z;
    const double standing = std::clamp((spine_down - .5) / .3, 0.0, 1.0);
    tone_ = fade;
    const double arm_tone = fade * fade;
    // The spring left in a limp joint shapes the fall and the landing. Once
    // the body is down and slowing it goes too: left on, it would draw an
    // outflung arm in across the ground long after the body had come to rest.
    bool down = age_ > stagger_;
    for (const auto& body : bodies_)
        down &= length(body.velocity) < 1.2 && length(body.angular_velocity) < 4.0;
    if (down || age_ > stagger_ + limp_seconds)
        slack_ = std::max(0.0, slack_ - step_seconds / limp_fade);
    const double slack_left = slack_;

    // Which way it is going, seen from the trunk: the legs step that way,
    // forward, backward or to a side. A trunk thrown back by a shot counts:
    // the feet go after the weight above them, as a living body's would.
    const auto level = [](Vec3 v) { return Vec3{v.x, v.y, 0}; };
    const auto ahead = level(rotate(trunk.orientation, {0, 1, 0}));
    const auto aside = level(rotate(trunk.orientation, {1, 0, 0}));
    if (length(ahead) > .3 && length(aside) > .3) {
        gait_left_ = unit(aside);
        gait_forward_ = unit(reject(ahead, gait_left_), gait_forward_);
    }
    const auto moving =
        level(mul(add(velocity_at(trunk, sub(joint(pelvis), trunk.position)),
                      velocity_at(trunk, sub(joint(neck), trunk.position))),
                  .5));
    gait_velocity_ = add(gait_velocity_,
                         mul(sub(moving, gait_velocity_), std::min(1.0, step_seconds / .08)));
    const double forward_speed = dot(gait_velocity_, gait_forward_);
    const double left_speed = dot(gait_velocity_, gait_left_);
    const double speed = std::hypot(forward_speed, left_speed);
    // A step covers the ground the body does: faster means quicker, longer
    // steps, up to what a leg can reach. Past that the feet fall behind.
    const double cadence = 8 + .6 * speed;
    gait_phase_ += cadence * step_seconds;
    double stride = std::min(.6, speed * std::numbers::pi / (cadence * 2.2));
    if (crouched_ || !grounded_)
        stride = 0;
    stride *= std::clamp((speed - .25) / .5, 0.0, 1.0);
    const double along = speed > 1e-6 ? stride * forward_speed / speed : 0;
    const double across = speed > 1e-6 ? std::min(.22, stride * .5) * left_speed / speed : 0;
    const double swing = std::sin(gait_phase_), lifting = std::cos(gait_phase_);
    // The leg that moves the way the body is going is the one in the air.
    const bool sideways = std::abs(left_speed) > std::abs(forward_speed);
    const bool left_leads = sideways ? left_speed > 0 : true;
    const double lift_left = std::max(0.0, left_leads ? lifting : -lifting);
    const double lift_right = std::max(0.0, left_leads ? -lifting : lifting);
    const double knee_lift = stride > .03 ? .3 + stride / .6 : 0;
    // As the strength goes the legs give: the body sinks as it steps.
    const double sag = (1 - fade) * 38 * radians;
    const double walking = leg_strength * fade * standing * (grounded_ ? 1.0 : .4);
    const double trailing = trailing_strength * fade * (1 - standing);

    const auto set = [&](std::size_t index, double pitch, double lateral, double flex,
                         double strength, double limp_pitch, double limp_lateral,
                         double limp_flex, double limp) {
        // One spring: what is left of the muscle pulling to its target and
        // the joint's own slack pulling to rest.
        auto& drive = drives_[index];
        limp *= slack_left;
        const double total = strength + limp;
        drive = {};
        if (total <= 0)
            return;
        drive.pitch = (pitch * strength + limp_pitch * limp) / total;
        drive.lateral = (lateral * strength + limp_lateral * limp) / total;
        drive.flex = (flex * strength + limp_flex * limp) / total;
        drive.stiffness = total;
        drive.world = false;
    };
    set(neck_link, held_[neck_link].pitch, held_[neck_link].lateral, 0, neck_strength * fade,
        5 * radians, 0, 0, limp_neck);
    for (const auto index : {shoulder_left_link, shoulder_right_link})
        set(index, held_[index].pitch, held_[index].lateral, 0, shoulder_strength * arm_tone,
            8 * radians, 10 * radians, 0, limp_shoulder);
    for (const auto index : {elbow_left_link, elbow_right_link})
        set(index, 0, 0, held_[index].flex, elbow_strength * arm_tone, 0, 0, 22 * radians,
            limp_elbow);
    for (const bool left : {true, false}) {
        const auto hip = left ? hip_left_link : hip_right_link;
        const auto knee = left ? knee_left_link : knee_right_link;
        const double lift = left ? lift_left : lift_right;
        double pitch = held_[hip].pitch, lateral = held_[hip].lateral, flex = held_[knee].flex;
        if (!crouched_) {
            pitch = 6 * radians + sag * .5 + (left ? along : -along) * swing;
            lateral = 3 * radians + std::abs(across) * (.5 + .5 * swing);
            flex = 10 * radians + sag + knee_lift * lift;
        }
        if (walking > trailing && walking > 1) {
            // Against the world while it is on its feet: the legs stay under
            // the body whatever a shot does to the trunk above them.
            drives_[hip] = {pitch, lateral, 0, walking + limp_hip * slack_left, true};
            set(knee, 0, 0, flex, walking, 0, 0, 10 * radians, limp_knee);
        } else {
            // Going over, or limp: a hip only knows the trunk it hangs from.
            set(hip, 4 * radians, 3 * radians, 0, trailing, 6 * radians, 4 * radians, 0, limp_hip);
            set(knee, 0, 0, 14 * radians, trailing, 0, 0, 10 * radians, limp_knee);
        }
    }
    upright_ = upright_strength * fade * std::sqrt(fade) * standing;
    // The hips are carried lower as the strength goes, and not at all once it
    // has gone or the body is going over.
    carried_ = crouched_ ? 0 : std::clamp(fade / .25, 0.0, 1.0) * standing;
    carried_height_ = standing_height_ - carry_sink * (1 - fade);
}

void ClassicCorpse::pose_limb(
    Part upper, Part lower, Vec3 upper_direction, Vec3 lower_direction, Vec3 bend) {
    const auto first = unit(upper_direction), second = unit(lower_direction);
    auto hinge = cross(first, second);
    if (length(hinge) < .05)
        hinge = cross(first, bend);
    const auto upper_x = unit(reject(hinge, first), right_);
    const auto lower_x = unit(reject(hinge, second), right_);
    bodies_[upper].orientation = from_axes(upper_x, cross(first, upper_x), first);
    bodies_[lower].orientation = from_axes(lower_x, cross(second, lower_x), second);
}

void ClassicCorpse::place_chain() {
    for (const auto& link : links_) {
        const auto& parent = bodies_[link.parent];
        auto& child = bodies_[link.child];
        child.position = sub(add(parent.position, rotate(parent.orientation, link.parent_anchor)),
                             rotate(child.orientation, link.child_anchor));
    }
}

void ClassicCorpse::set_arm_pose(bool left, Vec3 upper_direction, Vec3 lower_direction) {
    if (!ragdoll_ || age_ > 0 || !finite(upper_direction) || !finite(lower_direction) ||
        length(upper_direction) < 1e-8 || length(lower_direction) < 1e-8)
        return;
    pose_limb(left ? upper_arm_left : upper_arm_right, left ? forearm_left : forearm_right,
              upper_direction, lower_direction, rotate(bodies_[torso].orientation, {0, 1, 0}));
    place_chain();
    capture_pose();
}

void ClassicCorpse::lift_out_of_terrain(const VxlMap& map) {
    // A death against a wall or in a hole starts with parts inside blocks, as
    // the living model's were. First move the whole pose clear of the deepest
    // overlap, then let the joints give until nothing is inside anything.
    for (int pass = 0; pass < 12; ++pass) {
        std::optional<Penetration> deepest;
        for (std::size_t i = 0; i < ball_count_; ++i) {
            // What the body stands or lies on moves all of it. An arm hanging
            // into the ground bends at its joints; it does not hoist the rest.
            if (balls_[i].part >= upper_arm_left && balls_[i].part <= forearm_right)
                continue;
            const auto& body = bodies_[balls_[i].part];
            const auto hit = terrain_contact(
                map, add(body.position, rotate(body.orientation, balls_[i].local)), balls_[i].radius);
            if (hit && (!deepest || hit->depth > deepest->depth))
                deepest = hit;
        }
        if (!deepest || deepest->depth < .01)
            break;
        for (auto& body : bodies_)
            body.position = add(body.position, mul(deepest->normal, deepest->depth));
    }
    // This is a correction of the pose, not motion: the body keeps exactly
    // the velocity it died with.
    std::array<Vec3, part_count> moving{}, turning{};
    for (std::size_t part = 0; part < part_count; ++part) {
        moving[part] = bodies_[part].velocity;
        turning[part] = bodies_[part].angular_velocity;
    }
    const double h = step_seconds / substeps;
    for (int pass = 0; pass < 400; ++pass) {
        for (auto& body : bodies_) {
            body.position_before = body.position;
            body.orientation_before = body.orientation;
        }
        solve_links(h);
        solve_self();
        for (std::size_t i = 0; i < ball_count_; ++i)
            static_cast<void>(resolve_ball(bodies_[balls_[i].part], balls_[i], map));
        double moved = 0;
        for (const auto& body : bodies_)
            moved = std::max(moved, length(sub(body.position, body.position_before)));
        if (moved < 2e-5)
            break;
    }
    for (std::size_t part = 0; part < part_count; ++part) {
        bodies_[part].velocity = moving[part];
        bodies_[part].angular_velocity = turning[part];
    }
    touch_count_ = 0;
}

void ClassicCorpse::tick(double dt, const VxlMap& map, double gravity) {
    if (!std::isfinite(dt) || dt <= 0)
        return;
    if (resting() && map_revision_ == map.revision())
        return;
    if (map_revision_ != map.revision())
        quiet_steps_ = still_steps_ = keel_spent_ = 0;
    map_revision_ = map.revision();
    gravity = std::isfinite(gravity) ? std::clamp(gravity, 0.0, 4.0) : 1.0;
    if (ragdoll_ && !placed_) {
        lift_out_of_terrain(map);
        placed_ = true;
    }
    remainder_ += std::min(dt, .1);
    while (remainder_ + 1e-10 >= step_seconds) {
        step(map, gravity);
        remainder_ -= step_seconds;
        if (resting()) {
            // Asleep part way through a long frame: it stops here, exactly
            // as it would have at a higher frame rate.
            remainder_ = 0;
            break;
        }
    }
}

void ClassicCorpse::solve_links(double h) {
    for (std::size_t index = 0; index < link_count; ++index) {
        const auto& link = links_[index];
        const auto& drive = drives_[index];
        // A spring of this stiffness, as the solver takes it.
        const double slack = drive.stiffness > 0 ? 1 / (drive.stiffness * h * h) : 0;
        auto& parent = bodies_[link.parent];
        auto& child = bodies_[link.child];
        if (link.hinge) {
            // One axis in common, and a stop at each end of the swing.
            const auto parent_x = rotate(parent.orientation, {1, 0, 0});
            const auto child_x = rotate(child.orientation, {1, 0, 0});
            const auto stray = cross(child_x, parent_x);
            if (const double sine = length(stray); sine > 1e-9)
                turn_apart(parent, child,
                           mul(stray, std::atan2(sine, dot(child_x, parent_x)) / sine));
            const auto parent_z = rotate(parent.orientation, {0, 0, 1});
            const auto child_z = rotate(child.orientation, {0, 0, 1});
            const auto axis = rotate(parent.orientation, {1, 0, 0});
            const double flex =
                std::atan2(dot(cross(parent_z, child_z), axis), dot(parent_z, child_z));
            const double allowed = std::clamp(flex, link.flex_minimum, link.flex_maximum);
            if (allowed != flex)
                turn_apart(parent, child, mul(axis, allowed - flex));
            if (slack > 0)
                turn_apart(parent, child,
                           mul(rotate(parent.orientation, {1, 0, 0}),
                               std::clamp(drive.flex, link.flex_minimum, link.flex_maximum) -
                                   flex_of(link)),
                           slack);
        } else {
            const auto axis = rotate(parent.orientation, link.axis);
            const auto front = rotate(parent.orientation, link.front);
            const auto side = rotate(parent.orientation, link.side);
            auto bone = rotate(child.orientation, link.child_bone);
            const double pitch = std::atan2(dot(bone, front), dot(bone, axis));
            const double lateral = std::asin(std::clamp(dot(bone, side), -1.0, 1.0));
            const double pitch_allowed =
                std::clamp(pitch, link.pitch_minimum, link.pitch_maximum);
            const double lateral_allowed =
                std::clamp(lateral, link.side_minimum, link.side_maximum);
            if (pitch_allowed != pitch || lateral_allowed != lateral) {
                const auto target =
                    add(mul(add(mul(axis, std::cos(pitch_allowed)),
                                mul(front, std::sin(pitch_allowed))),
                            std::cos(lateral_allowed)),
                        mul(side, std::sin(lateral_allowed)));
                const auto swing = cross(bone, target);
                if (const double sine = length(swing); sine > 1e-9)
                    turn_apart(parent, child,
                               mul(swing, std::atan2(sine, dot(bone, target)) / sine),
                               1e-4 / (h * h));
                bone = rotate(child.orientation, link.child_bone);
            }
            // Twist: how far the child has turned about its own bone from
            // where a plain swing of the parent's neutral pose would leave it.
            const auto neutral = unit(reject(
                carried(rotate(parent.orientation, link.neutral_hinge), axis, bone, front), bone));
            const auto actual = unit(reject(rotate(child.orientation, {1, 0, 0}), bone));
            const double twist =
                std::atan2(dot(cross(neutral, actual), bone), dot(neutral, actual));
            const double twist_allowed = std::clamp(twist, -link.twist, link.twist);
            if (twist_allowed != twist)
                turn_apart(parent, child, mul(bone, twist_allowed - twist), 1e-4 / (h * h));
            if (slack > 0) {
                // The muscle: the bone toward where it is wanted, and no
                // roll of its own.
                const auto about = drive.world ? Vec3{0, 0, 1} : axis;
                const auto ahead = drive.world ? gait_forward_ : front;
                const auto aside = drive.world ? mul(gait_left_, link.side.x) : side;
                const auto wanted =
                    add(mul(add(mul(about, std::cos(drive.pitch)), mul(ahead, std::sin(drive.pitch))),
                            std::cos(drive.lateral)),
                        mul(aside, std::sin(drive.lateral)));
                bone = rotate(child.orientation, link.child_bone);
                const auto toward = cross(bone, wanted);
                if (const double sine = length(toward); sine > 1e-9)
                    turn_apart(parent, child,
                               mul(toward, std::atan2(sine, dot(bone, wanted)) / sine), slack);
                bone = rotate(child.orientation, link.child_bone);
                const auto level = unit(reject(
                    carried(rotate(parent.orientation, link.neutral_hinge), axis, bone, front),
                    bone));
                const auto rolled = unit(reject(rotate(child.orientation, {1, 0, 0}), bone));
                turn_apart(parent, child,
                           mul(bone, -std::atan2(dot(cross(level, rolled), bone), dot(level, rolled))),
                           slack * 2);
            }
        }
        // The joint itself: the two anchors are one point.
        const auto parent_arm = rotate(parent.orientation, link.parent_anchor);
        const auto child_arm = rotate(child.orientation, link.child_anchor);
        const auto gap =
            sub(add(child.position, child_arm), add(parent.position, parent_arm));
        if (const double distance = length(gap); distance > 1e-10) {
            const auto direction = mul(gap, 1 / distance);
            const double shared = distance / (compliance_at(parent, parent_arm, direction) +
                                              compliance_at(child, child_arm, direction));
            push(parent, parent_arm, mul(direction, shared));
            push(child, child_arm, mul(direction, -shared));
        }
    }
}

void ClassicCorpse::solve_self() {
    // The deepest overlap of each pair of parts that may not pass through
    // one another. One contact a pair: several balls of a forearm inside the
    // ribs are one arm against one trunk, not that many shoves.
    struct Deepest {
        double depth{};
        std::uint8_t one{}, other{};
    };
    std::array<Vec3, maximum_balls> centers{};
    for (std::size_t i = 0; i < ball_count_; ++i)
        centers[i] = add(bodies_[balls_[i].part].position,
                         rotate(bodies_[balls_[i].part].orientation, balls_[i].local));
    std::array<std::array<Deepest, part_count>, part_count> pairs{};
    for (std::size_t a = 0; a < ball_count_; ++a)
        for (std::size_t b = a + 1; b < ball_count_; ++b) {
            const auto& first = balls_[a];
            const auto& second = balls_[b];
            if (first.part == second.part || !blocks(first.part, second.part))
                continue;
            const double depth =
                first.radius + second.radius - length(sub(centers[a], centers[b]));
            auto& deepest = pairs[first.part][second.part];
            if (depth > deepest.depth)
                deepest = {depth, static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(b)};
        }
    touch_count_ = 0;
    for (const auto& row : pairs)
        for (const auto& pair : row) {
            if (pair.depth <= 0)
                continue;
            const auto& first = balls_[pair.one];
            const auto& second = balls_[pair.other];
            auto& one = bodies_[first.part];
            auto& other = bodies_[second.part];
            // Where they are now: an earlier pair may have moved them.
            const auto one_arm = rotate(one.orientation, first.local);
            const auto other_arm = rotate(other.orientation, second.local);
            const auto delta = sub(add(one.position, one_arm), add(other.position, other_arm));
            const double distance = length(delta), minimum = first.radius + second.radius;
            if (distance >= minimum || distance < 1e-9)
                continue;
            const auto normal = mul(delta, 1 / distance);
            const auto one_offset = sub(one_arm, mul(normal, first.radius));
            const auto other_offset = add(other_arm, mul(normal, second.radius));
            const double shared = (minimum - distance) /
                                  (compliance_at(one, one_offset, normal) +
                                   compliance_at(other, other_offset, normal));
            push(one, one_offset, mul(normal, shared));
            push(other, other_offset, mul(normal, -shared));
            touches_[touch_count_++] = {first.part, second.part, one_offset, other_offset, normal};
        }
}

void ClassicCorpse::substep(const VxlMap& map, double gravity, double h) {
    if (keel_pulling_) {
        auto& trunk = bodies_[torso];
        kick(trunk, sub(joint(keel_at_neck_ ? neck : pelvis), trunk.position),
             mul(keel_push_, h));
    }
    if (carried_ > 0 && gravity > 0) {
        // Its legs still carrying it: an upward push at the hips, as much as
        // holds them at the height the legs can still manage and never a
        // pull. Over a drop there is nothing to stand on and nothing is held.
        auto& trunk = bodies_[torso];
        const auto hips = joint(pelvis);
        const double clearance = clearance_below(map, hips);
        if (clearance < 1.7) {
            double weight = 0;
            for (const auto kilograms_of_part : kilograms)
                weight += kilograms_of_part * gravity_blocks * gravity;
            const auto offset = sub(hips, trunk.position);
            const double sinking = velocity_at(trunk, offset).z; // z is down
            // More when it is low or dropping, less when it is high or
            // rising: it is set down, never bounced.
            const double share = std::clamp(
                carry_base + carry_spring * (carried_height_ - clearance) + carry_damping * sinking,
                0.0, carry_most);
            kick(trunk, offset, Vec3{0, 0, -carried_ * share * weight * h});
            if (grounded_) {
                // Its feet going after its weight: the hips are pushed the way
                // the trunk leans, as a step would take them. Knocked back by
                // a shot it staggers back under itself before it goes over.
                const auto lean = sub(joint(neck), hips);
                const Vec3 over{lean.x, lean.y, 0};
                if (const double far = length(over); far > 1e-6) {
                    const double push = std::min(catch_most, catch_gain * far);
                    kick(trunk, offset, mul(over, carried_ * push * weight * h / far));
                }
            }
        }
    }
    for (auto& body : bodies_)
        begin_substep(body, gravity, h);
    if (upright_ > 0) {
        // What is left of its balance: the trunk drawn back toward upright.
        // A shot still rocks it; it is a spring, not a rail.
        auto& trunk = bodies_[torso];
        const auto spine = rotate(trunk.orientation, {0, 0, 1});
        const auto tilt = cross(spine, Vec3{0, 0, 1});
        if (const double sine = length(tilt); sine > 1e-9) {
            const auto axis = mul(tilt, 1 / sine);
            const auto turn = world_inverse_inertia(trunk, axis);
            const double angle = std::min(std::atan2(sine, spine.z), .35);
            trunk.orientation = turned(
                trunk.orientation,
                mul(turn, angle / (dot(axis, turn) + 1 / (upright_ * h * h))));
        }
    }
    solve_links(h);
    solve_self();
    std::array<std::optional<TerrainContact>, maximum_balls> contacts{};
    for (std::size_t i = 0; i < ball_count_; ++i) {
        contacts[i] = resolve_ball(bodies_[balls_[i].part], balls_[i], map);
        touched_ground_ |= contacts[i].has_value() &&
                           (balls_[i].part == shin_left || balls_[i].part == shin_right);
    }
    for (auto& body : bodies_)
        end_substep(body, h);
    for (std::size_t i = 0; i < ball_count_; ++i)
        if (contacts[i])
            finish_contact(*contacts[i], h);
    // Parts moved out of one another stay together: being pushed apart is
    // not being thrown apart.
    for (std::size_t i = 0; i < touch_count_; ++i) {
        const auto& touch = touches_[i];
        auto& one = bodies_[touch.one];
        auto& other = bodies_[touch.other];
        const double parting = dot(sub(velocity_at(one, touch.one_offset),
                                       velocity_at(other, touch.other_offset)),
                                   touch.normal);
        if (parting > parting_speed) {
            const double shared = (parting - parting_speed) /
                                  (compliance_at(one, touch.one_offset, touch.normal) +
                                   compliance_at(other, touch.other_offset, touch.normal));
            kick(one, touch.one_offset, mul(touch.normal, -shared));
            kick(other, touch.other_offset, mul(touch.normal, shared));
        }
        // Cloth on cloth: an arm lying across the chest stays there instead
        // of slipping off seconds after the body has come to rest.
        const auto sliding = reject(sub(velocity_at(one, touch.one_offset),
                                        velocity_at(other, touch.other_offset)),
                                    touch.normal);
        if (const double speed = length(sliding); speed > 1e-9) {
            const auto along = mul(sliding, 1 / speed);
            const double shared = speed * self_grip /
                                  (compliance_at(one, touch.one_offset, along) +
                                   compliance_at(other, touch.other_offset, along));
            kick(one, touch.one_offset, mul(along, -shared));
            kick(other, touch.other_offset, mul(along, shared));
        }
    }
    // Joints are not frictionless: tissue resists bending, and working
    // muscle more than slack.
    const double resistance = 7 + 9 * tone_;
    bodies_[torso].angular_velocity =
        mul(bodies_[torso].angular_velocity, 1 - 5 * tone_ * h);
    for (const auto& link : links_) {
        auto& parent = bodies_[link.parent];
        auto& child = bodies_[link.child];
        const auto relative = sub(child.angular_velocity, parent.angular_velocity);
        const double rate = length(relative);
        if (rate < 1e-9)
            continue;
        const auto axis = mul(relative, 1 / rate);
        const auto of_parent = world_inverse_inertia(parent, axis);
        const auto of_child = world_inverse_inertia(child, axis);
        const double shared = rate * std::min(1.0, resistance * h) /
                              (dot(axis, of_parent) + dot(axis, of_child));
        child.angular_velocity = sub(child.angular_velocity, mul(of_child, shared));
        parent.angular_velocity = add(parent.angular_velocity, mul(of_parent, shared));
    }
    // A body that has all but stopped is brought to a stop. Wedged between
    // walls, its parts would otherwise trade tiny corrections for ever. The
    // bounds are low: a trunk that starts to topple is past them within a few
    // degrees, and falls at its own pace.
    bool slow = true;
    for (const auto& body : bodies_)
        slow &= length(body.velocity) < .25 && length(body.angular_velocity) < .8;
    for (auto& body : bodies_) {
        if (slow) {
            body.velocity = mul(body.velocity, 1 - 9 * h);
            body.angular_velocity = mul(body.angular_velocity, 1 - 9 * h);
        }
        limit_rates(body);
    }
}

void ClassicCorpse::step(const VxlMap& map, double gravity) {
    age_ += step_seconds;
    if (!ragdoll_) {
        auto& body = bodies_[torso];
        const auto before = body.position;
        body.velocity.z += gravity_blocks * gravity * step_seconds;
        body.position = add(body.position, mul(body.velocity, step_seconds));
        for (int pass = 0; pass < 3; ++pass) {
            const auto hit = terrain_contact(map, body.position, .35);
            if (!hit)
                break;
            body.position = add(body.position, mul(hit->normal, hit->depth));
            // It lands and stays: no bounce, and the ground stops the slide.
            body.velocity = mul(reject(body.velocity, hit->normal), .8);
        }
        quiet_steps_ = length(sub(body.position, before)) < .0005 ? quiet_steps_ + 1U : 0U;
        return;
    }
    const auto backup = bodies_;
    const double h = step_seconds / substeps;
    // Knees down, hips up, head on the ground; or sat on the ground, folded
    // forward over its legs with its chest in the air: a stable heap, and a
    // limp body does not hold one. Whichever end of the trunk is held up is
    // drawn to one side, no harder than it takes to keep it going over at a
    // walking pace, and let go once it leans: its own weight lays it down.
    // Not while it is still on its feet: that is the stagger's business.
    bool settling = age_ > stagger_ + .2 && carried_ <= 0;
    for (const auto& body : bodies_)
        settling &= length(body.velocity) < .9 && length(body.angular_velocity) < 3.0;
    const double hips_up = clearance_below(map, joint(pelvis));
    const double chest_up = clearance_below(map, joint(neck));
    const auto shoulders = rotate(bodies_[torso].orientation, {1, 0, 0});
    const Vec3 level{shoulders.x, shoulders.y, 0};
    const bool on_knees = hips_up > keel_clearance && hips_up < 1.0;
    const bool sat_up = !on_knees && hips_up <= keel_clearance && chest_up > keel_sitting &&
                        chest_up < 1.6;
    const bool kneeling = gravity > 0 && age_ > stagger_ && carried_ <= 0 &&
                          (on_knees || sat_up) && std::abs(shoulders.z) < keel_lean &&
                          length(level) > .35;
    keel_pulling_ = false;
    if (!kneeling) {
        keel_steps_ = 0;
    } else if (keel_spent_ < keel_budget && (keel_steps_ > 0 || settling)) {
        if (keel_steps_ == 0) {
            // Toward the side that is already lower; z is down.
            const bool left = std::abs(shoulders.z) > .04 ? shoulders.z > 0 : keel_left_;
            keel_push_ = mul(unit(level), left ? 1.0 : -1.0);
            keel_at_neck_ = sat_up;
        }
        // The budget runs while it kneels: a heap that will not go over is
        // left to sleep where it is rather than worried at for ever.
        ++keel_spent_;
        const auto way = unit(keel_push_);
        const auto& trunk = bodies_[torso];
        const auto held = joint(keel_at_neck_ ? neck : pelvis);
        const double going = dot(velocity_at(trunk, sub(held, trunk.position)), way);
        keel_pulling_ = going < keel_speed;
        keel_steps_ = keel_pulling_ ? keel_steps_ + 1U : std::max(1U, keel_steps_);
        double weight = 0;
        for (const auto kilograms_of_part : kilograms)
            weight += kilograms_of_part * gravity_blocks * gravity;
        const double ramp = std::min(1.0, static_cast<double>(keel_steps_) / keel_ramp);
        // The chest is a long lever over hips that are already down.
        keel_push_ = mul(way, (keel_at_neck_ ? keel_share * .4 : keel_share) * weight * ramp);
    }
    plan_drives();
    touched_ground_ = false;
    for (int i = 0; i < substeps; ++i)
        substep(map, gravity, h);
    grounded_ = touched_ground_;
    bool sound = true, quiet = true;
    for (const auto& body : bodies_) {
        sound &= finite(body.position) && finite(body.velocity) &&
                 finite(body.angular_velocity) && std::isfinite(body.orientation.w);
        quiet &= length(body.velocity) < .1 && length(body.angular_velocity) < .45;
    }
    if (!sound) {
        // Never draw a broken pose: keep the last good one and lie still.
        bodies_ = backup;
        quiet = true;
        quiet_steps_ = sleep_steps;
    }
    quiet_steps_ = quiet ? quiet_steps_ + 1U : 0U;
    // Wedged where its joints and the walls cannot all be satisfied, a body's
    // parts keep exchanging corrections. It is going nowhere: let it sleep.
    bool moved = still_steps_ == 0;
    for (std::size_t part = 0; part < part_count && !moved; ++part) {
        const auto& body = bodies_[part];
        const auto bone = rotate(body.orientation, {0, 0, 1});
        const auto across = rotate(body.orientation, {1, 0, 0});
        moved = length(sub(body.position, still_[part].position)) > still_distance ||
                length(sub(bone, still_[part].bone)) > still_turn ||
                length(sub(across, still_[part].across)) > still_turn;
    }
    if (moved) {
        for (std::size_t part = 0; part < part_count; ++part)
            still_[part] = {bodies_[part].position,
                            rotate(bodies_[part].orientation, {0, 0, 1}),
                            rotate(bodies_[part].orientation, {1, 0, 0})};
        still_steps_ = 1;
    } else if (++still_steps_ > still_limit) {
        quiet_steps_ = sleep_steps;
    }
    if (keel_steps_ > 0 && keel_spent_ < keel_budget)
        quiet_steps_ = still_steps_ = 0; // about to go over: not asleep yet
    if (resting())
        for (auto& body : bodies_)
            body.velocity = body.angular_velocity = {};
}

Vec3 ClassicCorpse::joint_rest_to_world(Part part, Vec3 rest_point) const noexcept {
    const auto& body = bodies_[part];
    return add(body.position,
               rotate(body.orientation,
                      unrotate(body.rest_orientation, sub(rest_point, body.rest_position))));
}

Vec3 ClassicCorpse::joint(Joint id) const noexcept {
    if (!ragdoll_ || id >= joint_count)
        return bodies_[torso].position;
    return joint_rest_to_world(joint_owner[id], joint_rest_[id]);
}

std::optional<ClassicCorpse::Hit>
ClassicCorpse::trace(Vec3 origin, Vec3 direction, double range) const {
    if (!ragdoll_ || !finite(origin) || !finite(direction) || length(direction) < 1e-8 ||
        !std::isfinite(range) || range <= 0)
        return std::nullopt;
    direction = unit(direction);
    std::optional<Hit> nearest;
    for (const auto& bone : bones) {
        const auto a = joint(bone.from), b = joint(bone.to);
        const auto distance = ray_capsule(origin, direction, a, b, bone.radius);
        if (!distance || *distance >= range || (nearest && *distance >= nearest->distance))
            continue;
        const auto point = add(origin, mul(direction, *distance));
        const auto axis = sub(b, a);
        nearest =
            Hit{point,
                *distance,
                std::clamp(dot(sub(point, a), axis) / std::max(1e-12, dot(axis, axis)), 0.0, 1.0),
                bone.from,
                bone.to};
    }
    return nearest;
}

void ClassicCorpse::impulse(const Hit& hit, Vec3 velocity) {
    if (!ragdoll_ || !finite(velocity) || !finite(hit.position))
        return;
    double magnitude = length(velocity);
    if (magnitude < 1e-8)
        return;
    if (magnitude > 16) {
        velocity = mul(velocity, 16 / magnitude);
        magnitude = 16;
    }
    const auto struck = std::ranges::find_if(bones, [&](const Bone& bone) {
        return bone.from == hit.from && bone.to == hit.to;
    });
    if (struck == bones.end())
        return;
    quiet_steps_ = 0;
    auto& body = bodies_[struck->part];
    // The same momentum whatever it strikes: a head snaps round, a trunk
    // rocks, and the joints carry the rest into the body. What is struck can
    // leave little faster than the blow arrived, however light the part.
    const auto offset = sub(hit.position, body.position);
    const auto direction = mul(velocity, 1 / magnitude);
    const double carried_off = blow_follow * magnitude / compliance_at(body, offset, direction);
    kick(body, offset, mul(direction, std::min(blow_kilograms * magnitude, carried_off)));
    limit_rates(body);
    still_steps_ = keel_spent_ = 0;
}

void ClassicCorpse::impulse(Vec3 position, Vec3 velocity) {
    if (!ragdoll_ || !finite(position))
        return;
    Hit closest{};
    double distance = std::numeric_limits<double>::infinity();
    for (const auto& bone : bones) {
        const auto a = joint(bone.from), axis = sub(joint(bone.to), a);
        const double t =
            std::clamp(dot(sub(position, a), axis) / std::max(1e-12, dot(axis, axis)), 0.0, 1.0);
        const auto p = add(a, mul(axis, t));
        const double d = length(sub(position, p));
        if (d < distance) {
            distance = d;
            closest = Hit{p, d, t, bone.from, bone.to};
        }
    }
    impulse(closest, velocity);
}

std::optional<Vec3> shoot_classic_corpses(std::span<ClassicCorpse* const> corpses,
                                          const VxlMap& map,
                                          Vec3 origin,
                                          Vec3 direction,
                                          double range,
                                          double strength) {
    if (!finite(origin) || !finite(direction) || length(direction) < 1e-8 ||
        !std::isfinite(range) || range <= 0 || !std::isfinite(strength) || strength <= 0)
        return std::nullopt;
    direction = unit(direction);
    range = std::min(range, 256.0);
    const auto pack = [](Vec3 v) {
        return std::array<float, 3>{
            static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
    };
    if (const auto wall =
            trace_first_solid(map, pack(origin), pack(direction), static_cast<float>(range)))
        range = std::min(range, static_cast<double>(wall->distance));
    ClassicCorpse* nearest{};
    std::optional<ClassicCorpse::Hit> hit;
    for (auto* corpse : corpses) {
        if (!corpse)
            continue;
        if (const auto candidate = corpse->trace(origin, direction, range)) {
            nearest = corpse;
            hit = candidate;
            range = hit->distance;
        }
    }
    if (!hit)
        return std::nullopt;
    nearest->impulse(*hit, mul(direction, strength));
    return hit->position;
}

Vec3 ClassicCorpse::position() const noexcept {
    if (!ragdoll_)
        return sub(bodies_[torso].position, {0, 0, 1.7});
    return joint_rest_to_world(torso, origin_rest_);
}
ClassicCorpse::Matrix ClassicCorpse::part_transform(Part part) const noexcept {
    if (!ragdoll_ || part >= part_count) {
        const Vec3 forward{-right_.y, right_.x, 0};
        return matrix_of(right_, forward, {0, 0, 1}, position());
    }
    return rest_to_world(bodies_[part]);
}
ClassicCorpse::Matrix ClassicCorpse::body_transform() const noexcept {
    return part_transform(torso);
}
ClassicCorpse::Matrix ClassicCorpse::head_transform() const noexcept {
    return part_transform(skull);
}
ClassicCorpse::Matrix ClassicCorpse::leg_transform(bool left) const noexcept {
    return part_transform(left ? thigh_left : thigh_right);
}
ClassicCorpse::Matrix ClassicCorpse::shin_transform(bool left) const noexcept {
    return part_transform(left ? shin_left : shin_right);
}
ClassicCorpse::Matrix
ClassicCorpse::arm_transform(bool left, bool upper, Vec3 minimum, Vec3 maximum) const noexcept {
    const auto part = upper ? (left ? upper_arm_left : upper_arm_right)
                            : (left ? forearm_left : forearm_right);
    const auto from = upper ? (left ? shoulder_left : shoulder_right)
                            : (left ? elbow_left : elbow_right);
    const auto to = upper ? (left ? elbow_left : elbow_right) : (left ? hand_left : hand_right);
    const auto& body = bodies_[part];
    const auto extent = sub(maximum, minimum);
    const auto axis = extent.x > extent.y && extent.x > extent.z ? 0 : extent.y > extent.z ? 1 : 2;
    // The part's own frame gives the mesh its roll, so an arm that straightens
    // keeps the way it faces instead of taking it from a vanishing bend.
    const auto along =
        mul(rotate(body.orientation, {0, 0, 1}), length(sub(joint_rest_[to], joint_rest_[from])));
    const auto side = rotate(body.orientation, {1, 0, 0});
    const auto other = rotate(body.orientation, {0, 1, 0});
    std::array<Vec3, 3> basis{};
    basis[static_cast<std::size_t>(axis)] =
        mul(along, 1 / std::max(.001, axis == 0 ? extent.x : axis == 1 ? extent.y : extent.z));
    basis[static_cast<std::size_t>((axis + 1) % 3)] = mul(side, .05);
    basis[static_cast<std::size_t>((axis + 2) % 3)] = mul(other, .05);
    auto pivot = mul(add(minimum, maximum), .5);
    if (axis == 0)
        pivot.x = minimum.x;
    else if (axis == 1)
        pivot.y = minimum.y;
    else
        pivot.z = minimum.z;
    const auto origin =
        sub(joint(from),
            add(add(mul(basis[0], pivot.x), mul(basis[1], pivot.y)), mul(basis[2], pivot.z)));
    return matrix_of(basis[0], basis[1], basis[2], origin);
}

ClassicDroppedWeapon::ClassicDroppedWeapon(Vec3 center,
                                           Vec3 direction,
                                           Vec3 velocity,
                                           double half_length) {
    direction = unit(direction, {0, 1, 0});
    const auto side = unit(cross(direction, {0, 0, 1}), {1, 0, 0});
    half_length = std::clamp(half_length, .25, 1.2);
    set_box_inertia(body_, 4.0, {.12, .2, half_length * 2});
    body_.position = body_.rest_position = finite(center) ? center : Vec3{256, 256, 230};
    body_.orientation = body_.rest_orientation = from_axes(side, cross(direction, side), direction);
    if (!finite(velocity))
        velocity = {};
    if (length(velocity) > 12)
        velocity = mul(unit(velocity), 12);
    body_.velocity = velocity;
    // Let go, it turns over as it falls.
    body_.angular_velocity = {2, -1, 3};
    for (std::size_t i = 0; i < balls_.size(); ++i)
        balls_[i] = {0, {0, 0, half_length * .8 * (static_cast<double>(i) - 1)}, .09};
}
void ClassicDroppedWeapon::tick(double dt, const VxlMap& map, double gravity) {
    if (!std::isfinite(dt) || dt <= 0)
        return;
    if (quiet_steps_ >= 90 && revision_ == map.revision())
        return;
    if (revision_ != map.revision())
        quiet_steps_ = 0;
    revision_ = map.revision();
    gravity = std::isfinite(gravity) ? std::clamp(gravity, 0.0, 4.0) : 1;
    remainder_ += std::min(dt, .1);
    const double h = step_seconds / substeps;
    while (remainder_ + 1e-10 >= step_seconds) {
        remainder_ -= step_seconds;
        const auto backup = body_;
        for (int i = 0; i < substeps; ++i) {
            begin_substep(body_, gravity, h);
            std::array<std::optional<TerrainContact>, 3> contacts{};
            for (std::size_t ball = 0; ball < balls_.size(); ++ball)
                contacts[ball] = resolve_ball(body_, balls_[ball], map);
            end_substep(body_, h);
            for (const auto& contact : contacts)
                if (contact)
                    finish_contact(*contact, h);
            limit_rates(body_);
        }
        if (!finite(body_.position) || !finite(body_.velocity) ||
            !finite(body_.angular_velocity) || !std::isfinite(body_.orientation.w)) {
            body_ = backup;
            body_.velocity = body_.angular_velocity = {};
        }
        const bool quiet = length(body_.velocity) < .1 && length(body_.angular_velocity) < .45;
        quiet_steps_ = quiet ? quiet_steps_ + 1 : 0;
        if (quiet_steps_ >= 90) {
            body_.velocity = body_.angular_velocity = {};
            remainder_ = 0;
            break;
        }
    }
}
ClassicCorpse::Matrix ClassicDroppedWeapon::transform() const noexcept {
    return rest_to_world(body_);
}
Vec3 ClassicDroppedWeapon::position() const noexcept {
    return body_.position;
}
} // namespace battlespades::world
