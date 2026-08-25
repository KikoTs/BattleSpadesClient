#pragma once

#include <cstdint>

namespace battlespades::world {

/**
 * What a weapon plays when it fires.
 *
 * Retail has exactly three shapes and picks between them per weapon family, not
 * per shot. Getting the shape wrong is what makes a single trigger pull sound
 * like several: a per-burst recording played per round stacks, and a one-shot
 * grain held open as a loop repeats.
 */
enum class FireSoundShape : std::uint8_t {
    /** Follow-up rounds of a burst. Retail plays nothing for these. */
    silent,
    /** One sample per round. */
    one_shot_per_round,
    /** One infinite loop held while the trigger is live, closed on a grid. */
    sustained_loop,
};

/** Everything the shape decision depends on. */
struct FireSoundInput final {
    std::uint8_t tool_id{};
    /** The audio backend actually has a fire-loop group bound for this tool. */
    bool has_loop_cue{};
    bool burst_follow_up{};
    /** Retail `can_shoot_primary() && can_fire()`. */
    bool trigger_live{};
    /** Mounted machine gun only. */
    bool deployed{};
    /** Minigun only; barrel speed normalised to 0..1. */
    double spin_fraction{};
};

/**
 * Picks the retail shape for one shot.
 *
 * Pure so it can be tested without an audio device: the frontend's job is then
 * only to execute the decision.
 */
[[nodiscard]] FireSoundShape fire_sound_shape(const FireSoundInput& input) noexcept;

/**
 * The interval a sustained loop is quantised to, in seconds. 0 when the weapon
 * has no loop.
 *
 * This is NOT always the weapon's fire interval, and assuming so is the bug this
 * exists to prevent. The minigun's own interval ramps 0.3 -> 0.1 with barrel
 * speed, so retail quantises its fire loop to a fixed constant instead; and the
 * mounted gun's catalogued interval is its UNDEPLOYED one, five times the
 * deployed value its loop actually runs at.
 */
[[nodiscard]] double fire_loop_quantum(std::uint8_t tool_id, bool deployed) noexcept;

/**
 * Pure timing state for one sustained weapon loop.
 *
 * Audio ownership stays in the frontend, but keeping cadence here makes the
 * death/unset edge testable without an OpenAL device. `cancel()` is deliberately
 * immediate: retail weapon `on_unset()` closes its looping source without a
 * tail, rather than waiting for the next firing quantum.
 */
class FireLoopCadence final {
public:
    /** Opens a new loop clock at the authored grain interval. */
    void open(double quantum) noexcept;
    /** Marks a confirmed shot while an already-open loop remains selected. */
    void refresh() noexcept;
    /**
     * Advances the quantised close edge.
     *
     * Returns true exactly once when a released trigger reaches its next grain
     * boundary. Invalid/non-positive time is ignored.
     */
    [[nodiscard]] bool advance(double dt, bool trigger_live) noexcept;
    /** Cancels immediately and clears all previous-life timing state. */
    void cancel() noexcept;

    [[nodiscard]] bool firing() const noexcept { return firing_; }
    [[nodiscard]] double elapsed() const noexcept { return elapsed_; }
    [[nodiscard]] double quantum() const noexcept { return quantum_; }

private:
    bool firing_{};
    double elapsed_{};
    double quantum_{};
};

} // namespace battlespades::world
