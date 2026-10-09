#include "battlespades/world/classic_corpse.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace battlespades;
using C = world::ClassicCorpse;
using world::Vec3;

constexpr int ground = 200; // the top face of the floor: solid from z 200 down

void expect(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

world::VxlMap field() {
    std::vector<std::byte> bytes;
    for (std::size_t column{}; column < static_cast<std::size_t>(world::VxlMap::width) *
                                            world::VxlMap::depth;
         ++column)
        for (const auto value : {0, 1, 0, 0}) bytes.push_back(static_cast<std::byte>(value));
    auto loaded = world::VxlMap::load(bytes);
    expect(static_cast<bool>(loaded), "the empty map loads");
    auto map = std::move(*loaded.map);
    for (std::uint32_t y = 200; y < 330; ++y)
        for (std::uint32_t x = 200; x < 330; ++x)
            for (std::uint32_t z = ground; z < ground + 4; ++z)
                static_cast<void>(map.set_voxel(x, y, z, {120, 120, 120, 255}));
    return map;
}

void wall(world::VxlMap& map, int x0, int x1, int y0, int y1, int height) {
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = ground - height; z < ground; ++z)
                static_cast<void>(map.set_voxel(static_cast<std::uint32_t>(x),
                                                static_cast<std::uint32_t>(y),
                                                static_cast<std::uint32_t>(z), {90, 80, 70, 255}));
}

double distance(Vec3 a, Vec3 b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) +
                     (a.z - b.z) * (a.z - b.z));
}
Vec3 standing_eye(double x, double y) {
    return {x, y, ground - 2.25};
}
std::array<Vec3, C::joint_count> joints(const C& body) {
    std::array<Vec3, C::joint_count> all{};
    for (std::size_t j = 0; j < C::joint_count; ++j) all[j] = body.joint(static_cast<C::Joint>(j));
    return all;
}

// The bones: each pair of joints one rigid part keeps a fixed distance apart.
constexpr std::array<std::array<C::Joint, 2>, 11> bones{{{C::neck, C::pelvis},
                                                         {C::shoulder_left, C::shoulder_right},
                                                         {C::hip_left, C::hip_right},
                                                         {C::shoulder_left, C::elbow_left},
                                                         {C::shoulder_right, C::elbow_right},
                                                         {C::elbow_left, C::hand_left},
                                                         {C::elbow_right, C::hand_right},
                                                         {C::hip_left, C::knee_left},
                                                         {C::hip_right, C::knee_right},
                                                         {C::knee_left, C::foot_left},
                                                         {C::knee_right, C::foot_right}}};

/** What must hold at every instant, whatever is done to the body. */
struct Watch final {
    explicit Watch(const C& body) {
        const auto now = joints(body);
        for (std::size_t i = 0; i < bones.size(); ++i)
            lengths[i] = distance(now[bones[i][0]], now[bones[i][1]]);
        previous = now;
    }
    void check(const C& body, double dt, const world::VxlMap& map, const std::string& where) {
        const auto now = joints(body);
        for (std::size_t j = 0; j < C::joint_count; ++j) {
            const auto p = now[j];
            expect(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z),
                   where + ": every joint stays finite");
            fastest = std::max(fastest, distance(p, previous[j]) / dt);
            // Joints sit inside their parts, so one inside a block is a part
            // pushed through the terrain.
            expect(p.z < 0 || !map.solid(static_cast<std::uint32_t>(p.x),
                                         static_cast<std::uint32_t>(p.y),
                                         static_cast<std::uint32_t>(std::max(0.0, p.z))),
                   where + ": no joint is inside a block (joint " + std::to_string(j) + " at " +
                       std::to_string(p.x) + ", " + std::to_string(p.y) + ", " +
                       std::to_string(ground - p.z) + " above the ground)");
        }
        for (std::size_t i = 0; i < bones.size(); ++i) {
            const double length = distance(now[bones[i][0]], now[bones[i][1]]);
            expect(std::abs(length - lengths[i]) < .04 + lengths[i] * .04,
                   where + ": a part keeps its length (" + std::to_string(i) + ": " +
                       std::to_string(lengths[i]) + " became " + std::to_string(length) + ")");
        }
        // Elbows and knees close no further than their stops.
        const auto bend = [&](C::Joint a, C::Joint b, C::Joint c) {
            const Vec3 u{now[b].x - now[a].x, now[b].y - now[a].y, now[b].z - now[a].z};
            const Vec3 v{now[c].x - now[b].x, now[c].y - now[b].y, now[c].z - now[b].z};
            const double cosine = (u.x * v.x + u.y * v.y + u.z * v.z) /
                                  (distance(now[a], now[b]) * distance(now[b], now[c]));
            return std::acos(std::clamp(cosine, -1.0, 1.0)) * 180 / 3.14159265358979;
        };
        expect(bend(C::shoulder_left, C::elbow_left, C::hand_left) < 158 &&
                   bend(C::shoulder_right, C::elbow_right, C::hand_right) < 158,
               where + ": elbows do not fold past their stop");
        // The stop is at 150 degrees; a hard landing may press a few past it.
        expect(bend(C::hip_left, C::knee_left, C::foot_left) < 160 &&
                   bend(C::hip_right, C::knee_right, C::foot_right) < 160,
               where + ": knees do not fold past their stop");
        previous = now;
    }
    std::array<double, bones.size()> lengths{};
    std::array<Vec3, C::joint_count> previous{};
    double fastest{};
};

double highest_joint(const C& body) {
    double top = 0;
    for (const auto& p : joints(body)) top = std::max(top, ground - p.z);
    return top;
}

void a_standing_death_collapses_and_rests() {
    const auto map = field();
    for (const unsigned seed : {1U, 7U, 90U, 200U, 311U}) {
        C body{standing_eye(256, 256), seed * 37.0, {}, seed, true};
        expect(body.joint(C::head).z + 1.8 < body.joint(C::foot_left).z,
               "death starts upright where the player stood, not already on the ground");
        expect(std::abs(ground - body.joint(C::foot_left).z) < .15,
               "the feet start on the ground");
        Watch watch{body};
        const auto start = body.joint(C::pelvis);
        const double dt = 1.0 / 120;
        for (int tick = 0; tick < 10; ++tick) {
            body.tick(dt, map);
            watch.check(body, dt, map, "letting go");
        }
        // 80 ms in, a body has begun to sag but is nowhere near the floor.
        expect(body.joint(C::pelvis).z - body.joint(C::neck).z > .55 &&
                   ground - body.joint(C::neck).z > 1.5,
               "the first instant keeps the body up: it does not teleport to the ground");
        for (int tick = 0; tick < 230; ++tick) {
            body.tick(dt, map);
            watch.check(body, dt, map, "falling");
        }
        expect(highest_joint(body) < 1.1,
               "two seconds later the body is down (highest joint " +
                   std::to_string(highest_joint(body)) + ")");
        // Nothing but gravity acted: a part cannot move faster than a fall
        // from standing height explains. This is the "physics go flying" guard.
        expect(watch.fastest < 14.5,
               "no part outruns a fall from standing height (fastest " +
                   std::to_string(watch.fastest) + ")");
        expect(distance({start.x, start.y, 0}, {body.joint(C::pelvis).x, body.joint(C::pelvis).y, 0}) < 1.6,
               "a body that was standing still falls where it stood");
        for (int tick = 0; tick < 480 && !body.resting(); ++tick) {
            body.tick(dt, map);
            watch.check(body, dt, map, "settling");
        }
        expect(body.resting(), "the body comes to rest within six seconds");
        const auto settled = joints(body);
        for (int tick = 0; tick < 240; ++tick) body.tick(dt, map);
        for (std::size_t j = 0; j < C::joint_count; ++j)
            expect(distance(settled[j], body.joint(static_cast<C::Joint>(j))) < 1e-9,
                   "a resting body does not creep or twitch");
    }
}

void a_body_carries_its_momentum() {
    const auto map = field();
    // Running along +y (yaw 0 faces +y) at nine blocks a second.
    C runner{standing_eye(250, 240), 0, {0, 9, 0}, 4, true, false, 300};
    Watch watch{runner};
    const auto start = runner.joint(C::pelvis);
    const double dt = 1.0 / 120;
    for (int tick = 0; tick < 480; ++tick) {
        runner.tick(dt, map);
        watch.check(runner, dt, map, "running death");
    }
    const auto end = runner.joint(C::pelvis);
    expect(end.y - start.y > 1.5, "a running death keeps going the way it ran (moved " +
                                      std::to_string(end.y - start.y) + ")");
    expect(std::abs(end.x - start.x) < 1.2, "and not off to one side");
    expect(highest_joint(runner) < 1.1, "then it is down");
}

void a_blow_moves_light_parts_more_than_heavy_ones() {
    double total = 0;
    for (std::size_t part = 0; part < C::part_count; ++part)
        total += C::part_mass(static_cast<C::Part>(part));
    expect(std::abs(total - 74.2) < 1e-9 && C::part_mass(C::torso) > 4 * C::part_mass(C::skull) &&
               C::part_mass(C::thigh_left) > C::part_mass(C::forearm_left),
           "the parts weigh what a body's parts weigh, 74 kg in all");

    const auto map = field();
    // High in the air, so nothing but the blow acts in the first instant.
    const Vec3 eye{256, 256, ground - 30.0};
    C struck_head{eye, 0, {}, 3, true}, struck_trunk{eye, 0, {}, 3, true}, untouched{eye, 0, {}, 3, true};
    struck_head.impulse(struck_head.joint(C::head), {0, -5, 0});
    const auto chest = struck_trunk.joint(C::neck);
    struck_trunk.impulse(Vec3{chest.x, chest.y, chest.z + .4}, {0, -5, 0});
    const double dt = 1.0 / 120;
    for (int tick = 0; tick < 3; ++tick) {
        struck_head.tick(dt, map);
        struck_trunk.tick(dt, map);
        untouched.tick(dt, map);
    }
    const double head_moved =
        distance(struck_head.joint(C::head), untouched.joint(C::head));
    const double trunk_moved =
        distance(struck_trunk.joint(C::pelvis), untouched.joint(C::pelvis));
    expect(head_moved > .12, "a blow to the head whips the head (" + std::to_string(head_moved) + ")");
    expect(trunk_moved < head_moved * .45,
           "the same blow to the trunk moves it far less (" + std::to_string(trunk_moved) + ")");
    expect(distance(struck_head.joint(C::pelvis), untouched.joint(C::pelvis)) < head_moved * .5,
           "and a head shot does not throw the whole body");
}

void it_staggers_before_it_falls() {
    const auto map = field();
    const double dt = 1.0 / 120;
    // Walking toward the shooter at four blocks a second, shot in the head
    // from the front: the bullet travels toward -y.
    C body{standing_eye(250, 240), 0, {0, 4, 0}, 4, true, false, 200};
    const auto start = body.joint(C::pelvis);
    body.impulse(body.joint(C::head), {0, -5, -1});
    Watch watch{body};
    double thrown_back = 0, lifted = 0, highest = 0;
    bool left_up = false, right_up = false;
    for (int tick = 0; tick < 48; ++tick) { // the first 0.4 seconds
        body.tick(dt, map);
        watch.check(body, dt, map, "staggering");
        const auto hips = body.joint(C::pelvis), chest = body.joint(C::neck);
        // How far the chest is behind the hips: the upper body going back.
        thrown_back = std::max(thrown_back, hips.y - chest.y);
        highest = std::max(highest, ground - hips.z);
        // A foot in the air is a step being taken.
        left_up |= ground - body.joint(C::foot_left).z > .16;
        right_up |= ground - body.joint(C::foot_right).z > .16;
        lifted = std::max({lifted, ground - body.joint(C::foot_left).z,
                           ground - body.joint(C::foot_right).z});
        expect(ground - hips.z > .9, "it is still on its feet 0.4 seconds after the shot");
    }
    expect(thrown_back > .08, "the shot rocks the upper body back (" +
                                  std::to_string(thrown_back) + ")");
    expect(left_up && right_up, "and it goes on stepping, a foot at a time (highest " +
                                    std::to_string(lifted) + ")");
    expect(body.joint(C::pelvis).y - start.y > 1.0, "the way it was going");
    expect(highest < 1.3, "without hopping (hips reached " + std::to_string(highest) + ")");
    for (int tick = 0; tick < 240; ++tick) {
        body.tick(dt, map);
        watch.check(body, dt, map, "going down");
    }
    expect(highest_joint(body) < 1.1, "then it is down, 2.4 seconds after the shot");
    expect(body.joint(C::pelvis).y - start.y > 2.0, "having carried on the way it was going");

    // Standing still, the same shot: it has only the bullet's push, so it
    // goes back, and its feet go back under it before it falls.
    C still{standing_eye(270, 240), 0, {}, 4, true};
    const auto stood = still.joint(C::pelvis);
    still.impulse(still.joint(C::head), {0, -5, -1});
    double back = 0;
    for (int tick = 0; tick < 300; ++tick) {
        still.tick(dt, map);
        if (tick == 30)
            expect(ground - still.joint(C::pelvis).z > .95,
                   "a standing body is still up a quarter of a second after the shot");
        back = std::max(back, stood.y - std::min(still.joint(C::foot_left).y,
                                                 still.joint(C::foot_right).y));
    }
    expect(back > .25, "a body knocked back steps back (" + std::to_string(back) + ")");
    expect(still.joint(C::head).y < stood.y - .6, "and falls the way it was pushed");
}

void the_fall_follows_the_blow() {
    const auto map = field();
    // Facing +y, shot in the head from the front: the bullet travels toward -y.
    C body{standing_eye(256, 256), 0, {}, 5, true};
    const auto start = body.joint(C::head);
    body.impulse(body.joint(C::head), {0, -5, -1});
    Watch watch{body};
    const double dt = 1.0 / 120;
    for (int tick = 0; tick < 360; ++tick) {
        body.tick(dt, map);
        watch.check(body, dt, map, "headshot");
    }
    expect(body.joint(C::head).y < start.y - .6,
           "shot from the front, the body goes down backwards (head moved " +
               std::to_string(body.joint(C::head).y - start.y) + ")");
    expect(watch.fastest < 22, "the blow is the only speed added (fastest " +
                                   std::to_string(watch.fastest) + ")");
}

void abuse_does_not_break_it() {
    auto map = field();
    const double dt = 1.0 / 120;
    const std::array<Vec3, 6> ways{{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    const std::array targets{C::hand_left, C::hand_right, C::head, C::foot_left, C::foot_right, C::pelvis};
    // Nine blocks a second is the hardest hit the game gives a corpse; sixteen
    // is the most the body accepts at all.
    for (const double blow : {9.0, 16.0})
        for (unsigned seed = 0; seed < 6; ++seed) {
            C body{standing_eye(290, 290), seed * 57.0, {2, -1, 0}, seed, true};
            Watch watch{body};
            double top = 0;
            for (int tick = 0; tick < 900; ++tick) {
                if (tick % 40 == 0)
                    body.impulse(
                        body.joint(targets[static_cast<std::size_t>(tick / 40) % targets.size()]),
                        {ways[seed].x * blow, ways[seed].y * blow, ways[seed].z * blow});
                body.tick(dt, map);
                watch.check(body, dt, map, "under repeated blows");
                top = std::max(top, highest_joint(body));
            }
            // What is struck leaves at up to twice the blow; a limb beyond it
            // may whip a little faster. It never adds up from blow to blow.
            expect(watch.fastest < blow * 2.7,
                   "blows of " + std::to_string(blow) + " never turn into runaway speed (fastest " +
                       std::to_string(watch.fastest) + ")");
            // The head starts 2.3 up. No blow puts any of the body higher.
            expect(top < 2.7, "and never throw the body into the air (highest " +
                                  std::to_string(top) + ")");
            for (int tick = 0; tick < 840 && !body.resting(); ++tick) {
                body.tick(dt, map);
                watch.check(body, dt, map, "after the blows");
            }
            expect(body.resting(), "and the body settles once they stop");
        }
    // A shotgun's worth of hits in one frame, twenty-five times.
    C peppered{standing_eye(270, 270), 0, {}, 2, true};
    for (int tick = 0; tick < 300; ++tick) peppered.tick(1.0 / 60, map);
    Watch watch{peppered};
    for (int burst = 0; burst < 25; ++burst) {
        for (int pellet = 0; pellet < 8; ++pellet)
            peppered.impulse(peppered.joint(C::hand_left), {2, 4, -3});
        peppered.tick(1.0 / 30, map);
        watch.check(peppered, 1.0 / 30, map, "shotgun bursts");
    }
}

/**
 * Hips or chest held up off flat ground, where a body lying down has neither.
 * Lying on its side the hips are half a body's width up, one shoulder over
 * the other; kneeling they are that high with the shoulders level.
 */
bool propped(const C& body) {
    const double tilt =
        std::abs(body.joint(C::shoulder_left).z - body.joint(C::shoulder_right).z);
    return (ground - body.joint(C::pelvis).z > .52 && tilt < .45) ||
           ground - body.joint(C::neck).z > .78;
}

void a_body_ends_lying_down() {
    const auto map = field();
    // The pose the handoff complained of: head and legs on the ground and the
    // trunk held up between them. A crouched player shot from behind used to
    // end kneeling like that every time.
    for (const bool crouched : {false, true})
        for (int shot = 0; shot < 5; ++shot)
            for (unsigned n = 0; n < 12; ++n) {
                const std::uint32_t seed = n * 7919U + 13U;
                const double yaw = seed % 360U, turn = yaw * 3.14159265358979 / 180;
                const Vec3 right{std::cos(turn), std::sin(turn), 0}, forward{-right.y, right.x, 0};
                C body{{256, 256, ground - (crouched ? 1.35 : 2.25)}, yaw, {}, seed, true, crouched};
                const std::array<Vec3, 5> from{
                    {{}, {-forward.x, -forward.y, 0}, forward, {-right.x, -right.y, 0}, right}};
                if (shot > 0)
                    body.impulse(body.joint(n % 2 == 0 ? C::head : C::neck),
                                 {from[static_cast<std::size_t>(shot)].x * 5,
                                  from[static_cast<std::size_t>(shot)].y * 5, -1});
                // The first step sets the pose clear of the ground: the hands of
                // a squatting body hang into it and fold out.
                body.tick(1.0 / 120, map);
                Watch watch{body};
                for (int tick = 0; tick < 1200 && !body.resting(); ++tick) {
                    body.tick(1.0 / 120, map);
                    watch.check(body, 1.0 / 120, map, "going down");
                }
                const std::string which = std::string{crouched ? "crouched" : "standing"} +
                                          ", shot " + std::to_string(shot) + ", seed " +
                                          std::to_string(seed);
                expect(body.resting(), "the body comes to rest (" + which + ")");
                expect(!propped(body),
                       "and lies down: hips " + std::to_string(ground - body.joint(C::pelvis).z) +
                           ", neck " + std::to_string(ground - body.joint(C::neck).z) + " (" +
                           which + ")");
                // A fall from standing is 14.5 blocks a second, a struck head 10.
                expect(watch.fastest < 20, "without being thrown: fastest " +
                                               std::to_string(watch.fastest) + " (" + which + ")");
            }
}

void terrain_holds_and_releases_it() {
    auto map = field();
    const double dt = 1.0 / 120;

    // Dying against a wall: the body slides down it, it does not hang there.
    wall(map, 301, 301, 296, 306, 4);
    C against{standing_eye(300.55, 302), -90, {1, 0, 0}, 4, true};
    Watch wall_watch{against};
    for (int tick = 0; tick < 720; ++tick) {
        against.tick(dt, map);
        wall_watch.check(against, dt, map, "against a wall");
    }
    expect(highest_joint(against) < 1.6 && against.resting(),
           "a death against a wall ends on the ground at its foot");
    expect(wall_watch.fastest < 15, "the wall adds no speed");

    // A slot one block wide: the worst case for a body 0.9 wide. It must
    // neither be launched out nor shake for ever.
    wall(map, 280, 280, 266, 276, 5);
    wall(map, 282, 282, 266, 276, 5);
    wall(map, 280, 282, 266, 266, 5);
    C wedged{standing_eye(281.5, 269), 20, {0, -1, 0}, 4, true};
    // The slot is narrower than the hanging arms: the first step folds them
    // in from the walls the living model's arms were inside.
    wedged.tick(dt, map);
    Watch slot_watch{wedged};
    double top = 0;
    for (int tick = 0; tick < 1200; ++tick) {
        wedged.tick(dt, map);
        slot_watch.check(wedged, dt, map, "in a slot");
        top = std::max(top, highest_joint(wedged));
    }
    expect(top < 2.6, "a trapped body never gains height (highest " + std::to_string(top) + ")");
    expect(slot_watch.fastest < 15, "a trapped body never gains speed (fastest " +
                                        std::to_string(slot_watch.fastest) + ")");
    for (int tick = 0; tick < 960 && !wedged.resting(); ++tick) wedged.tick(dt, map);
    expect(wedged.resting(), "a trapped body settles instead of shaking");

    // The ground is dug away under a sleeping body: it wakes and falls.
    const auto before = wedged.joint(C::pelvis);
    for (std::uint32_t x = 276; x < 287; ++x)
        for (std::uint32_t y = 262; y < 280; ++y)
            for (std::uint32_t z = ground - 5; z < ground + 4; ++z)
                static_cast<void>(map.clear_voxel(x, y, z));
    for (int tick = 0; tick < 240; ++tick) wedged.tick(dt, map);
    expect(wedged.joint(C::pelvis).z > before.z + 2, "a sleeping body falls when its support goes");

    // Off a ledge: over the edge, down, and at rest below. Never inside it.
    wall(map, 310, 313, 310, 313, 3);
    C ledge{{312, 312, ground - 3 - 2.25}, 0, {0, 3, 0}, 3, true};
    Watch ledge_watch{ledge};
    for (int tick = 0; tick < 900; ++tick) {
        ledge.tick(dt, map);
        ledge_watch.check(ledge, dt, map, "off a ledge");
    }
    expect(ledge.resting(), "a body that fell from a ledge comes to rest");
}

void the_starting_pose_is_the_living_one() {
    const auto map = field();
    const double dt = 1.0 / 120;
    C holding{standing_eye(305, 250), 0, {}, 4, true};
    // Both arms raised forward, as they hold a rifle.
    holding.set_arm_pose(true, {.2, 1, .15}, {-.4, 1, -.2});
    holding.set_arm_pose(false, {-.1, 1, .15}, {.1, 1, -.2});
    const auto hand = holding.joint(C::hand_left);
    expect(holding.joint(C::elbow_left).y - holding.joint(C::shoulder_left).y > .3,
           "a death starts with the arms where the weapon held them");
    Watch watch{holding};
    holding.tick(dt, map);
    watch.check(holding, dt, map, "releasing the weapon pose");
    expect(distance(hand, holding.joint(C::hand_left)) < .08,
           "letting go does not snap the hand somewhere else");

    for (const unsigned seed : {3U, 8U, 40U}) {
        C crouched{{260, 260, ground - 1.35}, seed * 50.0, {}, seed, true, true};
        expect(ground - crouched.joint(C::head).z < 1.75, "a crouched death starts crouched");
        expect(ground - crouched.joint(C::foot_left).z > .05 &&
                   ground - crouched.joint(C::foot_right).z > .05 &&
                   ground - crouched.joint(C::pelvis).z > .2,
               "with its feet and seat above the ground, not in it");
        const double head_height = ground - crouched.joint(C::head).z;
        Watch crouch_watch{crouched};
        double top = 0;
        for (int tick = 0; tick < 60; ++tick) {
            crouched.tick(dt, map);
            crouch_watch.check(crouched, dt, map, "crouched death");
            top = std::max(top, ground - crouched.joint(C::head).z);
        }
        // Hands hanging by a squat reach the ground; they fold, the body
        // does not hop to make room for them.
        expect(top < head_height + .05, "and does not spring up as it lets go (rose " +
                                            std::to_string(top - head_height) + ")");
    }
}

void it_does_not_depend_on_the_frame_rate() {
    const auto map = field();
    C fine{standing_eye(250, 250), 0, {1, 2, 0}, 9, true}, coarse = fine;
    for (int tick = 0; tick < 240; ++tick) fine.tick(1.0 / 120, map);
    for (int tick = 0; tick < 60; ++tick) coarse.tick(1.0 / 30, map);
    for (std::size_t j = 0; j < C::joint_count; ++j)
        expect(distance(fine.joint(static_cast<C::Joint>(j)),
                        coarse.joint(static_cast<C::Joint>(j))) < 1e-8,
               "120 and 30 frames a second give the same body");
}

void shots_at_corpses_are_cosmetic() {
    auto map = field();
    const double dt = 1.0 / 60;
    C near{standing_eye(250, 250), 0, {}, 7, true}, far{standing_eye(254, 250), 0, {}, 7, true};
    for (int tick = 0; tick < 360; ++tick) {
        near.tick(dt, map);
        far.tick(dt, map);
    }
    expect(near.resting() && far.resting(), "both bodies are at rest");
    auto near_before = near, far_before = far;
    std::array<C*, 2> bodies{&far, &near}; // far first on purpose
    const auto chest = near.joint(C::neck);
    const Vec3 origin{244, chest.y, chest.z};
    expect(!world::shoot_classic_corpses(bodies, map, origin, {1, 0, 0}, 1, 8),
           "a shot that stops short touches nothing");
    wall(map, 247, 247, 246, 254, 4);
    expect(!world::shoot_classic_corpses(bodies, map, origin, {1, 0, 0}, 15, 8),
           "a wall stops the bullet before the body");
    for (std::uint32_t y = 246; y <= 254; ++y)
        for (std::uint32_t z = ground - 4; z < ground; ++z)
            static_cast<void>(map.clear_voxel(247, y, z));
    const auto revision = map.revision();
    const auto impact = world::shoot_classic_corpses(bodies, map, origin, {1, 0, 0}, 15, 8);
    expect(impact && impact->x < 252, "only the nearest body takes the bullet");
    expect(!near.resting(), "the shot wakes the body");
    double shifted = 0;
    for (int tick = 0; tick < 25; ++tick) {
        near.tick(dt, map);
        far.tick(dt, map);
        near_before.tick(dt, map);
        far_before.tick(dt, map);
        for (std::size_t j = 0; j < C::joint_count; ++j)
            shifted = std::max(shifted, distance(near.joint(static_cast<C::Joint>(j)),
                                                 near_before.joint(static_cast<C::Joint>(j))));
    }
    // A rifle bullet into a 74 kg body lying on the ground: it gives, and
    // is not shoved along.
    expect(shifted > .002 && shifted < 1.5, "and what it struck gives (" +
                                                std::to_string(shifted) + ")");
    for (int tick = 0; tick < 300 && !near.resting(); ++tick) near.tick(dt, map);
    expect(near.resting(), "which then lies still again");
    expect(distance(far.joint(C::pelvis), far_before.joint(C::pelvis)) < 1e-9,
           "the body behind is untouched");
    expect(map.revision() == revision, "shooting a corpse never changes the terrain");

    const auto foot = near.joint(C::foot_left);
    const auto hit = near.trace({foot.x, foot.y, foot.z - 5}, {0, 0, 1}, 6);
    expect(hit && hit->to == C::foot_left && hit->from == C::knee_left,
           "a shot at the foot strikes the shin, not a standing player's box");
}

void the_retail_corpse_still_falls() {
    auto map = field();
    C plain{{256, 256, ground - 20.0}, 30, {2, 0, 0}, 7, false};
    expect(!plain.ragdoll() && !plain.trace({244, 256, ground - 19.0}, {1, 0, 0}, 30),
           "without ragdolls the body is one point and does not react to gunfire");
    for (int tick = 0; tick < 600; ++tick) plain.tick(1.0 / 60, map);
    const double landed = plain.position().z;
    expect(std::abs(landed - (ground - 2.05)) < .15,
           "it lands where the player would stand (" + std::to_string(ground - landed) + ")");
    expect(plain.resting(), "and rests");
    for (std::uint32_t x = 254; x < 262; ++x)
        for (std::uint32_t y = 254; y < 259; ++y)
            for (std::uint32_t z = ground; z < ground + 4; ++z)
                static_cast<void>(map.clear_voxel(x, y, z));
    for (int tick = 0; tick < 120; ++tick) plain.tick(1.0 / 60, map);
    expect(plain.position().z > landed + 2, "and falls again when the ground under it is dug away");
}

void a_dropped_gun_is_one_rigid_thing() {
    const auto map = field();
    world::ClassicDroppedWeapon gun{{260, 250, ground - 12.0}, {1, 0, 0}, {2, 0, 0}, .6};
    const auto start = gun.position();
    for (int tick = 0; tick < 600; ++tick) gun.tick(1.0 / 60, map, 1);
    const auto end = gun.position();
    expect(end.z > start.z + 10 && end.z < ground, "a released gun falls and lands on the ground");
    expect(end.x > start.x + .4, "carrying the speed it was let go with");
    const auto matrix = gun.transform();
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double scale = std::sqrt(matrix[axis * 4] * matrix[axis * 4] +
                                       matrix[axis * 4 + 1] * matrix[axis * 4 + 1] +
                                       matrix[axis * 4 + 2] * matrix[axis * 4 + 2]);
        expect(std::abs(scale - 1) < 1e-5, "it turns and moves but never stretches");
    }
    const auto rested = gun.position();
    for (int tick = 0; tick < 240; ++tick) gun.tick(1.0 / 60, map, 1);
    expect(distance(rested, gun.position()) < 1e-9, "and lies still once it has landed");
}

} // namespace

int main() {
    const std::array<std::pair<const char*, void (*)()>, 13> tests{{
        {"a_body_ends_lying_down", a_body_ends_lying_down},
        {"it_staggers_before_it_falls", it_staggers_before_it_falls},
        {"a_standing_death_collapses_and_rests", a_standing_death_collapses_and_rests},
        {"a_body_carries_its_momentum", a_body_carries_its_momentum},
        {"a_blow_moves_light_parts_more_than_heavy_ones", a_blow_moves_light_parts_more_than_heavy_ones},
        {"the_fall_follows_the_blow", the_fall_follows_the_blow},
        {"abuse_does_not_break_it", abuse_does_not_break_it},
        {"terrain_holds_and_releases_it", terrain_holds_and_releases_it},
        {"the_starting_pose_is_the_living_one", the_starting_pose_is_the_living_one},
        {"it_does_not_depend_on_the_frame_rate", it_does_not_depend_on_the_frame_rate},
        {"shots_at_corpses_are_cosmetic", shots_at_corpses_are_cosmetic},
        {"the_retail_corpse_still_falls", the_retail_corpse_still_falls},
        {"a_dropped_gun_is_one_rigid_thing", a_dropped_gun_is_one_rigid_thing},
    }};
    int failures = 0;
    for (const auto& [name, body] : tests) {
        try {
            body();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }
    return failures == 0 ? 0 : 1;
}
