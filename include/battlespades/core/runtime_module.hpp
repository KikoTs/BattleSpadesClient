#pragma once

#include <chrono>
#include <cstdint>
#include <string_view>

namespace battlespades::core {

struct TickContext final {
    std::uint64_t index{};
    std::chrono::nanoseconds fixed_delta{};
    std::chrono::nanoseconds elapsed{};
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
};

} // namespace battlespades::core
