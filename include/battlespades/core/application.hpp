#pragma once

#include "battlespades/core/runtime_module.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace battlespades::core {

struct RuntimeConfig final {
    // Protocol parity uses one deterministic integer period everywhere. A
    // later rational clock may carry the sub-nanosecond remainder, but the
    // default and explicit --tick-rate 60 paths must never disagree.
    std::chrono::nanoseconds fixed_delta{std::chrono::nanoseconds{16'666'666}};
    std::optional<std::uint64_t> tick_limit{1U};
    bool pace_to_wall_clock{false};
};

enum class RunResult {
    success,
    invalid_state,
    invalid_configuration,
    module_start_failed,
    module_runtime_failed,
};

/**
 * Owns the deterministic fixed-step runtime and all registered modules.
 *
 * Modules may only be added before the first run. The runtime is intentionally
 * independent of window, renderer, network, and audio libraries so protocol
 * simulation and parity tests can run headlessly.
 */
class Application final {
public:
    explicit Application(RuntimeConfig config);

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    [[nodiscard]] bool add_module(std::unique_ptr<RuntimeModule> module);
    [[nodiscard]] RunResult run();

private:
    enum class State {
        accepting_modules,
        running,
        stopped,
    };

    RuntimeConfig config_;
    State state_{State::accepting_modules};
    std::vector<std::unique_ptr<RuntimeModule>> modules_;
};

} // namespace battlespades::core
