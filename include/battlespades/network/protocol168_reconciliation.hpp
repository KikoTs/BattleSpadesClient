#pragma once

#include "battlespades/world/player_movement.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>

namespace battlespades::network {

struct PredictionCorrection final {
    std::int32_t acknowledged_loop{};
    world::Vec3 position_delta{};
    world::Vec3 velocity_delta{};
};

/**
 * Bounded local prediction journal keyed by the exact ClientData loop sent
 * to the server. WorldUpdate positions describe that acknowledged past loop,
 * so comparing them to the present player is invalid and causes rollbacks.
 */
class Protocol168PredictionHistory final {
public:
    explicit Protocol168PredictionHistory(std::size_t capacity = 256U);

    void record(std::int32_t loop,
                const world::PlayerMovementState& state);
    [[nodiscard]] std::optional<PredictionCorrection>
    reconcile(std::int32_t acknowledged_loop,
              world::Vec3 authoritative_position,
              world::Vec3 authoritative_velocity);
    void clear() noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    struct Sample final {
        std::int32_t loop{};
        world::Vec3 position{};
        world::Vec3 velocity{};
    };

    std::size_t capacity_;
    std::deque<Sample> samples_;
    std::int32_t last_reconciled_loop_{std::numeric_limits<std::int32_t>::min()};
};

} // namespace battlespades::network
