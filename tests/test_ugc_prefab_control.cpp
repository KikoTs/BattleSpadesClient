#include "battlespades/world/ugc_prefab_control.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void expect(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string{message});
}

void expect_near(double actual, double expected, std::string_view message) {
    if (std::abs(actual - expected) > 1e-8) {
        throw std::runtime_error(std::string{message});
    }
}

[[nodiscard]] battlespades::world::UgcPrefabPlacement rotate_once(
    battlespades::world::UgcPrefabControlInput input,
    std::uint8_t camera_relative_yaw,
    std::uint8_t pitch) {
    using battlespades::world::UgcPrefabControl;
    using battlespades::world::UgcPrefabPlacement;
    using battlespades::world::Vec3;

    UgcPrefabControl control;
    control.prime(UgcPrefabPlacement{{100, 200, 30},
                                     {102.0, 203.0, 32.0},
                                     {4, 6, 4},
                                     camera_relative_yaw,
                                     pitch,
                                     0U});
    expect(control.activate(), "rotation fixture must enter fine control");
    control.set_input(input, true);
    // North contributes camera direction zero, so authored yaw is also the
    // exact camera-relative yaw consumed by UGCPrefabTool.apply_prefab_rotation.
    control.tick(1.0 / 60.0, Vec3{0.0, -1.0, 0.0});
    return control.placement();
}

void asymmetric_horizontal_rotations_match_retail() {
    using battlespades::world::UgcPrefabControlInput;

    const auto west_left_pitch_3 =
        rotate_once(UgcPrefabControlInput::rotate_left, 3U, 3U);
    expect(west_left_pitch_3.yaw == 1U && west_left_pitch_3.pitch == 0U &&
               west_left_pitch_3.roll == 2U,
           "WEST + left + pitch 3 must execute the retail compound turn");

    const auto west_left_pitch_1 =
        rotate_once(UgcPrefabControlInput::rotate_left, 3U, 1U);
    expect(west_left_pitch_1.yaw == 3U && west_left_pitch_1.pitch == 0U &&
               west_left_pitch_1.roll == 0U,
           "WEST + left + pitch 1 must use the retail single pitch turn");

    const auto east_right_pitch_3 =
        rotate_once(UgcPrefabControlInput::rotate_right, 1U, 3U);
    expect(east_right_pitch_3.yaw == 3U && east_right_pitch_3.pitch == 0U &&
               east_right_pitch_3.roll == 2U,
           "EAST + right + pitch 3 must execute the retail compound turn");

    const auto east_right_pitch_1 =
        rotate_once(UgcPrefabControlInput::rotate_right, 1U, 1U);
    expect(east_right_pitch_1.yaw == 1U && east_right_pitch_1.pitch == 0U &&
               east_right_pitch_1.roll == 0U,
           "EAST + right + pitch 1 must use the retail single pitch turn");
}

} // namespace

int main() {
    using battlespades::world::UgcPrefabControl;
    using battlespades::world::UgcPrefabControlInput;
    using battlespades::world::UgcPrefabPlacement;
    using battlespades::world::Vec3;

    try {
        UgcPrefabControl control;
        expect(!control.activate(), "stage two must reject an absent stage-one ghost");
        control.prime(UgcPrefabPlacement{{100, 200, 30}, {102.0, 203.0, 32.0}, {4, 6, 4}});
        expect_near(control.zoom_level(), 4.0 * 1.73205080757 * 0.5,
                    "initial camera zoom must use retail size_x formula");
        expect(control.activate(), "first LMB must capture a valid ghost");

        control.set_input(UgcPrefabControlInput::forward, true);
        control.tick(1.0 / 60.0, Vec3{0.0, -1.0, 0.0});
        expect_near(control.placement().center[1U], 202.0,
                    "first held input tick must nudge immediately toward camera north");
        const auto first_anchor = control.placement().anchor;
        control.tick(0.49, Vec3{0.0, -1.0, 0.0});
        expect(control.placement().anchor == first_anchor,
               "the first repeat must wait the recovered 0.5 seconds");
        control.tick(0.02, Vec3{0.0, -1.0, 0.0});
        expect(control.placement().anchor != first_anchor,
               "the second nudge must run after the 0.5 second repeat");

        control.set_input(UgcPrefabControlInput::forward, false);
        control.tick(0.0, Vec3{1.0, 0.0, 0.0});
        control.set_input(UgcPrefabControlInput::up, true);
        control.set_input(UgcPrefabControlInput::sprint, true);
        const auto before_up = control.placement().center[2U];
        control.tick(1.0 / 60.0, Vec3{1.0, 0.0, 0.0});
        expect_near(control.placement().center[2U], before_up - 10.0,
                    "jump+sprint must raise the blueprint ten VXL cells");

        control.set_input(UgcPrefabControlInput::up, false);
        control.set_input(UgcPrefabControlInput::sprint, false);
        control.tick(0.0, Vec3{0.0, -1.0, 0.0});
        control.set_input(UgcPrefabControlInput::rotate_right, true);
        const auto old_yaw = control.placement().yaw;
        const auto old_pitch = control.placement().pitch;
        const auto old_roll = control.placement().roll;
        control.tick(1.0 / 60.0, Vec3{0.0, -1.0, 0.0});
        expect(control.placement().yaw != old_yaw || control.placement().pitch != old_pitch ||
                   control.placement().roll != old_roll,
               "screen-relative arrow rotation must alter a quarter-turn axis");
        control.set_input(UgcPrefabControlInput::rotate_right, false);

        const auto initial_zoom = control.zoom_level();
        control.adjust_zoom(-2.0);
        expect(control.zoom_level() > initial_zoom,
               "wheel down must move the prefab camera farther away");

        control.set_input(UgcPrefabControlInput::carve, true);
        control.tick(0.0, Vec3{1.0, 0.0, 0.0});
        expect(control.take_carve_request(), "C must emit an immediate erase request");
        control.tick(0.05, Vec3{1.0, 0.0, 0.0});
        expect(!control.take_carve_request(), "erase repeat must remain closed before 0.1 s");
        control.tick(0.05, Vec3{1.0, 0.0, 0.0});
        expect(control.take_carve_request(), "held C must repeat at 0.1 s");

        control.deactivate();
        expect(!control.active() && !control.activate(),
               "exit must arm the recovered 0.5 second re-entry guard");
        control.tick(0.5, Vec3{1.0, 0.0, 0.0});
        expect(control.activate(), "re-entry must reopen after the guard expires");

        control.reset();
        expect(!control.active() && !control.has_placement(),
               "tool changes must clear every retained control-mode state");
        asymmetric_horizontal_rotations_match_retail();
        std::cout << "UGC prefab control tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "UGC prefab control tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
