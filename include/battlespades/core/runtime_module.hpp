#pragma once

#include <chrono>
#include <cstdint>
#include <string_view>

namespace battlespades::core {

struct TickContext final {
    std::uint64_t index{};
    std::chrono::nanoseconds fixed_delta{};
    std::chrono::nanoseconds elapsed{};
    // Catch-up steps still poll input and advance simulation, but must not
    // submit another expensive presentation. Unpaced runs keep true.
    bool present{true};
};

enum class TickDecision {
    continue_running,
    stop,
};

/**
 * A lifecycle participant owned and ticked by Application.
 *
 * SDL, bgfx, ENet, OpenAL, gameplay, and headless implementations enter the
 * runtime through this boundary. start(), tick(), and stop() always execute on
 * the main game thread. A module that returns false from start() must clean up
 * its own partial initialization; Application stops previously started modules
 * in reverse order.
 */
class RuntimeModule {
public:
    virtual ~RuntimeModule() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual bool start() = 0;
    [[nodiscard]] virtual TickDecision tick(const TickContext& context) = 0;
    virtual void stop() noexcept = 0;

    /**
     * Period of optional render-only frames between fixed ticks, or zero for
     * none (the default: exactly one presentation per tick, retail pacing).
     * Queried once per paced tick, after every module ticked.
     */
    [[nodiscard]] virtual std::chrono::nanoseconds intermediate_frame_period() const noexcept {
        return std::chrono::nanoseconds::zero();
    }

    /**
     * Presents one render-only frame `alpha` (0 <= alpha < 1) of the way from
     * the last tick to the next. Implementations must not poll input, advance
     * simulation or touch the network: the fixed-step cadence is unchanged.
     */
    [[nodiscard]] virtual TickDecision present_intermediate(double alpha) {
        static_cast<void>(alpha);
        return TickDecision::continue_running;
    }
};

} // namespace battlespades::core
