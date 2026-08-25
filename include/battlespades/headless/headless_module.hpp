#pragma once

#include "battlespades/core/runtime_module.hpp"

#include <cstdint>
#include <string_view>

namespace battlespades::headless {

/**
 * Minimal runtime module used by smoke tests, protocol tools, and dedicated
 * simulations before a graphical frontend is linked.
 */
class HeadlessModule final : public core::RuntimeModule {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] bool start() override;
    [[nodiscard]] core::TickDecision tick(const core::TickContext& context) override;
    void stop() noexcept override;

    [[nodiscard]] std::uint64_t ticks_observed() const noexcept;
    [[nodiscard]] bool is_running() const noexcept;

private:
    std::uint64_t ticks_observed_{};
    bool running_{false};
};

} // namespace battlespades::headless
