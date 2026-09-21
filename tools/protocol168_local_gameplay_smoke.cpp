#include "battlespades/frontend/match_overlays.hpp"
#include "battlespades/network/live_protocol168_connection.hpp"
#include "battlespades/network/protocol168_runtime.hpp"
#include "battlespades/network/protocol168_terrain.hpp"
#include "battlespades/network/protocol168_weapons.hpp"
#include "battlespades/world/chunk_mesh.hpp"
#include "battlespades/world/map_catalog.hpp"
#include "battlespades/world/voxel_raycast.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using namespace battlespades;
using Clock = std::chrono::steady_clock;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error{message};
}

void verify_flight_profile(const network::Protocol168InitialInfo& info) {
    const auto& profile = info.flight_profile;
    require(profile.drain[1U] == 30.0 && profile.drain[2U] == 9.0 &&
                profile.drain[3U] == 7.5 && profile.refill[1U] == 20.0 &&
                profile.refill[2U] == 20.0 && profile.refill[3U] == 20.0 &&
                profile.grounded_refill_only && profile.refill_idle_seconds == 1.0 &&
                profile.descending_parachute_only,
            "local server did not negotiate the authoritative bounded flight profile");
}

std::uint64_t mesh_hash(const world::ChunkMesh& mesh) {
    std::uint64_t hash{1469598103934665603ULL};
    for (const auto& vertex : mesh.vertices) {
        for (const auto value : {std::bit_cast<std::uint32_t>(vertex.x),
                                 std::bit_cast<std::uint32_t>(vertex.y),
                                 std::bit_cast<std::uint32_t>(vertex.z), vertex.abgr}) {
            hash = (hash ^ value) * 1099511628211ULL;
        }
    }
    return hash;
}

std::unique_ptr<network::Protocol168WorldBootstrap>
bootstrap(network::LiveProtocol168Connection& connection) {
    const auto until = Clock::now() + std::chrono::seconds{40};
    while (Clock::now() < until) {
        if (auto result = connection.take_bootstrap(); result != nullptr) return result;
        const auto status = connection.status();
        require(status.phase != network::LiveProtocol168Phase::failed &&
                    status.phase != network::LiveProtocol168Phase::disconnected,
                "bootstrap failed: " + status.error);
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    throw std::runtime_error{"bootstrap timed out"};
}
}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "usage: aos_protocol168_local_gameplay_smoke PORT (loopback only)");
        const auto port = std::strtoul(argv[1], nullptr, 10);
        require(port >= 1024UL && port <= 65535UL, "invalid local fixture port");
        network::Protocol168SessionConfig session;
        session.player_name = "LocalGameplayGate";
        session.team = 2U;
        session.class_id = 12U;
        network::LiveProtocol168Connection connection;
        const network::EnetProtocol168Config transport{
            "127.0.0.1", static_cast<std::uint16_t>(port), 30'000U};
        require(connection.start(transport, session), connection.status().error);
        auto initial = bootstrap(connection);
        require(initial->map != nullptr, "MapSync did not supply terrain");
        verify_flight_profile(initial->initial_info);
        const std::string initial_map = initial->initial_info.map_name;
        auto& map = *initial->map;
        require(map.solid(110U, 100U, 59U), "initial MapSync omitted the fixture wall");
        const auto before_hit = world::trace_first_solid(
            map, {105.5F, 100.5F, 59.5F}, {1.0F, 0.0F, 0.0F}, 10.0F);
        require(before_hit.has_value() && before_hit->cell.x == 110U,
                "initial collision ray must hit the solid wall");
        world::ChunkMesher mesher;
        const world::ChunkKey wall_chunk{6U, 6U};
        const auto before_hash = mesh_hash(mesher.mesh(map, wall_chunk));
        network::Protocol168TerrainReplica terrain{map};
        frontend::GenericVotingModel vote;
        std::string chosen_map;
        std::size_t removed_cells{};
        std::size_t terrain_packets{};
        bool dirty_wall{}, rebuilt_mesh{}, ray_clear{}, vote_closed{}, map_ended{};
        bool kill_seen{}, personal_score_seen{}, team_score_seen{};
        bool turret_seen{}, rocket_seen{};
        std::int32_t input_loop = static_cast<std::int32_t>(initial->next_client_loop_count);
        const auto until = Clock::now() + std::chrono::seconds{80};
        auto next_input = Clock::now();
        bool reconnected{};
        while (Clock::now() < until && !reconnected) {
            for (const auto& packet : connection.take_inbound(256U)) {
                if (packet.empty()) continue;
                const auto id = std::to_integer<std::uint8_t>(packet.front());
                const auto applied = terrain.apply(packet);
                if (applied.recognized) {
                    require(applied.error.empty(), "terrain decode/apply: " + applied.error);
                    if (id == network::DamagePacket::id) {
                        ++terrain_packets;
                        removed_cells += applied.mutation.changed_cells.size();
                        for (const auto& cell : applied.mutation.changed_cells)
                            require(!map.solid(cell.x, cell.y, cell.z), "destroyed cell still collides");
                    }
                    const auto dirty = terrain.take_dirty_chunks();
                    dirty_wall = dirty_wall || std::ranges::find(dirty, wall_chunk) != dirty.end();
                    if (dirty_wall && !map.solid(110U, 100U, 59U)) {
                        rebuilt_mesh = mesh_hash(mesher.mesh(map, wall_chunk)) != before_hash;
                        ray_clear = !world::trace_first_solid(
                            map, {105.5F, 100.5F, 59.5F}, {1.0F, 0.0F, 0.0F}, 10.0F);
                    }
                }
                const auto runtime = network::decode_runtime_packet(packet);
                if (!runtime) continue;
                if (const auto* entity = std::get_if<network::CreateEntityPacket>(&*runtime.packet)) {
                    turret_seen = turret_seen || entity->type == 8U;
                    rocket_seen = rocket_seen || entity->type == 21U;
                } else if (const auto* score = std::get_if<network::SetScorePacket>(&*runtime.packet)) {
                    personal_score_seen = personal_score_seen ||
                        (score->type == 1U && score->specifier == initial->local_player_id && score->value == 100);
                    team_score_seen = team_score_seen ||
                        (score->type == 0U && score->specifier == 2U && score->value == 1);
                } else if (const auto* killed = std::get_if<network::KillActionPacket>(&*runtime.packet)) {
                    kill_seen = kill_seen || (killed->killer_id == initial->local_player_id);
                } else if (const auto* ballot = std::get_if<network::GenericVoteMessagePacket>(&*runtime.packet)) {
                    vote.apply(*ballot);
                    if (ballot->message_type == network::GenericVoteMessagePacket::start) {
                        require(removed_cells > 0U && dirty_wall && rebuilt_mesh && ray_clear,
                                "turret blast failed terrain/mesh/collision gate before vote");
                        require(map.solid(100U, 100U, 62U),
                                "turret blast incorrectly removed the distant supported player floor");
                        require(turret_seen && rocket_seen, "turret/rocket CreateEntity not observed");
                        require(!vote.choices().empty(), "map vote has no visible choices");
                        chosen_map = vote.choices().front().display_text;
                        const auto token = vote.cast(0U);
                        require(token.has_value(), "native vote model rejected valid first choice");
                        require(connection.send(network::encode_generic_vote_cast(
                                    initial->local_player_id, *token)), "vote CAST send failed");
                    } else if (ballot->message_type == network::GenericVoteMessagePacket::closed) {
                        require(!chosen_map.empty() && !vote.can_vote() && vote.showing_result() &&
                                    vote.result_text().find(chosen_map) != std::string_view::npos,
                                "closed ballot lost its selected-map announcement");
                        vote_closed = true;
                    }
                } else if (std::holds_alternative<network::MapEndedPacket>(*runtime.packet)) {
                    map_ended = true;
                }
            }
            const auto status = connection.status();
            if (network::protocol168_should_reconnect_after_map_change(status, map_ended)) {
                require(vote_closed && kill_seen && personal_score_seen && team_score_seen,
                        "rollover arrived before verified score/vote events");
                connection.stop();
                require(connection.start(transport, session), "local map-rejoin failed");
                auto replacement = bootstrap(connection);
                verify_flight_profile(replacement->initial_info);
                require(replacement->initial_info.map_name == world::map_display_name(chosen_map) &&
                            replacement->initial_info.map_name != initial_map,
                        "map vote winner differs from rejoined map");
                reconnected = true;
                std::cout << "next_map=" << replacement->initial_info.map_name << '\n';
                break;
            }
            require(status.phase != network::LiveProtocol168Phase::failed &&
                        status.phase != network::LiveProtocol168Phase::disconnected,
                    "live fixture disconnected unexpectedly: " + status.error);
            if (Clock::now() >= next_input) {
                network::ClientDataPacket input;
                input.loop_count = input_loop++;
                input.player_id = initial->local_player_id;
                input.tool_id = 16U;
                input.opaque_state = network::protocol168_client_data_opaque_state(input.loop_count);
                input.orientation = {1.0F, 0.0F, 0.0F};
                input.action_flags = 0x10U;
                require(connection.send(network::encode_packet(input)), "ClientData send failed");
                next_input += std::chrono::milliseconds{16};
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
        connection.stop();
        require(reconnected, "score/vote/map rollover did not finish before deadline");
        std::cout << "PASS local turret packets=" << terrain_packets << " removed_cells=" << removed_cells
                  << " dirty_chunk=" << dirty_wall << " rebuilt_mesh=" << rebuilt_mesh
                  << " collision_ray_clear=" << ray_clear
                  << " supported_floor_retained=1 flight_profile_verified=1"
                  << " personal_score=100 team_score=1 vote_closed=" << vote_closed
                  << " map_ended=" << map_ended << " rejoined=" << reconnected << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Local gameplay gate failed: " << error.what() << '\n';
        return 1;
    }
}
