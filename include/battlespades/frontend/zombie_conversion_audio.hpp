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
 * Gate for retail's global Zombie outbreak sting (SOUND_MAP 28,
 * `zombie_become`).
 *
 * Only the server's PlaySound(28) plays it. The retail client never plays a
 * `*_SOUND_ID` cue on its own, and the retail server sends this one once per
 * outbreak, not per infection (BattleSpades docs/SOUNDS_RETAIL.md, "Zombie
 * outbreak ... once per outbreak"). A human-to-Zombie CreatePlayer -- which
 * is what every ordinary Zombie kill produces -- is therefore silent.
 * Duplicate packets within 300 ms still collapse to one cue.
 */
class ZombieConversionAudioGate final {
public:
    using clock = std::chrono::steady_clock;
    using time_point = clock::time_point;
    static constexpr auto duplicate_window = std::chrono::milliseconds{300};

    /** A CreatePlayer class change never plays the sting in retail. */
    [[nodiscard]] static constexpr bool
    plays_on_conversion(std::optional<std::uint8_t> /*previous_class*/,
                        std::uint8_t /*new_class*/) noexcept {
        return false;
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
