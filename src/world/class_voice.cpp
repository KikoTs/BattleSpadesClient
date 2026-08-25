#include "battlespades/world/class_voice.hpp"

#include <algorithm>

namespace battlespades::world {

const ClassVoiceBank& ClassVoiceSet::bank(ClassVoice voice) const noexcept {
    switch (voice) {
    case ClassVoice::death:
        return death;
    case ClassVoice::periodic:
        return periodic;
    case ClassVoice::spawn:
        return spawn;
    case ClassVoice::jump:
        return jump;
    case ClassVoice::water_jump:
        return water_jump;
    case ClassVoice::land:
        return land;
    case ClassVoice::water_land:
        return water_land;
    case ClassVoice::fall_hurt:
    case ClassVoice::count:
        break;
    }
    return fall_hurt;
}

std::string_view choose_voice_line(const ClassVoiceBank& bank, std::uint32_t roll,
                                   std::uint32_t pick,
                                   VoiceSelectionState& state) noexcept {
    // An empty bank is a deliberate silence, not an error: whole classes have no
    // speech at all, and one has no death voice while keeping its foley.
    if (bank.stems.empty() || bank.chance == 0U) {
        return {};
    }
    // media.py encodes a negative chance as "suppress the immediately next
    // trigger", not merely "choose another take". It clears the token on the
    // suppressed edge without consuming either random roll.
    if (bank.no_consecutive_repeat && state.suppress_next) {
        state.suppress_next = false;
        return {};
    }
    // random.randint(0, 100) has 101 outcomes and retail accepts chance >= roll.
    if (roll % 101U > bank.chance) {
        return {};
    }
    const auto count = static_cast<std::uint32_t>(bank.stems.size());
    auto index = pick % count;
    if (state.has_last && count > 1U && index == state.last_index) {
        // Step to the neighbour rather than re-rolling, so the guarantee holds
        // without an unbounded loop and every take stays reachable.
        index = (index + 1U) % count;
    }
    state.last_index = index;
    state.has_last = true;
    state.suppress_next = bank.no_consecutive_repeat;
    return bank.stems[index];
}

double local_spawn_voice_delay(std::uint32_t roll) noexcept {
    const double span = spawn_voice_delay_maximum - spawn_voice_delay_minimum;
    return spawn_voice_delay_minimum +
           span * (static_cast<double>(roll % 1001U) / 1000.0);
}

void begin_local_spawn_voice(LocalSpawnVoiceState& state,
                             std::uint8_t class_id,
                             double delay) noexcept {
    const bool class_changed =
        !state.has_previous_class || state.previous_class_id != class_id;
    state.class_id = class_id;
    state.previous_class_id = class_id;
    state.has_previous_class = true;
    state.force = class_changed;
    state.remaining = std::clamp(delay,
                                 spawn_voice_delay_minimum,
                                 spawn_voice_delay_maximum);
    state.armed = true;
}

std::optional<LocalSpawnVoiceEvent>
step_local_spawn_voice(LocalSpawnVoiceState& state, double dt) noexcept {
    if (!state.armed) {
        return std::nullopt;
    }
    state.remaining -= std::max(0.0, dt);
    if (state.remaining > 0.0) {
        return std::nullopt;
    }
    state.armed = false;
    return LocalSpawnVoiceEvent{state.class_id, state.force};
}

bool step_periodic_voice(PeriodicVoiceState& state, double dt,
                         std::uint32_t interval_roll) noexcept {
    const auto rearm = [interval_roll] {
        const double span = periodic_voice_maximum - periodic_voice_minimum;
        return periodic_voice_minimum +
               span * (static_cast<double>(interval_roll % 1000U) / 999.0);
    };
    if (!state.armed) {
        // Wait a full interval before the first line, so a class does not
        // vocalise on the very frame it spawns.
        state.armed = true;
        state.remaining = rearm();
        return false;
    }
    state.remaining -= dt;
    if (state.remaining > 0.0) {
        return false;
    }
    state.remaining = rearm();
    return true;
}

} // namespace battlespades::world
