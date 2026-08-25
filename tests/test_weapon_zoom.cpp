#include "battlespades/world/weapon_zoom.hpp"

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

void expect_near(double actual, double expected, double tolerance,
                 const std::string& message) {
    expect(std::abs(actual - expected) <= tolerance,
           message + " (got " + std::to_string(actual) + ", wanted " +
               std::to_string(expected) + ")");
}

constexpr std::uint8_t classic_rifle{6U};
constexpr std::uint8_t smg{7U};
constexpr std::uint8_t minigun{8U};
constexpr std::uint8_t machine_gun{15U};
constexpr std::uint8_t sniper{18U};
constexpr std::uint8_t sniper2{19U};
constexpr std::uint8_t grenade{11U};
constexpr double retail_tick{1.0 / 60.0};

void the_field_of_view_is_a_subtraction_not_a_division() {
    // The four values every ADS state in the game lands on. A divide-based
    // formula reproduces only the first of them.
    expect_near(zoom_fov_y_degrees(0.0), 75.0, 1.0e-12, "hip fire is 75 degrees");
    expect_near(zoom_fov_y_degrees(1.0), 37.5, 1.0e-12,
                "iron sights are 37.5 degrees, a 2x view");
    expect_near(zoom_fov_y_degrees(1.5), 18.75, 1.0e-12,
                "the sniper scope is 18.75 degrees");
    expect_near(zoom_fov_y_degrees(1.2), 30.0, 1.0e-12,
                "the second sniper scope is 30 degrees");
}

void only_the_recovered_aiming_tools_have_a_zoom_target() {
    expect_near(zoom_target_multiplier(sniper, true), 1.5, 1.0e-12,
                "the sniper aims at its recovered 1.5 multiplier");
    expect_near(zoom_target_multiplier(sniper2, true), 1.2, 1.0e-12,
                "the second sniper aims at its recovered 1.2 multiplier");
    // The point of the whole exercise: twenty weapons whose multiplier is the
    // inherited 1.0 still aim, and still halve the field of view doing it.
    for (const std::uint8_t iron : {classic_rifle, smg, std::uint8_t{9U},
                                    std::uint8_t{17U}, std::uint8_t{60U}}) {
        expect_near(zoom_target_multiplier(iron, true), 1.0, 1.0e-12,
                    "an iron-sight weapon aims at a multiplier of exactly 1.0");
    }
    // A sight the dispatcher can never reach must not become a target: the
    // minigun owns a real sight model and only can_zoom = False denies it.
    expect(zoom_target_multiplier(minigun, true) == 0.0,
           "the minigun must not acquire a scope through its unreachable sight");
    expect(zoom_target_multiplier(machine_gun, true) == 0.0,
           "the mounted gun deploys rather than entering the sight branch");
    expect(zoom_target_multiplier(grenade, true) == 0.0,
           "a sightless throwable has nothing to aim");
    expect(zoom_target_multiplier(sniper, false) == 0.0,
           "leaving the sight targets zero, not one");
    expect(zoom_target_multiplier(200U, true) == 0.0, "an unknown tool cannot aim");
}

void one_tick_at_sixty_hertz_reproduces_the_retail_recurrence() {
    // Retail adds (target - level) / divisor once per scheduled update. At the
    // scheduled rate this port must land on the identical number, or the whole
    // dt correction is measuring something else.
    const auto exact = [](double target, double divisor) {
        return target / divisor;
    };
    expect_near(advance_zoom_level(0.0, 1.5, sniper, retail_tick),
                exact(1.5, 7.5), 1.0e-12,
                "the sniper's first tick must match retail exactly");
    expect_near(advance_zoom_level(0.0, 1.0, classic_rifle, retail_tick),
                exact(1.0, 2.5), 1.0e-12,
                "the rifle's first tick must match retail exactly");
    expect_near(advance_zoom_level(0.0, 1.0, smg, retail_tick),
                exact(1.0, 5.0), 1.0e-12,
                "an unlisted tool must use the shared default divisor");
}

void the_transition_reverses_on_the_outgoing_rate() {
    // Direction is chosen by comparing the destination against the ramp, so a
    // weapon whose two rates differ must be visibly asymmetric.
    const double in_step = advance_zoom_level(0.0, 1.5, sniper, retail_tick);
    const double out_step = 1.5 - advance_zoom_level(1.5, 0.0, sniper, retail_tick);
    expect_near(in_step, 1.5 / 7.5, 1.0e-12, "zoom-in uses the in divisor");
    expect_near(out_step, 1.5 / 6.0, 1.0e-12, "zoom-out uses the out divisor");
    expect(out_step > in_step,
           "the sniper must leave its scope faster than it enters it");
}

void the_ramp_takes_the_recovered_number_of_ticks() {
    // The doc's characterisation: ticks to cover 99% of the travel. These are
    // what make the sniper read as a slow, deliberate scope and the rifle as a
    // quick shoulder -- roughly half a second against roughly a sixth.
    const auto ticks_to_99_percent = [](std::uint8_t tool, double target) {
        double level{};
        int ticks{};
        while (std::abs(target - level) > 0.01 * std::abs(target) && ticks < 1000) {
            level = advance_zoom_level(level, target, tool, retail_tick);
            ++ticks;
        }
        return ticks;
    };
    expect(ticks_to_99_percent(sniper, 1.5) == 33,
           "the sniper must take the recovered ~32 ticks to settle");
    expect(ticks_to_99_percent(classic_rifle, 1.0) == 10,
           "the rifle must take the recovered ~9 ticks to settle");
    expect(ticks_to_99_percent(smg, 1.0) == 21,
           "the shared default must take the recovered ~20.6 ticks");
    // Half a second of scope travel, which is the feel the snap replaced.
    expect_near(static_cast<double>(ticks_to_99_percent(sniper, 1.5)) * retail_tick,
                0.54, 0.02, "the sniper zoom-in must last about half a second");
}

void the_transition_lasts_the_same_time_at_any_step_length() {
    // Retail never promised this: its recurrence has no dt term and it simply
    // ran at a fixed 60 Hz, which is also what our session does. This pins the
    // free property of the dt-correct form -- insurance against the ramp ever
    // being advanced from a render loop, where retail's raw recurrence would
    // run 2.4x fast at 144 fps.
    //
    // A sixth of a second, deliberately: it is a whole number of steps at all
    // three rates. Rounding a fractional step count would compare different
    // elapsed times and report that as a rate dependence.
    constexpr double window{1.0 / 6.0};
    const auto level_after_window = [](double dt) {
        const auto steps = static_cast<int>(std::lround(window / dt));
        expect(std::abs(static_cast<double>(steps) * dt - window) < 1.0e-12,
               "the sample window must divide exactly into whole steps");
        double level{};
        for (int step{}; step < steps; ++step) {
            level = advance_zoom_level(level, 1.5, sniper, dt);
        }
        return level;
    };
    const double at_60 = level_after_window(retail_tick);
    const double at_144 = level_after_window(1.0 / 144.0);
    const double at_30 = level_after_window(1.0 / 30.0);
    expect(at_60 > 0.4 && at_60 < 1.4,
           "the sample point must be mid-transition for the comparison to mean anything");
    // Tight, because equal elapsed time must give the identical number up to
    // the rounding of the extra multiplications, not merely a similar one.
    expect_near(at_144, at_60, 1.0e-9,
                "144 Hz must reach the same place as 60 Hz in the same time");
    expect_near(at_30, at_60, 1.0e-9,
                "30 Hz must reach the same place as 60 Hz in the same time");
}

void the_ramp_settles_instead_of_approaching_forever() {
    double level{};
    for (int tick{}; tick < 600; ++tick) {
        level = advance_zoom_level(level, 1.5, sniper, retail_tick);
    }
    expect(level == 1.5, "a finished zoom-in must sit exactly on its target");
    for (int tick{}; tick < 600; ++tick) {
        level = advance_zoom_level(level, 0.0, sniper, retail_tick);
    }
    expect(level == 0.0, "a finished zoom-out must return exactly to hip fire");
    expect(advance_zoom_level(1.5, 1.5, sniper, retail_tick) == 1.5,
           "an idle ramp must not drift");
    expect(advance_zoom_level(0.7, 1.5, sniper, 0.0) == 0.7,
           "a zero-length step must not move the ramp");
}

void every_aiming_tool_has_a_usable_rate() {
    int aiming{};
    for (const auto& tool : weapon_catalog()) {
        if (!aims_down_sights(weapon_secondary_behavior(tool))) {
            continue;
        }
        ++aiming;
        const auto rate = zoom_transition_rate(tool.tool_id);
        expect(rate.in_divisor > 1.0 && rate.out_divisor > 1.0,
               "a divisor of 1 or less would snap or overshoot");
        const double target = zoom_target_multiplier(tool.tool_id, true);
        expect(target >= 1.0, "every aiming tool must magnify at least twofold");
        expect(zoom_fov_y_degrees(target) <= 37.5,
               "no aiming tool may end up wider than iron sights");
    }
    // The count is the headline of the recovery: twenty-two, not two.
    expect(aiming == 22, "exactly twenty-two catalog tools must aim down sights");
}

} // namespace

int main() {
    try {
        the_field_of_view_is_a_subtraction_not_a_division();
        only_the_recovered_aiming_tools_have_a_zoom_target();
        one_tick_at_sixty_hertz_reproduces_the_retail_recurrence();
        the_transition_reverses_on_the_outgoing_rate();
        the_ramp_takes_the_recovered_number_of_ticks();
        the_transition_lasts_the_same_time_at_any_step_length();
        the_ramp_settles_instead_of_approaching_forever();
        every_aiming_tool_has_a_usable_rate();
        std::cout << "weapon zoom tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
