#pragma once

namespace battlespades::render {

/**
 * Converts wall-clock seconds into the counter consumed by retail skydomes.
 *
 * The recovered SkyDome.draw increments `time_counted` once per rendered
 * frame and sends that number to the scrolling-UV shader. The original game
 * targets 60 Hz, so using seconds directly makes every authored cloud, rain,
 * fog and sunbeam layer about sixty times too slow. A wall-clock conversion
 * preserves the retail 60 Hz appearance without making animation speed depend
 * on the user's refresh rate.
 */
[[nodiscard]] constexpr float retail_skydome_time(float elapsed_seconds) noexcept {
    constexpr float retail_draws_per_second{60.0F};
    return elapsed_seconds > 0.0F ? elapsed_seconds * retail_draws_per_second : 0.0F;
}

} // namespace battlespades::render
