// Mouse-look smoothness harness.
//
// Players reported that turning quickly "doesn't look smooth, like some jittery
// effect". The cause: render-only frames between the fixed 60 Hz ticks re-used
// the tick frame's camera orientation, and mouse input was only read at the
// tick. On a 144 Hz display the view therefore turned in 60 Hz steps: three
// frames at one angle, then a jump.
//
// This drives a simulated clock through the same scheduling the client uses
// (core::IntermediateFramePacer, render_interpolation_paced_period, the
// intermediate_frame_alpha cut-off) with a 1 kHz mouse moving at constant
// speed and realistic tick/frame work and wake-up jitter, feeds every mouse
// count through the real TutorialWorldSession::apply_look_delta, and records
// the yaw each presented frame shows. It compares:
//
//   before: input drained at the tick only; render-only frames draw the tick
//           frame's yaw (the old interpolation_scene.camera);
//   after:  render-only frames first take the queued mouse motion
//           (WindowPort::take_leading_mouse_motion) and draw the live yaw
//           (LiveLookFollow).
//
// Smoothness is measured against the ideal for constant angular velocity: the
// angle on screen should be a straight line in presentation time. Reported:
//   step error  RMS of (frame-to-frame yaw step - ideal step), % of mean step
//   stalls      share of frames that showed no rotation at all
//
// It also proves the network contract is untouched: a session fed the same
// events split across render-only frames produces bit-identical orientation
// at every tick to one fed everything at the tick.

#include "battlespades/core/frame_pacing.hpp"
#include "battlespades/frontend/render_interpolation.hpp"
#include "battlespades/world/tutorial_session.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace std::chrono_literals;
using battlespades::core::IntermediateFramePacer;
using battlespades::world::TutorialWorldSession;
using battlespades::world::VxlMap;
using Clock = IntermediateFramePacer::Clock;

int failures{};

void expect(bool value, const std::string& message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message.c_str());
        ++failures;
    }
}

/** Deterministic across standard libraries (std distributions are not). */
class Lcg final {
public:
    explicit Lcg(std::uint64_t seed) : state_{seed} {}
    double uniform() noexcept {
        state_ = state_ * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>(state_ >> 11U) / 9007199254740992.0;
    }
    double between(double low, double high) noexcept { return low + (high - low) * uniform(); }

private:
    std::uint64_t state_;
};

std::shared_ptr<VxlMap> flat_map() {
    std::vector<std::byte> bytes;
    bytes.reserve(static_cast<std::size_t>(VxlMap::width) * VxlMap::depth * 4U);
    for (std::size_t column{}; column < static_cast<std::size_t>(VxlMap::width) * VxlMap::depth;
         ++column) {
        bytes.push_back(std::byte{0U});
        bytes.push_back(std::byte{1U});
        bytes.push_back(std::byte{0U});
        bytes.push_back(std::byte{0U});
    }
    auto loaded = VxlMap::load(bytes);
    if (!loaded) {
        throw std::runtime_error{"synthetic world must parse"};
    }
    return std::make_shared<VxlMap>(std::move(*loaded.map));
}

struct Scenario final {
    const char* name{};
    double refresh_hz{};
    bool vsync{};
};

struct Result final {
    double step_error_percent{};
    double stall_percent{};
    double frames_per_second{};
    bool orientation_identical{true};
};

struct MouseEvent final {
    double at_ms{};
    float dx{};
};

double unwrap(double delta) {
    while (delta > 180.0) delta -= 360.0;
    while (delta < -180.0) delta += 360.0;
    return delta;
}

/**
 * One run of the client loop on a simulated clock. `live_look` selects the
 * fixed behaviour; the shadow session always takes input at ticks only and is
 * the reference for the network orientation.
 */
Result run(const Scenario& scenario, bool live_look, const std::shared_ptr<VxlMap>& map) {
    constexpr double fixed_ms{1000.0 / 60.0};
    constexpr double duration_ms{6'000.0};
    constexpr float counts_per_event{5.0F}; // 1 kHz mouse: 5 counts/ms = 500 deg/s at 0.1
    const auto fixed_delta = std::chrono::nanoseconds{16'666'667};
    const auto to_tp = [](double ms) {
        return Clock::time_point{std::chrono::nanoseconds{static_cast<std::int64_t>(ms * 1e6)}};
    };
    const auto to_ms = [](Clock::time_point tp) {
        return static_cast<double>(tp.time_since_epoch().count()) / 1e6;
    };

    TutorialWorldSession session{map};
    TutorialWorldSession reference{map};
    std::deque<MouseEvent> queue;
    std::deque<MouseEvent> reference_queue;
    for (double at{0.5}; at < duration_ms + 50.0; at += 1.0) {
        queue.push_back({at, counts_per_event});
        reference_queue.push_back({at, counts_per_event});
    }
    const auto drain = [](std::deque<MouseEvent>& events, TutorialWorldSession& target,
                          double now) {
        while (!events.empty() && events.front().at_ms <= now) {
            target.apply_look_delta(events.front().dx, 0.0);
            events.pop_front();
        }
    };

    Lcg random{0x5EEDULL ^ static_cast<std::uint64_t>(scenario.refresh_hz * 10.0) ^
               (scenario.vsync ? 0xABCDULL : 0ULL)};
    const double vblank_ms = 1000.0 / scenario.refresh_hz;
    const double vblank_phase = random.between(0.0, vblank_ms);
    double last_present{-1e9};
    const auto present_at = [&](double submitted) {
        if (!scenario.vsync) {
            last_present = submitted;
            return submitted;
        }
        // FIFO swap chain: the first vblank after submission that the
        // previous frame has not taken.
        double slot = std::ceil((submitted - vblank_phase) / vblank_ms) * vblank_ms + vblank_phase;
        while (slot <= last_present + vblank_ms * 0.5) slot += vblank_ms;
        last_present = slot;
        return slot;
    };

    const auto refresh_mhz = static_cast<std::uint32_t>(std::lround(scenario.refresh_hz * 1000.0));
    const auto period = battlespades::frontend::render_interpolation_paced_period(
        true, refresh_mhz, scenario.vsync, fixed_delta);

    struct Shown final {
        double present_ms;
        double yaw;
    };
    std::vector<Shown> shown;
    IntermediateFramePacer pacer;
    Result result;

    for (std::uint64_t tick{}; static_cast<double>(tick) * fixed_ms < duration_ms; ++tick) {
        const double tick_time = static_cast<double>(tick) * fixed_ms;
        const double next_tick = tick_time + fixed_ms;
        // precise_sleep_until wakes within tens of microseconds.
        const double woke = tick_time + random.between(0.0, 0.08);
        drain(queue, session, woke);
        drain(reference_queue, reference, woke);
        session.tick();
        reference.tick();
        const auto& mine = session.player().orientation;
        const auto& theirs = reference.player().orientation;
        if (mine.x != theirs.x || mine.y != theirs.y || mine.z != theirs.z) {
            result.orientation_identical = false;
        }
        // Simulation work, then the tick frame. After the fix the frame first
        // takes the motion queued during the simulation (render_frame).
        const double render_start = woke + random.between(0.7, 2.0);
        if (live_look) {
            drain(queue, session, render_start);
        }
        const double tick_yaw = session.yaw();
        const double tick_submitted = render_start + random.between(0.8, 1.6);
        shown.push_back({present_at(tick_submitted), tick_yaw});

        if (period <= std::chrono::nanoseconds::zero()) {
            continue;
        }
        pacer.record_tick_work(to_tp(tick_submitted) - to_tp(tick_time));
        const auto plan = pacer.plan(fixed_delta, period);
        double now = tick_submitted;
        for (std::uint32_t frame{1U}; frame < plan.frames_per_tick; ++frame) {
            const auto start = pacer.start_of(frame, plan, to_tp(tick_submitted), to_tp(now),
                                              to_tp(next_tick));
            if (!start.has_value()) {
                break;
            }
            const double frame_woke = to_ms(*start) + random.between(0.0, 0.08);
            const auto spacing_ms = static_cast<double>(plan.spacing.count()) / 1e6;
            if (frame_woke + static_cast<double>(pacer.frame_work().count()) / 1e6 +
                    spacing_ms / 8.0 >=
                next_tick) {
                break;
            }
            double yaw = tick_yaw;
            if (live_look) {
                drain(queue, session, frame_woke);
                battlespades::frontend::LiveLookFollow follow{};
                follow.follows = true;
                yaw = follow.orient(session.yaw(), session.pitch(), tick_yaw, session.pitch())[0U];
            }
            const double work = random.between(0.8, 1.6);
            now = frame_woke + work;
            pacer.record_frame_work(to_tp(now) - to_tp(frame_woke));
            shown.push_back({present_at(now), yaw});
        }
    }

    // Skip the first half second while the pacer's work estimates settle.
    std::vector<Shown> steady;
    for (const auto& frame : shown) {
        if (frame.present_ms > 500.0) steady.push_back(frame);
    }
    std::sort(steady.begin(), steady.end(),
              [](const Shown& a, const Shown& b) { return a.present_ms < b.present_ms; });
    const double degrees_per_ms = counts_per_event * 0.1;
    double squared{};
    double total_step{};
    std::size_t stalls{};
    for (std::size_t index{1U}; index < steady.size(); ++index) {
        const double step = unwrap(steady[index].yaw - steady[index - 1U].yaw);
        const double ideal =
            degrees_per_ms * (steady[index].present_ms - steady[index - 1U].present_ms);
        squared += (step - ideal) * (step - ideal);
        total_step += step;
        if (std::fabs(step) < 1e-9) ++stalls;
    }
    const auto steps = static_cast<double>(steady.size() - 1U);
    const double mean_step = total_step / steps;
    result.step_error_percent = 100.0 * std::sqrt(squared / steps) / mean_step;
    result.stall_percent = 100.0 * static_cast<double>(stalls) / steps;
    result.frames_per_second =
        1000.0 * steps / (steady.back().present_ms - steady.front().present_ms);
    return result;
}

void live_look_follow_contract() {
    battlespades::frontend::LiveLookFollow still{};
    auto angles = still.orient(30.0, 10.0, 12.0, 4.0);
    expect(angles[0U] == 12.0 && angles[1U] == 4.0,
           "a view that does not follow the look keeps the tick frame's angles");
    battlespades::frontend::LiveLookFollow first_person{};
    first_person.follows = true;
    angles = first_person.orient(30.0, 10.0, 12.0, 4.0);
    expect(angles[0U] == 30.0 && angles[1U] == 10.0,
           "a first-person render-only frame draws the latest look angles");
    battlespades::frontend::LiveLookFollow corpse{true, 15.0, 85.0, 89.0};
    angles = corpse.orient(30.0, 10.0, 0.0, 0.0);
    expect(angles[0U] == 45.0 && angles[1U] == 89.0,
           "a spinning jetpack corpse keeps its offset and the pitch clamp");
}

} // namespace

int main() {
    try {
        live_look_follow_contract();
        const auto map = flat_map();
        const Scenario scenarios[]{
            {"60 Hz, VSync off", 60.0, false},   {"60 Hz, VSync on", 60.0, true},
            {"144 Hz, VSync off", 144.0, false}, {"144 Hz, VSync on", 144.0, true},
            {"240 Hz, VSync off", 240.0, false}, {"240 Hz, VSync on", 240.0, true},
        };
        std::printf("%-18s | %8s | %-22s | %-22s\n", "display", "fps", "before: step err / stalls",
                    "after: step err / stalls");
        for (const auto& scenario : scenarios) {
            const auto before = run(scenario, false, map);
            const auto after = run(scenario, true, map);
            std::printf("%-18s | %8.1f | %8.1f %% / %6.1f %%  | %8.1f %% / %6.1f %%\n",
                        scenario.name, after.frames_per_second, before.step_error_percent,
                        before.stall_percent, after.step_error_percent, after.stall_percent);
            expect(after.orientation_identical && before.orientation_identical,
                   std::string{scenario.name} +
                       ": the networked orientation must be bit-identical to tick-only input");
            const bool render_only_frames = after.frames_per_second > 70.0;
            if (render_only_frames) {
                expect(before.stall_percent > 25.0,
                       std::string{scenario.name} +
                           ": the harness must reproduce the reported judder before the fix");
                expect(after.stall_percent == 0.0,
                       std::string{scenario.name} + ": every presented frame must turn the view");
                expect(after.step_error_percent < before.step_error_percent / 2.5,
                       std::string{scenario.name} + ": live look must cut the step error 2.5x");
            }
            if (!scenario.vsync) {
                // Without VSync a frame is on screen when it is submitted, so
                // what is left is the variation of each frame's own work time.
                expect(after.step_error_percent < 30.0,
                       std::string{scenario.name} + ": residual look jitter must stay small");
            }
        }
        if (failures != 0) {
            std::fprintf(stderr, "look smoothness: %d failure(s)\n", failures);
            return 1;
        }
        std::printf("look smoothness: live look removes the 60 Hz camera stepping\n");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "look smoothness failed: %s\n", error.what());
        return 1;
    }
}
