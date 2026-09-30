#include "battlespades/core/frame_pacing.hpp"

#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace battlespades::core {
namespace {

using Clock = FixedStepPacer::Clock;

#if defined(_WIN32)
#if !defined(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

/**
 * One waitable timer per thread that paces. The high-resolution kind (Windows
 * 10 1803 and later) is independent of the process timer period, which a plain
 * Sleep() is not: measured here, a 1 ms Sleep woke 15 ms late before SDL raised
 * the period and still about 1 ms late after it.
 */
class PacingTimer final {
public:
    PacingTimer() noexcept
        : handle_{CreateWaitableTimerExW(nullptr, nullptr,
                                         CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                         TIMER_ALL_ACCESS)} {
        if (handle_ == nullptr) {
            // Older Windows: an ordinary timer, still better than Sleep()'s
            // whole-millisecond rounding.
            handle_ = CreateWaitableTimerExW(nullptr, nullptr, 0U, TIMER_ALL_ACCESS);
        }
    }

    ~PacingTimer() {
        if (handle_ != nullptr) {
            CloseHandle(handle_);
        }
    }

    PacingTimer(const PacingTimer&) = delete;
    PacingTimer& operator=(const PacingTimer&) = delete;
    PacingTimer(PacingTimer&&) = delete;
    PacingTimer& operator=(PacingTimer&&) = delete;

    /** False when no timer could wait; the caller then falls back. */
    [[nodiscard]] bool wait(std::chrono::nanoseconds duration) const noexcept {
        if (handle_ == nullptr) {
            return false;
        }
        LARGE_INTEGER due{};
        // Negative: relative, in 100 ns units.
        due.QuadPart = -(duration.count() / 100);
        if (due.QuadPart >= 0) {
            return true;
        }
        if (SetWaitableTimerEx(handle_, &due, 0, nullptr, nullptr, nullptr, 0U) == FALSE) {
            return false;
        }
        return WaitForSingleObject(handle_, INFINITE) == WAIT_OBJECT_0;
    }

private:
    HANDLE handle_{};
};

void relax() noexcept {
    YieldProcessor();
}
#else
void relax() noexcept {
    std::this_thread::yield();
}
#endif

} // namespace

void precise_sleep_until(Clock::time_point target) noexcept {
    thread_local SleepBudget budget;
#if defined(_WIN32)
    thread_local const PacingTimer timer;
#endif

    auto now = Clock::now();
    if (now >= target) {
        return;
    }
    if (const auto margin = budget.margin(); target - now > margin) {
        const auto wanted = target - margin;
#if defined(_WIN32)
        if (!timer.wait(wanted - now)) {
            std::this_thread::sleep_until(wanted);
        }
#else
        std::this_thread::sleep_until(wanted);
#endif
        now = Clock::now();
        budget.record(wanted, now);
    }
    while (now < target) {
        relax();
        now = Clock::now();
    }
}

} // namespace battlespades::core
