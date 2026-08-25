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
                   roster.apply_kill_relationships(
                       rival_id, rival_id, fixtures.front().player_id,
                       false, false, true) &&
                   !roster.player(rival_id)->dominating_local_player &&
                   !roster.player(rival_id)->dominated_by_local_player,
               "forced/team-change kills must clear both retail relationship markers");

        battlespades::network::WorldPlayerWeaponRow world_row;
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

        battlespades::network::RemoteMotionInterpolator remote_motion;
        remote_motion.reset({{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 0.0}});
        remote_motion.push({{3.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {3.0, 0.0, 0.0}},
                           1.0 / 30.0);
        remote_motion.tick(1.0 / 60.0);
        expect(remote_motion.sample().position.x == 1.5,
               "30 Hz peer snapshots must interpolate across two render ticks");
        remote_motion.tick(1.0 / 60.0);
        expect(remote_motion.sample().position.x == 3.0,
               "remote interpolation must land exactly on the authority");
        remote_motion.push({{20.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {}},
                           1.0 / 30.0);
        expect(remote_motion.sample().position.x == 20.0,
               "large semantic teleports must never smear through terrain");

        // A peer turning from +179 to -179 degrees must cross the two-degree
        // seam, not spin through zero or normalize an almost-zero vector.
        constexpr double seam = std::numbers::pi / 180.0;
        remote_motion.reset(
            {{0.0, 0.0, 0.0},
             {std::cos(179.0 * seam), std::sin(179.0 * seam), 0.0},
             {}});
        remote_motion.push(
            {{0.0, 0.0, 0.0},
             {std::cos(-179.0 * seam), std::sin(-179.0 * seam), 0.0},
             {}},
            1.0 / 30.0);
        remote_motion.tick(1.0 / 60.0);
        const auto seam_orientation = remote_motion.sample().orientation;
        expect(seam_orientation.x < -0.999 &&
                   std::abs(seam_orientation.y) < 0.002 &&
                   std::abs(std::hypot(seam_orientation.x,
                                       seam_orientation.y,
                                       seam_orientation.z) -
                            1.0) <
                       1.0e-9,
               "remote yaw must interpolate over the wrapped shortest arc");

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
