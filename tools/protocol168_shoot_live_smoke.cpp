// Live hit-registration smoke: a sniper connection fires the exact packets the
// native client sends (ClientData before ShootPacket in one tick, loop label of
// that ClientData, shot_on_world_update = newest WorldUpdate header) at a
// second connection, aiming at the head of the target as the native client
// presents it (RemoteMotionInterpolator extrapolation). The authoritative
// server's ShootResponse/health deltas tell whether each shot registered.
//
// usage: aos_protocol168_shoot_live_smoke HOST PORT [options]
//   --shots N         shots to fire (default 10)
//   --zoom-lead K     ticks the zoom bit is set before the shot; -1 = always
//                     zoomed (default -1), -2 = never zoomed (hip fire)
//   --strafe          the target strafes left/right every 40 ticks
//   --tool T          18 (sniper, clip 1) or 19 (default) or 6 (rifle)
#include "battlespades/network/live_protocol168_connection.hpp"
#include "battlespades/network/protocol168_players.hpp"
#include "battlespades/network/protocol168_runtime.hpp"
#include "battlespades/network/protocol168_weapons.hpp"
#include "battlespades/world/class_selection.hpp"
#include "battlespades/world/voxel_raycast.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
using namespace battlespades;
using namespace battlespades::network;
using Clock = std::chrono::steady_clock;

std::unique_ptr<Protocol168WorldBootstrap> wait_bootstrap(LiveProtocol168Connection& c) {
    const auto until = Clock::now() + std::chrono::seconds{40};
    while (Clock::now() < until) {
        if (auto b = c.take_bootstrap(); b != nullptr) return b;
        const auto s = c.status();
        if (s.phase == LiveProtocol168Phase::failed ||
            s.phase == LiveProtocol168Phase::disconnected) {
            std::cerr << "bootstrap failed: " << s.error << '\n';
            return nullptr;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return nullptr;
}

double quantize(double v) {
    constexpr double scale{8192.0};
    return std::round(std::clamp(v, -16383.0 / scale, 16383.0 / scale) * scale) / scale;
}

struct Row final {
    world::Vec3 position{};
    world::Vec3 orientation{1.0, 0.0, 0.0};
    world::Vec3 velocity{};
    std::uint8_t input_flags{};
    std::int16_t health{100};
    bool valid{};
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: aos_protocol168_shoot_live_smoke HOST PORT [--shots N] "
                     "[--zoom-lead K] [--strafe] [--tool T]\n";
        return 2;
    }
    const std::string host = argv[1];
    const auto port = static_cast<std::uint16_t>(std::strtoul(argv[2], nullptr, 10));
    int shots_wanted{10};
    int zoom_lead{-1};
    bool strafe{};
    bool check_los{true};
    double arena_range{0.0};
    std::uint8_t tool{19U};
    for (int i = 3; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--shots" && i + 1 < argc) shots_wanted = std::atoi(argv[++i]);
        else if (arg == "--zoom-lead" && i + 1 < argc) zoom_lead = std::atoi(argv[++i]);
        else if (arg == "--strafe") strafe = true;
        else if (arg == "--no-los") check_los = false;
        else if (arg == "--range" && i + 1 < argc) arena_range = std::atof(argv[++i]);
        else if (arg == "--tool" && i + 1 < argc)
            tool = static_cast<std::uint8_t>(std::atoi(argv[++i]));
        else return 2;
    }

    LiveProtocol168Connection shooter;
    Protocol168SessionConfig shooter_config;
    shooter_config.player_name = "ShootSmokeSniper";
    shooter_config.auto_join = false;
    shooter_config.class_id = 1U;
    shooter_config.team = 2U;
    if (!shooter.start(EnetProtocol168Config{host, port, 30'000U}, shooter_config)) {
        std::cerr << shooter.status().error << '\n';
        return 1;
    }
    auto sb = wait_bootstrap(shooter);
    if (sb == nullptr) return 1;

    LiveProtocol168Connection target;
    Protocol168SessionConfig target_config;
    target_config.player_name = "ShootSmokeTarget";
    target_config.class_id = 3U;
    target_config.team = 2U; // friendly_fire must be enabled on the test server
    if (!target.start(EnetProtocol168Config{host, port, 30'000U}, target_config)) {
        std::cerr << target.status().error << '\n';
        return 1;
    }
    auto tb = wait_bootstrap(target);
    if (tb == nullptr) return 1;
    const auto shooter_id = sb->local_player_id;
    const auto target_id = tb->local_player_id;

    const std::array<std::size_t, 4U> options{0U, tool == 19U ? 1U : 0U, 0U, 0U};
    auto selection = world::make_class_selection(1U, options, {});
    if (tool == 6U) selection = world::default_class_selection(0U);
    SetClassLoadoutPacket loadout;
    loadout.player_id = shooter_id;
    loadout.class_id = selection.class_id;
    loadout.instant = true;
    loadout.loadout = selection.loadout;
    loadout.prefabs = selection.prefabs;
    loadout.ugc_tools = selection.ugc_tools;
    if (!shooter.send(encode_packet(loadout)) ||
        !shooter.send(encode_protocol168_new_player_connection(shooter_config))) {
        std::cerr << "class transaction failed\n";
        return 1;
    }
    if (std::ranges::find(selection.loadout, tool) == selection.loadout.end()) {
        std::cerr << "loadout lacks tool " << static_cast<unsigned>(tool) << '\n';
        return 1;
    }

    // --range: admin-teleport both players onto open ground D blocks apart
    // with a clear eye-to-eye line (needs [admin] password on the server).
    std::optional<world::Vec3> shooter_spot;
    std::optional<world::Vec3> target_spot;
    if (arena_range > 0.0) {
        std::uint32_t rng{0x1234567U};
        const auto next_rand = [&rng]() {
            rng ^= rng << 13U; rng ^= rng >> 17U; rng ^= rng << 5U;
            return static_cast<double>(rng % 100000U) / 100000.0;
        };
        const auto& map = *sb->map;
        const auto stand = [&map](double x, double y) -> std::optional<world::Vec3> {
            if (x < 4.0 || y < 4.0 || x > 507.0 || y > 507.0) return std::nullopt;
            const auto z = map.surface_z(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
            if (z < 8U || z >= 236U) return std::nullopt;
            // flat 3x3 footing
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                    if (map.surface_z(static_cast<std::uint32_t>(x + dx),
                                      static_cast<std::uint32_t>(y + dy)) != z) return std::nullopt;
            return world::Vec3{std::floor(x) + 0.5, std::floor(y) + 0.5, static_cast<double>(z) - 2.4};
        };
        for (int attempt = 0; attempt < 200000 && !target_spot; ++attempt) {
            const auto a = stand(8.0 + next_rand() * 496.0, 8.0 + next_rand() * 496.0);
            if (!a) continue;
            const double heading = next_rand() * 6.283185307;
            const auto b = stand(a->x + std::cos(heading) * arena_range,
                                 a->y + std::sin(heading) * arena_range);
            if (!b) continue;
            bool clear = true;
            for (const double lift : {0.0, -0.8}) { // eye line and a little above
                const double dx = b->x - a->x, dy = b->y - a->y, dz = b->z - a->z;
                const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
                const auto blocked = world::trace_first_solid(
                    map, {static_cast<float>(a->x), static_cast<float>(a->y), static_cast<float>(a->z + lift)},
                    {static_cast<float>(dx / len), static_cast<float>(dy / len), static_cast<float>(dz / len)},
                    static_cast<float>(len));
                if (blocked) clear = false;
            }
            if (!clear) continue;
            shooter_spot = a;
            target_spot = b;
        }
        if (!target_spot) {
            std::cerr << "no open arena found at range " << arena_range << '\n';
            return 1;
        }
        std::cout << "arena shooter=(" << shooter_spot->x << ',' << shooter_spot->y << ','
                  << shooter_spot->z << ") target=(" << target_spot->x << ',' << target_spot->y
                  << ',' << target_spot->z << ")\n";
        for (auto* c : {&shooter, &target}) {
            ChatMessagePacket login;
            login.player_id = c == &shooter ? shooter_id : target_id;
            login.value = "/admin shoottestpass99";
            static_cast<void>(c->send(encode_packet(login)));
        }
    }
    int tp_cooldown_shooter{};
    int tp_cooldown_target{};
    const auto teleport = [&](LiveProtocol168Connection& c, std::uint8_t id,
                              const world::Vec3& spot) {
        ChatMessagePacket tp;
        tp.player_id = id;
        tp.value = "/tp " + std::to_string(spot.x) + " " + std::to_string(spot.y) + " " +
                   std::to_string(spot.z);
        static_cast<void>(c.send(encode_packet(tp)));
    };

    std::int32_t shooter_loop{static_cast<std::int32_t>(sb->next_client_loop_count)};
    std::int32_t target_loop{static_cast<std::int32_t>(tb->next_client_loop_count)};
    std::int32_t latest_world_loop{};
    Row self_row;
    Row target_row;
    RemoteMotionInterpolator target_motion;
    bool target_motion_ready{};
    bool target_alive{};
    bool shooter_alive{};

    int shots{};
    int hits{};
    int heads{};
    int kills{};
    int skipped_los{};
    int feedback_seen{};
    bool los_ok{};
    int in_clip{tool == 18U ? 1 : tool == 19U ? 5 : 10};
    const int clip_size = in_clip;
    const int interval_ticks = tool == 18U ? 64 : tool == 19U ? 70 : 34;
    const int reload_ticks = tool == 18U ? 130 : tool == 19U ? 190 : 160;
    int cooldown{180};
    int zoom_ticks_left{};
    int unzoomed_ticks{};
    std::optional<int> pending_shot_tick;
    int pending_health_before{};
    double pending_range{};
    bool pending_response{};
    std::int64_t tick{};
    double max_error{};
    double error_sum{};

    const auto deadline = Clock::now() + std::chrono::seconds{60 + shots_wanted * 8};
    auto next = Clock::now();
    while (Clock::now() < deadline && (shots < shots_wanted || pending_shot_tick)) {
        ++tick;
        // ---- inbound ----
        for (const auto& packet : target.take_inbound()) {
            if (packet.empty()) continue;
            const auto id = std::to_integer<std::uint8_t>(packet.front());
            if (id == CreatePlayerPacket::id) static_cast<void>(tb->roster.apply(packet));
            if (id == ShootFeedbackPacket::id) ++feedback_seen;
        }
        for (const auto& packet : shooter.take_inbound()) {
            if (packet.empty()) continue;
            const auto id = std::to_integer<std::uint8_t>(packet.front());
            if (id == CreatePlayerPacket::id) {
                const auto decoded = decode_create_player(packet);
                if (decoded.packet.has_value()) {
                    if (decoded.packet->player_id == target_id) {
                        target_alive = !decoded.packet->dead;
                        target_motion_ready = false;
                    }
                    if (decoded.packet->player_id == shooter_id) shooter_alive = true;
                }
            } else if (id == KillActionPacket::id && packet.size() >= 3U) {
                const auto victim = std::to_integer<std::uint8_t>(packet[1U]);
                if (victim == target_id) {
                    target_alive = false;
                    if (pending_shot_tick) ++kills;
                }
            } else if (id == ShootResponsePacket::id) {
                const auto decoded = decode_weapon_packet(packet);
                if (decoded) {
                    if (const auto* r = std::get_if<ShootResponsePacket>(&*decoded.packet);
                        r != nullptr && r->damage_by == shooter_id && pending_shot_tick) {
                        pending_response = true;
                    }
                }
            } else if (id == 2U) {
                const auto update = decode_world_update_weapon_rows(packet);
                if (!update) continue;
                latest_world_loop = std::max(latest_world_loop, update.loop_count);
                for (const auto& row : update.rows) {
                    Row* dst = row.player_id == shooter_id ? &self_row
                             : row.player_id == target_id  ? &target_row
                                                           : nullptr;
                    if (dst == nullptr) continue;
                    dst->position = {row.position[0U], row.position[1U], row.position[2U]};
                    dst->orientation = {row.orientation[0U], row.orientation[1U],
                                        row.orientation[2U]};
                    dst->velocity = {row.velocity[0U], row.velocity[1U], row.velocity[2U]};
                    dst->input_flags = row.input_flags;
                    dst->health = row.health;
                    dst->valid = true;
                    if (row.player_id == target_id) {
                        if (row.health > 0) target_alive = true;
                        RemoteMotionSample sample;
                        sample.position = dst->position;
                        sample.orientation = dst->orientation;
                        sample.velocity = dst->velocity;
                        sample.input_flags = dst->input_flags;
                        sample.class_id = 3U;
                        if (!target_motion_ready) {
                            target_motion.reset(sample);
                            target_motion_ready = true;
                        } else {
                            target_motion.push(sample, 2.0 / 60.0);
                        }
                    }
                    if (row.player_id == shooter_id && row.health > 0) shooter_alive = true;
                }
            }
        }
        if (target_motion_ready) target_motion.tick(1.0 / 60.0, sb->map.get(), 1.0);
        if (target_spot && tick > 60) {
            if (tp_cooldown_shooter > 0) --tp_cooldown_shooter;
            if (tp_cooldown_target > 0) --tp_cooldown_target;
            const auto far = [](const Row& r, const world::Vec3& s, double limit) {
                return !r.valid || std::hypot(r.position.x - s.x, r.position.y - s.y) > limit ||
                       std::abs(r.position.z - s.z) > 3.0;
            };
            if (shooter_alive && tp_cooldown_shooter == 0 && far(self_row, *shooter_spot, 1.0)) {
                teleport(shooter, shooter_id, *shooter_spot);
                tp_cooldown_shooter = 90;
                cooldown = std::max(cooldown, 90);
            }
            if (target_alive && tp_cooldown_target == 0 && far(target_row, *target_spot, 6.0)) {
                teleport(target, target_id, *target_spot);
                tp_cooldown_target = 90;
                cooldown = std::max(cooldown, 90);
            }
        }

        // ---- resolve the previous shot after 30 ticks ----
        if (pending_shot_tick && tick - *pending_shot_tick >= 30) {
            const int delta = pending_health_before - static_cast<int>(target_row.health);
            const bool hit = pending_response || delta > 0 || !target_alive;
            if (hit) ++hits;
            if (delta >= 80 || (!target_alive && pending_health_before <= 85)) ++heads;
            std::cout << "shot " << shots << " range=" << pending_range << " response=" << pending_response
                      << " health " << pending_health_before << "->" << target_row.health
                      << " alive=" << target_alive << '\n';
            pending_shot_tick.reset();
            pending_response = false;
        }

        // ---- target input ----
        {
            ClientDataPacket input;
            input.loop_count = target_loop++;
            input.player_id = target_id;
            input.opaque_state = protocol168_client_data_opaque_state(input.loop_count);
            const auto* me = tb->roster.player(target_id);
            input.tool_id = me != nullptr && !me->loadout.empty() ? me->loadout.front() : 0U;
            world::Vec3 face{-1.0, 0.0, 0.0};
            if (self_row.valid && target_row.valid) {
                const double dx = self_row.position.x - target_row.position.x;
                const double dy = self_row.position.y - target_row.position.y;
                const double len = std::hypot(dx, dy);
                if (len > 1e-6) face = {dx / len, dy / len, 0.0};
            }
            input.orientation = {static_cast<float>(quantize(face.x)),
                                 static_cast<float>(quantize(face.y)), 0.0F};
            input.movement_flags =
                !los_ok ? std::uint8_t{0x01U}
                : strafe ? static_cast<std::uint8_t>(((tick / 40) % 2 == 0) ? 0x04U : 0x08U)
                         : std::uint8_t{0U};
            input.action_flags = 0x10U;
            static_cast<void>(target.send(encode_packet(input)));
        }

        // ---- shooter: aim, ClientData, then ShootPacket (native order) ----
        world::Vec3 aim{-1.0, 0.0, 0.0};
        bool can_aim = self_row.valid && target_motion_ready && target_alive && shooter_alive;
        world::Vec3 eye = self_row.position;
        world::Vec3 head{};
        if (can_aim) {
            head = target_motion.sample().position;
            const double dx = head.x - eye.x;
            const double dy = head.y - eye.y;
            const double dz = head.z - eye.z;
            const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
            aim = {quantize(dx / len), quantize(dy / len), quantize(dz / len)};
            const auto blocker = world::trace_first_solid(
                *sb->map,
                {static_cast<float>(eye.x), static_cast<float>(eye.y),
                 static_cast<float>(eye.z)},
                {static_cast<float>(dx / len), static_cast<float>(dy / len),
                 static_cast<float>(dz / len)},
                static_cast<float>(len));
            los_ok = target_spot.has_value() || !blocker.has_value() || len < 2.5;
        }
        if (cooldown > 0) --cooldown;
        bool fire = can_aim && cooldown == 0 && !pending_shot_tick && shots < shots_wanted;
        if (fire) {
            const double len = std::sqrt((head.x - eye.x) * (head.x - eye.x) +
                                         (head.y - eye.y) * (head.y - eye.y) +
                                         (head.z - eye.z) * (head.z - eye.z));
            const auto blocked = world::trace_first_solid(
                *sb->map,
                {static_cast<float>(eye.x), static_cast<float>(eye.y),
                 static_cast<float>(eye.z)},
                {static_cast<float>(aim.x), static_cast<float>(aim.y),
                 static_cast<float>(aim.z)},
                static_cast<float>(len));
            if (check_los && blocked.has_value()) {
                ++skipped_los;
                cooldown = 60;
                fire = false;
                if (skipped_los == 1 || skipped_los % 10 == 0) {
                    std::cout << "no line of sight cell=(" << blocked->cell.x << ','
                              << blocked->cell.y << ',' << blocked->cell.z << ") eye=(" << eye.x << ',' << eye.y << ','
                              << eye.z << ") target=(" << head.x << ',' << head.y << ','
                              << head.z << ")\n";
                }
            }
        }
        // zoom bit schedule
        if (zoom_lead >= 0 && cooldown == zoom_lead && can_aim) zoom_ticks_left = zoom_lead + 20;
        bool zoomed = zoom_lead == -1 || (zoom_lead >= 0 && zoom_ticks_left > 0);
        if (zoom_ticks_left > 0) --zoom_ticks_left;
        if (fire && zoom_lead == 0) zoomed = true;
        // Native clip-1 sniper: the round that empties the magazine drops the
        // sight on the next tick and the reload keeps it down (update_reload_aim).
        if (unzoomed_ticks > 0) {
            --unzoomed_ticks;
            zoomed = false;
        }

        ClientDataPacket input;
        input.loop_count = shooter_loop++;
        input.player_id = shooter_id;
        input.tool_id = tool;
        input.opaque_state = protocol168_client_data_opaque_state(input.loop_count);
        input.orientation = {static_cast<float>(aim.x), static_cast<float>(aim.y),
                             static_cast<float>(aim.z)};
        input.movement_flags = (!los_ok && can_aim) ? std::uint8_t{0x01U} : std::uint8_t{0U};
        if (input.movement_flags != 0U) {
            // Walk toward the target horizontally until it is in view.
            const double hx = head.x - eye.x;
            const double hy = head.y - eye.y;
            const double hl = std::hypot(hx, hy);
            if (hl > 1e-6) {
                input.orientation = {static_cast<float>(quantize(hx / hl)),
                                     static_cast<float>(quantize(hy / hl)), 0.0F};
            }
        }
        input.action_flags = static_cast<std::uint8_t>(0x10U | (fire ? 0x01U : 0U) |
                                                       (zoomed ? 0x04U : 0U));
        static_cast<void>(shooter.send(encode_packet(input)));
        if (fire) {
            ShootPacket shot;
            shot.loop_count = input.loop_count;
            shot.shooter_id = shooter_id;
            shot.shot_on_world_update = latest_world_loop;
            shot.position = {static_cast<float>(eye.x), static_cast<float>(eye.y),
                             static_cast<float>(eye.z)};
            shot.orientation = input.orientation;
            shot.damage = 3;
            shot.penetration = 2;
            shot.seed = static_cast<std::uint8_t>(1U + (tick * 37U) % 255U);
            static_cast<void>(shooter.send(encode_packet(shot)));
            ++shots;
            pending_shot_tick = static_cast<int>(tick);
            pending_health_before = target_row.health;
            pending_range = std::sqrt((head.x - eye.x) * (head.x - eye.x) +
                                      (head.y - eye.y) * (head.y - eye.y) +
                                      (head.z - eye.z) * (head.z - eye.z));
            // Presentation error: extrapolated vs newest authoritative row.
            const double ex = head.x - target_row.position.x;
            const double ey = head.y - target_row.position.y;
            const double ez = head.z - target_row.position.z;
            const double err = std::sqrt(ex * ex + ey * ey + ez * ez);
            max_error = std::max(max_error, err);
            error_sum += err;
            cooldown = interval_ticks;
            if (--in_clip == 0) {
                in_clip = clip_size;
                cooldown = reload_ticks;
                if (clip_size == 1) unzoomed_ticks = reload_ticks - 5;
                static_cast<void>(shooter.send(encode_packet(
                    WeaponReloadPacket{shooter_id, tool, false})));
            }
        }
        // keep the target supplied with health: respawned targets restart at 100
        next += std::chrono::microseconds{16'667};
        std::this_thread::sleep_until(next);
    }
    shooter.stop();
    target.stop();
    std::cout << "RESULT tool=" << static_cast<unsigned>(tool) << " zoom_lead=" << zoom_lead
              << " strafe=" << strafe << " shots=" << shots << " hits=" << hits
              << " heads=" << heads << " kills=" << kills << " los_skips=" << skipped_los << " accepted_feedback=" << feedback_seen
              << " mean_present_vs_row=" << (shots ? error_sum / shots : 0.0)
              << " max_present_vs_row=" << max_error << '\n';
    return shots > 0 ? 0 : 1;
}
