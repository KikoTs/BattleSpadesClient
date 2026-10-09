#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace battlespades::world {

/** The eight vocal events a class can have. */
enum class ClassVoice : std::uint8_t {
    death,
    /** Idle vocalisation on a re-arming timer; the zombie groan. */
    periodic,
    spawn,
    jump,
    water_jump,
    land,
    water_land,
    fall_hurt,
    count,
};

/**
 * One vocal bank.
 *
 * An EMPTY bank is a deliberate silence rather than missing data: several
 * classes have no spawn line, the zombies have no speech at all, and one class
 * has no death voice while keeping full movement foley.
 */
struct ClassVoiceBank final {
    std::span<const std::string_view> stems;
    /** Percent chance the line plays at all when its event fires. */
    std::uint8_t chance{};
    /**
     * Suppress the trigger straight after a played line (media.py
     * `disallow_consecutive_plays`).
     *
     * Encoded in the source as a NEGATIVE chance (the jump/land VO rows),
     * which is a flag rather than a negative probability -- reading it
     * literally silences the slot. Positive-chance rows (spawn 25, death and
     * fall-hurt 100, the Zombie groan) never set it. Not repeating the same
     * take is separate and applies to every bank.
     */
    bool no_consecutive_repeat{};
};

/** Every vocal bank for one class, in ClassVoice order. */
struct ClassVoiceSet final {
    std::uint8_t class_id{};
    ClassVoiceBank death;
    ClassVoiceBank periodic;
    ClassVoiceBank spawn;
    ClassVoiceBank jump;
    ClassVoiceBank water_jump;
    ClassVoiceBank land;
    ClassVoiceBank water_land;
    ClassVoiceBank fall_hurt;

    [[nodiscard]] const ClassVoiceBank& bank(ClassVoice voice) const noexcept;
};

[[nodiscard]] const ClassVoiceSet* find_class_voice(std::uint8_t class_id) noexcept;
[[nodiscard]] std::span<const ClassVoiceSet> class_voice_table() noexcept;

/** Client presentation: Deuce uses varied human death takes; other banks stay authored. */
[[nodiscard]] ClassVoiceBank presentation_voice_bank(std::uint8_t class_id,
                                                     ClassVoice voice) noexcept;

/** Idle-vocalisation timer state. */
struct PeriodicVoiceState final {
    double remaining{};
    bool armed{};
};

/**
 * Mutable selection state for one speaker and one voice slot.
 *
 * Retail stores both the previously chosen take and a one-event suppression
 * token in the shared sound row. Keeping them explicit prevents an initial
 * zero from being mistaken for "take zero already played" and lets each
 * network character own its own deterministic state.
 */
struct VoiceSelectionState final {
    std::uint32_t last_index{};
    bool has_last{};
    bool suppress_next{};
};

/**
 * Retail's local Character.update_spawn_sound latch.
 *
 * CreatePlayer arms one delayed cue for the new life.  The first life and a
 * class change force the authored spawn bank to play; respawning as the same
 * class retains the bank's 25/101 chance.  Keeping this separate from death
 * presentation prevents KillAction from inventing a mode-independent timer
 * sound. Ordinary respawn uses the non-spoken local beep1/beep2 sequence;
 * the only spoken countdown asset belongs to Zombie mode.
 */
struct LocalSpawnVoiceState final {
    double remaining{};
    std::uint8_t class_id{};
    std::uint8_t previous_class_id{};
    bool has_previous_class{};
    bool force{};
    bool armed{};
};

/** One ready-to-present local spawn cue produced after the retail delay. */
struct LocalSpawnVoiceEvent final {
    std::uint8_t class_id{};
    bool force{};
};

inline constexpr double spawn_voice_delay_minimum{1.0};
inline constexpr double spawn_voice_delay_maximum{2.0};

/** Map an injected random roll onto retail's inclusive 1-2 second delay. */
[[nodiscard]] double local_spawn_voice_delay(std::uint32_t roll) noexcept;

/** Arm a fresh local-life cue at the authoritative CreatePlayer boundary. */
void begin_local_spawn_voice(LocalSpawnVoiceState& state,
                             std::uint8_t class_id,
                             double delay) noexcept;

/** Advance the latch and return its one-shot cue when the delay expires. */
[[nodiscard]] std::optional<LocalSpawnVoiceEvent>
step_local_spawn_voice(LocalSpawnVoiceState& state, double dt) noexcept;

/** The idle line re-arms on a uniform interval in this range. */
inline constexpr double periodic_voice_minimum{3.0};
inline constexpr double periodic_voice_maximum{6.0};

/**
 * Chooses a take from a bank, or nothing.
 *
 * `roll` and `pick` are supplied by the caller so the whole decision stays a
 * pure function and can be unit tested without a random source. `roll` is
 * folded to retail's inclusive 0..100 range; `pick` indexes the bank.
 *
 * `last_index` carries the previously played take so the no-repeat flag can be
 * honoured, and is updated on a successful choice.
 */
[[nodiscard]] std::string_view choose_voice_line(const ClassVoiceBank& bank,
                                                 std::uint32_t roll,
                                                 std::uint32_t pick,
                                                 VoiceSelectionState& state) noexcept;

/**
 * Advances the idle-vocalisation timer.
 *
 * Returns true on the tick the line should play. `interval_roll` is 0..999 and
 * selects a re-arm delay across the uniform range, again so the caller owns the
 * randomness.
 */
[[nodiscard]] bool step_periodic_voice(PeriodicVoiceState& state, double dt,
                                       std::uint32_t interval_roll) noexcept;

} // namespace battlespades::world
