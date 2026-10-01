// Shooting contract between the native client and the authoritative server.
//
// 1. A zoomed sniper shot at a head 120 blocks away survives the wire
//    (ShootPacket floats, ClientData-grid orientation) with sub-centimetre
//    error, and carries the ClientData label of its own tick plus the newest
//    WorldUpdate header loop, exactly as the frontend fills the context.
// 2. The view the client presents of a strafing peer (snap to the newest row,
//    extrapolate with the native mover) is the body the server's lag
//    compensation rewinds to (rewind = one RTT from arrival, view_delay 0):
//    the presented target and the rewound hitbox agree to well inside a head.
//    A buffered interpolator that presents peers one row interval late would
//    not, which is why it must never come back.
// 3. The client's own spread for a zoomed sniper is zero (accuracy_zoom 0),
//    the value the Beta 0.2 server resolves too; hip fire is not.
#include "battlespades/network/protocol168_players.hpp"
#include "battlespades/network/protocol168_weapon_action_adapter.hpp"
#include "battlespades/network/protocol168_weapons.hpp"
#include "battlespades/world/player_movement.hpp"
#include "battlespades/world/vxl_map.hpp"
#include "battlespades/world/weapon_catalog.hpp"
#include "battlespades/world/weapon_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <vector>

namespace {

using namespace battlespades;
using world::Vec3;

int failures{};
bool g_trace{};

void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

double quantize(double value) {
    // TutorialWorldSession::quantized_orientation_component (live mover grid).
    constexpr double scale{8192.0};
    return std::round(std::clamp(value, -16383.0 / scale, 16383.0 / scale) * scale) / scale;
}

std::shared_ptr<world::VxlMap> flat_world(std::uint32_t surface) {
    std::vector<std::byte> bytes;
    bytes.reserve(static_cast<std::size_t>(world::VxlMap::width) * world::VxlMap::depth * 4U);
    for (std::size_t column{};
         column < static_cast<std::size_t>(world::VxlMap::width) * world::VxlMap::depth;
         ++column) {
        bytes.push_back(std::byte{0U});
        bytes.push_back(std::byte{1U});
        bytes.push_back(std::byte{0U});
        bytes.push_back(std::byte{0U});
    }
    auto loaded = world::VxlMap::load(bytes);
    expect(static_cast<bool>(loaded), "synthetic world must parse");
    auto map = std::make_shared<world::VxlMap>(std::move(*loaded.map));
    for (std::uint32_t y{176U}; y <= 336U; ++y) {
        for (std::uint32_t x{176U}; x <= 336U; ++x) {
            static_cast<void>(map->set_voxel(x, y, surface, world::VxlColor{0x80U, 0x80U, 0x80U}));
        }
    }
    return map;
}

double distance_to_ray(const Vec3& origin, const Vec3& direction, const Vec3& point) {
    const Vec3 rel{point.x - origin.x, point.y - origin.y, point.z - origin.z};
    const double t = rel.x * direction.x + rel.y * direction.y + rel.z * direction.z;
    const Vec3 closest{origin.x + direction.x * t, origin.y + direction.y * t,
                       origin.z + direction.z * t};
    return std::hypot(point.x - closest.x, point.y - closest.y, point.z - closest.z);
}

void sniper_shot_survives_the_wire() {
    const Vec3 eye{100.5, 200.5, 50.25};
    const Vec3 head{196.25, 272.75, 48.0}; // ~120 blocks
    const double dx = head.x - eye.x, dy = head.y - eye.y, dz = head.z - eye.z;
    const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
    expect(len > 119.0 && len < 121.0, "fixture must be a 120-block shot");
    const Vec3 aim{quantize(dx / len), quantize(dy / len), quantize(dz / len)};

    world::WeaponAction action;
    action.kind = world::WeaponActionKind::hitscan;
    action.tool_id = 18U;
    action.seed = 77U;
    network::WeaponActionWireContext context;
    context.loop_count = 4242;          // the ClientData sent earlier this tick
    context.shot_on_world_update = 9001; // newest WorldUpdate header loop
    context.player_id = 5U;
    context.position = {static_cast<float>(eye.x), static_cast<float>(eye.y),
                        static_cast<float>(eye.z)};
    context.orientation = {static_cast<float>(aim.x), static_cast<float>(aim.y),
                           static_cast<float>(aim.z)};
    const auto encoded = network::encode_weapon_action(action, context);
    expect(encoded.packets.size() == 1U, "one trigger pull sends exactly one ShootPacket");
    if (encoded.packets.size() != 1U) return;
    const auto decoded = network::decode_weapon_packet(encoded.packets.front());
    const auto* shot =
        decoded ? std::get_if<network::ShootPacket>(&*decoded.packet) : nullptr;
    expect(shot != nullptr, "the hitscan action must encode a ShootPacket(6)");
    if (shot == nullptr) return;
    expect(shot->loop_count == 4242 && shot->shot_on_world_update == 9001 &&
               shot->shooter_id == 5U && shot->seed == 77U && !shot->secondary,
           "ShootPacket must carry its own ClientData label, the WorldUpdate loop and seed");
    const Vec3 origin{shot->position[0U], shot->position[1U], shot->position[2U]};
    Vec3 direction{shot->orientation[0U], shot->orientation[1U], shot->orientation[2U]};
    const double norm = std::hypot(direction.x, direction.y, direction.z);
    direction = {direction.x / norm, direction.y / norm, direction.z / norm};
    const double miss = distance_to_ray(origin, direction, head);
    // Scout head half-extent is 0.3 blocks; the wire must cost far less.
    expect(miss < 0.03, "wire quantisation must not move a 120-block shot off the head");
}

void zoomed_sniper_has_no_spread() {
    for (const std::uint8_t tool : {std::uint8_t{18U}, std::uint8_t{19U}}) {
        const auto* weapon = world::find_weapon_definition(tool);
        expect(weapon != nullptr, "sniper definition must exist");
        if (weapon == nullptr) continue;
        expect(weapon->retail.aim.accuracy_zoom.has_value() &&
                   *weapon->retail.aim.accuracy_zoom == 0.0,
               "zoomed sniper accuracy must be retail accuracy_zoom 0 (straight shot)");
        expect(weapon->retail.aim.accuracy.value_or(0.0) > 0.0,
               "hip-fired sniper keeps its class accuracy");
    }
}

struct ServerSample final {
    int tick{};
    Vec3 position{};
};

/**
 * Server simulates a strafing peer at 60 Hz and publishes a row every
 * `interval` ticks. Rows reach the shooter `down` ticks later; a shot reaches
 * the server `up` ticks after it was fired. Returns the worst distance between
 * the presented target at the firing tick and the server's rewound body.
 */
double worst_presentation_error(const world::VxlMap& map, int down, int up, int interval,
                                int presentation_delay_rows) {
    world::PlayerMovementState server_body;
    server_body.position = {256.5, 256.5, 200.0 - 2.25};
    server_body.orientation = {1.0, 0.0, 0.0};
    const auto movement = world::movement_config_for_class(0U);
    // settle onto the ground
    for (int i = 0; i < 120; ++i) {
        static_cast<void>(world::step_player(server_body, {}, &map, 1.0 / 60.0, movement));
    }
    std::vector<ServerSample> history;
    struct Row final {
        int arrive{};
        network::RemoteMotionSample sample;
    };
    std::deque<Row> in_flight;
    std::deque<network::RemoteMotionSample> received;
    network::RemoteMotionInterpolator presented;
    bool started{};
    double worst{};
    for (int tick = 0; tick < 1200; ++tick) {
        world::PlayerInputState input;
        // Change direction every 20..47 ticks: the hardest case for extrapolation.
        const int phase = (tick / 37) % 3;
        input.left = phase == 0;
        input.right = phase == 1;
        input.sprint = (tick / 111) % 2 == 0;
        static_cast<void>(world::step_player(server_body, input, &map, 1.0 / 60.0, movement));
        history.push_back({tick, server_body.position});
        if (tick % interval == 0) {
            network::RemoteMotionSample sample;
            sample.position = server_body.position;
            sample.orientation = server_body.orientation;
            sample.velocity = server_body.velocity;
            sample.input_flags = static_cast<std::uint8_t>((input.left ? 0x04U : 0U) |
                                                           (input.right ? 0x08U : 0U) |
                                                           (input.sprint ? 0x80U : 0U));
            in_flight.push_back({tick + down, sample});
        }
        // client frame `tick`: receive, then advance (frontend order)
        while (!in_flight.empty() && in_flight.front().arrive <= tick) {
            received.push_back(in_flight.front().sample);
            in_flight.pop_front();
            if (received.size() > static_cast<std::size_t>(presentation_delay_rows)) {
                const auto& row =
                    received[received.size() - 1U - static_cast<std::size_t>(presentation_delay_rows)];
                if (!started) {
                    presented.reset(row);
                    started = true;
                } else {
                    presented.push(row, interval / 60.0);
                }
            }
        }
        if (!started) continue;
        presented.tick(1.0 / 60.0, &map, 1.0);
        if (tick < 240) continue;
        // Shot fired now arrives at tick+up; server rewinds RTT = down+up.
        const int rewind_tick = tick + up - (down + up);
        const auto it = std::ranges::find_if(
            history, [rewind_tick](const ServerSample& s) { return s.tick == rewind_tick; });
        if (it == history.end()) continue;
        const auto& p = presented.sample().position;
        const double e = std::hypot(p.x - it->position.x, p.y - it->position.y,
                                    p.z - it->position.z);
        if (g_trace && e > 0.25) {
            std::cout << "  tick " << tick << " dx=" << p.x - it->position.x
                      << " dy=" << p.y - it->position.y << " dz=" << p.z - it->position.z << '\n';
        }
        worst = std::max(worst, e);
    }
    return worst;
}

void presented_peer_matches_server_rewind() {
    const auto map = flat_world(200U);
    for (const int one_way : {1, 3, 6}) {
        const double error = worst_presentation_error(*map, one_way, one_way, 2, 0);
        std::cout << "one-way " << one_way << " ticks: worst presented-vs-rewound "
                  << error << " blocks\n";
        // Head half-extent 0.3 blocks: the presented peer must be the rewound
        // hitbox even through direction changes.
        expect(error < 0.3, "extrapolated peer must sit inside the rewound head box");
    }
    // A one-row interpolation buffer presents a sprinting peer behind the
    // body the server rewinds to.
    const double buffered = worst_presentation_error(*map, 3, 3, 2, 2);
    std::cout << "two-row buffered interpolation: worst " << buffered << " blocks\n";
    expect(buffered > 0.3,
           "a buffered interpolator would draw ghosts outside the rewound hitbox");
}

} // namespace

int main(int argc, char**) {
    g_trace = argc > 1;
    sniper_shot_survives_the_wire();
    zoomed_sniper_has_no_spread();
    presented_peer_matches_server_rewind();
    if (failures != 0) {
        std::cerr << failures << " shooting contract check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "shooting contract tests passed\n";
    return EXIT_SUCCESS;
}
