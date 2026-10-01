#pragma once

#include <array>
#include <cmath>

namespace battlespades::render {

/**
 * Applies a skydome layer's authored `rotation` (degrees) in retail order.
 *
 * SkyDome.do_mesh_rotation (gameScene.pyd 0x1010FF40) issues
 * glRotatef(x, 1,0,0), then glRotatef(y, 0,1,0), then glRotatef(z, 0,0,1),
 * each skipped when zero. Fixed-function GL post-multiplies, so a vertex sees
 * Rx * Ry * Rz: Z acts first, X last. Applying X first instead is harmless
 * for the common (180, 180, 0) but yaws Colosseum (180, 45, 0) by 90 degrees
 * and Invasion (180, 90, 0) by 180, because Rx(180) Ry(a) = Ry(-a) Rx(180).
 * Works in the exporter's y-up space, before the z-down swizzle.
 */
[[nodiscard]] inline std::array<float, 3U>
retail_skydome_rotate(std::array<float, 3U> value, const std::array<float, 3U>& degrees) noexcept {
    constexpr float radians_per_degree{0.01745329251994329577F};
    const auto sx = std::sin(degrees[0U] * radians_per_degree);
    const auto cx = std::cos(degrees[0U] * radians_per_degree);
    const auto sy = std::sin(degrees[1U] * radians_per_degree);
    const auto cy = std::cos(degrees[1U] * radians_per_degree);
    const auto sz = std::sin(degrees[2U] * radians_per_degree);
    const auto cz = std::cos(degrees[2U] * radians_per_degree);
    // Rz first ...
    value = {value[0U] * cz - value[1U] * sz, value[0U] * sz + value[1U] * cz, value[2U]};
    // ... then Ry ...
    value = {value[0U] * cy + value[2U] * sy, value[1U], -value[0U] * sy + value[2U] * cy};
    // ... then Rx, the outermost glRotatef.
    return {value[0U], value[1U] * cx - value[2U] * sx, value[1U] * sx + value[2U] * cx};
}

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

/**
 * Converts an authored `uv_speeds` pair into the speed our skydome shader adds
 * to the raw .aos texture coordinate.
 *
 * Retail samples a different V than the one stored in the .aos file:
 * - mesh.pyd's .aos loader (0x100042F0) rewrites every vertex as
 *   `v = 1.0 - v` on load;
 * - mesh.pyd's TGA loader (0x100015B0) hands the file rows to glTexImage2D
 *   unflipped, so for the shipped skies (all 117 are bottom-origin) GL t=0 is
 *   the bottom row of the picture;
 * - skydome_vert adds `time * uv_speed` to that flipped coordinate, and
 *   SkyDome.draw (gameScene.pyd 0x1010E730) passes uv_speeds[layer] through
 *   with no sign change.
 * Measured from the top of the picture, retail therefore samples
 * `1 - t = v - time * uv_speed.y`.
 *
 * We keep the raw V and bimg decodes TGAs top row first, so a static texel
 * already lands where retail puts it, but adding the authored speed as-is
 * samples `v + time * uv_speed.y`: every vertically scrolling cloud, rain and
 * fog layer drifts the opposite way to retail. U is sampled identically on
 * both sides (left-to-right files, no U flip in the loader), so only V turns
 * round.
 */
[[nodiscard]] constexpr std::array<float, 2U>
retail_skydome_uv_speed(std::array<float, 2U> authored_uv_speed) noexcept {
    return {authored_uv_speed[0U], -authored_uv_speed[1U]};
}

} // namespace battlespades::render
