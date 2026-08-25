#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace battlespades::frontend {

/** Class ids used by retail Zombie, Legacy Zombie and Fast Zombie variants. */
[[nodiscard]] constexpr bool is_zombie_class(std::uint8_t class_id) noexcept {
    return class_id == 4U || class_id == 14U || class_id == 15U;
}

/**
 * Deduplicates retail's global Zombie-conversion sting.
 *
 * Some compatible servers send PlaySound(28), while others expose only the
 * authoritative human-to-Zombie CreatePlayer transition. The client accepts
 * both representations, groups simultaneous Patient Zero conversions, and
 * emits exactly one global cue. Initial Zombie roster creation and ordinary
 * Zombie respawns are deliberately silent.
 */
class ZombieConversionAudioGate final {
public:
    using clock = std::chrono::steady_clock;
    using time_point = clock::time_point;
    static constexpr auto duplicate_window = std::chrono::milliseconds{300};

    [[nodiscard]] bool observe_conversion(
        std::optional<std::uint8_t> previous_class,
        std::uint8_t new_class, time_point now) noexcept {
        if (!previous_class.has_value() || is_zombie_class(*previous_class) ||
            !is_zombie_class(new_class)) {
            return false;
        }
        return accept(now);
    }

    [[nodiscard]] bool observe_explicit(time_point now) noexcept {
        return accept(now);
    }

    void reset() noexcept { last_cue_.reset(); }

private:
    [[nodiscard]] bool accept(time_point now) noexcept {
        if (last_cue_.has_value() && now >= *last_cue_ &&
            now - *last_cue_ <= duplicate_window) {
            return false;
        }
        last_cue_ = now;
        return true;
    }

    std::optional<time_point> last_cue_{};
};

} // namespace battlespades::frontend
