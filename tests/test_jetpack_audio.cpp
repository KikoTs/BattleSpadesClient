#include "battlespades/world/footstep_audio.hpp"
#include "battlespades/world/jetpack_audio.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using namespace battlespades::world;

void expect(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error{message};
}

[[nodiscard]] JetpackAudioInput rocketeer() {
    JetpackAudioInput input;
    input.has_pack = true;
    input.alive = true;
    input.class_id = 2U;
    return input;
}

constexpr double frame{1.0 / 60.0};

void the_loop_follows_update_jetpack_sound() {
    auto input = rocketeer();
    expect(retail_jetpack_loop(input) == JetpackLoop::none, "an idle pack is silent");

    input.passive = true;
    expect(retail_jetpack_loop(input) == JetpackLoop::low_thrust,
           "passive without active loops JP_lowthrust_lp");

    input.active = true;
    expect(retail_jetpack_loop(input) == JetpackLoop::flight,
           "active closes the passive loop and loops JP_flight_lp");

    input.active = false;
    input.controlling_prefab = true;
    expect(retail_jetpack_loop(input) == JetpackLoop::none,
           "the main character steering a prefab has no low-thrust loop");

    input.controlling_prefab = false;
    input.class_id = jetpack_silent_glide_class;
    expect(retail_jetpack_loop(input) == JetpackLoop::none,
           "the UGC builder has no low-thrust loop");
    input.active = true;
    expect(retail_jetpack_loop(input) == JetpackLoop::flight,
           "the UGC builder still has the flight loop");

    input = rocketeer();
    input.active = true;
    input.passive = true;
    input.has_pack = false;
    expect(retail_jetpack_loop(input) == JetpackLoop::none, "NO_JETPACK closes both loops");
    input.has_pack = true;
    input.alive = false;
    expect(retail_jetpack_loop(input) == JetpackLoop::none, "a dead character is silent");
}

void the_wire_flags_alone_drive_retail_cues() {
    // A server that sends the passive flag itself: thrust, a passive glide,
    // then the flag clears on landing.
    JetpackAudioState state;
    auto input = rocketeer();
    auto cues = step_jetpack_audio(state, input, frame);
    expect(cues.loop == JetpackLoop::none && !cues.ignite && !cues.release && !cues.land,
           "the first observation adopts the state without a cue");

    input.airborne = true;
    input.active = true;
    input.passive = true;
    cues = step_jetpack_audio(state, input, frame);
    expect(cues.ignite && !cues.release && !cues.land && cues.loop == JetpackLoop::flight,
           "set_jetpack_active(True) plays JP_ignite once and starts the flight loop");
    cues = step_jetpack_audio(state, input, frame);
    expect(!cues.ignite && cues.loop == JetpackLoop::flight, "the ignite cue is an edge");

    input.active = false;
    cues = step_jetpack_audio(state, input, frame);
    expect(cues.release && !cues.land && cues.loop == JetpackLoop::low_thrust,
           "set_jetpack_active(False) plays JP_release; the passive loop takes over");

    input.passive = false;
    input.airborne = false;
    for (int index{}; index < 20 && !cues.land; ++index) {
        cues = step_jetpack_audio(state, input, frame);
    }
    expect(cues.land && cues.loop == JetpackLoop::none,
           "set_jetpack_passive(False) plays JETPACK_LAND_SOUND and closes the loop");
    cues = step_jetpack_audio(state, input, frame);
    expect(!cues.land && !cues.release && !cues.ignite, "the land cue is an edge");
}

void a_flight_stays_engaged_until_the_landing() {
    // The BattleSpades server: rocket pack 66, active only, no passive flag.
    JetpackAudioState state;
    auto input = rocketeer();
    static_cast<void>(step_jetpack_audio(state, input, frame));

    input.airborne = true;
    input.active = true;
    auto cues = step_jetpack_audio(state, input, frame);
    expect(cues.ignite && cues.loop == JetpackLoop::flight, "thrust ignites");

    input.active = false;
    cues = step_jetpack_audio(state, input, frame);
    expect(cues.release && !cues.land && cues.loop == JetpackLoop::low_thrust,
           "after the thrust the falling character holds the low-thrust loop");

    int frames{};
    for (; frames < 90; ++frames) {
        cues = step_jetpack_audio(state, input, frame);
        expect(cues.loop == JetpackLoop::low_thrust && !cues.land,
               "the low-thrust loop holds for the whole descent");
    }

    input.airborne = false;
    cues = step_jetpack_audio(state, input, frame);
    expect(cues.land && cues.loop == JetpackLoop::none,
           "touching down ends the loop with the land cue");
    cues = step_jetpack_audio(state, input, frame);
    expect(!cues.land && cues.loop == JetpackLoop::none, "one land cue per flight");

    // A plain jump afterwards has nothing to do with the pack.
    input.airborne = true;
    cues = step_jetpack_audio(state, input, frame);
    expect(cues.loop == JetpackLoop::none && !cues.ignite && !cues.land,
           "a jump without thrust is silent");
}

void the_glide_pack_gives_one_land_cue() {
    // Glide pack 67: the server sends active and passive together and clears
    // both on release, in the air.
    JetpackAudioState state;
    auto input = rocketeer();
    static_cast<void>(step_jetpack_audio(state, input, frame));
    input.airborne = true;
    input.active = true;
    input.passive = true;
    static_cast<void>(step_jetpack_audio(state, input, frame));

    input.active = false;
    input.passive = false;
    auto cues = step_jetpack_audio(state, input, frame);
    expect(cues.release && !cues.land && cues.loop == JetpackLoop::low_thrust,
           "releasing in the air is not a landing");

    int lands{};
    for (int index{}; index < 30; ++index) {
        lands += step_jetpack_audio(state, input, frame).land ? 1 : 0;
    }
    input.airborne = false;
    for (int index{}; index < 30; ++index) {
        lands += step_jetpack_audio(state, input, frame).land ? 1 : 0;
    }
    expect(lands == 1, "the flight ends with exactly one land cue");
}

void a_hovering_observer_reading_is_not_a_landing() {
    // An observer derives `airborne` from vertical speed, and a hovering
    // glider has none: the grounded reading at the release must be ignored.
    JetpackAudioState state;
    auto input = rocketeer();
    static_cast<void>(step_jetpack_audio(state, input, frame));
    input.active = true;
    input.airborne = false;
    static_cast<void>(step_jetpack_audio(state, input, frame));

    input.active = false;
    auto cues = step_jetpack_audio(state, input, frame);
    expect(cues.release && !cues.land, "the release frame's grounded reading is not trusted");

    input.airborne = true; // the fall shows up within the grace period
    for (double elapsed{}; elapsed < 2.0 * jetpack_descent_grace_seconds; elapsed += frame) {
        cues = step_jetpack_audio(state, input, frame);
        expect(!cues.land && cues.loop == JetpackLoop::low_thrust,
               "the descent holds the low-thrust loop");
    }
    input.airborne = false;
    cues = step_jetpack_audio(state, input, frame);
    expect(cues.land, "the real landing plays the cue");

    // Thrust that never left the ground ends once the grace period passed.
    input.active = true;
    static_cast<void>(step_jetpack_audio(state, input, frame));
    input.active = false;
    int lands{};
    for (double elapsed{}; elapsed < 2.0 * jetpack_descent_grace_seconds; elapsed += frame) {
        lands += step_jetpack_audio(state, input, frame).land ? 1 : 0;
    }
    expect(lands == 1, "a grounded release still ends with one land cue");
}

void death_and_a_lost_pack_close_everything() {
    JetpackAudioState state;
    auto input = rocketeer();
    static_cast<void>(step_jetpack_audio(state, input, frame));
    input.airborne = true;
    input.active = true;
    static_cast<void>(step_jetpack_audio(state, input, frame));

    input.alive = false; // Character.set_dead clears both flags
    auto cues = step_jetpack_audio(state, input, frame);
    expect(cues.loop == JetpackLoop::none && cues.release && cues.land,
           "dying in flight clears the active and the passive flag");
    cues = step_jetpack_audio(state, input, frame);
    expect(cues.loop == JetpackLoop::none && !cues.release && !cues.land,
           "a corpse stays silent");

    JetpackAudioState other;
    input = rocketeer();
    static_cast<void>(step_jetpack_audio(other, input, frame));
    input.airborne = true;
    input.active = true;
    static_cast<void>(step_jetpack_audio(other, input, frame));
    input.active = false;
    static_cast<void>(step_jetpack_audio(other, input, frame));
    input.has_pack = false;
    cues = step_jetpack_audio(other, input, frame);
    expect(cues.loop == JetpackLoop::none && cues.land && !other.thrust_this_flight,
           "losing the pack in the air ends the glide at once");
}

void the_builder_flies_without_glide_sounds() {
    JetpackAudioState state;
    auto input = rocketeer();
    input.class_id = jetpack_silent_glide_class;
    static_cast<void>(step_jetpack_audio(state, input, frame));
    input.airborne = true;
    input.active = true;
    auto cues = step_jetpack_audio(state, input, frame);
    expect(cues.ignite && cues.loop == JetpackLoop::flight, "the builder's pack ignites");
    input.active = false;
    cues = step_jetpack_audio(state, input, frame);
    expect(cues.release && cues.loop == JetpackLoop::none,
           "the builder has no low-thrust loop");
    expect(std::string{class_movement_sound_stem(jetpack_silent_glide_class,
                                                 MovementSound::jetpack_land, 0U)}
               .empty(),
           "and its JETPACK_LAND_SOUND is blank");
}

void the_sound_files_ship() {
    const std::filesystem::path root{AOS_TEST_ASSET_ROOT};
    for (const auto* stem : {"JP_lowthrust_lp", "JP_lowthrust_rel", "JP_flight_lp", "JP_ignite",
                             "JP_release"}) {
        expect(std::filesystem::exists(root / "sounds" / (std::string{stem} + ".ogg")),
               "every jetpack cue must exist in the retail sound set");
    }
    expect(std::string{class_movement_sound_stem(2U, MovementSound::jetpack_land, 0U)} ==
               "JP_lowthrust_rel",
           "GENERIC_JETPACK_LAND_SOUND is JP_lowthrust_rel");
}

} // namespace

int main() {
    try {
        the_loop_follows_update_jetpack_sound();
        the_wire_flags_alone_drive_retail_cues();
        a_flight_stays_engaged_until_the_landing();
        the_glide_pack_gives_one_land_cue();
        a_hovering_observer_reading_is_not_a_landing();
        death_and_a_lost_pack_close_everything();
        the_builder_flies_without_glide_sounds();
        the_sound_files_ship();
    } catch (const std::exception& error) {
        std::cerr << "jetpack audio test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
