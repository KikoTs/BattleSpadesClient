#include "battlespades/world/footstep_audio.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using namespace battlespades::world;

void expect(bool value, const std::string& message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] FootstepInput walking() {
    FootstepInput input;
    input.walking = true;
    return input;
}

/**
 * Water is chosen by the wade bit alone.
 *
 * Worth pinning because the obvious guess -- look up the material of the block
 * underfoot -- is wrong, and no per-surface samples ship to support it.
 */
void water_is_selected_by_the_wade_bit() {
    auto dry = walking();
    expect(footstep_family(dry) == MovementSound::footstep,
           "walking on dry land must use the footstep family");
    auto wet = walking();
    wet.wade = true;
    expect(footstep_family(wet) == MovementSound::wade,
           "walking in water must use the wade family");
}

void crouching_and_sneaking_and_falling_are_silent() {
    for (const auto suppress : {0, 1, 2, 3}) {
        auto input = walking();
        if (suppress == 0)
            input.crouch = true;
        if (suppress == 1)
            input.sneak = true;
        if (suppress == 2)
            input.airborne = true;
        if (suppress == 3)
            input.walking = false;
        expect(footstep_family(input) == MovementSound::none,
               "a suppressed character must make no footstep");
    }
}

/** Sprinting is audibly faster, and the cadence must not drift. */
void the_cadence_matches_the_gait() {
    const auto count_steps = [](bool sprint, double seconds) {
        FootstepState state;
        auto input = walking();
        input.sprint = sprint;
        std::size_t steps{};
        constexpr double dt{1.0 / 60.0};
        double now{};
        for (double elapsed{}; elapsed < seconds; elapsed += dt) {
            now += dt;
            if (step_footsteps(state, input, now) != MovementSound::none) {
                ++steps;
            }
        }
        return steps;
    };
    // 10 s at 0.512 s per step is about 19 walking steps; at 0.386 about 25.
    const auto walk_steps = count_steps(false, 10.0);
    const auto sprint_steps = count_steps(true, 10.0);
    expect(walk_steps >= 18U && walk_steps <= 21U,
           "walking cadence drifted: " + std::to_string(walk_steps) + " steps in 10 s");
    expect(sprint_steps > walk_steps, "sprinting must produce more steps than walking");
    expect(sprint_steps >= 24U && sprint_steps <= 28U,
           "sprint cadence drifted: " + std::to_string(sprint_steps) + " steps in 10 s");
}

/** Soft contact is silent; hard landings and real jumps keep retail's lockout. */
void land_and_jump_share_a_lockout() {
    LandJumpState state;
    LandJumpInput input;
    input.landed = true;
    auto sounds = step_land_jump(state, input, 1.0 / 60.0);
    expect(sounds.impact == MovementSound::none,
           "a damage-free landing must not play the fall cue");

    // Safe contact must not consume the shared repeat lockout.
    LandJumpInput hop;
    hop.jumped = true;
    sounds = step_land_jump(state, hop, 1.0 / 60.0);
    expect(sounds.impact == MovementSound::jump,
           "a real jump immediately after safe contact must sound");

    // Fall hurt is never swallowed even when its landing thud is locked out.
    LandJumpInput hurt;
    hurt.landed = true;
    hurt.hard_landing = true;
    hurt.fall_damage = true;
    sounds = step_land_jump(state, hurt, 1.0 / 60.0);
    expect(sounds.impact == MovementSound::none && sounds.fall_hurt,
           "fall hurt must layer independently of the impact lockout");

    // Past the lockout a damaging fall carries both cues.
    for (int tick{}; tick < 12; ++tick) {
        static_cast<void>(step_land_jump(state, LandJumpInput{}, 1.0 / 60.0));
    }
    sounds = step_land_jump(state, hurt, 1.0 / 60.0);
    expect(sounds.impact == MovementSound::land && sounds.fall_hurt,
           "a damaging landing must play both the land cue and the hurt cue");
}

void damage_free_hard_landing_still_thuds() {
    LandJumpState state;
    LandJumpInput hard;
    hard.landed = true;
    hard.hard_landing = true;
    const auto sounds = step_land_jump(state, hard, 1.0 / 60.0);
    expect(sounds.impact == MovementSound::land && !sounds.fall_hurt,
           "a damage-free hard landing must thud without a hurt layer");
}

void water_impacts_differ_from_dry_ones() {
    LandJumpState state;
    LandJumpInput splash;
    splash.landed = true;
    splash.hard_landing = true;
    splash.fall_damage = true;
    splash.wade = true;
    const auto sounds = step_land_jump(state, splash, 1.0 / 60.0);
    expect(sounds.impact == MovementSound::water_land, "landing in water must splash, not thud");
}

/**
 * Every stem this module can name must exist on disk.
 *
 * A movement system that references a sample we do not ship fails silently --
 * the player simply hears nothing and there is no error anywhere.
 */
void every_named_sample_ships() {
    const std::filesystem::path root{AOS_TEST_ASSET_ROOT};
    const std::filesystem::path sounds = root / "sounds";
    if (!std::filesystem::is_directory(sounds)) {
        return;
    }
    constexpr MovementSound all[]{MovementSound::footstep,
                                  MovementSound::wade,
                                  MovementSound::jump,
                                  MovementSound::water_jump,
                                  MovementSound::jetpack_land,
                                  MovementSound::land,
                                  MovementSound::water_land,
                                  MovementSound::fall_hurt};
    for (const auto sound : all) {
        const auto variants = movement_sound_variants(sound);
        expect(variants > 0U, "a real movement sound must have at least one variant");
        for (std::uint32_t variant{}; variant < variants; ++variant) {
            const std::string stem = movement_sound_stem(sound, variant);
            expect(!stem.empty(), "a real movement sound must have a stem");
            const auto path = sounds / (stem + ".ogg");
            expect(std::filesystem::is_regular_file(path),
                   "missing movement sample: " + stem + ".ogg");
        }
    }
    expect(std::string{movement_sound_stem(MovementSound::none, 0U)}.empty(),
           "the silent family must name no sample");
    expect(std::string{class_movement_sound_stem(4U, MovementSound::footstep, 2U)} ==
               "zombie_footstep_003",
           "Zombie footsteps must use their authored class bank");
    expect(std::string{class_movement_sound_stem(14U, MovementSound::water_land, 0U)} ==
               "zombie_land_water",
           "Fast Zombie water landing must use Zombie foley");
    expect(std::string{class_movement_sound_stem(13U, MovementSound::jetpack_land, 0U)}.empty(),
           "UGC Builder's authored jetpack-land silence must remain blank");
}

} // namespace

int main() {
    try {
        water_is_selected_by_the_wade_bit();
        crouching_and_sneaking_and_falling_are_silent();
        the_cadence_matches_the_gait();
        land_and_jump_share_a_lockout();
        damage_free_hard_landing_still_thuds();
        water_impacts_differ_from_dry_ones();
        every_named_sample_ships();
        std::cout << "footstep audio tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
