#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

namespace battlespades::frontend {

/**
 * The Graphics tab's frame-rate counter.
 *
 * Every presented frame (tick frames and render-only frames alike) is
 * recorded. Twice a second the counter publishes the frame rate, the mean
 * frame time and the slowest frame of the window: a stutter shows up in the
 * last number long before it moves the average.
 */
class FrameRateMeter final {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::chrono::milliseconds window{500};

    struct Reading final {
        double frames_per_second{};
        double mean_frame_ms{};
        double worst_frame_ms{};
    };

    void record(Clock::time_point presented) noexcept {
        if (previous_.has_value()) {
            const auto interval = std::chrono::duration<double, std::milli>(presented - *previous_);
            if (interval.count() >= 0.0 && interval.count() < 1000.0) {
                ++frames_;
                total_ms_ += interval.count();
                worst_ms_ = std::max(worst_ms_, interval.count());
            } else {
                // A suspend or a long load is not a frame time.
                reset_window(presented);
            }
        }
        previous_ = presented;
        if (!window_start_.has_value()) {
            window_start_ = presented;
        }
        if (presented - *window_start_ >= window && frames_ > 0U) {
            reading_ = Reading{1000.0 * static_cast<double>(frames_) / total_ms_,
                               total_ms_ / static_cast<double>(frames_), worst_ms_};
            reset_window(presented);
        }
    }

    [[nodiscard]] const std::optional<Reading>& reading() const noexcept {
        return reading_;
    }

    void reset() noexcept {
        previous_.reset();
        window_start_.reset();
        reading_.reset();
        frames_ = 0U;
        total_ms_ = 0.0;
        worst_ms_ = 0.0;
    }

    /** "144 FPS  6.9 MS  MAX 8.1" */
    [[nodiscard]] static std::string text(const Reading& reading) {
        char buffer[64]{};
        std::snprintf(buffer, sizeof(buffer), "%.0f FPS  %.1f MS  MAX %.1f",
                      reading.frames_per_second, reading.mean_frame_ms, reading.worst_frame_ms);
        return buffer;
    }

private:
    void reset_window(Clock::time_point start) noexcept {
        window_start_ = start;
        frames_ = 0U;
        total_ms_ = 0.0;
        worst_ms_ = 0.0;
    }

    std::optional<Clock::time_point> previous_{};
    std::optional<Clock::time_point> window_start_{};
    std::optional<Reading> reading_{};
    std::uint32_t frames_{};
    double total_ms_{};
    double worst_ms_{};
};

} // namespace battlespades::frontend
