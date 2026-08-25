#include "battlespades/world/tutorial_bootstrap.hpp"
#include "battlespades/world/tutorial_session.hpp"
#include "battlespades/world/particle_system.hpp"
#include "battlespades/world/terrain_effects.hpp"
#include "battlespades/world/vxl_map.hpp"
#include "battlespades/world/weapon_zoom.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using battlespades::world::BootstrapStage;
using battlespades::world::EntityEvent;
using battlespades::world::EntityEventKind;
using battlespades::world::LocalEntity;
using battlespades::world::ServerEntityProperty;
using battlespades::world::SkylightMap;
using battlespades::world::TutorialAction;
using battlespades::world::TutorialSessionConfig;
using battlespades::world::TutorialTool;
using battlespades::world::TutorialWorldBootstrap;
using battlespades::world::TutorialWorldSession;
using battlespades::world::Vec3;
using battlespades::world::VxlColor;
using battlespades::world::VxlMap;

void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] std::shared_ptr<VxlMap> spawn_platform_world() {
    std::vector<std::byte> bytes;
    bytes.reserve(static_cast<std::size_t>(VxlMap::width) * VxlMap::depth * 4U);
    for (std::size_t column{}; column < static_cast<std::size_t>(VxlMap::width) * VxlMap::depth;
         ++column) {
        bytes.push_back(std::byte{0U});
        bytes.push_back(std::byte{1U});
        bytes.push_back(std::byte{0U});
        bytes.push_back(std::byte{0U});
    }
    auto loaded = VxlMap::load(bytes);
    expect(static_cast<bool>(loaded), "synthetic world must parse");
    // A ground plane at z=233 puts the recovered spawn anchor (z=230.75)
    // exactly at standing contact: feet 230.75 + 2.25 = 233.
    for (std::uint32_t y{60U}; y <= 90U; ++y) {
        for (std::uint32_t x{110U}; x <= 150U; ++x) {
            expect(loaded.map->set_voxel(x, y, 233U, VxlColor{100U, 120U, 90U, 255U}),
                   "spawn platform voxel must be accepted");
        }
    }
    // Anchor the authored platform to the mandatory z=239 collision bed.
    // Without this pillar the synthetic fixture is one unsupported component,
    // so retail collapse correctly removes the entire plane after one hole.
    for (std::uint32_t z{234U}; z < VxlMap::height - 1U; ++z) {
        expect(loaded.map->set_voxel(110U, 60U, z, VxlColor{100U, 120U, 90U, 255U}),
               "spawn platform support must be accepted");
    }
    return std::make_shared<VxlMap>(std::move(*loaded.map));
}

} // namespace

int main() {
    try {
        const auto map = spawn_platform_world();

        // Spawn parity: recovered tutorial spawn and -x facing.
        {
            TutorialWorldSession session{map};
            expect(session.player().position.x == 140.5 && session.player().position.y == 76.5 &&
                       session.player().position.z == 230.75,
                   "session must spawn at the recovered tutorial anchor");
            expect(session.yaw() == 0.0 && session.pitch() == 0.0,
                   "spawn must face -x down the course at retail yaw zero");
            expect(session.player().orientation.x == -1.0,
                   "spawn orientation must be the recovered (-1, 0, 0)");
            for (int tick{}; tick < 120; ++tick) {
                session.tick();
            }
            expect(session.diagnostics().grounded, "spawn must settle grounded");
            expect(std::fabs(session.player().position.z - 230.75) < 0.01,
                   "settled spawn must rest at the authored anchor height");
        }

        // Retail BlockToolCommon constrains normal placement/color-pick rays
        // to MAX_BLOCK_DISTANCE=10 (Classic uses 5). A former 128-block
        // default made the BlockLine preview and packet appear unbounded.
        {
            auto placement_map = spawn_platform_world();
            expect(placement_map->set_voxel(120U, 76U, 230U, VxlColor{100U, 120U, 90U, 255U}),
                   "far placement target must be accepted");
            TutorialWorldSession session{placement_map};
            expect(!session.placement_cell().has_value(),
                   "default placement ray must reject a target beyond ten blocks");
            expect(session.placement_cell(128.0).has_value(),
                   "explicit long diagnostic rays must remain available");
            expect(placement_map->set_voxel(132U, 76U, 230U, VxlColor{100U, 120U, 90U, 255U}),
                   "near placement target must be accepted");
            const auto near = session.placement_cell();
            expect(near.has_value() && (*near)[0U] == 133 && (*near)[1U] == 76 &&
                       (*near)[2U] == 230,
                   "ten-block placement ray must return the adjacent face cell");
        }

        // Gadget placement is not block placement. Retail sends the SOLID
        // supporting voxel and its face, while the block tool sends adjacent
        // air. Ground-only tools reject walls; dynamite/C4 may attach to them.
        {
            auto placement_map = spawn_platform_world();
            TutorialWorldSession session{placement_map};
            session.apply_authoritative_transform({140.5, 76.5, 230.75}, {0.0, 0.0, 1.0});
            const auto mine = session.deployable_target(20U);
            expect(mine.has_value() && mine->cell == std::array<std::int16_t, 3U>{140, 76, 233} &&
                       mine->face == 4U,
                   "landmine must target the raw top face of its supporting ground");

            expect(placement_map->set_voxel(137U, 76U, 230U, VxlColor{100U, 120U, 90U, 255U}),
                   "deployable wall fixture must be authored");
            session.apply_authoritative_transform({140.5, 76.5, 230.75}, {-1.0, 0.0, 0.0});
            const auto dynamite = session.deployable_target(21U);
            expect(dynamite.has_value() &&
                       dynamite->cell == std::array<std::int16_t, 3U>{137, 76, 230} &&
                       dynamite->face == 1U,
                   "dynamite must preserve the selected wall voxel and face");
            expect(!session.deployable_target(20U).has_value(),
                   "ground-only landmine must reject the same wall face");
            expect(session.deployable_target(15U).has_value(),
                   "mounted gun must resolve its player-centred deployment voxel");
        }

        // Replicated ground entities use the same fixed-step VXL gravity as
        // offline entities. This catches both historical failures: the old
        // 10 Hz teleport and its one-air-voxel levitation anchor.
        {
            auto entity_map = spawn_platform_world();
            TutorialWorldSession session{entity_map};
            LocalEntity grave;
            grave.id = 901U;
            grave.type = 11U;
            grave.position = {130.25, 70.25, 225.0};
            expect(session.apply_server_entity(grave), "replicated grave fixture must be accepted");
            session.tick();
            expect(session.entities().front().position.z > 225.0 &&
                       session.entities().front().position.z < 233.0,
                   "replicated grave must fall smoothly on its first tick");
            for (int tick{}; tick < 120 && !session.entities().front().grounded; ++tick) {
                session.tick();
            }
            expect(session.entities().front().grounded &&
                       session.entities().front().position.z == 233.0,
                   "replicated grave must anchor to the solid platform voxel");
            expect(entity_map->clear_voxel(130U, 70U, 233U),
                   "grave support must be removable for the wake regression");
            session.tick();
            expect(!session.entities().front().grounded &&
                       session.entities().front().position.z > 233.0,
                   "terrain mutation must wake the replicated grave immediately");
            for (int tick{}; tick < 120 && !session.entities().front().grounded; ++tick) {
                session.tick();
            }
            expect(session.entities().front().grounded &&
                       session.entities().front().position.z == 239.0,
                   "replicated grave must settle on the next surviving VXL bed");
        }

        // Lesson events surface through the session: INTRO ends after the
        // recovered 3 seconds and hands BASIC_CONTROLS to the frontend once.
        {
            TutorialWorldSession session{map};
            expect(session.lessons().stage() == battlespades::world::TutorialLessonStage::intro,
                   "sessions must start in the INTRO lesson");
            bool entered{};
            for (int tick{}; tick < 200; ++tick) {
                session.tick();
                if (const auto stage = session.take_entered_stage(); stage.has_value()) {
                    expect(*stage == battlespades::world::TutorialLessonStage::basic_controls,
                           "the first lesson event must be BASIC_CONTROLS");
                    entered = true;
                }
            }
            expect(entered, "the INTRO timer must emit a lesson event");
            expect(!session.take_entered_stage().has_value(),
                   "lesson events must be consumed exactly once");
        }

        // A live Protocol 168 spawn reuses movement/camera while keeping the
        // server authoritative over weapons, lessons and terrain mutation.
        {
            TutorialSessionConfig live;
            live.network_authoritative = true;
            live.initial_position = {130.0, 70.0, 230.0};
            live.initial_orientation = {0.0, -1.0, 0.0};
            live.initial_class_id = 1U;
            live.initial_loadout = {17U, 2U, 5U};
            live.initial_tool = static_cast<std::uint8_t>(17U);
            TutorialWorldSession session{spawn_platform_world(), live};
            expect(session.network_authoritative() && session.player().position.x == 130.0 &&
                       session.selected_tool_id() == 17U,
                   "live session must use the server CreatePlayer spawn/loadout");
            const auto debug_class_before = session.debug_class_id();
            session.debug_grant_full_loadout();
            session.debug_cycle_class(1);
            LocalEntity server_entity;
            server_entity.id = 900U;
            server_entity.type = 10U;
            server_entity.position = {131.0, 70.0, 230.0};
            expect(session.apply_server_entity(server_entity),
                   "authoritative entity fixture must be accepted");
            session.clear_entities();
            expect(session.selected_tool_id() == 17U &&
                       session.debug_class_id() == debug_class_before &&
                       session.debug_spawn_every_entity() == 0U && session.entities().size() == 1U,
                   "authoritative sessions must reject debug grants, class "
                   "changes, entity spawning and entity clearing");
            session.set_primary_held(true);
            session.set_action_held(TutorialAction::forward, true);
            session.set_server_health(180.0);
            expect(session.health() == 180.0,
                   "authoritative health above tutorial 100 must survive intact");
            session.set_server_health(0.0);
            expect(!session.alive() && !session.action_held(TutorialAction::forward),
                   "authoritative death must clear held movement and attacks");
            const auto entities_at_death = session.entities().size();
            session.tick();
            expect(session.entities().size() == entities_at_death,
                   "authoritative death must not fabricate an offline grave");
            session.set_server_health(100.0);
            expect(session.alive() && session.health() == 100.0,
                   "authoritative CreatePlayer health must start a fresh life");
            session.set_action_held(TutorialAction::forward, true);
            session.set_action_held(TutorialAction::jump, true);
            session.set_action_held(TutorialAction::hover, true);
            session.set_primary_held(true);
            expect(session.movement_flags() == 0x11U && session.action_flags() == 0x91U,
                   "live held state must map to the measured ClientData bytes");
            const auto solids = session.map().solid_voxels();
            for (int tick{}; tick < 30; ++tick)
                session.tick();
            expect(session.map().solid_voxels() == solids,
                   "network-authoritative attacks must never mutate local terrain");
            expect(session.lessons().stage() == battlespades::world::TutorialLessonStage::intro,
                   "live matches must not advance Tutorial lesson gates");

            const auto eye_before = session.eye_position();
            session.apply_authoritative_delta({0.5, 0.0, 0.0}, {0.25, 0.0, 0.0});
            expect(std::fabs(session.player().position.x - (eye_before[0U] + 0.5)) < 1e-9 &&
                       std::fabs(session.eye_position()[0U] - eye_before[0U]) < 1e-9,
                   "ACK correction must update physics without teleporting the camera");
            session.tick();
            const auto expected_first_interpolation =
                session.player().position.x * 0.1 + eye_before[0U] * 0.9 +
                session.player().velocity.x * (live.fixed_dt * 16.0);
            expect(std::fabs(session.eye_position()[0U] - expected_first_interpolation) < 1e-9,
                   "ACK presentation must use retail's 10/90 blend and "
                   "half-scale velocity extrapolation");
            for (int tick{1}; tick < 60; ++tick)
                session.tick();
            expect(std::fabs(session.eye_position()[0U] - session.player().position.x) < 0.01,
                   "camera correction must converge quickly to authoritative physics");

            // Retail's semantic threshold is four blocks squared (16.0).
            // A larger correction snaps instead of entering the 0.1-second
            // presentation window.
            session.apply_authoritative_delta({5.0, 0.0, 0.0}, {});
            expect(std::fabs(session.eye_position()[0U] - session.player().position.x) < 1e-9,
                   "semantic position corrections must snap at the retail limit");
        }

        // Character hides non-melee FPS tools for the entire sprint, but
        // keeps Zombie hands visible/usable. Releasing sprint starts the same
        // half-second Character.pullout return used by retail.
        {
            TutorialSessionConfig zombie;
            zombie.network_authoritative = true;
            zombie.initial_class_id = 4U;
            zombie.initial_loadout = {24U};
            zombie.initial_tool = static_cast<std::uint8_t>(24U);
            TutorialWorldSession session{spawn_platform_world(), zombie};
            session.set_action_held(TutorialAction::sprint, true);
            expect(session.weapon_view_model_visible(),
                   "Zombie hands must stay visible while sprinting");

            TutorialSessionConfig armed;
            armed.network_authoritative = true;
            armed.initial_class_id = 1U;
            armed.initial_loadout = {17U};
            armed.initial_tool = static_cast<std::uint8_t>(17U);
            TutorialWorldSession pistol{spawn_platform_world(), armed};
            pistol.set_action_held(TutorialAction::sprint, true);
            expect(!pistol.weapon_view_model_visible(),
                   "non-melee weapons must be put away while sprinting");
            pistol.set_action_held(TutorialAction::sprint, false);
            expect(pistol.pullout_remaining() > 0.49,
                   "stopping a non-melee sprint must start the 0.5s pullout");
            for (int tick{}; tick < 31; ++tick) {
                pistol.tick();
            }
            expect(pistol.pullout_remaining() == 0.0,
                   "sprint pullout must finish on the fixed-tick clock");
        }

        // Retail sends ClientData after stepping its frame; the server's
        // movement latch therefore consumes the previous packet's locomotion.
        // Prediction must use the same phase or every input edge is corrected.
        {
            TutorialSessionConfig live;
            live.network_authoritative = true;
            live.initial_position = {130.0, 70.0, 230.75};
            live.initial_orientation = {-1.0, 0.0, 0.0};
            live.initial_class_id = 1U;
            live.initial_loadout = {17U, 2U, 5U};
            TutorialWorldSession session{spawn_platform_world(), live};
            const auto start_x = session.player().position.x;
            session.set_action_held(TutorialAction::forward, true);
            expect((session.movement_flags() & 0x01U) != 0U,
                   "ClientData must publish the current held input immediately");
            session.tick();
            expect(std::fabs(session.player().position.x - start_x) < 1e-9,
                   "live prediction frame L must apply locomotion from L-1");
            session.tick();
            expect(session.player().position.x < start_x,
                   "the next prediction frame must consume the latched forward input");

            // CreatePlayer begins a new life. It must clear the prior frame's
            // locomotion latch and collision/presentation state while keeping
            // the currently held key available for the new first packet.
            session.set_action_held(TutorialAction::crouch, true);
            session.tick();
            session.apply_authoritative_delta({0.5, 0.0, 0.0}, {});
            session.apply_authoritative_transform({140.0, 76.0, 230.75}, {0.0, -1.0, 0.0});
            expect(!session.player().crouch && !session.player().wade &&
                       session.player().fall_distance == 0.0 &&
                       session.player().climb_timer == 0.0 &&
                       std::fabs(session.eye_position()[0U] - 140.0) < 1e-9,
                   "authoritative respawn must clear prior-life movement and camera state");
            session.set_action_held(TutorialAction::crouch, false);
            session.tick();
            expect(std::fabs(session.player().position.x - 140.0) < 1e-9,
                   "respawn's first frame must start from an idle movement latch");
            session.tick();
            expect(session.player().position.y < 76.0,
                   "held locomotion must enter the new life on its next latched frame");
        }

        // Owner correction restores the ACK state and replays later movement.
        // A coordinate-only rebase cannot reproduce a collision branch.
        {
            auto replay_map = spawn_platform_world();
            for (std::uint32_t y{75U}; y <= 77U; ++y) {
                for (std::uint32_t z{230U}; z <= 232U; ++z) {
                    expect(replay_map->set_voxel(139U, y, z, VxlColor{90U, 90U, 90U, 255U}),
                           "reconciliation wall voxel must be accepted");
                }
            }
            TutorialSessionConfig live;
            live.network_authoritative = true;
            live.initial_position = {140.5, 76.5, 230.75};
            live.initial_orientation = {-1.0, 0.0, 0.0};
            live.initial_class_id = 1U;
            live.initial_loadout = {17U, 2U, 5U};
            TutorialWorldSession session{replay_map, live};
            session.set_action_held(TutorialAction::forward, true);
            session.tick(); // loop 100 consumes the idle latch.
            const auto acknowledged = session.player();
            session.record_network_prediction(100);
            session.tick(); // loop 101 consumes forward against the wall.
            const auto before = session.player();
            session.record_network_prediction(101);

            expect(session.reconcile_authoritative(100,
                                                   {acknowledged.position.x + 0.2,
                                                    acknowledged.position.y,
                                                    acknowledged.position.z},
                                                   acknowledged.velocity),
                   "retained ACK loop must be replayable");
            expect(session.player().position.x < before.position.x + 0.2,
                   "replay must re-run the later wall collision instead of "
                   "adding one delta to the current position");

            // WorldUpdate is unreliable. Repeating an already-applied pong
            // must be inert even if a later snapshot was serialized with the
            // same stamp; otherwise each duplicate compounds into a teleport.
            const auto after_first_ack = session.player();
            expect(session.reconcile_authoritative(
                       100,
                       {after_first_ack.position.x + 2.0,
                        after_first_ack.position.y,
                        after_first_ack.position.z},
                       {after_first_ack.velocity.x + 1.0,
                        after_first_ack.velocity.y,
                        after_first_ack.velocity.z}),
                   "a duplicate retained ACK must be recognized");
            expect(std::fabs(session.player().position.x - after_first_ack.position.x) < 1e-9 &&
                       std::fabs(session.player().velocity.x - after_first_ack.velocity.x) < 1e-9,
                   "a duplicate ACK must never apply another correction");

            // The retail 0.1 position gate owns the complete correction. A
            // velocity-only disagreement inside it is intentionally ignored.
            session.record_network_prediction(102);
            const auto stable = session.player();
            expect(session.reconcile_authoritative(
                       102,
                       stable.position,
                       {stable.velocity.x + 2.0, stable.velocity.y, stable.velocity.z}) &&
                       std::fabs(session.player().velocity.x - stable.velocity.x) < 1e-9,
                   "sub-threshold owner rows must not apply velocity-only jitter");
        }

        // The authoritative server retains the pre-launch vertical position
        // on the frame that accepts a jump while publishing the new upward
        // velocity. Matching that phase prevents a correction on every jump.
        {
            TutorialSessionConfig live;
            live.network_authoritative = true;
            // Use the fixture's real standing contact plane. A logical-only
            // grounded flag in empty air becomes airborne before the latched
            // jump recurrence and cannot characterize the button phase.
            live.initial_position = {130.0, 70.0, 230.75};
            live.initial_airborne = false;
            live.initial_orientation = {-1.0, 0.0, 0.0};
            live.initial_class_id = 1U;
            live.initial_loadout = {17U, 2U, 5U};
            TutorialWorldSession session{spawn_platform_world(), live};
            session.set_action_held(TutorialAction::jump, true);
            const auto before_launch = session.player().position;
            session.note_authoritative_snapshot(0, before_launch);
            session.note_authoritative_snapshot(
                0, {before_launch.x + 3.0, before_launch.y, before_launch.z});
            session.tick();
            expect(!session.diagnostics().airborne,
                   "live jump must remain in packet L's observed-frame latch");
            session.tick();
            expect(session.diagnostics().airborne && session.player().velocity.z < 0.0,
                   "live jump must enter physics on the packet-L-1 recurrence");
            expect(std::fabs(session.player().position.x - before_launch.x) < 1e-9 &&
                       std::fabs(session.player().position.y - before_launch.y) < 1e-9 &&
                       std::fabs(session.player().position.z - before_launch.z) < 1e-9,
                   "live jump launch must retain the authoritative owner-row anchor");
        }

        // A network jump that enters a one-block step is not the flat-ground
        // anchor case above. Retail boxclipmove reports jump and climb in the
        // same recurrence and its upward/forward displacement must survive the
        // session wrapper; discarding it causes a latency-amplified rollback at
        // voxel edges.
        {
            auto edge_map = spawn_platform_world();
            for (std::uint32_t y{68U}; y <= 73U; ++y) {
                for (std::uint32_t x{134U}; x <= 138U; ++x) {
                    expect(edge_map->set_voxel(
                               x, y, 232U, VxlColor{90U, 90U, 90U, 255U}),
                           "network jump-step voxel must be accepted");
                }
            }
            TutorialSessionConfig live;
            live.network_authoritative = true;
            live.initial_position = {130.5, 70.5, 230.75};
            live.initial_airborne = false;
            live.initial_orientation = {1.0, 0.0, 0.0};
            live.initial_class_id = 1U;
            live.initial_loadout = {17U, 2U, 5U};
            TutorialWorldSession session{edge_map, live};
            session.set_action_held(TutorialAction::forward, true);
            session.tick(); // Fill the network input latch.
            static_cast<void>(session.take_movement_events());

            bool exercised{};
            for (int frame{}; frame < 240 && !exercised; ++frame) {
                auto next = session.player();
                battlespades::world::PlayerInputState forward;
                forward.forward = true;
                static_cast<void>(battlespades::world::step_player(
                    next, forward, edge_map.get(), live.fixed_dt,
                    battlespades::world::movement_config_for_class(1U)));
                auto edge_launch = next;
                auto jump = forward;
                jump.jump = true;
                const auto expected = battlespades::world::step_player(
                    edge_launch, jump, edge_map.get(), live.fixed_dt,
                    battlespades::world::movement_config_for_class(1U));
                if (!expected.climbed) {
                    session.tick();
                    static_cast<void>(session.take_movement_events());
                    continue;
                }

                session.set_action_held(TutorialAction::jump, true);
                session.tick(); // Latch jump while consuming the predicted forward step.
                static_cast<void>(session.take_movement_events());
                session.tick(); // Consume the forward+jump recurrence at the edge.
                const auto events = session.take_movement_events();
                expect(events.jumped && events.climbed,
                       "network edge launch must preserve both native transitions");
                expect(std::fabs(session.player().position.x - edge_launch.position.x) < 1e-5 &&
                           std::fabs(session.player().position.y - edge_launch.position.y) < 1e-5 &&
                           std::fabs(session.player().position.z - edge_launch.position.z) < 1e-5,
                       "network edge launch must retain the native climb displacement");
                exercised = true;
            }
            expect(exercised, "network fixture must reach a jump-and-climb recurrence");
        }

        // SetClassLoadout is authoritative for all three inventory address
        // spaces and must replace the local guessed selection atomically.
        {
            TutorialSessionConfig live;
            live.network_authoritative = true;
            live.initial_class_id = 1U;
            live.initial_loadout = {17U, 2U, 5U};
            TutorialWorldSession session{spawn_platform_world(), live};
            expect(session.blocks_remaining() == 400,
                   "live Scout inventory must publish its initial block stock");
            session.set_block_color({18U, 52U, 86U, 255U});
            expect(session.block_color() == battlespades::world::VxlColor{18U, 52U, 86U, 255U},
                   "SetColor must become the held/build palette without channel swapping");
            const std::vector<std::uint8_t> loadout{17U, 2U, 23U};
            const std::vector<std::string> prefabs{"bunker", "bridge", "tower"};
            const std::vector<std::uint8_t> ugc{44U, 45U};
            expect(session.apply_server_selection(12U, loadout, prefabs, ugc, std::uint8_t{45U}) &&
                       session.selected_tool_id() == 45U &&
                       session.inventory().slots().size() == 7U &&
                       session.blocks_remaining() == 2000,
                   "server loadout ACK must rebuild tools, prefabs and UGC slots");
            expect(session.apply_server_tool(17U) && session.selected_tool_id() == 45U,
                   "a delayed owner WorldUpdate tool must not replace local input");
            expect(!session.apply_server_tool(63U) && session.selected_tool_id() == 45U,
                   "server tool outside the normalized loadout must fail closed");

            const std::vector<std::uint8_t> objective_loadout{17U, 2U, 30U, 25U, 26U};
            expect(session.apply_server_selection(1U, objective_loadout, {}, {}, std::uint8_t{17U}),
                   "captured objective-capable loadout must be accepted");
            session.apply_server_movement_state(0U, 0U, 16U);
            expect(session.apply_server_tool(30U) && session.selected_tool_id() == 30U,
                   "owner pickup byte must expose intel before applying its tool byte");
            session.apply_server_movement_state(0U, 0U, 0xFFU);
            expect(session.selected_tool_id() != 30U && !session.apply_server_tool(30U),
                   "owner pickup clear must remove the dormant intel slot");
        }

        // Restock carries only an event type, so the client must reproduce
        // retail's partial ammo-crate rule. Infinite-block authority likewise
        // changes debiting without fabricating a different HUD count.
        {
            TutorialSessionConfig live;
            live.network_authoritative = true;
            live.initial_class_id = 1U;
            live.initial_loadout = {17U, 2U, 5U};
            live.initial_tool = std::uint8_t{17U};
            TutorialWorldSession session{spawn_platform_world(), live};
            const auto fresh_magazine = session.selected_ammo()->magazine;
            session.set_primary_held(true);
            session.tick();
            session.set_primary_held(false);
            session.tick();
            expect(session.selected_ammo()->magazine + 1U == fresh_magazine,
                   "ammo restock fixture must spend one local predicted round");
            session.restock_from_ammo_crate();
            expect(session.selected_ammo()->magazine + 1U == fresh_magazine,
                   "Restock type 3 must top rifle reserve without resetting its magazine");
            session.restock_ammunition();
            expect(session.selected_ammo()->magazine == fresh_magazine,
                   "Restock type 0 must remain the distinct full spawn reset");

            const auto block_stock = session.blocks_remaining();
            expect(session.spend_server_confirmed_blocks(3U) == 3U &&
                       session.blocks_remaining() == block_stock - 3,
                   "finite blocks must debit only on authoritative terrain echoes");
            session.set_infinite_blocks(true);
            expect(session.spend_server_confirmed_blocks(3U) == 0U &&
                       session.blocks_remaining() == block_stock - 3,
                   "TeamInfiniteBlocks must preserve the HUD wallet and suppress debits");
        }

        // Block Cannon spends the shared block wallet at launch; Disguise is
        // gated by the authoritative WorldUpdate bit rather than local hope.
        {
            TutorialSessionConfig live;
            live.network_authoritative = true;
            live.initial_class_id = 16U;
            live.initial_loadout = {29U, 64U, 2U, 5U};
            live.initial_tool = std::uint8_t{29U};
            TutorialWorldSession session{spawn_platform_world(), live};
            const auto before = session.blocks_remaining();
            session.set_primary_held(true);
            session.tick();
            session.set_primary_held(false);
            session.tick();
            const auto cannon_actions = session.take_weapon_actions();
            expect(std::ranges::any_of(
                       cannon_actions,
                       [](const auto& action) {
                           return action.kind ==
                                      battlespades::world::WeaponActionKind::oriented_item &&
                                  action.tool_id == 29U;
                       }) &&
                       session.blocks_remaining() == before - 1,
                   "Block Cannon launch must spend exactly one shared block");
            session.grant_blocks();
            expect(session.blocks_remaining() == before,
                   "Block Sucker grant damage must restore one shared block");

            expect(session.apply_server_tool(64U),
                   "Disguise fixture must select its normalized loadout tool");
            session.apply_server_movement_state(0U, 0x02U, 0xFFU);
            session.set_primary_held(true);
            session.tick();
            expect(session.take_weapon_actions().empty(),
                   "an already-disguised player must not emit another activation");
        }

        // Live prefab slots expand tool 23 into concrete names and emit the
        // semantic action consumed by the Protocol 168 packet-30 adapter.
        {
            TutorialSessionConfig live;
            live.network_authoritative = true;
            live.initial_position = {130.0, 70.0, 230.0};
            live.initial_orientation = {0.0, -1.0, 0.0};
            live.initial_class_id = 1U;
            live.initial_loadout = {17U, 2U, 5U, 23U};
            live.initial_prefabs = {"prefab_caltrop", "prefab_supertower", "prefab_superbridge"};
            TutorialWorldSession session{spawn_platform_world(), live};
            expect(session.equip_inventory_slot(4U) && session.selected_tool_id() == 23U &&
                       session.selected_prefab() == "prefab_supertower",
                   "combined inventory must select the concrete prefab variant");
            session.set_primary_held(true);
            session.tick();
            const auto actions = session.take_weapon_actions();
            expect(actions.size() == 1U &&
                       actions.front().kind ==
                           battlespades::world::WeaponActionKind::prefab_place &&
                       actions.front().tool_id == 23U,
                   "live prefab click must emit BuildPrefabAction semantics");
            const auto blocks_before_ack = session.blocks_remaining();
            expect(session.spend_server_confirmed_blocks(7U) == 7U &&
                       session.blocks_remaining() == blocks_before_ack - 7,
                   "accepted prefab voxels must debit the shared block wallet");
        }

        // Held forward input walks along the facing (-x) direction.
        {
            TutorialWorldSession session{map};
            for (int tick{}; tick < 60; ++tick) {
                session.tick();
            }
            session.set_action_held(TutorialAction::forward, true);
            for (int tick{}; tick < 120; ++tick) {
                session.tick();
            }
            expect(session.player().position.x < 138.0,
                   "held forward must move down the -x course");
            expect(std::fabs(session.player().position.y - 76.5) < 0.05,
                   "a straight walk must hold its lane line");

            // Losing focus clears held input: motion friction-decays to rest.
            // The recovered ground friction integrates a v*8-block glide
            // (walk speed 0.175 -> exactly 1.4 blocks), then the player stays
            // put.
            session.clear_input();
            expect(!session.action_held(TutorialAction::forward),
                   "clear_input must release held actions");
            const auto before = session.player().position.x;
            for (int tick{}; tick < 240; ++tick) {
                session.tick();
            }
            const auto drift = std::fabs(session.player().position.x - before);
            expect(drift < 1.6, "cleared input must friction-stop within the retail glide");
            expect(std::fabs(session.player().velocity.x) < 1e-3,
                   "cleared input must decay velocity to rest");
        }

        // Mouse look: retail degree model — sensitivity is degrees per raw
        // count, yaw decreases with rightward motion, pitch clamps at 89.9.
        {
            TutorialWorldSession session{map};
            session.apply_look_delta(100.0, 0.0);
            expect(std::fabs(session.yaw() - 10.0) < 1e-12,
                   "yaw must integrate 0.1 degrees per count toward strafe-right");
            session.apply_look_delta(0.0, 1e6);
            expect(session.pitch() == 89.9, "pitch must clamp at the retail down limit");
            session.apply_look_delta(0.0, -2e6);
            expect(session.pitch() == -89.9, "pitch must clamp at the retail up limit");

            TutorialSessionConfig inverted;
            inverted.invert_mouse = true;
            TutorialWorldSession inverted_session{map, inverted};
            inverted_session.apply_look_delta(0.0, 50.0);
            expect(inverted_session.pitch() < 0.0, "inverted mouse must pitch up on a pull back");

            TutorialWorldSession live_settings_session{map};
            live_settings_session.set_look_preferences(0.25, true);
            live_settings_session.apply_look_delta(20.0, 20.0);
            expect(std::fabs(live_settings_session.yaw() - 5.0) < 1e-12 &&
                       std::fabs(live_settings_session.pitch() + 5.0) < 1e-12,
                   "confirmed look settings must update an already-live session");
            live_settings_session.set_look_preferences(9.0, false);
            live_settings_session.apply_look_delta(1.0, 1.0);
            expect(std::fabs(live_settings_session.yaw() - 6.0) < 1e-12 &&
                       std::fabs(live_settings_session.pitch() + 4.0) < 1e-12,
                   "the runtime settings boundary must clamp malformed sensitivity");
        }

        // Jump from the ground rises and returns; crouch lowers the eye.
        {
            TutorialWorldSession session{map};
            for (int tick{}; tick < 120; ++tick) {
                session.tick();
            }
            static_cast<void>(session.take_movement_events());
            const auto standing_z = session.player().position.z;
            session.set_action_held(TutorialAction::jump, true);
            session.tick();
            session.set_action_held(TutorialAction::jump, false);
            expect(session.player().airborne, "a grounded jump must launch");
            const auto launch = session.take_movement_events();
            expect(launch.jumped, "the solver must publish the accepted jump edge");
            expect(!session.take_movement_events().jumped,
                   "movement edges must be consumed exactly once");
            double apex = standing_z;
            for (int tick{}; tick < 240; ++tick) {
                session.tick();
                apex = std::min(apex, session.player().position.z);
            }
            expect(standing_z - apex > 1.0, "jump apex must clear one block");
            expect(session.diagnostics().grounded, "jump must land again");
            expect(session.take_movement_events().landing_damage <= 0,
                   "an ordinary jump return must not become fall damage");

            session.set_action_held(TutorialAction::crouch, true);
            session.tick();
            expect(session.player().crouch, "held crouch must crouch");
            session.set_action_held(TutorialAction::crouch, false);
            session.tick();
            expect(!session.player().crouch, "released crouch must stand with headroom");
        }

        // Debug sandbox boundary: F4 exposes every Protocol 168 tool with
        // real per-tool ammo while preserving direct number selection and
        // the animated wheel selector semantics.
        {
            TutorialWorldSession session{map};
            session.debug_grant_full_loadout();
            static_cast<void>(session.take_inventory_event());
            const auto& slots = session.inventory().slots();
            expect(slots.size() == 65U && slots.front().tool_id == 0U &&
                       slots.back().tool_id == 64U,
                   "sandbox inventory must expose every selectable tool");
            expect(session.selected_tool_id() == 17U && session.selected_ammo() != nullptr,
                   "sandbox must begin on the stocked pistol");
            expect(session.equip_inventory_slot(0U) && session.selected_tool_id() == 0U,
                   "number 1 must directly equip the first catalog slot");
            auto event = session.take_inventory_event();
            expect(event.has_value() && !event->animate_toolbar &&
                       !session.inventory().toolbar_visible(),
                   "number selection must not start the wheel toolbar animation");
            session.cycle_tool(1);
            event = session.take_inventory_event();
            expect(event.has_value() && event->animate_toolbar &&
                       session.selected_tool_id() == 1U && session.inventory().toolbar_visible(),
                   "wheel-down must advance and animate the combined HUD index");
        }

        // RMB semantics are tool-owned. Twenty-two tools aim -- two through a
        // magnified scope and twenty through their own iron sights -- the
        // minigun spins, and secondary tools route through WeaponRuntime
        // instead of hiding the viewmodel.
        {
            TutorialWorldSession session{map};
            session.debug_grant_full_loadout();

            expect(session.equip_inventory_slot(18U), "sniper must be selectable");
            session.set_secondary_held(true);
            expect(session.zoomed() && session.magnified_scope() &&
                       std::fabs(session.zoom_target() - 1.5) < 1e-12,
                   "sniper RMB must enter its recovered 1.5x scope");
            // The scope opens over roughly half a second, so the press itself
            // must not have moved the projection anywhere near its target.
            expect(session.zoom_level() == 0.0,
                   "the aim ramp must not have advanced before a tick runs");
            session.tick();
            expect(std::fabs(session.zoom_level() - 1.5 / 7.5) < 1e-9,
                   "one tick must advance the ramp by the recovered sniper rate");
            for (int tick{}; tick < 120; ++tick)
                session.tick();
            expect(std::fabs(session.zoom_level() - 1.5) < 1e-12,
                   "a settled sniper scope must sit exactly on its multiplier");
            // The number the player actually sees. A divide-based formula puts
            // this at 50 degrees -- a scope barely a third as strong.
            expect(std::fabs(battlespades::world::zoom_fov_y_degrees(session.zoom_level()) -
                             18.75) < 1e-12,
                   "a settled sniper scope must render at 18.75 degrees");
            session.apply_look_delta(100.0, 0.0);
            expect(std::fabs(session.yaw() - 4.0) < 1e-12,
                   "sniper scope must apply its recovered 0.4 look sensitivity");
            session.set_secondary_held(false);
            expect(session.zoomed(), "releasing RMB must retain retail toggle ADS");
            session.set_secondary_held(true);
            expect(!session.zoomed(), "a second RMB press must toggle the same sight off");
            session.set_secondary_held(false);

            expect(session.equip_inventory_slot(7U), "SMG must be selectable");
            expect(session.zoom_level() == 0.0,
                   "changing tool must return the projection to hip fire");
            session.set_secondary_held(true);
            expect(session.zoomed() && !session.magnified_scope() &&
                       std::fabs(session.zoom_target() - 1.0) < 1e-12,
                   "an ordinary gun must aim through its own iron sights");
            for (int tick{}; tick < 120; ++tick)
                session.tick();
            expect(std::fabs(session.zoom_level() - 1.0) < 1e-12,
                   "settled iron sights sit at a multiplier of one, not zero");
            expect(std::fabs(battlespades::world::zoom_fov_y_degrees(session.zoom_level()) - 37.5) <
                       1e-12,
                   "settled iron sights must render at 37.5 degrees, a 2x view");
            const double before_aimed_look = session.yaw();
            session.apply_look_delta(100.0, 0.0);
            // Retail's Tool base default, which every non-sniper inherits. A
            // neutral 1.0 here would aim at double the correct speed.
            expect(std::fabs(session.yaw() - before_aimed_look - 5.0) < 1e-12,
                   "iron sights must apply the recovered 0.5 base look factor");
            session.set_secondary_held(false);
            expect(session.zoomed(), "iron sights must hold through the release, like the scope");
            session.set_secondary_held(true);
            expect(!session.zoomed(), "iron sights must toggle off on the next press");
            session.set_secondary_held(false);
            for (int tick{}; tick < 120; ++tick)
                session.tick();
            expect(session.zoom_level() == 0.0,
                   "leaving iron sights must ramp all the way back to hip fire");

            expect(session.equip_inventory_slot(8U), "minigun must be selectable");
            const auto* minigun_before = session.selected_ammo();
            const auto minigun_rounds = minigun_before != nullptr ? minigun_before->magazine : 0U;
            session.set_secondary_held(true);
            for (int tick{}; tick < 20; ++tick)
                session.tick();
            expect(!session.zoomed() && session.weapon_mechanism_phase() > 0.0 &&
                       session.selected_ammo() != nullptr &&
                       session.selected_ammo()->magazine == minigun_rounds,
                   "minigun RMB must spin visibly without aiming or spending ammo");
            session.set_secondary_held(false);

            expect(session.equip_inventory_slot(11U), "grenade must be selectable");
            session.set_secondary_held(true);
            expect(!session.zoomed(), "a sightless throwable must not inherit generic aim");
            session.set_secondary_held(false);

            expect(session.equip_inventory_slot(59U), "C4 must be selectable");
            session.set_secondary_held(true);
            expect(!session.zoomed(), "C4 secondary must never enter aim");
            session.tick();
            const auto actions = session.take_weapon_actions();
            expect(actions.size() == 1U &&
                       actions.front().kind == battlespades::world::WeaponActionKind::c4_detonate &&
                       actions.front().secondary,
                   "C4 RMB must route the recovered detonation action");
            session.set_secondary_held(false);
        }

        // The minigun owns two concurrent audio/mechanism states: its barrel
        // motor and its quantised fire loop.  Character.reload keeps the motor
        // update alive with the inactive ramp, but makes can_fire false
        // immediately.  Pin that at the session boundary used by the frontend;
        // a WeaponRuntime-only test cannot catch a stale `weapon_trigger_live`
        // presentation signal keeping the local loop alive through reload.
        {
            TutorialWorldSession session{map};
            session.debug_grant_full_loadout();
            expect(session.equip_inventory_slot(8U), "minigun reload fixture must select tool 8");
            session.set_secondary_held(true);
            for (int tick{}; tick < 120; ++tick)
                session.tick();
            expect(session.weapon_spin_fraction() > 0.99,
                   "minigun reload fixture must reach full barrel speed");

            session.set_primary_held(true);
            session.tick();
            static_cast<void>(session.take_weapon_actions());
            expect(session.request_reload() ==
                       battlespades::world::WeaponStateResult::accepted,
                   "a minigun that spent a round must accept reload");
            expect(!session.weapon_trigger_live(),
                   "reload must close the minigun fire loop on the same frontend frame");
            const double spin_before_reload = session.weapon_spin_fraction();
            for (int tick{}; tick < 60; ++tick)
                session.tick();
            expect(session.weapon_spin_fraction() < spin_before_reload &&
                       !session.weapon_trigger_live(),
                   "reload must wind the minigun motor down without reviving its fire loop");
        }

        // Tool changes cancel the outgoing input and animation state. This
        // regresses F4 + wheel changes replaying a shot/melee pose on the new
        // hands or continuing a held action after selection.
        {
            TutorialWorldSession session{map};
            session.debug_grant_full_loadout();
            expect(session.equip_inventory_slot(17U), "transition test must select the pistol");
            session.set_primary_held(true);
            session.tick();
            expect(session.seconds_since_primary() < 0.1,
                   "fixture must begin with an active weapon animation");
            expect(!session.take_weapon_actions().empty(),
                   "fixture attack must emit before testing transition isolation");
            session.cycle_tool(1);
            expect(session.seconds_since_primary() > 1.0e8 &&
                       session.weapon_action_sequence() == 0U && !session.zoomed(),
                   "wheel selection must return the new hands to idle");
            session.tick();
            expect(session.take_weapon_actions().empty(),
                   "the old held trigger must not fire the newly selected tool");
        }

        // Cook duration changes both remaining fuse and throw velocity. The
        // retail normal-grenade curve is 25 + cooked_fraction * 50.
        {
            TutorialWorldSession session{spawn_platform_world()};
            session.debug_grant_full_loadout();
            expect(session.equip_inventory_slot(11U), "normal grenade must be selectable");
            session.set_primary_held(true);
            for (int tick{}; tick < 30; ++tick)
                session.tick();
            expect(session.projectiles().empty(),
                   "a grenade must remain modeled in-hand while cooking");
            session.set_primary_held(false);
            session.tick();
            expect(session.projectiles().size() == 1U,
                   "releasing a cooked grenade must create one projectile");
            const auto& grenade = session.projectiles().front();
            expect(grenade.behavior == battlespades::world::TutorialProjectileBehavior::bounce,
                   "normal grenade must use bounce physics");
            expect(std::fabs(grenade.velocity.x + 35.0) < 0.1,
                   "half-second cook must raise normal throw speed from 25 to 35");
            expect(grenade.remaining > 1.9 && grenade.remaining < 2.05,
                   "half-second cook must leave about two seconds of fuse");
            for (int tick{}; tick < 140 && !session.projectiles().empty(); ++tick) {
                session.tick();
            }
            expect(session.projectiles().empty(),
                   "cooked grenade must detonate when its remaining fuse expires");
            const auto impacts = session.take_terrain_impacts();
            expect(std::ranges::any_of(impacts,
                                       [](const auto& impact) {
                                           return impact.kind ==
                                                  battlespades::world::TerrainImpactKind::explosion;
                                       }),
                   "grenade detonation must dispatch the explosion particle/sound event");
        }

        // Molotov charge and terrain data are distinct from grenade cooking:
        // retail throws at 35 + charge*40, with a four-block impact crater.
        // Its persistent block-fire spread is radius two and must never shadow
        // the primary impact radius during suffix lookup.
        {
            TutorialWorldSession session{spawn_platform_world()};
            session.debug_grant_full_loadout();
            expect(session.equip_inventory_slot(33U), "Molotov must be selectable");
            session.set_primary_held(true);
            for (int tick{}; tick < 90; ++tick)
                session.tick();
            session.set_primary_held(false);
            session.tick();
            expect(session.projectiles().size() == 1U,
                   "Molotov release must create one projectile");
            const auto& molotov = session.projectiles().front();
            const double speed = std::sqrt(molotov.velocity.x * molotov.velocity.x +
                                           molotov.velocity.y * molotov.velocity.y +
                                           molotov.velocity.z * molotov.velocity.z);
            expect(std::fabs(speed - 55.0) < 0.2 && molotov.crater_radius == 4U &&
                       std::fabs(molotov.block_damage - 3.0) < 0.01,
                   "Molotov must use its recovered half-charge speed and impact damage");
        }

        // The Specialist grenade launcher is an oriented projectile weapon,
        // not a hitscan gun or an in-hand throwable. Its recovered constants
        // specify a 75-unit launch, three-second failsafe, four-block crater,
        // and six block-damage points on contact.
        {
            TutorialWorldSession session{spawn_platform_world()};
            session.debug_grant_full_loadout();
            expect(session.equip_inventory_slot(55U), "grenade launcher must be selectable");
            session.set_primary_held(true);
            session.tick();
            session.set_primary_held(false);
            expect(session.projectiles().size() == 1U,
                   "grenade launcher fire must create one projectile");
            const auto& projectile = session.projectiles().front();
            const double speed = std::sqrt(projectile.velocity.x * projectile.velocity.x +
                                           projectile.velocity.y * projectile.velocity.y +
                                           projectile.velocity.z * projectile.velocity.z);
            expect(projectile.behavior ==
                           battlespades::world::TutorialProjectileBehavior::contact &&
                       std::fabs(speed - 75.0) < 0.1,
                   "grenade launcher must use recovered contact behavior and speed");
            expect(projectile.remaining > 2.9 && projectile.remaining <= 3.0 &&
                       projectile.crater_radius == 4U &&
                       std::fabs(projectile.block_damage - 6.0) < 0.01,
                   "grenade launcher must use recovered lifespan and terrain damage");
        }

        // Native ZombieHands expands one hit into a centered 3x3x3 cube.
        {
            auto dig_map = spawn_platform_world();
            for (std::uint32_t x{136U}; x <= 138U; ++x)
                for (std::uint32_t y{75U}; y <= 77U; ++y)
                    for (std::uint32_t z{229U}; z <= 231U; ++z)
                        expect(dig_map->set_voxel(x, y, z, VxlColor{120U, 90U, 70U, 255U}),
                               "zombie dig fixture must be authored");
            for (std::uint32_t z{229U}; z <= 233U; ++z)
                expect(dig_map->set_voxel(136U, 78U, z, VxlColor{120U, 90U, 70U, 255U}),
                       "zombie dig fixture must be anchored");
            const auto before = dig_map->solid_voxels();
            TutorialWorldSession session{dig_map};
            session.debug_grant_full_loadout();
            expect(session.equip_inventory_slot(24U), "Zombie Hands must be selectable");
            session.set_primary_held(true);
            session.tick();
            session.set_primary_held(false);
            // The first surface cell is x=138, so the centered native cube
            // overlaps the authored wall at x=137..138 (18 solid cells) and
            // x=139 is air. The x=136 layer is deliberately outside it.
            expect(session.map().solid_voxels() == before - 18U,
                   "one Zombie Hands swing must clear the solid part of its 27-cell cube");
            for (std::uint32_t y{75U}; y <= 77U; ++y)
                for (std::uint32_t z{229U}; z <= 231U; ++z)
                    expect(session.map().solid(136U, y, z),
                           "Zombie Hands must not dig outside the centered cube");
        }

        // Every digging family owns a distinct server-visible footprint. Keep
        // these counts pinned so a future animation or weapon refactor cannot
        // silently regress back to one generic raycast cell.
        const auto affected_by_melee = [](std::uint8_t tool_id, bool secondary = false) {
            auto dig_map = spawn_platform_world();
            for (std::uint32_t x{136U}; x <= 138U; ++x)
                for (std::uint32_t y{75U}; y <= 77U; ++y)
                    for (std::uint32_t z{229U}; z <= 231U; ++z)
                        expect(dig_map->set_voxel(x, y, z, VxlColor{120U, 90U, 70U, 255U}),
                               "melee footprint fixture must be authored");
            for (std::uint32_t z{229U}; z <= 233U; ++z)
                expect(dig_map->set_voxel(136U, 78U, z, VxlColor{120U, 90U, 70U, 255U}),
                       "melee footprint fixture must be anchored");
            TutorialWorldSession session{dig_map};
            session.debug_grant_full_loadout();
            expect(session.equip_inventory_slot(tool_id),
                   "melee footprint tool must be selectable");
            if (secondary) {
                session.set_secondary_held(true);
                // Covers the Classic Spade's recovered 0.8 second delay; the
                // UGC Super Spade fires only on its initial edge.
                for (int tick{}; tick < 52; ++tick)
                    session.tick();
                session.set_secondary_held(false);
            } else {
                session.set_primary_held(true);
                session.tick();
                session.set_primary_held(false);
            }
            std::size_t affected{};
            for (std::uint32_t x{136U}; x <= 138U; ++x)
                for (std::uint32_t y{75U}; y <= 77U; ++y)
                    for (std::uint32_t z{229U}; z <= 231U; ++z) {
                        if (!dig_map->solid(x, y, z) || dig_map->damage_fraction(x, y, z) > 0.0F) {
                            ++affected;
                        }
                    }
            return affected;
        };
        expect(affected_by_melee(0U) == 1U, "Pickaxe must affect one cell");
        expect(affected_by_melee(1U) == 1U,
               "Knife must affect one cell without pretending its low damage destroys it");
        expect(affected_by_melee(34U) == 1U, "Crowbar must affect one cell");
        expect(affected_by_melee(2U) == 3U && affected_by_melee(4U) == 3U &&
                   affected_by_melee(4U, true) == 3U,
               "Spade and both Classic Spade attacks must affect a Z column");
        expect(affected_by_melee(3U) == 18U && affected_by_melee(24U) == 18U,
               "Super Spade and Zombie Hands must affect the solid part of a 3x3x3 cube");
        expect(affected_by_melee(45U) == 1U && affected_by_melee(45U, true) == 18U,
               "UGC Super Spade must split single-cell LMB and cubic RMB");
        expect(affected_by_melee(50U) == 2U, "Machete must affect its recovered two-cell Z column");

        // F5 debug class selection changes the FPS arm owner without losing
        // the weapon under inspection. Shift/F5 direction wraps all classes.
        {
            TutorialWorldSession session{map};
            session.debug_grant_full_loadout();
            expect(session.equip_inventory_slot(18U), "class test needs sniper selected");
            expect(session.debug_class_id() == 0U, "debug sandbox starts as Soldier");
            session.debug_cycle_class(1);
            expect(session.debug_class_id() == 1U && session.selected_tool_id() == 18U,
                   "class forward must select Scout and preserve the tool");
            session.debug_cycle_class(-1);
            expect(session.debug_class_id() == 0U && session.selected_tool_id() == 18U,
                   "class backward must restore Soldier and preserve the tool");
            session.debug_cycle_class(-1);
            expect(session.debug_class_id() == 17U,
                   "class backward from Soldier must wrap to Medic");
        }

        // A red bullseye hit detaches the complete authored red/white face,
        // never leaving its white ring floating in the collision map.
        {
            auto target_map = spawn_platform_world();
            expect(target_map->set_voxel(39U, 62U, 221U, VxlColor{228U, 51U, 52U, 255U}) &&
                       target_map->set_voxel(39U, 61U, 221U, VxlColor{232U, 233U, 233U, 255U}),
                   "target fixture must be authored");
            TutorialWorldSession session{target_map};
            session.debug_grant_full_loadout();
            expect(session.equip_inventory_slot(18U),
                   "target test must select the zero-spread zoomed sniper");
            const double dx = 39.0 - session.player().position.x;
            const double dy = 62.0 - session.player().position.y;
            const double dz = 221.0 - session.player().position.z;
            const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
            const double yaw = std::atan2(-dy, -dx) * 180.0 / 3.14159265358979323846;
            const double pitch = std::asin(dz / length) * 180.0 / 3.14159265358979323846;
            session.apply_look_delta(yaw / 0.1, pitch / 0.1);
            session.set_secondary_held(true);
            session.set_primary_held(true);
            session.tick();
            session.set_primary_held(false);
            session.set_secondary_held(false);
            expect(!session.map().solid(39U, 62U, 221U) && !session.map().solid(39U, 61U, 221U),
                   "red and white target-face cells must detach together");
            const auto falling = session.take_falling_components();
            expect(falling.size() == 1U && falling.front().size() == 2U,
                   "the target face must be one coherent falling component");
        }

        // A live match leaves damage and ammunition authoritative, but the
        // firing client still owns the immediate camera kick.
        {
            TutorialSessionConfig live;
            live.network_authoritative = true;
            live.initial_position = {130.0, 70.0, 230.75};
            live.initial_orientation = {-1.0, 0.0, 0.0};
            live.initial_class_id = 0U;
            live.initial_loadout = {12U};
            live.initial_tool = static_cast<std::uint8_t>(12U);
            TutorialWorldSession session{spawn_platform_world(), live};
            const double before = session.pitch();
            session.set_primary_held(true);
            session.tick();
            const auto actions = session.take_weapon_actions();
            expect(!actions.empty() && session.pitch() < before,
                   "network-authoritative firing must retain local camera recoil");
        }

        // Launcher sandbox: the shared runtime spends real ammo, applies
        // catalog recoil/sight state and emits a moving projectile at retail speed.
        {
            TutorialWorldSession session{spawn_platform_world()};
            session.debug_grant_full_loadout();
            expect(session.equip_inventory_slot(12U), "catalog slot 12 must select the RPG");
            const auto* before = session.selected_ammo();
            expect(before != nullptr && before->magazine == 1U,
                   "RPG must start with its recovered one-round magazine");
            session.set_secondary_held(true);
            // The bazooka's own rpg_sight.kv6 makes it one of the twenty iron
            // sight weapons; retail overwrites its `sight = None` on a later
            // line, and the later binding is the one that ships.
            expect(session.zoomed() && !session.magnified_scope(),
                   "the RPG must aim through its own sight, not a sniper scope");
            session.set_secondary_held(false);
            session.set_secondary_held(true);
            session.set_secondary_held(false);
            session.set_primary_held(true);
            session.tick();
            session.set_primary_held(false);
            const auto* after = session.selected_ammo();
            expect(after != nullptr && after->magazine == 0U,
                   "launching must spend one real round");
            expect(session.projectiles().size() == 1U,
                   "RPG action must create a visible simulated projectile");
            const auto& projectile = session.projectiles().front();
            const double speed = std::sqrt(projectile.velocity.x * projectile.velocity.x +
                                           projectile.velocity.y * projectile.velocity.y +
                                           projectile.velocity.z * projectile.velocity.z);
            expect(speed > 70.0 && speed < 80.0,
                   "RPG projectile must use the recovered 75-block speed");
            expect(session.pitch() < 0.0, "negative catalog recoil-up must lift the camera");
        }

        // Primary attack: the pistol needs two 3-damage hits to break a
        // 5-health block, the spade one 5-damage chop, and placing a block
        // spends supply and dirties the touched chunk.
        {
            TutorialWorldSession session{spawn_platform_world()};
            for (int tick{}; tick < 120; ++tick) {
                session.tick();
            }
            session.debug_grant_full_loadout();
            session.equip_tool(TutorialTool::pistol);
            static_cast<void>(session.take_dirty_chunks());
            // Pitch exactly 45 degrees down the -x course: the ray strikes
            // the platform a safe 2.5 blocks ahead of the player's feet.
            session.apply_look_delta(0.0, 1e6);
            session.apply_look_delta(0.0, -449.0);
            const auto solid_before = session.map().solid_voxels();
            const auto fire_once = [&session]() {
                session.set_primary_held(true);
                session.tick();
                session.set_primary_held(false);
                for (int tick{}; tick < 30; ++tick) {
                    session.tick(); // let the 0.3 s interval lapse
                }
            };
            fire_once();
            auto events = session.take_attack_events();
            expect(events.pistol_fired, "a fresh clip must fire on click");
            expect(session.pistol_clip() == 5, "firing must spend one round");
            expect(session.map().solid_voxels() == solid_before,
                   "one pistol hit must leave a 5-health block standing");
            fire_once();
            expect(session.map().solid_voxels() == solid_before - 1U,
                   "the second pistol hit must break the block");
            expect(!session.take_dirty_chunks().empty(), "block edits must dirty their chunk");

            session.equip_tool(TutorialTool::spade);
            session.set_primary_held(true);
            session.tick();
            session.set_primary_held(false);
            events = session.take_attack_events();
            expect(events.spade_swung, "the spade must swing on click");
            expect(events.spade_hit_block && session.map().solid_voxels() == solid_before - 2U,
                   "one spade chop must break the struck block");

            session.equip_tool(TutorialTool::block);
            const auto supply = session.blocks_remaining();
            expect(supply > 0, "the debug loadout must stock blocks");
            // Steepen to 65 degrees: the hit cell's roof neighbor clears the
            // player's bounding box, so the build must succeed.
            session.apply_look_delta(0.0, 1e6);
            session.apply_look_delta(0.0, -249.0);
            for (int tick{}; tick < 30; ++tick) {
                session.tick();
            }
            session.set_primary_held(true);
            session.tick();
            session.set_primary_held(false);
            events = session.take_attack_events();
            expect(events.block_placed, "clicking with the block tool must build");
            expect(session.blocks_remaining() == supply - 1, "building must spend one block");
            expect(session.map().solid_voxels() == solid_before - 1U,
                   "building must restore one solid voxel");
        }

        // Packet-21 entities keep their server-assigned ids. Duplicate creates
        // are inert like retail, while malformed records fail without
        // disturbing the live registry.
        {
            TutorialSessionConfig live;
            live.network_authoritative = true;
            TutorialWorldSession session{map, live};
            LocalEntity capture;
            capture.id = 60'000U;
            capture.type = 25U;
            capture.position = {125.0, 75.0, 230.0};
            capture.team = 2U;
            capture.face = 4U;
            capture.color = {44U, 117U, 179U};
            capture.has_color = true;
            expect(session.apply_server_entity(capture),
                   "valid authoritative entity must be installed");
            expect(session.entities().size() == 1U && session.entities().front().id == 60'000U &&
                       session.entities().front().type == 25U &&
                       session.entities().front().color ==
                           std::array<std::uint8_t, 3U>{44U, 117U, 179U},
                   "server id, type, and packet tint must survive unchanged");
            auto duplicate = capture;
            duplicate.position.x = 400.0;
            expect(session.apply_server_entity(duplicate) &&
                       session.entities().front().position.x == 125.0,
                   "duplicate CreateEntity must be inert");
            auto malformed = capture;
            malformed.id = 60'001U;
            malformed.type = 255U;
            expect(!session.apply_server_entity(malformed) && session.entities().size() == 1U,
                   "unknown entity type must fail atomically");
            expect(session.apply_server_entity_update(
                       {60'000U, ServerEntityProperty::position, {126.0, 76.0, 229.0}}) &&
                       session.entities().front().position.x == 126.0 &&
                       session.entities().front().position.y == 76.0 &&
                       session.entities().front().position.z == 229.0 &&
                       session.entities().front().home.x == 126.0 &&
                       session.entities().front().home.y == 76.0 &&
                       session.entities().front().home.z == 229.0,
                   "ChangeEntity position must update the body and pickup home");
            expect(session.apply_server_entity_update(
                       {60'000U, ServerEntityProperty::target, {}, 0.0, 7}) &&
                       session.entities().front().target ==
                           std::optional<std::uint8_t>{static_cast<std::uint8_t>(7U)},
                   "ChangeEntity target must update the authoritative target");
            const auto target_events = session.take_entity_events();
            expect(std::ranges::any_of(target_events,
                                       [](const EntityEvent& event) {
                                           return event.kind == EntityEventKind::target_changed &&
                                                  event.value == 1.0;
                                       }),
                   "target acquisition must reach presentation exactly once");
            expect(session.apply_server_entity_update(
                       {60'000U, ServerEntityProperty::forward, {-1.0, 0.0, 0.0}}) &&
                       std::abs(session.entities().front().yaw) < 1e-6,
                   "forward -X must map to retail entity yaw zero");
            expect(
                !session.apply_server_entity_update({60'000U, ServerEntityProperty::forward, {}}),
                "zero ChangeEntity forward vector must fail atomically");
            expect(session.despawn_entity(60'000U) && session.entities().empty(),
                   "DestroyEntity must remove the exact wire id");
        }

        // WorldUpdate repeats the packet-21 spawn anchor. Terrain-owned props
        // must keep their locally simulated fall instead of being rewound to
        // that airborne anchor on every snapshot.
        for (const std::uint8_t type :
             {std::uint8_t{8U}, std::uint8_t{11U}, std::uint8_t{30U}}) {
            TutorialSessionConfig live;
            live.network_authoritative = true;
            TutorialWorldSession session{spawn_platform_world(), live};
            LocalEntity prop;
            prop.id = 60'010U + type;
            prop.type = type;
            prop.position = {130.5, 75.5, 225.0};
            prop.velocity = {0.0, 0.0, 0.0};
            prop.face = 4U;
            expect(session.apply_server_entity(prop),
                   "airborne replicated prop fixture must be accepted");

            for (int tick{}; tick < 8; ++tick) {
                session.tick();
            }
            const auto locally_fallen_z = session.entities().front().position.z;
            expect(locally_fallen_z > prop.position.z,
                   "replicated prop presentation must begin falling toward the VXL");

            expect(session.apply_server_entity_snapshot(prop) &&
                       std::abs(session.entities().front().position.z - locally_fallen_z) < 1e-9,
                   "duplicate server anchor must not rewind local entity gravity");

            auto relocated = prop;
            relocated.position = {132.5, 76.5, 226.0};
            expect(session.apply_server_entity_snapshot(relocated) &&
                       session.entities().front().position.x == 132.5 &&
                       session.entities().front().position.y == 76.5 &&
                       session.entities().front().position.z == 226.0 &&
                       session.entities().front().home.x == 132.5,
                   "a genuinely changed server anchor must still relocate the entity");
        }

        // Protocol 168 packet 10 carries observer grenade flight, including
        // the already-cooked remaining fuse. Live presentation must simulate
        // it and emit the blast while leaving terrain changes to packets 22/32/33.
        {
            TutorialSessionConfig live;
            live.network_authoritative = true;
            TutorialWorldSession session{spawn_platform_world(), live};
            const auto solids = session.map().solid_voxels();
            expect(session.apply_server_oriented_item(
                       7U, 11U, 0.05, {130.0, 70.0, 228.0}, {12.0, 0.0, 0.0}),
                   "UseOrientedItem must create a supported observer grenade");
            expect(session.projectiles().size() == 1U &&
                       session.projectiles().front().source_player ==
                           std::optional<std::uint8_t>{std::uint8_t{7U}},
                   "replicated grenade must preserve its remote owner");
            session.tick();
            expect(session.projectiles().size() == 1U &&
                       session.projectiles().front().position.x > 130.0,
                   "replicated grenade must advance between network packets");
            for (int tick{}; tick < 8 && !session.projectiles().empty(); ++tick) {
                session.tick();
            }
            const auto impacts = session.take_terrain_impacts();
            expect(session.projectiles().empty() &&
                       std::ranges::any_of(
                           impacts,
                           [](const auto& impact) {
                               return impact.kind ==
                                          battlespades::world::TerrainImpactKind::explosion &&
                                      impact.source_tool == 11U;
                           }),
                   "replicated grenade fuse expiry must emit its explosion");
            expect(session.map().solid_voxels() == solids,
                   "replicated grenade VFX must never predict a live terrain crater");
        }

        // Bazooka-family rockets are packet-21 moving entities, not packet-10
        // grenades. The client integrates them until packet 19 and turns that
        // authoritative destruction edge into the retail rocket blast.
        {
            TutorialSessionConfig live;
            live.network_authoritative = true;
            TutorialWorldSession session{spawn_platform_world(), live};
            const auto solids = session.map().solid_voxels();
            LocalEntity rocket;
            rocket.id = 61'000U;
            rocket.type = 21U;
            rocket.position = {130.0, 70.0, 228.0};
            rocket.velocity = {75.0, 0.0, 0.0};
            expect(session.apply_server_entity(rocket),
                   "CreateEntity must install an RPG projectile");
            session.tick();
            expect(session.entities().size() == 1U && session.entities().front().position.x > 131.0,
                   "RPG CreateEntity must move at its packet velocity");
            expect(session.destroy_server_entity(61'000U) && session.entities().empty(),
                   "DestroyEntity must remove the authoritative RPG");
            const auto impacts = session.take_terrain_impacts();
            const auto blast = std::ranges::find_if(impacts, [](const auto& impact) {
                return impact.kind == battlespades::world::TerrainImpactKind::explosion &&
                       impact.source_tool == 12U;
            });
            expect(blast != impacts.end(), "RPG DestroyEntity must emit an RPG explosion");
            expect(blast->position.has_value() && (*blast->position)[0U] > 131.0F &&
                       (*blast->position)[0U] < 132.0F &&
                       std::abs(blast->source_velocity[0U] - 75.0F) < 0.01F,
                   "RPG delete VFX must preserve the exact moving entity position and velocity");

            battlespades::world::ParticleSystem particles;
            battlespades::world::TerrainEffectSimulation effects;
            effects.set_particle_sink(&particles);
            effects.spawn_impact(*blast);
            expect(particles.live_count() == 18U,
                   "network RPG deletion must enter the eight-glow/ten-debris compositor");
            for (int tick{}; tick < 3; ++tick) {
                particles.tick_unbounded(1.0 / 60.0);
            }
            particles.build_draw_list({}, 0.0F);
            const auto smoke_children = std::ranges::count_if(
                particles.batches(), [](const auto& batch) {
                    return batch.atlas == battlespades::world::ParticleAtlas::smoke_trail &&
                           batch.color_mode ==
                               battlespades::world::ParticleColorMode::smoke_lut;
                });
            expect(particles.live_count() == 42U && smoke_children == 1,
                   "network RPG blast must grow the original SmokeTrail/particle_lut fingers");
            expect(session.map().solid_voxels() == solids,
                   "RPG delete VFX must not race authoritative terrain changes");
        }

        // Drill CreateEntity spins every presentation tick, each Damage packet
        // owns a bore contact, and only DestroyEntity owns the terminal blast.
        {
            TutorialSessionConfig live;
            live.network_authoritative = true;
            TutorialWorldSession session{spawn_platform_world(), live};
            LocalEntity drill;
            drill.id = 103U;
            drill.type = 23U;
            drill.owner = 7U;
            drill.position = {130.0, 70.0, 228.0};
            drill.velocity = {20.0, 0.0, 0.0};
            expect(session.apply_server_entity(drill),
                   "CreateEntity must install a Drill projectile");
            session.tick();
            expect(session.entities().front().accumulators[0U] > 9.9,
                   "Drill projectile must advance its recovered ten-degree roll tick");
            expect(!session.apply_server_drill_contact(-1, 8U, {131.0, 70.0, 228.0}),
                   "a bore packet must not attach to another player's Drill");
            expect(session.apply_server_drill_contact(103, 7U, {131.0, 70.0, 228.0}) &&
                       session.entities().front().drilling_audio_remaining == 0.5,
                   "Damage must refresh the Drill's recovered half-second contact loop");
            for (int tick{}; tick < 15; ++tick) {
                session.tick();
            }
            expect(session.entities().front().drilling_audio_remaining > 0.2,
                   "the Drill contact loop must remain live during its recovered tail");
            for (int tick{}; tick < 20; ++tick) {
                session.tick();
            }
            expect(session.entities().front().drilling_audio_remaining == 0.0,
                   "the Drill contact loop must close after half a second without contact");
            expect(session.destroy_server_entity(103U),
                   "DestroyEntity must remove the authoritative Drill");
            const auto impacts = session.take_terrain_impacts();
            expect(std::ranges::any_of(impacts, [](const auto& impact) {
                       return impact.kind ==
                                  battlespades::world::TerrainImpactKind::explosion &&
                              impact.source_tool == 14U;
                   }),
                   "Drill DestroyEntity must emit its one terminal Drillgun explosion");
        }

        // Entity 11 survives the corpse-to-grave transition. Its final packet
        // 19 is the seven-second grave detonation, not a silent despawn.
        {
            TutorialSessionConfig live;
            live.network_authoritative = true;
            TutorialWorldSession session{spawn_platform_world(), live};
            LocalEntity grave;
            grave.id = 61'002U;
            grave.type = 11U;
            grave.position = {130.25, 70.75, 228.5};
            grave.color = {55U, 125U, 215U};
            grave.has_color = true;
            expect(session.apply_server_entity(grave),
                   "CreateEntity must install the authoritative grave");
            expect(session.destroy_server_entity(61'002U) && session.entities().empty(),
                   "DestroyEntity must remove the authoritative grave");
            const auto impacts = session.take_terrain_impacts();
            const auto burst = std::ranges::find_if(impacts, [](const auto& impact) {
                return impact.kind == battlespades::world::TerrainImpactKind::grave_explosion;
            });
            expect(burst != impacts.end() && burst->position.has_value(),
                   "grave DestroyEntity must emit a positioned death explosion");
            expect(std::abs((*burst->position)[0U] - 130.25F) < 0.001F && burst->color.red == 55U &&
                       burst->color.green == 125U && burst->color.blue == 215U,
                   "grave burst must preserve its exact position and packet colour");
        }

        // The server owns Block Cannon placement but its projectile deletion
        // is only a compact snow impact. It must never enter the explosion
        // compositor (which adds an RPG flash and dynamic light).
        {
            TutorialSessionConfig live;
            live.network_authoritative = true;
            TutorialWorldSession session{spawn_platform_world(), live};
            LocalEntity snowball;
            snowball.id = 61'001U;
            snowball.type = 24U;
            snowball.position = {130.0, 70.0, 228.0};
            snowball.velocity = {50.0, 0.0, 0.0};
            expect(session.apply_server_entity(snowball),
                   "CreateEntity must install a Block Cannon snowball");
            expect(session.destroy_server_entity(61'001U),
                   "DestroyEntity must remove the Block Cannon snowball");
            const auto impacts = session.take_terrain_impacts();
            expect(std::ranges::any_of(
                       impacts,
                       [](const auto& impact) {
                           return impact.kind ==
                                      battlespades::world::TerrainImpactKind::block_cannon &&
                                  impact.source_tool == 29U;
                       }) &&
                       std::ranges::none_of(
                           impacts,
                           [](const auto& impact) {
                               return impact.kind ==
                                      battlespades::world::TerrainImpactKind::explosion;
                           }),
                   "Block Cannon deletion must emit smoke, never an explosion");
        }

        // A live client presents deployables; it never runs a second copy of
        // the server's targeting or proximity rules. Previously this turret
        // aimed at the observing player and this mine detonated after arming,
        // even when both carried the observer's team state.
        {
            TutorialSessionConfig live;
            live.network_authoritative = true;
            TutorialWorldSession session{spawn_platform_world(), live};
            const auto origin = session.player().position;
            const auto starting_health = session.health();

            LocalEntity mine;
            mine.id = 61'010U;
            mine.type = 9U;
            mine.team = 2U;
            mine.owner = 7U;
            mine.position = origin;
            expect(session.apply_server_entity(mine),
                   "CreateEntity must install a friendly landmine");

            LocalEntity turret;
            turret.id = 61'011U;
            turret.type = 8U;
            turret.team = 2U;
            turret.owner = 7U;
            turret.position = {origin.x + 2.0, origin.y, origin.z};
            expect(session.apply_server_entity(turret),
                   "CreateEntity must install a friendly rocket turret");
            static_cast<void>(session.take_entity_events());

            for (int tick{}; tick < 360; ++tick) {
                session.tick();
            }
            const auto events = session.take_entity_events();
            expect(session.entities().size() == 2U && session.projectiles().empty() &&
                       session.health() == starting_health &&
                       std::ranges::none_of(events,
                                            [](const auto& event) {
                                                return event.kind == EntityEventKind::detonated ||
                                                       event.kind == EntityEventKind::fired ||
                                                       event.kind ==
                                                           EntityEventKind::target_changed;
                                            }),
                   "live deployables must wait for server target/fire/destroy packets");
            expect(session.apply_server_turret_aim(61'011U, 72.5, -14.25),
                   "WorldUpdate must articulate an existing rocket turret");
            const auto found = std::ranges::find(session.entities(), 61'011U, &LocalEntity::id);
            expect(found != session.entities().end() && found->yaw == 72.5 &&
                       found->pitch == -14.25 && found->aim_yaw == 72.5 &&
                       found->aim_pitch == -14.25,
                   "turret model must follow the server angle, not the local camera");
        }

        // Bootstrap: Training.vxl parses and meshes off-thread with bounded
        // draining, ending ready with every chunk delivered exactly once.
        {
            TutorialWorldBootstrap bootstrap{AOS_TRAINING_VXL, 4U};
            bootstrap.start();
            std::size_t drained{};
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes{2};
            for (;;) {
                const auto progress = bootstrap.progress();
                expect(progress.stage != BootstrapStage::failed, progress.error.c_str());
                drained += bootstrap.take_ready_meshes(16U).size();
                if (progress.stage == BootstrapStage::ready) {
                    drained += bootstrap.take_ready_meshes(2'000U).size();
                    break;
                }
                expect(std::chrono::steady_clock::now() < deadline,
                       "bootstrap must finish within the test deadline");
                std::this_thread::sleep_for(std::chrono::milliseconds{5});
            }
            expect(drained == 1'024U, "bootstrap must deliver every chunk exactly once");
            expect(bootstrap.map() != nullptr, "bootstrap must publish the parsed map");
            const auto derived = bootstrap.take_derived_world();
            expect(derived.has_value(), "ready bootstrap must publish map-derived visual data");
            expect(derived->minimap_rgba.size() ==
                       static_cast<std::size_t>(VxlMap::width) * VxlMap::depth * 4U,
                   "bootstrap minimap must cover every canonical map column");
            expect(derived->skylight.data().size() ==
                       static_cast<std::size_t>(SkylightMap::edge) * SkylightMap::edge,
                   "bootstrap skylight must cover every canonical map column");
            expect(derived->map_revision == bootstrap.map()->revision(),
                   "derived visual data must identify its source map revision");
            expect(bootstrap.progress().fraction == 1.0,
                   "ready bootstrap must report full progress");
        }

        // Cancelling mid-load must not hang destruction.
        {
            TutorialWorldBootstrap bootstrap{AOS_TRAINING_VXL, 2U};
            bootstrap.start();
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
            bootstrap.cancel();
        }

        std::cout << "tutorial session: spawn/input/look/bootstrap checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
