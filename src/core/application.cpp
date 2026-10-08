#include "battlespades/core/application.hpp"
#include "battlespades/core/frame_pacing.hpp"

#include <chrono>
#include <cstddef>
#include <exception>
#include <iostream>
#include <thread>
#include <utility>

namespace battlespades::core {

Application::Application(RuntimeConfig config) : config_{std::move(config)} {}

bool Application::add_module(std::unique_ptr<RuntimeModule> module) {
    if (state_ != State::accepting_modules || module == nullptr) {
        return false;
    }

    modules_.push_back(std::move(module));
    return true;
}

RunResult Application::run() {
    if (state_ != State::accepting_modules) {
        return RunResult::invalid_state;
    }
    if (config_.fixed_delta <= std::chrono::nanoseconds::zero()) {
        state_ = State::stopped;
        return RunResult::invalid_configuration;
    }

    state_ = State::running;
    std::size_t started_modules{};
    RunResult result{RunResult::success};

    try {
        for (auto& module : modules_) {
            if (!module->start()) {
                result = RunResult::module_start_failed;
                break;
            }
            ++started_modules;
        }

        FixedStepPacer pacer{FixedStepPacer::Clock::now(), config_.fixed_delta};
        IntermediateFramePacer frame_pacer;
        std::chrono::nanoseconds elapsed{};
        std::uint64_t tick_index{};
        bool stop_requested{false};

        while (result == RunResult::success && !stop_requested &&
               (!config_.tick_limit.has_value() || tick_index < *config_.tick_limit)) {
            const auto pacing = config_.pace_to_wall_clock
                                    ? pacer.step(FixedStepPacer::Clock::now())
                                    : FixedStepPacer::Step{};
            const TickContext context{
                .index = tick_index,
                .fixed_delta = config_.fixed_delta,
                .elapsed = elapsed,
                .present = pacing.present,
                .paced = config_.pace_to_wall_clock,
                .scheduled_at = config_.pace_to_wall_clock
                                    ? pacing.next_tick - config_.fixed_delta
                                    : FixedStepPacer::Clock::time_point{},
            };

            for (auto& module : modules_) {
                if (module->tick(context) == TickDecision::stop) {
                    stop_requested = true;
                    break;
                }
            }

            ++tick_index;
            elapsed += config_.fixed_delta;

            bool render_only_frames{false};
            // Time the loop would sleep is offered to the modules first, less
            // a margin so their work can never make the next frame late.
            const auto offer_idle = [this](FixedStepPacer::Clock::time_point wake) {
                constexpr std::chrono::nanoseconds margin{500'000};
                constexpr std::chrono::nanoseconds worthwhile{1'000'000};
                const auto deadline = wake - margin;
                if (deadline - FixedStepPacer::Clock::now() < worthwhile) {
                    return;
                }
                for (auto& module : modules_) {
                    module->idle(deadline);
                }
            };
            if (config_.pace_to_wall_clock && !stop_requested && pacing.present) {
                // Optional render-only frames for high-refresh displays. They
                // never poll input or advance simulation, and scheduling keeps
                // slack before the next tick so the 60 Hz cadence is intact.
                auto period = std::chrono::nanoseconds::zero();
                for (const auto& module : modules_) {
                    const auto requested = module->intermediate_frame_period();
                    if (requested > std::chrono::nanoseconds::zero() &&
                        (period == std::chrono::nanoseconds::zero() || requested < period)) {
                        period = requested;
                    }
                }
                render_only_frames = period > std::chrono::nanoseconds::zero();
                if (render_only_frames) {
                    const auto tick_time = pacing.next_tick - config_.fixed_delta;
                    const auto tick_presented = FixedStepPacer::Clock::now();
                    frame_pacer.record_tick_work(tick_presented - tick_time);
                    const auto plan = frame_pacer.plan(config_.fixed_delta, period);
                    for (std::uint32_t frame = 1U;
                         !stop_requested && frame < plan.frames_per_tick; ++frame) {
                        const auto start = frame_pacer.start_of(
                            frame, plan, tick_presented, FixedStepPacer::Clock::now(),
                            pacing.next_tick);
                        if (!start.has_value()) {
                            break;
                        }
                        offer_idle(*start);
                        precise_sleep_until(*start);
                        const auto woke = FixedStepPacer::Clock::now();
                        if (woke + frame_pacer.frame_work() + plan.spacing / 8 >=
                            pacing.next_tick) {
                            // Woken late; the tick has priority.
                            break;
                        }
                        const double alpha =
                            intermediate_frame_alpha(tick_time, woke, config_.fixed_delta);
                        for (auto& module : modules_) {
                            if (module->present_intermediate(alpha) == TickDecision::stop) {
                                stop_requested = true;
                                break;
                            }
                        }
                        frame_pacer.record_frame_work(FixedStepPacer::Clock::now() - woke);
                    }
                }
            }
            if (config_.pace_to_wall_clock && !stop_requested) {
                // A plain sleep wakes about a millisecond late on Windows, and
                // that lateness is different every tick: it is frame-time
                // jitter the player sees as micro-stutter.
                if (render_only_frames) {
                    offer_idle(pacing.next_tick);
                }
                precise_sleep_until(pacing.next_tick);
            }
        }
    } catch (const std::exception& exception) {
        // Keep the cause in the player's diagnostics log; the result code alone
        // cannot distinguish a renderer, network or asset failure.
        std::cerr << "BattleSpadesClient: module exception: " << exception.what() << '\n';
        result = started_modules == modules_.size() ? RunResult::module_runtime_failed
                                                    : RunResult::module_start_failed;
    } catch (...) {
        std::cerr << "BattleSpadesClient: module exception of unknown type\n";
        result = started_modules == modules_.size() ? RunResult::module_runtime_failed
                                                    : RunResult::module_start_failed;
    }

    while (started_modules > 0U) {
        --started_modules;
        modules_[started_modules]->stop();
    }

    state_ = State::stopped;
    return result;
}

} // namespace battlespades::core
