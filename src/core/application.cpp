#include "battlespades/core/application.hpp"

#include <chrono>
#include <cstddef>
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

        const auto wall_clock_start = std::chrono::steady_clock::now();
        std::chrono::nanoseconds elapsed{};
        std::uint64_t tick_index{};
        bool stop_requested{false};

        while (result == RunResult::success && !stop_requested &&
               (!config_.tick_limit.has_value() || tick_index < *config_.tick_limit)) {
            const TickContext context{
                .index = tick_index,
                .fixed_delta = config_.fixed_delta,
                .elapsed = elapsed,
            };

            for (auto& module : modules_) {
                if (module->tick(context) == TickDecision::stop) {
                    stop_requested = true;
                    break;
                }
            }

            ++tick_index;
            elapsed += config_.fixed_delta;

            if (config_.pace_to_wall_clock && !stop_requested) {
                std::this_thread::sleep_until(wall_clock_start + elapsed);
            }
        }
    } catch (...) {
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
