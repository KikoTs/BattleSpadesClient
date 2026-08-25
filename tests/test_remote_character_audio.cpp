#include "battlespades/world/remote_character_audio.hpp"

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

bool has_voice(const CharacterAudioEvents& events, ClassVoice voice, bool force = false) {
    for (const auto& event : events.events()) {
        if (event.kind == CharacterAudioEventKind::voice && event.voice == voice &&
            (!force || event.force_voice)) {
            return true;
        }
    }
    return false;
}

bool has_movement(const CharacterAudioEvents& events, MovementSound sound) {
    for (const auto& event : events.events()) {
        if (event.kind == CharacterAudioEventKind::movement && event.movement == sound) {
            return true;
        }
    }
    return false;
}

RemoteCharacterAudioSnapshot soldier() {
    RemoteCharacterAudioSnapshot snapshot;
    snapshot.generation = 7U;
    snapshot.class_id = 0U;
    snapshot.health = 100;
    return snapshot;
}

void spawn_is_delayed_and_forced_for_observers() {
    expect(remote_spawn_voice_delay(0U) == 1.0 && remote_spawn_voice_delay(1000U) == 2.0,
           "remote spawn delay must cover retail's exact 1-2 second range");
    RemoteCharacterAudioState state;
    begin_remote_character_life(state, soldier(), 1.25);
    expect(!has_voice(tick_remote_character_audio(state, 1.24, 0U), ClassVoice::spawn),
           "spawn VO fired before its authored delay");
    expect(has_voice(tick_remote_character_audio(state, 0.02, 0U), ClassVoice::spawn, true),
           "remote spawn VO must fire once and bypass the local 25% roll");
    expect(!has_voice(tick_remote_character_audio(state, 2.0, 0U), ClassVoice::spawn),
           "spawn VO repeated within one life");
}

void world_rows_derive_jump_fall_and_death_once() {
    RemoteCharacterAudioState state;
    auto snapshot = soldier();
    begin_remote_character_life(state, snapshot, 2.0);

    snapshot.input_flags = 0x10U;
    snapshot.vertical_speed = -0.2;
    auto events = observe_remote_character_audio(state, snapshot);
    expect(has_movement(events, MovementSound::jump) && has_voice(events, ClassVoice::jump),
           "replicated jump edge did not emit foley plus class VO");
    // A real fall necessarily outlives the shared 100 ms jump/land lockout.
    // Advance the client clock here instead of feeding physically impossible
    // back-to-back airborne and landing authority rows.
    static_cast<void>(tick_remote_character_audio(state, 0.2, 0U));

    // Two settled rows are required before the authority latch calls this a
    // landing. Damage on the landing row selects the fall-hurt pair.
    snapshot.input_flags = 0U;
    snapshot.vertical_speed = 0.0;
    static_cast<void>(observe_remote_character_audio(state, snapshot));
    snapshot.health = 72;
    events = observe_remote_character_audio(state, snapshot);
    expect(has_movement(events, MovementSound::land) &&
               has_movement(events, MovementSound::fall_hurt) &&
               has_voice(events, ClassVoice::fall_hurt),
           "damaging remote landing did not layer landing and fall-hurt audio");

    snapshot.health = 0;
    events = observe_remote_character_audio(state, snapshot);
    expect(has_voice(events, ClassVoice::death, true),
           "authoritative alive-to-dead edge did not emit the class death voice");
    expect(!has_voice(observe_remote_character_audio(state, snapshot), ClassVoice::death),
           "a repeated dead WorldUpdate replayed the death voice");
}

void walking_and_zombie_idle_use_bounded_client_ticks() {
    RemoteCharacterAudioState state;
    auto snapshot = soldier();
    snapshot.horizontal_speed = 1.0;
    snapshot.input_flags = 0x01U;
    begin_remote_character_life(state, snapshot, 2.0);
    expect(has_movement(tick_remote_character_audio(state, 1.0 / 60.0, 0U),
                        MovementSound::footstep),
           "moving remote player did not start the footstep cadence");

    snapshot.class_id = 4U;
    snapshot.generation = 8U;
    snapshot.horizontal_speed = 0.0;
    snapshot.input_flags = 0U;
    begin_remote_character_life(state, snapshot, 2.0);
    bool groaned{};
    for (int tick{}; tick < 60 * 7; ++tick) {
        if (has_voice(tick_remote_character_audio(state, 1.0 / 60.0, 0U),
                      ClassVoice::periodic)) {
            groaned = true;
            break;
        }
    }
    expect(groaned, "Zombie observer state never emitted its 3-6 second idle groan");
}

void player_id_reuse_resets_every_edge() {
    RemoteCharacterAudioState state;
    auto snapshot = soldier();
    begin_remote_character_life(state, snapshot, 1.0);
    static_cast<void>(tick_remote_character_audio(state, 1.1, 0U));
    snapshot.health = 0;
    static_cast<void>(observe_remote_character_audio(state, snapshot));

    snapshot.generation += 1U;
    snapshot.health = 100;
    begin_remote_character_life(state, snapshot, 1.0);
    expect(!state.death_announced && state.spawn_pending && state.health == 100,
           "new generation inherited stale death/spawn state from the reused id");
}

} // namespace

int main() {
    try {
        spawn_is_delayed_and_forced_for_observers();
        world_rows_derive_jump_fall_and_death_once();
        walking_and_zombie_idle_use_bounded_client_ticks();
        player_id_reuse_resets_every_edge();
        std::cout << "remote character audio tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
