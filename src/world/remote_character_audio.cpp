#include "battlespades/world/remote_character_audio.hpp"

#include <algorithm>
#include <cmath>

namespace battlespades::world {

void CharacterAudioEvents::add_movement(MovementSound sound) noexcept {
    if (sound == MovementSound::none || size >= values.size()) {
        return;
    }
    values[size++] = {CharacterAudioEventKind::movement, sound, ClassVoice::count, false};
}

void CharacterAudioEvents::add_voice(ClassVoice voice, bool force) noexcept {
    if (voice == ClassVoice::count || size >= values.size()) {
        return;
    }
    values[size++] = {CharacterAudioEventKind::voice, MovementSound::none, voice, force};
}

std::span<const CharacterAudioEvent> CharacterAudioEvents::events() const noexcept {
    return {values.data(), size};
}

double remote_spawn_voice_delay(std::uint32_t roll) noexcept {
    return 1.0 + static_cast<double>(roll % 1001U) / 1000.0;
}

void begin_remote_character_life(RemoteCharacterAudioState& state,
                                 const RemoteCharacterAudioSnapshot& snapshot,
                                 double spawn_delay) noexcept {
    state = {};
    state.initialized = true;
    state.generation = snapshot.generation;
    state.class_id = snapshot.class_id;
    state.health = snapshot.health;
    state.input_flags = snapshot.input_flags;
    state.action_flags = snapshot.action_flags;
    state.state_flags = snapshot.state_flags;
    state.horizontal_speed = snapshot.horizontal_speed;
    state.vertical_speed = snapshot.vertical_speed;
    state.jump_was_held = (snapshot.input_flags & 0x10U) != 0U;
    state.jetpack_was_active = (snapshot.action_flags & 0x04U) != 0U;
    state.spawn_pending = snapshot.health > 0;
    state.spawn_remaining = std::clamp(spawn_delay, 1.0, 2.0);
}

CharacterAudioEvents
observe_remote_character_audio(RemoteCharacterAudioState& state,
                               const RemoteCharacterAudioSnapshot& snapshot) noexcept {
    CharacterAudioEvents result;
    if (!state.initialized || state.generation != snapshot.generation) {
        begin_remote_character_life(
            state, snapshot, remote_spawn_voice_delay(snapshot.generation + snapshot.class_id));
        return result;
    }

    const bool was_airborne = state.airborne;
    const bool was_jump_held = state.jump_was_held;
    const std::int16_t old_health = state.health;

    // WorldUpdate has no grounded bit. Use the same two-settled-row latch as
    // the remote character renderer so tiny slope/quantisation noise cannot
    // machine-gun jump/land sounds.
    if (std::abs(snapshot.vertical_speed) > 0.07) {
        state.airborne = true;
        state.grounded_snapshot_count = 0U;
        state.maximum_downward_speed =
            std::max(state.maximum_downward_speed, snapshot.vertical_speed);
    } else if (std::abs(snapshot.vertical_speed) < 0.02) {
        state.grounded_snapshot_count = static_cast<std::uint8_t>(
            std::min<unsigned int>(2U, state.grounded_snapshot_count + 1U));
        if (state.grounded_snapshot_count >= 2U) {
            state.airborne = false;
        }
    }

    const bool jump_held = (snapshot.input_flags & 0x10U) != 0U;
    const bool jetpack_active = (snapshot.action_flags & 0x04U) != 0U;
    if (was_airborne && snapshot.health < old_health) {
        state.airborne_health_loss = true;
    }

    LandJumpInput edge;
    edge.jumped = !was_airborne && state.airborne && jump_held && !was_jump_held;
    edge.landed = was_airborne && !state.airborne;
    // WorldUpdate does not carry the native fall-result integer. The last
    // observed downward velocity is the only observer-side distinction
    // between soft contact and the retail hard-landing path.
    edge.hard_landing =
        edge.landed &&
        (state.maximum_downward_speed > 0.24 || snapshot.health < old_health ||
         state.airborne_health_loss);
    edge.wade = (snapshot.state_flags & 0x08U) != 0U;
    edge.fall_damage = edge.landed &&
                       (snapshot.health < old_health || state.airborne_health_loss);
    const auto sounds = step_land_jump(state.land_jump, edge, 0.0);
    if (sounds.impact != MovementSound::none) {
        result.add_movement(sounds.impact);
        switch (sounds.impact) {
        case MovementSound::jump:
            result.add_voice(ClassVoice::jump);
            break;
        case MovementSound::water_jump:
            result.add_voice(ClassVoice::water_jump);
            break;
        case MovementSound::land:
            result.add_voice(ClassVoice::land);
            break;
        case MovementSound::water_land:
            result.add_voice(ClassVoice::water_land);
            break;
        default:
            break;
        }
    }
    // JETPACK_LAND_SOUND is not a landing event here: retail plays it when
    // Player.set_jetpack_passive clears the flag (world::step_jetpack_audio).
    if (sounds.fall_hurt) {
        result.add_movement(MovementSound::fall_hurt);
        result.add_voice(ClassVoice::fall_hurt);
    }
    if (old_health > 0 && snapshot.health <= 0 && !state.death_announced) {
        result.add_voice(ClassVoice::death, true);
        state.death_announced = true;
        state.spawn_pending = false;
    }
    if (edge.landed) {
        state.airborne_health_loss = false;
        state.maximum_downward_speed = 0.0;
    }

    state.class_id = snapshot.class_id;
    state.health = snapshot.health;
    state.input_flags = snapshot.input_flags;
    state.action_flags = snapshot.action_flags;
    state.state_flags = snapshot.state_flags;
    state.horizontal_speed = snapshot.horizontal_speed;
    state.vertical_speed = snapshot.vertical_speed;
    state.jump_was_held = jump_held;
    state.jetpack_was_active = jetpack_active;
    return result;
}

CharacterAudioEvents
tick_remote_character_audio(RemoteCharacterAudioState& state,
                            double dt,
                            std::uint32_t interval_roll) noexcept {
    CharacterAudioEvents result;
    if (!state.initialized || !std::isfinite(dt) || dt <= 0.0) {
        return result;
    }
    static_cast<void>(step_land_jump(state.land_jump, {}, dt));
    if (state.health <= 0) {
        return result;
    }

    state.clock += dt;
    if (state.spawn_pending) {
        state.spawn_remaining -= dt;
        if (state.spawn_remaining <= 0.0) {
            state.spawn_pending = false;
            result.add_voice(ClassVoice::spawn, true);
        }
    }

    const bool periodic_class =
        state.class_id == 4U || state.class_id == 14U || state.class_id == 15U;
    if (periodic_class && step_periodic_voice(state.periodic, dt, interval_roll)) {
        result.add_voice(ClassVoice::periodic);
    }

    FootstepInput input;
    input.walking = state.horizontal_speed > 0.02;
    input.airborne = state.airborne;
    input.crouch = (state.input_flags & 0x20U) != 0U;
    input.sneak = (state.input_flags & 0x40U) != 0U;
    input.sprint = (state.input_flags & 0x80U) != 0U;
    input.wade = (state.state_flags & 0x08U) != 0U;
    result.add_movement(step_footsteps(state.footsteps, input, state.clock));
    return result;
}

} // namespace battlespades::world
