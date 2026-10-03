#pragma once

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace battlespades::frontend {

/**
 * Render-only camera smoothing between fixed 60 Hz ticks.
 *
 * The simulation, input and ClientData cadence stay at the retail 60 Hz. On a
 * high-refresh display the frontend presents extra frames between ticks and
 * draws the camera eye `alpha` of the way from the previous tick's eye to the
 * current one (a one-tick presentation delay for position only; yaw/pitch
 * always use the latest mouse input, see LiveLookFollow). Jumps longer than `snap_distance`
 * (respawn, teleport, camera-mode switch) are never smoothed.
 */
class CameraEyeInterpolator final {
public:
    static constexpr double snap_distance{4.0};

    /** Records the eye computed from tick `tick`'s state (tick frames only). */
    void record(std::array<double, 3U> eye, std::uint64_t tick) noexcept {
        if (!valid_) {
            previous_ = eye;
            current_ = eye;
            tick_ = tick;
            valid_ = true;
            return;
        }
        if (tick != tick_) {
            previous_ = current_;
            tick_ = tick;
        }
        current_ = eye;
        const double dx = current_[0U] - previous_[0U];
        const double dy = current_[1U] - previous_[1U];
        const double dz = current_[2U] - previous_[2U];
        if (!(dx * dx + dy * dy + dz * dz <= snap_distance * snap_distance)) {
            previous_ = current_;
        }
    }

    /** Eye `alpha` (clamped to [0, 1]) of the way from the previous tick. */
    [[nodiscard]] std::array<double, 3U> sample(double alpha) const noexcept {
        const double t = std::isfinite(alpha) ? (alpha < 0.0 ? 0.0 : (alpha > 1.0 ? 1.0 : alpha))
                                              : 1.0;
        return {previous_[0U] + (current_[0U] - previous_[0U]) * t,
                previous_[1U] + (current_[1U] - previous_[1U]) * t,
                previous_[2U] + (current_[2U] - previous_[2U]) * t};
    }

    [[nodiscard]] bool valid() const noexcept {
        return valid_;
    }

    void reset() noexcept {
        valid_ = false;
    }

private:
    std::array<double, 3U> previous_{};
    std::array<double, 3U> current_{};
    std::uint64_t tick_{};
    bool valid_{false};
};

/**
 * How a render-only frame orients the camera.
 *
 * Mouse look is consumed between ticks (WindowPort::take_leading_mouse_motion)
 * and accumulates in the session's yaw/pitch, which the next tick turns into
 * the networked orientation exactly as before. A render-only frame of a
 * first-person view must therefore draw the camera at the LATEST look angles,
 * not at the angles the last tick frame was drawn with: re-using the tick
 * frame's camera turned the view in 60 Hz steps on a 144 Hz display, three
 * identical orientations and then a jump, which is the judder players see
 * when they move the mouse quickly.
 *
 * Views that do not follow the player's look (death camera, match results,
 * construct placement) keep the tick frame's orientation.
 */
struct LiveLookFollow final {
    bool follows{false};
    /** Added to the live angles (a jetpack corpse spins the dead view). */
    double yaw_offset_degrees{};
    double pitch_offset_degrees{};
    double pitch_limit_degrees{90.0};

    /** {yaw, pitch} for a render-only frame; the tick frame's angles when not following. */
    [[nodiscard]] constexpr std::array<double, 2U> orient(double live_yaw, double live_pitch,
                                                          double tick_yaw,
                                                          double tick_pitch) const noexcept {
        if (!follows) {
            return {tick_yaw, tick_pitch};
        }
        const double pitch = live_pitch + pitch_offset_degrees;
        return {live_yaw + yaw_offset_degrees,
                pitch < -pitch_limit_degrees
                    ? -pitch_limit_degrees
                    : (pitch > pitch_limit_degrees ? pitch_limit_degrees : pitch)};
    }
};

/** Displays at or below this refresh rate keep exactly one frame per tick. */
inline constexpr std::uint32_t render_interpolation_minimum_refresh_millihertz{75'000U};
/** Intermediate frames are never scheduled faster than this. */
inline constexpr std::chrono::nanoseconds render_interpolation_minimum_period{2'000'000};

/**
 * Period of render-only frames for a display, or zero when interpolation is
 * disabled, the display is (near) 60 Hz, or the refresh rate is unknown.
 */
[[nodiscard]] constexpr std::chrono::nanoseconds render_interpolation_period(
    bool enabled, std::uint32_t refresh_millihertz) noexcept {
    if (!enabled || refresh_millihertz < render_interpolation_minimum_refresh_millihertz) {
        return std::chrono::nanoseconds::zero();
    }
    const std::chrono::nanoseconds period{1'000'000'000'000LL / refresh_millihertz};
    return period < render_interpolation_minimum_period ? render_interpolation_minimum_period
                                                        : period;
}

/**
 * The same, as the period handed to the frame pacer, which spaces frames
 * evenly across the fixed step.
 *
 * Without VSync the display period is passed through and the pacer rounds the
 * frame count up, so every refresh has a new frame (144 Hz: three frames a
 * tick). With VSync a frame that arrives faster than the display blocks in
 * present and would hold up the fixed step, so the count is rounded down to
 * what the display can take (144 Hz: two frames a tick) and zero is returned
 * when that leaves only the tick's own frame.
 */
[[nodiscard]] constexpr std::chrono::nanoseconds render_interpolation_paced_period(
    bool enabled, std::uint32_t refresh_millihertz, bool vertical_sync,
    std::chrono::nanoseconds fixed_delta) noexcept {
    const auto period = render_interpolation_period(enabled, refresh_millihertz);
    if (!vertical_sync || period <= std::chrono::nanoseconds::zero() ||
        fixed_delta <= std::chrono::nanoseconds::zero()) {
        return period;
    }
    // A tenth of a frame of tolerance: 120 Hz is two frames however the two
    // periods were rounded.
    const auto frames = (fixed_delta.count() * 10 / period.count() + 1) / 10;
    if (frames <= 1) {
        return std::chrono::nanoseconds::zero();
    }
    return fixed_delta / frames;
}

} // namespace battlespades::frontend
