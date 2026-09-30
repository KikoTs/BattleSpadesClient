#include "battlespades/network/protocol168_players.hpp"
#include "battlespades/network/protocol168_reconciliation.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

namespace {

void expect(bool value, const char* message) {
    if (!value) throw std::runtime_error{message};
}

} // namespace

int main() {
    try {
        battlespades::network::Protocol168Roster roster;
        const auto fixtures =
            battlespades::network::tutorial_create_player_fixtures();
        expect(fixtures.size() == 3U, "tutorial must expose three packet fixtures");
        for (const auto& fixture : fixtures) {
            const auto bytes = battlespades::network::encode_packet(fixture);
            expect(!bytes.empty(), "valid CreatePlayer fixture must encode");
            const auto decoded = battlespades::network::decode_create_player(bytes);
            expect(decoded && decoded.packet->name == fixture.name &&
                       decoded.packet->position == fixture.position &&
                       roster.apply(bytes),
                   "CreatePlayer must round-trip into the generation-safe roster");
        }
        expect(roster.players().size() == 3U,
               "all tutorial players must be represented by packet-created state");
        {
            // The allocation-free view must see exactly the same players in
            // the same order as the copying players() accessor.
            const auto copied = roster.players();
            const auto view = roster.present_players();
            expect(view.size() == copied.size() && !view.empty(),
                   "present_players must count the same occupied slots");
            std::size_t index{};
            for (const auto& player : view) {
                expect(index < copied.size() && player.player_id == copied[index].player_id &&
                           player.name == copied[index].name,
                       "present_players must iterate in player-id order without copies");
                ++index;
            }
            expect(index == copied.size(), "present_players must visit every player once");
            expect(std::ranges::none_of(view,
                                        [](const auto& player) { return player.name.empty(); }),
                   "present_players must be a standard range");
            battlespades::network::Protocol168Roster empty;
            expect(empty.present_players().empty() && empty.present_players().size() == 0U,
                   "an empty roster view has no players");
        }

        auto localized_demo = fixtures.front();
        localized_demo.player_id = 105U;
        localized_demo.name = "Localized Demo";
        localized_demo.demo_player = true;
        localized_demo.local_language = 6U;
        const auto localized_bytes =
            battlespades::network::encode_packet(localized_demo);
        const auto localized_decoded =
            battlespades::network::decode_create_player(localized_bytes);
        expect(localized_decoded && localized_decoded.packet->demo_player &&
                   localized_decoded.packet->local_language == 6U &&
                   roster.apply(localized_bytes) &&
                   roster.player(105U)->demo_player &&
                   roster.player(105U)->local_language == 6U,
               "CreatePlayer must retain demo and local-language roster branches");
        roster.remove(105U);

        const auto rival_id = fixtures[1U].player_id;
        expect(roster.apply_kill_relationships(
                   fixtures.front().player_id, rival_id,
                   fixtures.front().player_id, true, false, false) &&
                   roster.player(rival_id)->dominating_local_player,
               "a rival domination kill must mark the rival as dominating local");
        expect(roster.apply_kill_relationships(
                   rival_id, fixtures.front().player_id,
                   fixtures.front().player_id, false, true, false) &&
                   !roster.player(rival_id)->dominating_local_player,
               "a local revenge kill must clear the rival domination marker");
        expect(roster.apply_kill_relationships(
                   rival_id, fixtures.front().player_id,
                   fixtures.front().player_id, true, false, false) &&
                   roster.player(rival_id)->dominated_by_local_player,
               "a local domination kill must mark the rival as dominated");
        expect(roster.apply(fixtures[1U]) &&
                   roster.player(rival_id)->dominated_by_local_player,
               "same-identity CreatePlayer respawn must preserve relationship markers");
        expect(roster.apply_kill_relationships(
                   fixtures.front().player_id, rival_id,
                   fixtures.front().player_id, false, true, false) &&
                   !roster.player(rival_id)->dominated_by_local_player,
               "a rival revenge kill must clear the local domination marker");
        expect(roster.apply_kill_relationships(
                   rival_id, fixtures.front().player_id,
                   fixtures.front().player_id, true, false, false) &&
                   roster.apply_kill_relationships(
                       fixtures.front().player_id, rival_id,
                       fixtures.front().player_id, true, false, false) &&
                   roster.player(rival_id)->dominating_local_player &&
                   roster.player(rival_id)->dominated_by_local_player &&
                   !roster.apply_kill_relationships(
                       rival_id, 250U, fixtures.front().player_id, false, false, false) &&
                   roster.player(rival_id)->dominating_local_player &&
                   roster.player(rival_id)->dominated_by_local_player &&
                   roster.apply_kill_relationships(
                       rival_id, rival_id, fixtures.front().player_id,
                       false, false, true) &&
                   !roster.player(rival_id)->dominating_local_player &&
                   !roster.player(rival_id)->dominated_by_local_player,
               "forced/team-change kills must clear both retail relationship markers");

        battlespades::network::WorldPlayerWeaponRow world_row;
        {
            battlespades::network::Protocol168Roster ordered;
            expect(ordered.apply(fixtures[0U]) && ordered.apply(fixtures[1U]),
                   "split snapshot fixture players");
            battlespades::network::WorldPlayerWeaponRow row;
            row.player_id = fixtures[0U].player_id;
            row.health = 100;
            row.position = {10.0F, 20.0F, 30.0F};
            expect(ordered.update_world_state(row, 100), "first observer row accepted");
            row.position[0U] = 99.0F;
            expect(!ordered.update_world_state(row, 98) &&
                       !ordered.update_world_state(row, 100) &&
                       ordered.player(row.player_id)->position.x == 10.0,
                   "reordered and duplicate observer rows never rewind state");
            row.player_id = fixtures[1U].player_id;
            expect(ordered.update_world_state(row, 100),
                   "another player in a same-loop split chunk is still accepted");
            row.player_id = fixtures[0U].player_id;
            row.acknowledged_client_loop = 8;
            expect(ordered.update_world_state(row, 100, true),
                   "owner ACK can advance while its observer prefix stays unchanged");
            row.acknowledged_client_loop = 9;
            row.position[0U] = 101.0F;
            expect(ordered.update_world_state(row, 98, true),
                   "a newer owner ACK is valid even with an older observer prefix");
            row.acknowledged_client_loop = 8;
            row.position[0U] = 102.0F;
            expect(!ordered.update_world_state(row, 102, true) &&
                       ordered.player(row.player_id)->position.x == 101.0,
                   "newer global loops cannot authorize a stale owner ACK");
            expect(ordered.apply(fixtures[0U]) && ordered.update_world_state(row, 1, true),
                   "reliable respawn resets per-life snapshot ordering");
            ordered.remove(row.player_id);
            expect(!ordered.update_world_state(row, 200),
                   "an unreliable row cannot recreate a removed player");
        }
        world_row.player_id = 100U;
        world_row.position = {4.0F, 5.0F, 6.0F};
        world_row.orientation = {0.0F, 1.0F, 0.25F};
        world_row.velocity = {0.1F, 0.2F, -0.3F};
        world_row.acknowledged_client_loop = 42;
        world_row.health = 73;
        world_row.input_flags = 0x91U;
        world_row.action_flags = 0x11U;
        world_row.state_flags = 0x02U;
        world_row.tool_id = 18U;
        world_row.jetpack_fuel = 0.5F;
        expect(roster.update_world_state(world_row) &&
                   roster.player(100U)->tool_id == 18U &&
                   roster.player(100U)->input_flags == 0x91U &&
                   roster.player(100U)->health == 73,
               "WorldUpdate must retain remote tool, animation, and health state");
        world_row.tool_id = 0xFFU;
        expect(roster.update_world_state(world_row) &&
                   roster.player(100U)->tool_id == 18U,
               "a tool id outside the retail range (0xFF) keeps the held tool");
        world_row.tool_id = 18U;
        expect(roster.update_health(100U, 41) &&
                   roster.player(100U)->health == 41 &&
                   !roster.player(100U)->dead &&
                   roster.update_health(100U, 0) &&
                   roster.player(100U)->dead,
               "SetHP must update health and death state without fabricating a row");
        expect(roster.update_jetpack_fuel(100U, 100.0F) &&
                   roster.player(100U)->jetpack_fuel == 100.0F,
               "jetpack Restock must refresh the live resource immediately");
        expect(!roster.update_jetpack_fuel(99U, 100.0F) &&
                   !roster.update_jetpack_fuel(
                       100U, std::numeric_limits<float>::quiet_NaN()),
               "jetpack updates must reject missing players and non-finite values");
        expect(roster.update_mode_visibility(100U, true, std::nullopt) &&
                   roster.player(100U)->high_minimap_visibility &&
                   !roster.player(100U)->chase_cam &&
                   roster.update_mode_visibility(100U, std::nullopt, true) &&
                   roster.player(100U)->chase_cam,
               "ChangePlayer mode flags must update independently");
        expect(!roster.update_mode_visibility(200U, true, std::nullopt),
               "mode flags must reject an absent/out-of-range player id");
        expect(roster.update_pickup(100U, 16U) &&
                   roster.player(100U)->pickup_id == 16U &&
                   !roster.update_pickup(200U, 16U),
               "PickPickup must update only an existing roster player");

        const std::array<std::uint8_t, 2U> normalized_loadout{18U, 20U};
        const std::array<std::string, 1U> normalized_prefabs{"bunker"};
        const std::array<std::uint8_t, 2U> normalized_ugc{44U, 45U};
        expect(roster.update_loadout(100U, 2U, normalized_loadout,
                                     normalized_prefabs, normalized_ugc) &&
                   roster.player(100U)->ugc_tools ==
                       std::vector<std::uint8_t>(normalized_ugc.begin(),
                                                 normalized_ugc.end()),
               "SetClassLoadout must persist its UGC suffix in replica state");

        battlespades::network::Protocol168PredictionHistory history{8U};
        battlespades::world::PlayerMovementState past;
        past.position = {10.0, 20.0, 30.0};
        past.velocity = {1.0, 0.0, 0.0};
        history.record(100, past);
        past.position.x = 12.0;
        history.record(101, past);
        const auto correction = history.reconcile(
            100, {10.5, 20.0, 30.0}, {1.25, 0.0, 0.0});
        expect(correction.has_value() && correction->position_delta.x == 0.5 &&
                   correction->velocity_delta.x == 0.25,
               "reconciliation must compare the server to its ACKed past sample");
        const auto repeated = history.reconcile(
            100, {10.5, 20.0, 30.0}, {1.25, 0.0, 0.0});
        expect(repeated.has_value() && repeated->position_delta.x == 0.0,
               "repeated ACKs must be idempotent after rebasing future samples");

        battlespades::network::Protocol168PredictionHistory bounded{2U};
        bounded.record(200, past);
        bounded.record(201, past);
        bounded.record(202, past);
        expect(!bounded.reconcile(200, {}, {}).has_value(),
               "an evicted ACK must never reconcile against the current player");
        const auto retained = bounded.reconcile(
            201, past.position, past.velocity);
        expect(retained.has_value() && retained->position_delta.x == 0.0 &&
                   bounded.size() == 2U,
               "the oldest retained exact-loop sample must remain reconcilable");

        battlespades::network::Protocol168PredictionHistory retail_deadband{4U};
        battlespades::world::PlayerMovementState slope_sample;
        slope_sample.position = {139.277496, 256.5, 212.231888};
        retail_deadband.record(110, slope_sample);
        const auto sub_adjust = retail_deadband.reconcile(
            110, {139.312302, 256.5, 212.298462}, {});
        expect(sub_adjust.has_value() &&
                   sub_adjust->position_delta.x == 0.0 &&
                   sub_adjust->position_delta.z == 0.0,
               "retail must ignore sub-0.1 slope displacement instead of micro-correcting");
        const auto velocity_only = retail_deadband.reconcile(
            110, slope_sample.position, {10.0, 0.0, 0.0});
        expect(velocity_only.has_value() &&
                   velocity_only->velocity_delta.x == 0.0,
               "retail position tolerance must also suppress velocity-only correction");

        // Retail Character.apply_interpolations: peers snap to the newest row
        // and keep simulating with their replicated buttons (extrapolation),
        // which is the view the server's lag compensation rewinds to.
        battlespades::network::RemoteMotionInterpolator remote_motion;
        remote_motion.reset({{100.0, 100.0, 100.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 0.0}});
        battlespades::network::RemoteMotionSample runner;
        runner.position = {103.0, 100.0, 100.0};
        runner.orientation = {0.0, 1.0, 0.0};
        runner.velocity = {0.3, 0.0, 0.0};
        remote_motion.push(runner, 1.0 / 30.0);
        expect(remote_motion.sample().position.x == 103.0 &&
                   remote_motion.sample().orientation.y == 1.0,
               "a new peer row must snap immediately, never trail by an interpolation window");
        remote_motion.tick(1.0 / 60.0);
        battlespades::world::PlayerMovementState expected_body;
        expected_body.position = runner.position;
        expected_body.velocity = runner.velocity;
        expected_body.orientation = runner.orientation;
        static_cast<void>(battlespades::world::step_player(
            expected_body, {}, nullptr, 1.0 / 60.0,
            battlespades::world::movement_config_for_class(0U)));
        expect(remote_motion.sample().position.x > 103.0 &&
                   remote_motion.sample().position.x == expected_body.position.x &&
                   remote_motion.sample().position.z == expected_body.position.z,
               "between rows the peer must be extrapolated by the native mover");
        remote_motion.push({{120.0, 100.0, 100.0}, {1.0, 0.0, 0.0}, {}}, 1.0 / 30.0);
        expect(remote_motion.sample().position.x == 120.0,
               "large semantic teleports must never smear through terrain");

        // Replicated buttons drive the extrapolation exactly like retail.
        {
            battlespades::network::RemoteMotionInterpolator walker;
            battlespades::network::RemoteMotionSample forward;
            forward.position = {100.0, 100.0, 100.0};
            forward.orientation = {1.0, 0.0, 0.0};
            forward.input_flags = 0x01U;
            walker.reset(forward);
            walker.tick(1.0 / 60.0);
            expect(walker.sample().velocity.x > 0.0,
                   "a held forward button must accelerate the extrapolated peer");
            battlespades::network::RemoteMotionSample dead = forward;
            dead.dead = true;
            dead.position = {5.0, 5.0, 5.0};
            walker.push(dead, 1.0 / 30.0);
            walker.tick(1.0 / 60.0);
            expect(walker.sample().position.x == 5.0,
                   "a dead peer holds its authoritative corpse position");
        }

        // The extrapolation input is built from the authoritative replica.
        {
            battlespades::network::RemotePlayerReplica replica;
            replica.class_id = 2U;
            replica.input_flags = 0x11U;
            replica.action_flags = 0x14U;
            replica.state_flags = 0x01U;
            replica.loadout = {17U, 2U, 67U, 72U};
            const auto sample = battlespades::network::remote_motion_sample(replica, 1.25);
            expect(sample.jetpack == 2U && sample.jetpack_active && sample.parachute &&
                       sample.parachute_active && sample.input_flags == 0x11U &&
                       sample.class_id == 2U && sample.movement_speed_scale == 1.25 &&
                       !sample.hover,
                   "remote extrapolation must mirror the replicated pack, canopy and buttons");
        }

        // Local prediction always collides enemies, filters allies when the
        // InitialInfo flag is clear, and uses crouched/wading body heights.
        expect(roster.update_health(100U, 100),
               "collision fixture must restore the local life after its death-state test");
        auto ally = fixtures[1U];
        ally.player_id = 103U;
        ally.team = roster.player(100U)->team;
        ally.name = "Blue Ally";
        expect(roster.apply(ally), "ally collision fixture must enter the roster");
        auto enemy = fixtures[1U];
        enemy.player_id = 104U;
        enemy.team = 3U;
        enemy.name = "Green Enemy";
        expect(roster.apply(enemy), "enemy collision fixture must enter the roster");
        battlespades::network::WorldPlayerWeaponRow crouched_enemy;
        crouched_enemy.player_id = 104U;
        crouched_enemy.position = enemy.position;
        crouched_enemy.orientation = enemy.orientation;
        crouched_enemy.health = 100;
        crouched_enemy.input_flags = 0x20U;
        expect(roster.update_world_state(crouched_enemy),
               "enemy collision fixture must accept its authoritative pose");
        const auto enemies_only =
            battlespades::network::protocol168_collision_bodies(roster, 100U, false);
        const auto with_allies =
            battlespades::network::protocol168_collision_bodies(roster, 100U, true);
        expect(enemies_only.size() == 2U && with_allies.size() == 4U,
               "collision bodies must always include enemies and gate only allies");
        const auto crouched = std::ranges::find_if(
            enemies_only, [](const battlespades::world::PlayerCollisionBody& body) {
                return body.player_id == 104U &&
                       std::fabs(body.height - 1.8) < 1e-9;
            });
        expect(crouched != enemies_only.end(),
               "collision bodies must retain identity and crouch height");

        auto replacement = fixtures.front();
        replacement.class_id = 2U;
        replacement.name = "Respawned";
        // Change the existing identity first, then prove a same-identity
        // respawn retains data unavailable in CreatePlayer(28).
        auto same_identity = fixtures.front();
        expect(roster.apply(same_identity),
               "same player respawn must replace its life generation");
        expect(roster.update_loadout(100U, 2U, normalized_loadout,
                                     normalized_prefabs, normalized_ugc),
               "respawn fixture must accept normalized loadout");
        expect(roster.apply(same_identity) &&
                   roster.player(100U)->ugc_tools ==
                       std::vector<std::uint8_t>(normalized_ugc.begin(),
                                                 normalized_ugc.end()),
               "CreatePlayer respawn must not erase acknowledged UGC tools");
        expect(roster.apply(replacement) && roster.player(100U)->generation == 4U &&
                   roster.player(100U)->class_id == 2U,
               "a repeated id must atomically create a new player life");

        auto malformed = battlespades::network::encode_packet(fixtures.front());
        malformed.pop_back();
        std::string error;
        expect(!roster.apply(malformed, &error) && !error.empty() &&
                   roster.player(100U)->generation == 4U,
               "truncated packets must not partially replace a live player");
        std::cout << "Protocol 168 CreatePlayer tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
