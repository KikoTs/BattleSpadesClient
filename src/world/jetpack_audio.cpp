#include "battlespades/world/jetpack_audio.hpp"

#include <algorithm>

namespace battlespades::world {

JetpackLoop retail_jetpack_loop(const JetpackAudioInput& input) noexcept {
    if (!input.has_pack || !input.alive) return JetpackLoop::none;
    if (input.active) return JetpackLoop::flight;
    if (input.passive && !input.controlling_prefab &&
        input.class_id != jetpack_silent_glide_class) {
        return JetpackLoop::low_thrust;
    }
    return JetpackLoop::none;
}

JetpackAudioCues step_jetpack_audio(JetpackAudioState& state,
                                    const JetpackAudioInput& input,
                                    double dt) noexcept {
    const bool usable = input.has_pack && input.alive;
    const bool active = usable && input.active;

    if (active) {
        state.thrust_this_flight = true;
        state.seconds_since_thrust = 0.0;
    } else if (state.thrust_this_flight) {
        state.seconds_since_thrust += std::clamp(dt, 0.0, 0.25);
        const bool settled = state.seconds_since_thrust >= jetpack_descent_grace_seconds;
        if (!usable || (!input.airborne && settled)) {
            state.thrust_this_flight = false;
        }
    }
    const bool passive = usable && (input.passive || state.thrust_this_flight);

    JetpackAudioCues cues;
    auto presented = input;
    presented.active = active;
    presented.passive = passive;
    cues.loop = retail_jetpack_loop(presented);
    if (state.initialized) {
        cues.ignite = active && !state.active;
        cues.release = !active && state.active;
        cues.land = !passive && state.passive;
    }
    state.initialized = true;
    state.active = active;
    state.passive = passive;
    return cues;
}

} // namespace battlespades::world
