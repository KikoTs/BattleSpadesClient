#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>

namespace battlespades::core {

/**
 * Schedules fixed simulation steps independently of expensive presentations.
 * Every scheduled step still pumps input and simulation. Only presentation is
 * skipped while catching up, and long loading/suspend stalls discard wall-clock
 * debt instead of accelerating the simulation for seconds after the stall.
 */
class FixedStepPacer final {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::int64_t maximum_catch_up_ticks{3};

    struct Step final {
        bool present{true};
        Clock::time_point next_tick{};
    };

    // Application validates the positive period before constructing a pacer.
    FixedStepPacer(Clock::time_point start, std::chrono::nanoseconds fixed_delta) noexcept
        : next_tick_{start}, fixed_delta_{fixed_delta} {}

    [[nodiscard]] Step step(Clock::time_point now) noexcept {
        const auto overdue_ticks = (now - next_tick_) / fixed_delta_;
        if (overdue_ticks > maximum_catch_up_ticks) {
            // Compare by division first so even a very large valid period does
            // not overflow merely calculating the maximum debt.
            next_tick_ = now - fixed_delta_ * maximum_catch_up_ticks;
        }
        const bool behind = now - next_tick_ >= fixed_delta_;
        const bool present = !behind || skipped_presentations_ == maximum_catch_up_ticks;
        skipped_presentations_ = present ? 0 : skipped_presentations_ + 1;
        next_tick_ += fixed_delta_;
        return {.present = present, .next_tick = next_tick_};
    }

private:
    Clock::time_point next_tick_;
    std::chrono::nanoseconds fixed_delta_;
    std::int64_t skipped_presentations_{};
};

/** One scheduled render-only frame between two fixed ticks. */
struct IntermediateFrameSlot final {
    FixedStepPacer::Clock::time_point at{};
};

/**
 * Schedules the next render-only frame after `previous_frame`, or nothing when
 * it would not finish comfortably before `next_tick`. A quarter period of slack
 * stays in front of the tick so interpolation can never delay the fixed step.
 */
[[nodiscard]] inline std::optional<IntermediateFrameSlot> next_intermediate_frame(
    FixedStepPacer::Clock::time_point previous_frame,
    FixedStepPacer::Clock::time_point now,
    FixedStepPacer::Clock::time_point next_tick,
    std::chrono::nanoseconds period) noexcept {
    if (period <= std::chrono::nanoseconds::zero()) {
        return std::nullopt;
    }
    const auto at = std::max(previous_frame + period, now);
    if (at + period / 4 >= next_tick) {
        return std::nullopt;
    }
    return IntermediateFrameSlot{at};
}

/** Fraction of one fixed step elapsed since `tick_time`, clamped to [0, 1). */
[[nodiscard]] inline double intermediate_frame_alpha(FixedStepPacer::Clock::time_point tick_time,
                                                     FixedStepPacer::Clock::time_point now,
                                                     std::chrono::nanoseconds fixed_delta) noexcept {
    if (fixed_delta <= std::chrono::nanoseconds::zero() || now <= tick_time) {
        return 0.0;
    }
    const double alpha = std::chrono::duration<double>(now - tick_time).count() /
                         std::chrono::duration<double>(fixed_delta).count();
    return alpha < 0.999 ? alpha : 0.999;
}

/**
 * How much of a wait is slept and how much is spun, from measured oversleep.
 *
 * An operating-system sleep wakes late by an amount that depends on the timer
 * the platform gives us (about a millisecond on a stock Windows timer, far less
 * on a high-resolution one). Sleeping the whole wait therefore presents frames
 * late and unevenly; spinning the whole wait burns a core. The budget sleeps
 * until `margin` before the deadline and spins the rest, with `margin` tracking
 * the oversleep actually observed so a good timer costs almost no spinning.
 */
class SleepBudget final {
public:
    static constexpr std::chrono::nanoseconds minimum_margin{100'000};
    static constexpr std::chrono::nanoseconds maximum_margin{2'000'000};

    [[nodiscard]] constexpr std::chrono::nanoseconds margin() const noexcept {
        return margin_;
    }

    /** Records one sleep that was asked to end at `wanted` and ended at `woke`. */
    constexpr void record(FixedStepPacer::Clock::time_point wanted,
                          FixedStepPacer::Clock::time_point woke) noexcept {
        const auto late = woke > wanted ? woke - wanted : std::chrono::nanoseconds::zero();
        // Rise at once to a worse oversleep, decay slowly towards a better one:
        // one late frame is visible, a little extra spinning is not.
        const auto target = late + late / 2 + minimum_margin;
        margin_ = target > margin_ ? target : margin_ - (margin_ - target) / 16;
        margin_ = std::clamp(margin_, minimum_margin, maximum_margin);
    }

private:
    std::chrono::nanoseconds margin_{1'000'000};
};

/**
 * Blocks until `target` and returns within tens of microseconds of it.
 *
 * Main-thread pacing only. Sleeps on the platform's best timer until a measured
 * margin before the deadline, then spins. A deadline already in the past
 * returns immediately.
 */
void precise_sleep_until(FixedStepPacer::Clock::time_point target) noexcept;

/**
 * Spaces a tick's frames evenly across the fixed step.
 *
 * The tick presents one frame itself; the pacer places the render-only frames
 * after it so that every frame-to-frame interval is the same, the next tick's
 * frame included. Scheduling them a display period apart, as before, left a
 * short or a long interval in front of every tick (6.9 / 9.7 / 6.9 / 9.7 ms on
 * a 144 Hz display), which reads as judder however smooth the motion is.
 *
 * The frame count is the smallest that reaches the requested period, reduced
 * while the tick's own work or one render-only frame would not fit in a slot:
 * a frame must never delay the fixed step.
 */
class IntermediateFramePacer final {
public:
    using Clock = FixedStepPacer::Clock;
    static constexpr std::uint32_t maximum_frames_per_tick{8U};

    struct Plan final {
        /** Frames per fixed step, the tick's own frame included; 1 adds none. */
        std::uint32_t frames_per_tick{1U};
        std::chrono::nanoseconds spacing{};
    };

    /** Work the last paced tick needed, from its scheduled time to its frame. */
    constexpr void record_tick_work(std::chrono::nanoseconds work) noexcept {
        tick_work_ = smooth(tick_work_, work);
    }

    /** Wall time one render-only frame needed. */
    constexpr void record_frame_work(std::chrono::nanoseconds work) noexcept {
        frame_work_ = smooth(frame_work_, work);
    }

    [[nodiscard]] constexpr std::chrono::nanoseconds frame_work() const noexcept {
        return frame_work_;
    }

    [[nodiscard]] constexpr Plan plan(std::chrono::nanoseconds fixed_delta,
                                      std::chrono::nanoseconds period) const noexcept {
        if (fixed_delta <= std::chrono::nanoseconds::zero() ||
            period <= std::chrono::nanoseconds::zero() || period >= fixed_delta) {
            return {1U, fixed_delta};
        }
        // Smallest count whose spacing is no longer than the period, with a
        // tenth of a frame of tolerance so 120 Hz asks for two frames, not
        // three, whatever the rounding of either period.
        const auto scaled = fixed_delta.count() * 20 / period.count();
        auto frames = static_cast<std::uint32_t>(
            std::clamp<std::int64_t>((scaled + 18) / 20, 1, maximum_frames_per_tick));
        const auto busiest = std::max(tick_work_, frame_work_);
        while (frames > 1U && busiest + slack(fixed_delta / frames) > fixed_delta / frames) {
            --frames;
        }
        return {frames, fixed_delta / frames};
    }

    /**
     * When render-only frame `index` (1 .. frames_per_tick - 1) should start
     * so that it is on screen `index` slots after the tick's frame, or nothing
     * when it can no longer finish comfortably before `next_tick`.
     */
    [[nodiscard]] constexpr std::optional<Clock::time_point>
    start_of(std::uint32_t index, const Plan& plan, Clock::time_point tick_presented,
             Clock::time_point now, Clock::time_point next_tick) const noexcept {
        if (index == 0U || index >= plan.frames_per_tick) {
            return std::nullopt;
        }
        const auto on_screen = tick_presented + plan.spacing * index;
        const auto start = std::max(on_screen - frame_work_, now);
        if (start + frame_work_ + slack(plan.spacing) >= next_tick) {
            return std::nullopt;
        }
        return start;
    }

private:
    [[nodiscard]] static constexpr std::chrono::nanoseconds
    slack(std::chrono::nanoseconds spacing) noexcept {
        return spacing / 8;
    }

    [[nodiscard]] static constexpr std::chrono::nanoseconds
    smooth(std::chrono::nanoseconds estimate, std::chrono::nanoseconds sample) noexcept {
        sample = std::clamp(sample, std::chrono::nanoseconds::zero(),
                            std::chrono::nanoseconds{100'000'000});
        // Follow a slower frame at once, a faster one gradually: dropping a
        // frame late is a stutter, keeping one slot free a little longer is not.
        return sample > estimate ? sample : estimate - (estimate - sample) / 8;
    }

    std::chrono::nanoseconds tick_work_{};
    std::chrono::nanoseconds frame_work_{};
};

} // namespace battlespades::core
