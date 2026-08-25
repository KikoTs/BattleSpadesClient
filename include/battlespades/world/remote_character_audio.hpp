#pragma once

#include "battlespades/world/class_voice.hpp"
#include "battlespades/world/footstep_audio.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace battlespades::world {

/** One immutable observer snapshot decoded from a Protocol 168 WorldUpdate. */
struct RemoteCharacterAudioSnapshot final {
    std::uint32_t generation{};
    std::uint8_t class_id{};
    std::int16_t health{};
    std::uint8_t input_flags{};
    std::uint8_t action_flags{};
    std::uint8_t state_flags{};
    double horizontal_speed{};
    double vertical_speed{};
};

enum class CharacterAudioEventKind : std::uint8_t {
    movement,
    voice,
};

/** A semantic event; the frontend supplies position and actual audio assets. */
struct CharacterAudioEvent final {
    CharacterAudioEventKind kind{CharacterAudioEventKind::movement};
    MovementSound movement{MovementSound::none};
    ClassVoice voice{ClassVoice::count};
    /** Remote spawn VO is guaranteed by retail after its 1-2 second delay. */
    bool force_voice{};
};

/** Fixed-capacity batch: a packet/tick cannot allocate on the gameplay thread. */
struct CharacterAudioEvents final {
    std::array<CharacterAudioEvent, 8U> values{};
    std::size_t size{};

    void add_movement(MovementSound sound) noexcept;
    void add_voice(ClassVoice voice, bool force = false) noexcept;
    [[nodiscard]] std::span<const CharacterAudioEvent> events() const noexcept;
};

/**
 * Generation-safe observer audio state for one remote player.
 *
 * Protocol 168 does not send separate footstep/jump packets. The stock client
 * derives them from replicated motion/input just as it does animation, while
 * CreatePlayer owns spawn and health/life edges own death. This state keeps
 * those derived edges bounded and is discarded on leave or player-id reuse.
 */
struct RemoteCharacterAudioState final {
    bool initialized{};
    std::uint32_t generation{};
    std::uint8_t class_id{};
    std::int16_t health{};
    std::uint8_t input_flags{};
    std::uint8_t action_flags{};
    std::uint8_t state_flags{};
    double horizontal_speed{};
    double vertical_speed{};
    /** Greatest observed downward speed retained across settled landing rows. */
    double maximum_downward_speed{};
    bool airborne{};
    std::uint8_t grounded_snapshot_count{};
    bool jump_was_held{};
    bool jetpack_was_active{};
    bool airborne_health_loss{};
    bool death_announced{};
    bool spawn_pending{};
    double spawn_remaining{};
    double clock{};
    FootstepState footsteps{};
    LandJumpState land_jump{};
    PeriodicVoiceState periodic{};
};

/** Retail arms remote spawn VO for a uniform 1-2 second delay. */
[[nodiscard]] double remote_spawn_voice_delay(std::uint32_t roll) noexcept;

/** Reset every sound edge at authoritative CreatePlayer/new generation. */
void begin_remote_character_life(RemoteCharacterAudioState& state,
                                 const RemoteCharacterAudioSnapshot& snapshot,
                                 double spawn_delay) noexcept;

/** Consume a 30 Hz authority row and emit jump/land/fall/death edges once. */
[[nodiscard]] CharacterAudioEvents
observe_remote_character_audio(RemoteCharacterAudioState& state,
                               const RemoteCharacterAudioSnapshot& snapshot) noexcept;

/** Advance spawn delay, idle VO and movement cadence at the client tick rate. */
[[nodiscard]] CharacterAudioEvents
tick_remote_character_audio(RemoteCharacterAudioState& state,
                            double dt,
                            std::uint32_t interval_roll) noexcept;

} // namespace battlespades::world
