#include "battlespades/network/live_protocol168_connection.hpp"
#include "battlespades/network/protocol168_players.hpp"
#include "battlespades/network/protocol168_reconciliation.hpp"
#include "battlespades/network/protocol168_runtime.hpp"
#include "battlespades/network/protocol168_terrain.hpp"
#include "battlespades/network/protocol168_tool_actions.hpp"
#include "battlespades/network/protocol168_weapons.hpp"
#include "battlespades/world/tutorial_session.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using battlespades::world::TutorialAction;
using battlespades::world::TutorialWorldSession;
using battlespades::world::Vec3;

struct PredictedFrame final {
    Vec3 position{};
    Vec3 velocity{};
    Vec3 encoded_orientation{};
    std::uint8_t expected_server_flags{};
    bool airborne{};
    bool wade{};
    bool crouch{};
    double climb_timer{};
};

/**
 * Deterministic ordered delay line used to exercise prediction with realistic
 * RTT without installing a machine-wide packet filter. ENet reliable traffic
 * remains ordered: jitter changes delivery spacing but never reorders packets.
 */
class PacketDelayLine final {
public:
    using clock = std::chrono::steady_clock;
    using Packet = std::vector<std::byte>;

    PacketDelayLine(std::uint32_t base_delay_ms, std::uint32_t jitter_ms)
        : base_delay_ms_{base_delay_ms}, jitter_ms_{jitter_ms} {}

    void push(Packet packet, clock::time_point now) {
        const auto jitter = deterministic_jitter();
        const auto delay = std::max<std::int64_t>(
            0, static_cast<std::int64_t>(base_delay_ms_) + jitter);
        auto due = now + std::chrono::milliseconds{delay};
        if (last_due_.has_value() && due < *last_due_) {
            due = *last_due_;
        }
        last_due_ = due;
        packets_.push_back(DelayedPacket{due, std::move(packet)});
    }

    [[nodiscard]] std::vector<Packet> take_due(clock::time_point now,
                                                std::size_t budget) {
        std::vector<Packet> ready;
        ready.reserve(std::min(budget, packets_.size()));
        while (!packets_.empty() && ready.size() < budget &&
               packets_.front().due <= now) {
            ready.push_back(std::move(packets_.front().packet));
            packets_.pop_front();
        }
        return ready;
    }

    [[nodiscard]] std::size_t size() const noexcept { return packets_.size(); }

private:
    struct DelayedPacket final {
        clock::time_point due{};
        Packet packet;
    };

    [[nodiscard]] std::int64_t deterministic_jitter() noexcept {
        if (jitter_ms_ == 0U) return 0;
        // A fixed LCG makes every parity trace reproducible. Use the high bits,
        // then center the result on zero for a bounded +/- jitter window.
        random_state_ = random_state_ * 6364136223846793005ULL +
                        1442695040888963407ULL;
        const auto width = static_cast<std::uint64_t>(jitter_ms_) * 2ULL + 1ULL;
        return static_cast<std::int64_t>((random_state_ >> 32U) % width) -
               static_cast<std::int64_t>(jitter_ms_);
    }

    std::uint32_t base_delay_ms_{};
    std::uint32_t jitter_ms_{};
    std::uint64_t random_state_{0xA05B168ULL};
    std::optional<clock::time_point> last_due_;
    std::deque<DelayedPacket> packets_;
};

[[nodiscard]] std::optional<std::uint32_t>
parse_delay_option(std::string_view argument, std::string_view prefix) noexcept {
    if (!argument.starts_with(prefix)) return std::nullopt;
    const auto value = argument.substr(prefix.size());
    if (value.empty()) return std::uint32_t{0U};
    char* end{};
    const std::string owned{value};
    const auto parsed = std::strtoul(owned.c_str(), &end, 10);
    if (end == owned.c_str() || *end != '\0' || parsed > 10'000UL) {
        return std::uint32_t{0U};
    }
    return static_cast<std::uint32_t>(parsed);
}

[[nodiscard]] double length(Vec3 value) noexcept {
    return std::hypot(value.x, value.y, value.z);
}

[[nodiscard]] std::optional<std::array<std::int16_t, 3U>>
supported_build_cell(const TutorialWorldSession& session) noexcept {
    const auto& player = session.player();
    const auto& map = session.map();
    const auto horizontal = std::hypot(player.orientation.x, player.orientation.y);
    if (horizontal <= 1.0e-6) return std::nullopt;
    const double forward_x = player.orientation.x / horizontal;
    const double forward_y = player.orientation.y / horizontal;
    const double side_x = -forward_y;
    const double side_y = forward_x;
    for (double distance : {4.0, 5.0, 6.0, 3.0}) {
        for (double lateral : {0.0, -1.0, 1.0, -2.0, 2.0}) {
            const auto x = static_cast<std::int32_t>(std::floor(
                player.position.x + forward_x * distance + side_x * lateral));
            const auto y = static_cast<std::int32_t>(std::floor(
                player.position.y + forward_y * distance + side_y * lateral));
            if (x <= 0 || y <= 0 || x >= 511 || y >= 511) continue;
            const auto surface = static_cast<std::int32_t>(
                map.surface_z(static_cast<std::uint32_t>(x),
                              static_cast<std::uint32_t>(y)));
            const auto z = surface - 1;
            if (z <= 0 || z >= 238) {
                continue;
            }
            const auto cell_x = static_cast<std::uint32_t>(x);
            const auto cell_y = static_cast<std::uint32_t>(y);
            const auto cell_z = static_cast<std::uint32_t>(z);
            if (map.solid(cell_x, cell_y, cell_z) ||
                !map.solid(cell_x, cell_y, cell_z + 1U)) {
                continue;
            }
            return std::array<std::int16_t, 3U>{
                static_cast<std::int16_t>(x), static_cast<std::int16_t>(y),
                static_cast<std::int16_t>(z)};
        }
    }
    return std::nullopt;
}

void set_inputs(TutorialWorldSession& session, std::uint32_t frame,
                bool jump_enabled, bool straight_walk,
                bool gentle_turn) noexcept {
    if (gentle_turn) {
        session.set_action_held(TutorialAction::forward, frame >= 30U);
        session.set_action_held(TutorialAction::sprint, false);
        session.set_action_held(TutorialAction::jump, false);
        session.set_action_held(TutorialAction::crouch, false);
        session.set_action_held(TutorialAction::right, false);
        // Slow, ordinary mouse motion is the user-visible failure case. One
        // count is 0.1 degree in retail, so this traces a smooth S-turn
        // without the stress profile's full revolutions or rapid reversals.
        if (frame >= 60U && frame < 180U) {
            session.apply_look_delta(1.0, 0.0);
        } else if (frame >= 180U && frame < 420U) {
            session.apply_look_delta(-1.0, 0.0);
        } else if (frame >= 420U && frame < 540U) {
            session.apply_look_delta(1.0, 0.0);
        }
        return;
    }
    if (straight_walk) {
        session.set_action_held(TutorialAction::forward, frame >= 30U);
        session.set_action_held(TutorialAction::sprint, false);
        session.set_action_held(TutorialAction::jump, false);
        session.set_action_held(TutorialAction::crouch, false);
        session.set_action_held(TutorialAction::right, false);
        return;
    }
    // Exercise edges while the player is still moving, before a random spawn
    // route is likely to meet a wall. The later strafe/crouch pass covers the
    // remaining locomotion bits and a second moving jump.
    const bool forward = frame >= 30U && frame < 360U;
    const bool sprint = frame >= 120U && frame < 300U;
    const bool jump =
        jump_enabled && ((frame >= 90U && frame < 108U) ||
                         (frame >= 450U && frame < 468U));
    const bool crouch = frame >= 330U && frame < 390U;
    const bool right = frame >= 390U && frame < 570U;
    session.set_action_held(TutorialAction::forward, forward);
    session.set_action_held(TutorialAction::sprint, sprint);
    session.set_action_held(TutorialAction::jump, jump);
    session.set_action_held(TutorialAction::crouch, crouch);
    session.set_action_held(TutorialAction::right, right);
    // Normal play does not turn by five mouse counts for one second and stop.
    // Stress the camera/movement boundary with rapid reversals and more than a
    // full revolution while W/sprint remain held. This still crosses negative
    // diagonal orientation (the signed-3.13 decoder regression), but also
    // exposes a stale-yaw phase or visually mirrored movement basis.
    if (frame >= 150U && frame < 240U) {
        session.apply_look_delta(40.0, 0.0); // +360 degrees in 1.5 seconds
    } else if (frame >= 240U && frame < 300U) {
        session.apply_look_delta(-60.0, 0.0); // reverse to the starting yaw
    } else if (frame >= 405U && frame < 465U) {
        session.apply_look_delta((frame & 1U) == 0U ? 75.0 : -50.0, 0.0);
    }
}

[[nodiscard]] bool is_runtime_packet(std::uint8_t id) noexcept {
    using namespace battlespades::network;
    return id == SetHpPacket::id || id == DestroyEntityPacket::id ||
           id == CreateEntityPacket::id ||
           id == CreateAmbientSoundPacket::id || id == PlaySoundPacket::id ||
           id == PlayAmbientSoundPacket::id || id == PlayMusicPacket::id ||
           id == StopMusicPacket::id || id == PlayerLeftPacket::id ||
           id == PickPickupPacket::id || id == DisplayCountdownPacket::id;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: aos_protocol168_movement_parity HOST PORT "
                     "[SECONDS] [TRACE.csv] [--require-peer] [--class=N] "
                     "[--no-player-collision] [--no-jump] [--no-build] "
                     "[--straight] [--gentle-turn] [--uplink-ms=N] "
                     "[--downlink-ms=N] [--jitter-ms=N] [--team=2|3] "
                     "[--inbound-budget=N] [--chase-peer]\n";
        return 2;
    }
    const auto parsed_port = std::strtoul(argv[2], nullptr, 10);
    const auto seconds = argc >= 4 ? std::strtoul(argv[3], nullptr, 10) : 12UL;
    if (parsed_port == 0UL || parsed_port > 65'535UL || seconds == 0UL) return 2;
    std::optional<std::string> trace_path;
    bool require_peer{};
    bool suppress_player_collision{};
    bool suppress_jump{};
    bool suppress_build{};
    bool straight_walk{};
    bool gentle_turn{};
    bool chase_peer{};
    std::uint32_t uplink_delay_ms{};
    std::uint32_t downlink_delay_ms{};
    std::uint32_t jitter_ms{};
    std::size_t inbound_budget{16U};
    std::uint8_t requested_team{2U};
    std::uint8_t requested_class{1U};
    for (int index{4}; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--require-peer") {
            require_peer = true;
        } else if (argument == "--no-player-collision") {
            suppress_player_collision = true;
        } else if (argument == "--no-jump") {
            suppress_jump = true;
        } else if (argument == "--no-build") {
            suppress_build = true;
        } else if (argument == "--straight") {
            straight_walk = true;
        } else if (argument == "--gentle-turn") {
            gentle_turn = true;
        } else if (argument == "--chase-peer") {
            chase_peer = true;
        } else if (argument.starts_with("--class=")) {
            const auto parsed = std::strtoul(argv[index] + 8, nullptr, 10);
            if (parsed > 17UL) return 2;
            requested_class = static_cast<std::uint8_t>(parsed);
        } else if (argument.starts_with("--team=")) {
            const auto parsed = std::strtoul(argv[index] + 7, nullptr, 10);
            if (parsed != 2UL && parsed != 3UL) return 2;
            requested_team = static_cast<std::uint8_t>(parsed);
        } else if (argument.starts_with("--inbound-budget=")) {
            const auto parsed = std::strtoul(argv[index] + 17, nullptr, 10);
            if (parsed == 0UL || parsed > 16'384UL) return 2;
            inbound_budget = static_cast<std::size_t>(parsed);
        } else if (const auto uplink = parse_delay_option(argument, "--uplink-ms=");
                   uplink.has_value()) {
            uplink_delay_ms = *uplink;
        } else if (const auto downlink = parse_delay_option(argument, "--downlink-ms=");
                   downlink.has_value()) {
            downlink_delay_ms = *downlink;
        } else if (const auto jitter = parse_delay_option(argument, "--jitter-ms=");
                   jitter.has_value()) {
            jitter_ms = *jitter;
        } else if (!trace_path.has_value()) {
            trace_path = argv[index];
        } else {
            return 2;
        }
    }

    using namespace battlespades::network;
    LiveProtocol168Connection connection;
    Protocol168SessionConfig session_config;
    session_config.player_name = "NativeMovementParity";
    session_config.team = requested_team;
    session_config.class_id = requested_class;
    if (!connection.start(
            EnetProtocol168Config{argv[1], static_cast<std::uint16_t>(parsed_port),
                                  30'000U},
            session_config)) {
        std::cerr << connection.status().error << '\n';
        return 1;
    }

    const auto handshake_deadline = std::chrono::steady_clock::now() +
                                    std::chrono::seconds{35};
    std::unique_ptr<Protocol168WorldBootstrap> bootstrap;
    while (std::chrono::steady_clock::now() < handshake_deadline &&
           bootstrap == nullptr) {
        const auto status = connection.status();
        if (status.phase == LiveProtocol168Phase::failed ||
            status.phase == LiveProtocol168Phase::disconnected) {
            std::cerr << status.error << '\n';
            return 1;
        }
        bootstrap = connection.take_bootstrap();
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    if (bootstrap == nullptr || bootstrap->map == nullptr) {
        std::cerr << "server did not publish a complete Protocol 168 world\n";
        return 1;
    }
    const auto* local = bootstrap->roster.player(bootstrap->local_player_id);
    if (local == nullptr || local->loadout.empty()) {
        std::cerr << "bootstrap omitted the local player or normalized loadout\n";
        return 1;
    }

    Vec3 initial_position = local->position;
    Vec3 initial_orientation = local->orientation;
    Vec3 initial_velocity = local->velocity;
    std::uint8_t initial_input_flags = local->input_flags;
    std::uint8_t initial_action_flags = local->action_flags;
    std::uint8_t initial_state_flags = local->state_flags;
    std::uint8_t initial_pickup_id = local->pickup_id;
    std::int32_t initial_ack_loop{-1};
    bool received_initial_owner_row{};
    std::array<std::size_t, 256U> packet_counts{};
    std::size_t matched_acknowledgements{};
    std::size_t input_mismatches{};
    std::size_t malformed_packets{};
    std::size_t sentinel_owner_tools{};
    std::size_t remote_world_rows{};
    std::size_t dynamic_create_players{};
    std::size_t orphan_world_rows{};
    std::size_t terrain_echoes{};
    double maximum_position_correction{};
    double maximum_velocity_correction{};
    double maximum_orientation_error{};
    static constexpr int phase_minimum{-4};
    static constexpr int phase_maximum{4};
    static constexpr std::size_t phase_count{
        static_cast<std::size_t>(phase_maximum - phase_minimum + 1)};
    std::array<double, phase_count> phase_position_error_sum{};
    std::array<double, phase_count> phase_position_error_max{};
    std::array<std::size_t, phase_count> phase_position_error_count{};
    const auto initial_row_deadline = std::chrono::steady_clock::now() +
                                      std::chrono::seconds{2};
    while (!received_initial_owner_row &&
           std::chrono::steady_clock::now() < initial_row_deadline) {
        for (const auto& packet : connection.take_inbound(256U)) {
            if (packet.empty()) continue;
            const auto id = std::to_integer<std::uint8_t>(packet.front());
            ++packet_counts[id];
            if (id == CreatePlayerPacket::id) {
                std::string error;
                if (!bootstrap->roster.apply(packet, &error)) {
                    ++malformed_packets;
                    std::cerr << error << '\n';
                } else {
                    ++dynamic_create_players;
                }
                continue;
            }
            if (id != 2U) {
                if (is_runtime_packet(id)) {
                    const auto decoded = decode_runtime_packet(packet);
                    if (!decoded) {
                        ++malformed_packets;
                        std::cerr << decoded.error << '\n';
                    }
                }
                continue;
            }
            const auto update = decode_world_update_weapon_rows(packet);
            if (!update) {
                ++malformed_packets;
                std::cerr << update.error << '\n';
                continue;
            }
            for (const auto& row : update.rows) {
                if (!bootstrap->roster.update_world_state(row)) {
                    ++orphan_world_rows;
                    continue;
                }
                if (row.player_id != bootstrap->local_player_id) {
                    ++remote_world_rows;
                    continue;
                }
                if (row.tool_id == 0xFFU) ++sentinel_owner_tools;
                initial_position = {row.position[0U], row.position[1U],
                                    row.position[2U]};
                initial_orientation = {row.orientation[0U], row.orientation[1U],
                                       row.orientation[2U]};
                initial_velocity = {row.velocity[0U], row.velocity[1U],
                                    row.velocity[2U]};
                initial_input_flags = row.input_flags;
                initial_action_flags = row.action_flags;
                initial_state_flags = row.state_flags;
                initial_pickup_id = row.pickup_id;
                initial_ack_loop = row.acknowledged_client_loop;
                received_initial_owner_row = true;
            }
        }
        if (!received_initial_owner_row) {
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
    }
    if (!received_initial_owner_row) {
        // Protocol 168 bootstrap already carries a complete local transform.
        // Some retail-compatible authorities do not emit an additional owner
        // row until ClientData starts advancing. Treat that omission as a
        // measured server behavior, not as a reason to skip the movement run.
        std::cerr << "server omitted the redundant initial owner row; "
                     "starting from bootstrap state\n";
    }

    battlespades::world::TutorialSessionConfig simulation_config;
    simulation_config.network_authoritative = true;
    simulation_config.fixed_dt = 1.0 / 60.0;
    simulation_config.initial_position = initial_position;
    simulation_config.initial_orientation = initial_orientation;
    simulation_config.initial_velocity = initial_velocity;
    simulation_config.initial_crouch = (initial_input_flags & 0x20U) != 0U;
    simulation_config.initial_wade = (initial_state_flags & 0x08U) != 0U;
    simulation_config.initial_class_id = local->class_id;
    simulation_config.movement_speed_scale =
        protocol168_movement_scale(bootstrap->initial_info, local->class_id);
    simulation_config.initial_loadout = local->loadout;
    simulation_config.initial_prefabs = local->prefabs;
    simulation_config.initial_ugc_tools = local->ugc_tools;
    simulation_config.initial_tool = local->tool_id;
    TutorialWorldSession simulation{bootstrap->map, simulation_config};
    Protocol168TerrainReplica terrain_replica{*bootstrap->map};
    constexpr std::uint32_t parity_block_color{0x527BA3U};
    const auto local_palette = encode_packet(
        SetColorPacket{bootstrap->local_player_id, parity_block_color});
    const auto local_palette_result = terrain_replica.apply(local_palette);
    if (!local_palette_result.recognized || !local_palette_result.error.empty()) {
        std::cerr << "could not initialize terrain parity palette\n";
        return 1;
    }
    simulation.apply_server_movement_state(initial_action_flags,
                                           initial_state_flags,
                                           initial_pickup_id);
    simulation.note_authoritative_snapshot(initial_ack_loop, initial_position);
    // Run the opposite collision policy as a shadow predictor. ClientData does
    // not carry position, so both predictors can be judged against the exact
    // same authoritative owner rows without changing server behaviour.
    TutorialWorldSession collision_shadow{bootstrap->map, simulation_config};
    collision_shadow.apply_server_movement_state(initial_action_flags,
                                                 initial_state_flags,
                                                 initial_pickup_id);
    collision_shadow.note_authoritative_snapshot(initial_ack_loop,
                                                 initial_position);
    const bool initial_airborne = simulation.diagnostics().airborne;

    std::ofstream trace;
    if (trace_path.has_value()) {
        trace.open(*trace_path, std::ios::trunc);
        if (!trace) {
            std::cerr << "could not open trace output\n";
            return 1;
        }
        trace << "world_loop,ack_loop,input_expected,input_server,"
                  "pred_x,pred_y,pred_z,server_x,server_y,server_z,"
                  "pred_vx,pred_vy,pred_vz,server_vx,server_vy,server_vz,"
                  "sent_ox,sent_oy,sent_oz,server_ox,server_oy,server_oz,"
                  "orientation_error,pred_airborne,pred_wade,pred_crouch,"
                  "pred_climb_timer,server_state_flags,"
                  "correction_x,correction_y,correction_z,"
                  "position_correction,velocity_correction\n";
        trace << std::fixed << std::setprecision(6);
    }

    std::map<std::int32_t, PredictedFrame> predicted;
    std::map<std::int32_t, PredictedFrame> shadow_predicted;
    double shadow_maximum_position_correction{};
    double shadow_maximum_velocity_correction{};
    std::int32_t client_loop = static_cast<std::int32_t>(
        bootstrap->next_client_loop_count);
    std::uint8_t previous_packet_flags{};
    PacketDelayLine uplink{uplink_delay_ms, jitter_ms};
    PacketDelayLine downlink{downlink_delay_ms, jitter_ms};

    const auto queue_uplink = [&](std::vector<std::byte> packet,
                                  PacketDelayLine::clock::time_point now) {
        uplink.push(std::move(packet), now);
    };
    const auto flush_uplink = [&](PacketDelayLine::clock::time_point now) {
        for (const auto& packet : uplink.take_due(now, 1'024U)) {
            if (!connection.send(packet)) return false;
        }
        return true;
    };

    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::seconds{seconds};
    auto next_tick = start;
    std::uint32_t frame{};
    while (std::chrono::steady_clock::now() < deadline) {
        const auto pump_time = std::chrono::steady_clock::now();
        for (auto& packet : connection.take_inbound(1'024U)) {
            downlink.push(std::move(packet), pump_time);
        }
        // Match the playable frontend's packet application tranche by default.
        // A large reliable burst can otherwise look healthy here while the real
        // client spends seconds reconciling against owner rows trapped behind it.
        for (const auto& packet : downlink.take_due(pump_time, inbound_budget)) {
            if (packet.empty()) continue;
            const auto id = std::to_integer<std::uint8_t>(packet.front());
            ++packet_counts[id];
            if (id == 2U) {
                const auto update = decode_world_update_weapon_rows(packet);
                if (!update) {
                    ++malformed_packets;
                    std::cerr << update.error << '\n';
                    continue;
                }
                for (const auto& row : update.rows) {
                    if (!bootstrap->roster.update_world_state(row)) {
                        ++orphan_world_rows;
                        continue;
                    }
                    if (row.player_id != bootstrap->local_player_id) {
                        ++remote_world_rows;
                        continue;
                    }
                    if (row.tool_id == 0xFFU) ++sentinel_owner_tools;
                    const Vec3 server_position{row.position[0U], row.position[1U],
                                               row.position[2U]};
                    const Vec3 server_velocity{row.velocity[0U], row.velocity[1U],
                                               row.velocity[2U]};
                    const Vec3 server_orientation{
                        row.orientation[0U], row.orientation[1U],
                        row.orientation[2U]};
                    for (int offset = phase_minimum; offset <= phase_maximum;
                         ++offset) {
                        const auto candidate = predicted.find(
                            row.acknowledged_client_loop + offset);
                        if (candidate == predicted.end()) continue;
                        const auto phase_index = static_cast<std::size_t>(
                            offset - phase_minimum);
                        const double phase_error = length(
                            {server_position.x - candidate->second.position.x,
                             server_position.y - candidate->second.position.y,
                             server_position.z - candidate->second.position.z});
                        phase_position_error_sum[phase_index] += phase_error;
                        phase_position_error_max[phase_index] = std::max(
                            phase_position_error_max[phase_index], phase_error);
                        ++phase_position_error_count[phase_index];
                    }
                    simulation.note_authoritative_snapshot(
                        row.acknowledged_client_loop, server_position);
                    simulation.apply_server_movement_state(
                        row.action_flags, row.state_flags, row.pickup_id);
                    collision_shadow.note_authoritative_snapshot(
                        row.acknowledged_client_loop, server_position);
                    collision_shadow.apply_server_movement_state(
                        row.action_flags, row.state_flags, row.pickup_id);
                    const auto sample = predicted.find(
                        row.acknowledged_client_loop);
                    if (sample == predicted.end()) continue;
                    ++matched_acknowledgements;
                    const auto orientation_error = length(
                        {server_orientation.x -
                             sample->second.encoded_orientation.x,
                         server_orientation.y -
                             sample->second.encoded_orientation.y,
                         server_orientation.z -
                             sample->second.encoded_orientation.z});
                    maximum_orientation_error =
                        std::max(maximum_orientation_error, orientation_error);
                    if (row.input_flags != sample->second.expected_server_flags) {
                        ++input_mismatches;
                    }
                    const auto before_correction = simulation.player();
                    if (!simulation.reconcile_authoritative(
                            row.acknowledged_client_loop, server_position,
                            server_velocity)) {
                        continue;
                    }
                    const auto after_correction = simulation.player();
                    const Vec3 applied_position_delta{
                        after_correction.position.x -
                            before_correction.position.x,
                        after_correction.position.y -
                            before_correction.position.y,
                        after_correction.position.z -
                            before_correction.position.z};
                    const Vec3 applied_velocity_delta{
                        after_correction.velocity.x -
                            before_correction.velocity.x,
                        after_correction.velocity.y -
                            before_correction.velocity.y,
                        after_correction.velocity.z -
                            before_correction.velocity.z};
                    const auto position_error = length(applied_position_delta);
                    const auto velocity_error = length(applied_velocity_delta);
                    maximum_position_correction =
                        std::max(maximum_position_correction, position_error);
                    maximum_velocity_correction =
                        std::max(maximum_velocity_correction, velocity_error);
                    const auto shadow_sample = shadow_predicted.find(
                        row.acknowledged_client_loop);
                    if (shadow_sample != shadow_predicted.end()) {
                        const auto shadow_before = collision_shadow.player();
                        if (collision_shadow.reconcile_authoritative(
                                row.acknowledged_client_loop, server_position,
                                server_velocity)) {
                            const auto shadow_after = collision_shadow.player();
                            shadow_maximum_position_correction = std::max(
                                shadow_maximum_position_correction,
                                length({shadow_after.position.x -
                                            shadow_before.position.x,
                                        shadow_after.position.y -
                                            shadow_before.position.y,
                                        shadow_after.position.z -
                                            shadow_before.position.z}));
                            shadow_maximum_velocity_correction = std::max(
                                shadow_maximum_velocity_correction,
                                length({shadow_after.velocity.x -
                                            shadow_before.velocity.x,
                                        shadow_after.velocity.y -
                                            shadow_before.velocity.y,
                                        shadow_after.velocity.z -
                                            shadow_before.velocity.z}));
                        }
                    }
                    if (trace) {
                        trace << update.loop_count << ','
                              << row.acknowledged_client_loop << ','
                              << static_cast<unsigned>(
                                     sample->second.expected_server_flags)
                              << ',' << static_cast<unsigned>(row.input_flags) << ','
                              << sample->second.position.x << ','
                              << sample->second.position.y << ','
                              << sample->second.position.z << ','
                              << server_position.x << ',' << server_position.y << ','
                              << server_position.z << ','
                              << sample->second.velocity.x << ','
                              << sample->second.velocity.y << ','
                              << sample->second.velocity.z << ','
                               << server_velocity.x << ',' << server_velocity.y << ','
                               << server_velocity.z << ','
                               << sample->second.encoded_orientation.x << ','
                               << sample->second.encoded_orientation.y << ','
                               << sample->second.encoded_orientation.z << ','
                               << server_orientation.x << ','
                               << server_orientation.y << ','
                               << server_orientation.z << ','
                               << orientation_error << ','
                               << (sample->second.airborne ? 1 : 0) << ','
                               << (sample->second.wade ? 1 : 0) << ','
                               << (sample->second.crouch ? 1 : 0) << ','
                               << sample->second.climb_timer << ','
                               << static_cast<unsigned>(row.state_flags) << ','
                               << applied_position_delta.x << ','
                              << applied_position_delta.y << ','
                              << applied_position_delta.z << ','
                              << position_error << ','
                              << velocity_error << '\n';
                    }
                }
            } else if (id == SetColorPacket::id || id == DamagePacket::id ||
                       id == BlockBuildColoredPacket::id ||
                       id == BlockLinePacket::id ||
                       id == BlockBuildPacket::id ||
                       id == PaintBlockPacket::id) {
                const auto applied = terrain_replica.apply(packet);
                if (!applied.recognized || !applied.error.empty()) {
                    ++malformed_packets;
                    std::cerr << applied.error << '\n';
                } else if (id == BlockLinePacket::id ||
                           id == BlockBuildPacket::id ||
                           id == BlockBuildColoredPacket::id ||
                           id == PaintBlockPacket::id) {
                    ++terrain_echoes;
                }
            } else if (id == CreatePlayerPacket::id) {
                std::string error;
                if (!bootstrap->roster.apply(packet, &error)) {
                    ++malformed_packets;
                    std::cerr << error << '\n';
                } else {
                    ++dynamic_create_players;
                }
            } else if (is_runtime_packet(id)) {
                const auto decoded = decode_runtime_packet(packet);
                if (!decoded) {
                    ++malformed_packets;
                    std::cerr << decoded.error << '\n';
                } else if (const auto* pickup =
                               std::get_if<PickPickupPacket>(&*decoded.packet);
                           pickup != nullptr &&
                           pickup->player_id == bootstrap->local_player_id) {
                    simulation.apply_server_pickup_burden(
                        pickup->burdensome);
                    collision_shadow.apply_server_pickup_burden(
                        pickup->burdensome);
                }
            }
        }

        if (chase_peer) {
            const auto players = bootstrap->roster.players();
            const auto target = std::find_if(
                players.begin(), players.end(), [&](const auto& candidate) {
                    return candidate.player_id != bootstrap->local_player_id &&
                           candidate.health > 0;
                });
            const auto chase = [&](TutorialWorldSession& session) {
                session.set_action_held(TutorialAction::forward,
                                        target != players.end());
                session.set_action_held(TutorialAction::sprint, false);
                session.set_action_held(TutorialAction::jump, false);
                session.set_action_held(TutorialAction::crouch, false);
                session.set_action_held(TutorialAction::right, false);
                if (target == players.end()) return;
                const auto& player = session.player();
                const double desired_yaw = std::atan2(
                    -(target->position.y - player.position.y),
                    -(target->position.x - player.position.x));
                const double current_yaw =
                    std::atan2(-player.orientation.y, -player.orientation.x);
                double delta = (desired_yaw - current_yaw) *
                               (180.0 / std::numbers::pi);
                while (delta > 180.0) delta -= 360.0;
                while (delta < -180.0) delta += 360.0;
                session.apply_look_delta(delta / 0.1, 0.0);
            };
            chase(simulation);
            chase(collision_shadow);
        } else {
            set_inputs(simulation, frame, !suppress_jump, straight_walk,
                       gentle_turn);
            set_inputs(collision_shadow, frame, !suppress_jump, straight_walk,
                       gentle_turn);
        }
        const auto collision_bodies = protocol168_collision_bodies(
            bootstrap->roster, bootstrap->local_player_id,
            bootstrap->initial_info.same_team_collision);
        if (suppress_player_collision) {
            simulation.set_player_collision_bodies({});
            collision_shadow.set_player_collision_bodies(collision_bodies);
        } else {
            simulation.set_player_collision_bodies(collision_bodies);
            collision_shadow.set_player_collision_bodies({});
        }
        simulation.tick();
        collision_shadow.tick();
        const auto packet_flags = simulation.movement_flags();
        const auto expected_server_flags = static_cast<std::uint8_t>(
            // Movement/jump/sprint are L-1. Crouch geometry is the only
            // movement byte transition already visible in history row L.
            (previous_packet_flags & ~0x20U) | (packet_flags & 0x20U));

        ClientDataPacket input;
        input.loop_count = client_loop;
        input.player_id = bootstrap->local_player_id;
        const auto ordinary_tool =
            simulation.selected_tool_id().value_or(local->loadout.front());
        input.tool_id = frame >= 70U && frame < 85U ? 5U : ordinary_tool;
        const auto orientation = simulation.player().orientation;
        input.orientation = {static_cast<float>(orientation.x),
                             static_cast<float>(orientation.y),
                             static_cast<float>(orientation.z)};
        input.opaque_state = protocol168_client_data_opaque_state(client_loop);
        input.movement_flags = packet_flags;
        input.action_flags = simulation.action_flags();
        simulation.record_network_prediction(client_loop);
        collision_shadow.record_network_prediction(client_loop);
        const auto encoded_input = encode_packet(input);
        const auto decoded_input = decode_weapon_packet(encoded_input);
        if (!decoded_input) {
            std::cerr << decoded_input.error << '\n';
            return 1;
        }
        const auto* encoded_client_data =
            std::get_if<ClientDataPacket>(&*decoded_input.packet);
        if (encoded_client_data == nullptr) {
            std::cerr << "encoded ClientData did not round-trip\n";
            return 1;
        }
        const Vec3 encoded_orientation{
            encoded_client_data->orientation[0U],
            encoded_client_data->orientation[1U],
            encoded_client_data->orientation[2U]};
        predicted[client_loop] = PredictedFrame{
            simulation.player().position, simulation.player().velocity,
            encoded_orientation, expected_server_flags,
            simulation.player().airborne, simulation.player().wade,
            simulation.player().crouch, simulation.player().climb_timer};
        shadow_predicted[client_loop] = PredictedFrame{
            collision_shadow.player().position,
            collision_shadow.player().velocity,
            encoded_orientation,
            expected_server_flags,
            collision_shadow.player().airborne,
            collision_shadow.player().wade,
            collision_shadow.player().crouch,
            collision_shadow.player().climb_timer};
        while (predicted.size() > 2'048U) predicted.erase(predicted.begin());
        while (shadow_predicted.size() > 2'048U) {
            shadow_predicted.erase(shadow_predicted.begin());
        }
        const auto send_time = std::chrono::steady_clock::now();
        queue_uplink(encoded_input, send_time);
        if (!flush_uplink(send_time)) {
            std::cerr << connection.status().error << '\n';
            return 1;
        }
        if (!suppress_build && frame == 72U) {
            queue_uplink(local_palette, send_time);
        }
        if (!suppress_build && frame == 80U) {
            const auto cell = supported_build_cell(simulation);
            if (!cell.has_value()) {
                std::cerr << "movement parity could not find a supported build cell\n";
                return 1;
            }
            BlockLinePacket line;
            line.loop_count = input.loop_count;
            line.player_id = bootstrap->local_player_id;
            line.start = *cell;
            line.end = *cell;
            queue_uplink(encode_packet(line), send_time);
        }
        if (!flush_uplink(send_time)) {
            std::cerr << connection.status().error << '\n';
            return 1;
        }
        previous_packet_flags = packet_flags;
        ++client_loop;
        ++frame;
        next_tick += std::chrono::microseconds{16'667};
        std::this_thread::sleep_until(next_tick);
    }

    // Give the final reliable 30 Hz owner snapshots time to cross the worker
    // boundary without advancing the simulated input clock.
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    const auto status = connection.status();
    connection.stop();

    const auto& team1 = bootstrap->state_info.team1_color;
    const auto& team2 = bootstrap->state_info.team2_color;
    const auto initial_map_x = static_cast<std::uint32_t>(
        std::clamp(initial_position.x, 0.0, 511.0));
    const auto initial_map_y = static_cast<std::uint32_t>(
        std::clamp(initial_position.y, 0.0, 511.0));
    std::cout << std::fixed << std::setprecision(6)
              << "map=" << bootstrap->initial_info.map_name
              << " player=" << static_cast<unsigned>(bootstrap->local_player_id)
              << " class=" << static_cast<unsigned>(local->class_id)
              << " team=" << static_cast<unsigned>(requested_team)
              << " movement_scale=" << simulation_config.movement_speed_scale
              << " player_collision=" << (suppress_player_collision ? 0 : 1)
              << " jump=" << (suppress_jump ? 0 : 1)
              << " build=" << (suppress_build ? 0 : 1)
              << " uplink_ms=" << uplink_delay_ms
              << " downlink_ms=" << downlink_delay_ms
              << " jitter_ms=" << jitter_ms
              << " inbound_budget=" << inbound_budget
              << " chase_peer=" << (chase_peer ? 1 : 0)
              << " initial_owner_row=" << (received_initial_owner_row ? 1 : 0)
              << " initial_ack=" << initial_ack_loop
              << " initial_z=" << initial_position.z
              << " initial_vz=" << initial_velocity.z
              << " initial_orientation=" << initial_orientation.x << ':'
              << initial_orientation.y << ':' << initial_orientation.z
              << " initial_surface_z="
              << bootstrap->map->surface_z(initial_map_x, initial_map_y)
              << " initial_airborne="
              << (initial_airborne ? 1 : 0)
              << " team1_rgb=" << static_cast<unsigned>(team1[0U]) << ':'
              << static_cast<unsigned>(team1[1U]) << ':'
              << static_cast<unsigned>(team1[2U])
              << " team2_rgb=" << static_cast<unsigned>(team2[0U]) << ':'
              << static_cast<unsigned>(team2[1U]) << ':'
              << static_cast<unsigned>(team2[2U])
              << " frames=" << frame
              << " final_orientation=" << simulation.player().orientation.x
              << ':' << simulation.player().orientation.y << ':'
              << simulation.player().orientation.z
              << " matched_acks=" << matched_acknowledgements
              << " input_mismatches=" << input_mismatches
              << " malformed_packets=" << malformed_packets
              << " owner_tool_sentinels=" << sentinel_owner_tools
              << " remote_world_rows=" << remote_world_rows
              << " dynamic_create_players=" << dynamic_create_players
              << " orphan_world_rows=" << orphan_world_rows
              << " terrain_echoes=" << terrain_echoes
              << " max_position_correction=" << maximum_position_correction
              << " max_velocity_correction=" << maximum_velocity_correction
              << " max_orientation_error=" << maximum_orientation_error
              << " shadow_player_collision="
              << (suppress_player_collision ? 1 : 0)
              << " shadow_max_position_correction="
              << shadow_maximum_position_correction
              << " shadow_max_velocity_correction="
              << shadow_maximum_velocity_correction
              << " delayed_uplink=" << uplink.size()
              << " delayed_downlink=" << downlink.size()
              << " sent=" << status.sent_datagrams
              << " received=" << status.received_datagrams << '\n';
    std::cout << "packet_ids=";
    bool first = true;
    for (std::size_t id{}; id < packet_counts.size(); ++id) {
        if (packet_counts[id] == 0U) continue;
        if (!first) std::cout << ',';
        first = false;
        std::cout << id << ':' << packet_counts[id];
    }
    std::cout << '\n';
    std::cout << "ack_phase_errors=";
    for (int offset = phase_minimum; offset <= phase_maximum; ++offset) {
        const auto index =
            static_cast<std::size_t>(offset - phase_minimum);
        if (offset != phase_minimum) std::cout << ',';
        const auto count = phase_position_error_count[index];
        const auto mean = count == 0U
                              ? 0.0
                              : phase_position_error_sum[index] /
                                    static_cast<double>(count);
        std::cout << offset << ':' << count << ':' << mean << ':'
                  << phase_position_error_max[index];
    }
    std::cout << '\n';

    const bool decoded_colors = team1 != std::array<std::uint8_t, 3U>{} &&
                                team2 != std::array<std::uint8_t, 3U>{};
    if (status.phase != LiveProtocol168Phase::ready ||
        matched_acknowledgements < 30U || input_mismatches != 0U ||
        malformed_packets != 0U || sentinel_owner_tools == 0U ||
        orphan_world_rows != 0U || (!suppress_build && terrain_echoes == 0U) ||
        !decoded_colors || maximum_position_correction > 0.5 ||
        maximum_velocity_correction > 0.1 ||
        (require_peer && remote_world_rows < 30U)) {
        std::cerr << "live server/client packet parity gate failed\n";
        return 1;
    }
    return 0;
}
