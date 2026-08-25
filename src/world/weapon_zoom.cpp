#include "battlespades/world/weapon_zoom.hpp"

#include "battlespades/world/weapon_catalog.hpp"

#include <cmath>

namespace battlespades::world {
namespace {

constexpr std::uint8_t classic_rifle_tool_id{6U};
constexpr std::uint8_t sniper_tool_id{18U};
constexpr std::uint8_t sniper2_tool_id{19U};

/**
 * Below this much remaining travel the ramp is treated as arrived.
 *
 * A tenth of a milli-multiplier is under four thousandths of a degree of
 * vertical field of view -- invisible, and small enough that snapping cannot
 * be mistaken for the transition finishing early.
 */
constexpr double arrival_epsilon{1.0e-4};

} // namespace

ZoomTransitionRate zoom_transition_rate(std::uint8_t tool_id) noexcept {
    switch (tool_id) {
    case classic_rifle_tool_id:
        return {2.5, 6.0};
    case sniper_tool_id:
        // Three times slower going in than the rifle, and the same coming out.
        return {7.5, 6.0};
    case sniper2_tool_id:
        return {2.5, 6.0};
    default:
        return {5.0, 5.0};
    }
}

double zoom_target_multiplier(std::uint8_t tool_id, bool aiming) noexcept {
    if (!aiming) {
        return 0.0;
    }
    const auto* weapon = find_weapon_definition(tool_id);
    if (weapon == nullptr || !aims_down_sights(weapon_secondary_behavior(*weapon))) {
        return 0.0;
    }
    // Iron sights leave this at the inherited 1.0 and still magnify: one unit
    // of zoom is 37.5 degrees of field of view, a 2x view, not a null state.
    return weapon->retail.use.zoom_factor.value_or(1.0);
}

double advance_zoom_level(double zoom_level, double target, std::uint8_t tool_id,
                          double dt) noexcept {
    const double remaining = target - zoom_level;
    if (std::abs(remaining) < arrival_epsilon) {
        return target;
    }
    if (!(dt > 0.0)) {
        return zoom_level;
    }
    const auto rate = zoom_transition_rate(tool_id);
    // Retail selects the direction by comparing the destination against the
    // ramp, not against the previous destination, so reversing mid-transition
    // switches rate immediately rather than finishing the old sweep.
    const double divisor = remaining > 0.0 ? rate.in_divisor : rate.out_divisor;
    if (!(divisor > 1.0)) {
        return target;
    }
    // This is the dt-correct exponential `1 - exp(-rate * dt)`, written with
    // the recovered divisor left literal instead of pre-converted into a rate
    // constant: raising the per-tick retention to the number of ticks dt spans
    // is exactly exp(-rate * dt) for rate = -60 * ln(1 - 1/divisor), and it has
    // two properties the pre-converted form loses. At the scheduled 60 Hz step
    // the exponent is 1 and the arithmetic collapses to retail's own
    // `level += (target - level) / divisor`, bit for bit; and the number a
    // reader checks against constants.py is still on the page.
    //
    // Frame-rate independence is a free bonus, not a parity requirement:
    // retail's recurrence has no dt term at all and never promised it, and our
    // session advances this on a fixed 60 Hz tick regardless. It matters only
    // as insurance against this ever being called from a render loop, where
    // the raw recurrence would run 2.4x fast at 144 fps.
    const double retained_per_tick = 1.0 - 1.0 / divisor;
    const double retained =
        std::pow(retained_per_tick, dt * zoom_transition_tick_hz);
    const double advanced = target - remaining * retained;
    return std::abs(target - advanced) < arrival_epsilon ? target : advanced;
}

} // namespace battlespades::world
