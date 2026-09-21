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

        std::cout << "jetpack death presentation tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
