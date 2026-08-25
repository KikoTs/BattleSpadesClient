#include "battlespades/world/weapon_fire_sound.hpp"

#include "battlespades/world/weapon_catalog.hpp"

#include <cmath>
#include <cstdint>
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

constexpr std::uint8_t smg{7U};
constexpr std::uint8_t minigun{8U};
constexpr std::uint8_t mounted_gun{15U};
constexpr std::uint8_t assault_rifle{60U};
constexpr std::uint8_t pistol{6U};

void burst_follow_up_rounds_are_always_silent() {
    // Whatever else is true -- loop cue present, trigger live, fully spun -- a
    // follow-up round says nothing, because the opener's sample already contains
    // the whole burst.
    for (const bool has_loop : {false, true}) {
        for (const bool live : {false, true}) {
            const FireSoundInput input{assault_rifle, has_loop, true, live, false, 1.0};
            expect(fire_sound_shape(input) == FireSoundShape::silent,
                   "a burst follow-up round must be silent");
        }
    }
}

void a_weapon_without_a_loop_cue_fires_per_round() {
    const FireSoundInput input{pistol, false, false, true, false, 0.0};
    expect(fire_sound_shape(input) == FireSoundShape::one_shot_per_round,
           "a weapon with no loop cue must play one sample per round");
}

/**
 * The minigun swaps shape at FULL barrel speed, not as soon as it can fire.
 *
 * Firing is allowed from half spin, but retail keeps playing discrete reports
 * until the barrels are at full speed. Opening the loop during spin-up is what
 * turned a single round into roughly five: the loop grain is a tenth of a second
 * and its tail is another one and three quarter seconds.
 */
void the_minigun_only_loops_at_full_spin() {
    for (const double spin : {0.0, 0.4, 0.5, 0.75, 0.99}) {
        const FireSoundInput input{minigun, true, false, true, false, spin};
        expect(fire_sound_shape(input) == FireSoundShape::one_shot_per_round,
               "the minigun must fire discrete reports below full spin (spin " +
                   std::to_string(spin) + ")");
    }
    const FireSoundInput full{minigun, true, false, true, false, 1.0};
    expect(fire_sound_shape(full) == FireSoundShape::sustained_loop,
           "the minigun must switch to its loop at full spin");

    // The fraction is a ratio of catalogued doubles whose divisor is not exactly
    // representable, so a fully spun barrel can land a hair under 1.0. Without
    // an epsilon the loop would simply never open.
    const FireSoundInput nearly{minigun, true, false, true, false,
                                std::nextafter(1.0, 0.0)};
    expect(fire_sound_shape(nearly) == FireSoundShape::sustained_loop,
           "full spin must survive floating-point rounding");
}

void the_mounted_gun_swaps_shape_on_deployment() {
    const FireSoundInput carried{mounted_gun, true, false, true, false, 0.0};
    expect(fire_sound_shape(carried) == FireSoundShape::one_shot_per_round,
           "an undeployed mounted gun fires discrete reports");
    const FireSoundInput deployed{mounted_gun, true, false, true, true, 0.0};
    expect(fire_sound_shape(deployed) == FireSoundShape::sustained_loop,
           "a deployed mounted gun runs a sustained loop");
}

/**
 * The quantum must match the loop GRAIN, not the weapon's catalogued cadence.
 *
 * These two coincide for the SMG and differ for exactly the two weapons whose
 * cadence is not what their loop runs at -- which is why this is a lookup rather
 * than a field read. The minigun's interval ramps with barrel speed; the mounted
 * gun's catalogued interval is its undeployed one.
 */
void the_loop_quantum_matches_the_sample_grain() {
    const auto* smg_weapon = find_weapon_definition(smg);
    expect(smg_weapon != nullptr, "the SMG must exist");
    expect(std::abs(fire_loop_quantum(smg, false) - smg_weapon->fire_interval) < 1e-9,
           "an ordinary automatic quantises to its own fire interval");
    // smg_fire_loop.ogg measures 0.094 s against this 0.1 s cadence: one round.
    expect(std::abs(fire_loop_quantum(smg, false) - 0.1) < 1e-6,
           "the SMG's quantum must be 0.1 s");

    const auto* minigun_weapon = find_weapon_definition(minigun);
    expect(minigun_weapon != nullptr, "the minigun must exist");
    expect(std::abs(fire_loop_quantum(minigun, false) - 0.1) < 1e-6,
           "the minigun quantises to its fixed sound length, not its interval");
    expect(minigun_weapon->fire_interval > 0.25,
           "this test is pointless unless the minigun's interval really differs");

    expect(std::abs(fire_loop_quantum(mounted_gun, true) - 0.1) < 1e-6,
           "a deployed mounted gun quantises to its deployed cadence");
    const auto* mounted = find_weapon_definition(mounted_gun);
    expect(mounted != nullptr && mounted->fire_interval > 0.25,
           "the mounted gun's catalogued interval should be the undeployed one");

    expect(fire_loop_quantum(200U, false) == 0.0,
           "an unknown tool has no quantum");
}

void death_cancels_a_partial_fire_grain_immediately() {
    FireLoopCadence cadence;
    cadence.open(0.1);
    expect(cadence.firing() && !cadence.advance(0.04, false),
           "ordinary trigger release must wait for its grain boundary");

    // Death/unset is not an ordinary release. The old frontend merely cleared
    // `firing`, which also stopped this clock and stranded the OpenAL loop
    // forever. The retail on_unset path closes immediately with no tail.
    cadence.cancel();
    expect(!cadence.firing() && cadence.elapsed() == 0.0 && cadence.quantum() == 0.0,
           "death must erase the partial grain instead of stranding it");
    expect(!cadence.advance(1.0, false),
           "a cancelled previous-life loop must never close or tail again");

    cadence.open(0.1);
    expect(cadence.advance(0.1, false) && !cadence.firing(),
           "ordinary release still closes with a tail at the quantised boundary");
}

} // namespace

int main() {
    try {
        burst_follow_up_rounds_are_always_silent();
        a_weapon_without_a_loop_cue_fires_per_round();
        the_minigun_only_loops_at_full_spin();
        the_mounted_gun_swaps_shape_on_deployment();
        the_loop_quantum_matches_the_sample_grain();
        death_cancels_a_partial_fire_grain_immediately();
        std::cout << "weapon fire sound tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
