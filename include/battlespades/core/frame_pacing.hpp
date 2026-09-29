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

} // namespace battlespades::core
