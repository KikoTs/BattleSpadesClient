#pragma once

#include <array>

namespace battlespades::world {

/** Resource policy negotiated with BattleSpades; native thrust stays retail. */
struct FlightProfile final {
    std::array<double, 5U> drain{0.0, 75.0, 17.0, 18.0, 0.0};
    std::array<double, 5U> refill{0.0, 10.0, 9.0, 3.0, 100.0};
    bool grounded_refill_only{};
    double refill_idle_seconds{};
    bool descending_parachute_only{};
};

/** Requested local balance, distinct from the recovered original resource table. */
[[nodiscard]] constexpr FlightProfile balanced_flight_profile() noexcept {
    return {{0.0, 30.0, 9.0, 7.5, 0.0}, {0.0, 20.0, 20.0, 20.0, 100.0}, true, 1.0, true};
}

} // namespace battlespades::world
