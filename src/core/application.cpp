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
            };

            for (auto& module : modules_) {
                if (module->tick(context) == TickDecision::stop) {
                    stop_requested = true;
                    break;
                }
            }

            ++tick_index;
            elapsed += config_.fixed_delta;

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
                if (period > std::chrono::nanoseconds::zero()) {
                    const auto tick_time = pacing.next_tick - config_.fixed_delta;
                    auto previous_frame = FixedStepPacer::Clock::now();
                    while (!stop_requested) {
                        const auto slot = next_intermediate_frame(
                            previous_frame, FixedStepPacer::Clock::now(), pacing.next_tick, period);
                        if (!slot.has_value()) {
                            break;
                        }
                        std::this_thread::sleep_until(slot->at);
                        const auto woke = FixedStepPacer::Clock::now();
                        if (woke + period / 4 >= pacing.next_tick) {
                            // A coarse OS timer overslept; the tick has priority.
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
                        previous_frame = slot->at;
                    }
                }
            }
            if (config_.pace_to_wall_clock && !stop_requested) {
                std::this_thread::sleep_until(pacing.next_tick);
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
