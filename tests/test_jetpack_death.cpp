#include "battlespades/world/jetpack_death.hpp"
#include "battlespades/world/parachute.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using battlespades::world::JetpackDeathPresentation;
using battlespades::world::Vec3;

void expect(bool value, const char* message) {
    if (!value) throw std::runtime_error{message};
}

[[nodiscard]] bool near(double lhs, double rhs, double tolerance = 1.0e-9) {
    return std::abs(lhs - rhs) <= tolerance;
}

} // namespace

int main() {
    try {
        // The original canopy meshes are world attachments, including the
        // first-person variant: authored up stays up as the player turns.
        for (const double yaw : {0.0, 90.0, -90.0, 180.0}) {
            const auto third = battlespades::world::retail_parachute_world_transform(
                {10.0, 20.0, 30.0}, yaw, false);
            const auto first = battlespades::world::retail_parachute_world_transform(
                {10.0, 20.0, 30.0}, yaw, true);
            expect(near(third[12U], 10.0) && near(third[13U], 20.0) && near(third[14U], 30.0),
                   "third-person canopy must inherit the body anchor without an extra offset");
            expect(near(first[14U], 28.5) && first[12U] == third[12U] && first[13U] == third[13U],
                   "first-person canopy must remain 1.5 blocks above the eye anchor");
            expect(near(third[4U], 0.0) && near(third[5U], 0.0) && near(third[6U], -0.12, 1e-7),
                   "KV6 render-up must become negative world Z at the recovered 0.12 scale");
            const double axis_length = std::hypot(third[0U], third[1U]);
            expect(near(axis_length, 0.12, 1e-7) && third[2U] == 0.0F,
                   "yaw must rotate the canopy horizontally without pitch or scale distortion");
        }
        const std::vector<std::uint8_t> ordinary{5U, 9U, 64U};
        const std::vector<std::uint8_t> normal_pack{5U, 66U};
        const std::vector<std::uint8_t> glider{67U};
        const std::vector<std::uint8_t> engineer_pack{68U};
        const std::vector<std::uint8_t> ugc_pack{69U};
        expect(!battlespades::world::has_retail_jetpack(ordinary),
               "ordinary equipment must not enter the jetpack death fuse");
        expect(battlespades::world::has_retail_jetpack(normal_pack) &&
                   battlespades::world::has_retail_jetpack(glider) &&
                   battlespades::world::has_retail_jetpack(engineer_pack) &&
                   battlespades::world::has_retail_jetpack({}, ugc_pack),
               "all four retail pack ids must select the death presentation");
        expect(battlespades::world::retail_jetpack_id(engineer_pack) == 68U &&
                   battlespades::world::retail_jetpack_id({}, ugc_pack) == 69U,
               "pack selection must retain the exact normalized equipment row");
        expect(battlespades::world::retail_jetpack_model(66U) == "jetpack" &&
                   battlespades::world::retail_jetpack_model(67U) == "Jetpack2" &&
                   battlespades::world::retail_jetpack_model(68U) == "JetpackEngineer" &&
                   battlespades::world::retail_jetpack_model(69U) == "JetpackUGCBuilder",
               "all death packs must resolve their exact recovered KV6 stem");
        constexpr auto attachment = battlespades::world::retail_jetpack_attachment();
        static_assert(attachment.z_offset == 6);
        expect(near(attachment.y, -0.6) && near(attachment.z, 0.8) &&
                   near(attachment.size, 0.075),
               "third-person jetpacks must retain the retail DisplayList attachment");

        JetpackDeathPresentation presentation;
        expect(!presentation.begin(7U, 3U, false, {1.0, 2.0, 3.0}),
               "a non-pack death must remain on the ordinary immediate path");
        expect(presentation.begin(7U, 3U, true, {1.0, 2.0, 3.0}),
               "a selected pack must retain its corpse");
        const auto* initial = presentation.state(7U);
        expect(initial != nullptr && initial->generation == 3U,
               "retained state must bind to the exact player generation");
        const auto axis = initial->rotation_axis;
        expect(near(std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z), 1.0),
               "the recovered random death vector must be normalized");

        presentation.tick(1.0 / 60.0);
        const auto* one_frame = presentation.state(7U);
        expect(one_frame != nullptr && near(one_frame->rotation_degrees.x, axis.x) &&
                   near(one_frame->rotation_degrees.y, axis.y) &&
                   near(one_frame->rotation_degrees.z, axis.z),
               "one fixed tick must add the retail vector exactly once");
        for (std::size_t frame{1U}; frame < 60U; ++frame) {
            presentation.tick(1.0 / 60.0);
        }
        const auto* one_second = presentation.state(7U);
        expect(one_second != nullptr && near(one_second->rotation_degrees.x, axis.x * 60.0) &&
                   near(one_second->rotation_degrees.y, axis.y * 60.0) &&
                   near(one_second->rotation_degrees.z, axis.z * 60.0),
               "the one-second server fuse must contain sixty retail spin updates");

        expect(!presentation.update_authority(7U, 4U, {9.0, 9.0, 9.0}),
               "an id-reused life must not move the old corpse");
        expect(presentation.update_authority(7U, 3U, {4.0, 5.0, -2.0}),
               "matching dead WorldUpdates must lift the retained corpse");
        const auto finished = presentation.finish(7U);
        expect(finished.has_value() && near(finished->position.x, 4.0) &&
                   near(finished->position.y, 5.0) && near(finished->position.z, -2.0),
               "packet 36 must explode at the last authoritative airborne point");
        expect(presentation.state(7U) == nullptr,
               "the corpse must disappear at the packet-36 boundary");

        JetpackDeathPresentation smooth;
        expect(smooth.begin(2U, 1U, true, {10.0, 10.0, 10.0}), "smooth corpse setup");
        expect(smooth.update_authority(2U, 1U, {10.0, 10.0, 9.0}), "corpse authority update");
        expect(near(smooth.state(2U)->position.z, 10.0), "a packet must not snap rendered corpse position");
        smooth.tick(1.0 / 60.0);
        expect(near(smooth.state(2U)->position.z, 9.5), "first frame must interpolate halfway to 30Hz row");
        expect(smooth.update_authority(2U, 1U, {10.0, 10.0, 9.0}), "duplicate row accepted");
        smooth.tick(1.0 / 60.0);
        expect(near(smooth.state(2U)->position.z, 9.0), "duplicate rows must not restart interpolation");
        smooth.tick(0.2);
        expect(near(smooth.state(2U)->position.z, 9.0), "missing rows must not extrapolate through walls");
        expect(smooth.update_authority(2U, 1U, {20.0, 10.0, 9.0}) &&
                   near(smooth.state(2U)->position.x, 20.0), "teleports must not interpolate through geometry");

        JetpackDeathPresentation replay;
        expect(replay.begin(7U, 3U, true, {0.0, 0.0, 0.0}),
               "deterministic replay setup must succeed");
        const auto* replayed = replay.state(7U);
        expect(replayed != nullptr && near(replayed->rotation_axis.x, axis.x) &&
                   near(replayed->rotation_axis.y, axis.y) &&
                   near(replayed->rotation_axis.z, axis.z),
               "the same player generation must reproduce the same testable spin");
        replay.clear();
        expect(replay.state(7U) == nullptr, "map teardown must clear every retained corpse");

        JetpackDeathPresentation engineer;
        expect(engineer.begin(8U, 1U, std::uint8_t{68U}, {2.0, 3.0, 4.0}) &&
                   engineer.state(8U) != nullptr &&
                   engineer.state(8U)->jetpack_id == 68U,
               "engineer death must retain JetpackEngineer rather than a generic pack");

        // Character.set_team: color = team * 0.5, never the full team colour.
        {
            namespace w = battlespades::world;
            const auto half = w::retail_character_color({44U, 117U, 179U, 255U});
            expect(half.red == 22U && half.green == 59U && half.blue == 90U && half.alpha == 255U,
                   "characters must use the team colour at half intensity");
            const auto flash = w::retail_spawn_flash_color(half);
            expect(flash.red == 44U && flash.green == 118U && flash.blue == 180U,
                   "SPAWN_COLOR_MULTIPLIER must restore the full team colour");

            // spawn_color_blink_timer: bright every frame until the timer runs
            // out; that one frame is plain and re-arms with remaining / 3.
            w::RetailSpawnBlink blink;
            w::retail_spawn_blink_reset(blink);
            expect(w::retail_spawn_blink_draw(blink, 3.0),
                   "a fresh spawn draws the doubled team colour");
            std::size_t plain_frames{};
            std::vector<double> plain_times;
            double remaining{3.0};
            constexpr double dt{1.0 / 60.0};
            for (int frame{}; frame < 180; ++frame) {
                w::retail_spawn_blink_update(blink, dt);
                remaining -= dt;
                if (!w::retail_spawn_blink_draw(blink, remaining)) {
                    ++plain_frames;
                    plain_times.push_back(3.0 - remaining);
                }
            }
            expect(plain_frames >= 4U && plain_frames < 60U,
                   "the plain-colour blink is a single frame per interval");
            expect(plain_times.size() >= 3U &&
                       plain_times[2U] - plain_times[1U] < plain_times[1U] - plain_times[0U],
                   "the blink interval shrinks as protection runs out");
            expect(!w::retail_spawn_blink_draw(blink, 0.0),
                   "an unprotected character never flashes");

            // Character.draw 2020/2052 tool colour paths.
            expect(w::retail_tool_color_path(5U, false) == w::RetailToolColorPath::block_color &&
                       w::retail_tool_color_path(23U, false) ==
                           w::RetailToolColorPath::block_color &&
                       w::retail_tool_color_path(43U, false) ==
                           w::RetailToolColorPath::block_color &&
                       w::retail_tool_color_path(64U, false) ==
                           w::RetailToolColorPath::block_color &&
                       w::retail_tool_color_path(42U, false) == w::RetailToolColorPath::none,
                   "use_color tools take the holder's block colour; UGCPrefabTool resets it");
            expect(w::retail_tool_color_path(30U, false) ==
                           w::RetailToolColorPath::other_team_color &&
                       w::retail_tool_color_path(24U, true) == w::RetailToolColorPath::team_color,
                   "intel uses the other team's colour; team tools the holder's");
            expect(w::retail_tool_flashes_with_spawn_protection(24U) &&
                       !w::retail_tool_flashes_with_spawn_protection(5U),
                   "only ZombieHandTool flashes with spawn protection");

            // set_crouch intel placements (pyx 812-849).
            const auto stand = w::retail_back_intel_attachment(false, std::nullopt);
            const auto stand66 = w::retail_back_intel_attachment(false, std::uint8_t{66U});
            const auto stand68 = w::retail_back_intel_attachment(false, std::uint8_t{68U});
            const auto crouch = w::retail_back_intel_attachment(true, std::uint8_t{65U});
            const auto crouch67 = w::retail_back_intel_attachment(true, std::uint8_t{67U});
            expect(near(stand.y, -0.38) && near(stand.z, 0.9) && near(stand.size, 0.1) &&
                       stand.z_offset == 6,
                   "standing intel without a pack");
            expect(near(stand66.y, -1.1) && near(stand68.y, -1.0) && near(stand68.z, 0.9),
                   "standing intel behind a pack");
            expect(near(crouch.y, -0.65) && near(crouch.z, 0.5) && near(crouch67.y, -1.15) &&
                       near(crouch67.z, 1.0),
                   "crouched intel with and without a pack");
            constexpr auto corpse = w::retail_classic_corpse_attachment();
            static_assert(corpse.z_offset == 6 && corpse.z == 2.0 && corpse.size == 0.05);
        }

        std::cout << "jetpack death presentation tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
