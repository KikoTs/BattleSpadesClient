#pragma once

#include <cstdint>

namespace battlespades::world {

/**
 * Retail's camera kick for one shot, as pure arithmetic.
 *
 * Recovered from `Character.shoot` in the stock character.pyd (0x10049db0,
 * recoil block 0x1004b7b8-0x1004c3a0):
 *
 *     up = weapon.recoil_up
 *     if (scene.timer & 1023) > 511: side = ((timer & 511) - 255.5) *  recoil_side
 *     else:                          side = ((timer & 511) - 255.5) * -recoil_side
 *     if self.walking and not self.is_crouching(): up *= 2; side *= 2
 *     if self.airborne:                            up *= 2; side *= 2
 *     elif self.is_crouching():                    up /= 2; side /= 2
 *     self.set_view(self.pitch + up * 60, self.yaw + side * 60)
 *
 * The side kick is a deterministic sawtooth on the scene's millisecond
 * clock, not a random draw: its sign flips every 512 ms and its magnitude
 * reaches 255.5 times `recoil_side`. The final factor is the integer 60
 * (dword_10098D74), not 180/pi. `walking` is `Character.set_walk`'s "any
 * movement key held" input flag, not a velocity test.
 */
struct RetailRecoilKick final {
    double pitch_degrees{};
    double yaw_degrees{};
};

inline constexpr double retail_recoil_view_scale{60.0};
inline constexpr double retail_recoil_side_centre{255.5};

[[nodiscard]] constexpr RetailRecoilKick retail_recoil_kick(double recoil_up,
                                                           double recoil_side,
                                                           std::uint64_t timer_ms,
                                                           bool walking,
                                                           bool crouching,
                                                           bool airborne) noexcept {
    double up = recoil_up;
    const double saw = static_cast<double>(timer_ms & 511U) - retail_recoil_side_centre;
    double side = (timer_ms & 1023U) > 511U ? saw * recoil_side : saw * -recoil_side;
    if (walking && !crouching) {
        up *= 2.0;
        side *= 2.0;
    }
    if (airborne) {
        up *= 2.0;
        side *= 2.0;
    } else if (crouching) {
        up /= 2.0;
        side /= 2.0;
    }
    return {up * retail_recoil_view_scale, side * retail_recoil_view_scale};
}

/** The scene clock retail's recoil and view-model bob read: ms at 60 Hz ticks. */
[[nodiscard]] constexpr std::uint64_t retail_scene_timer_ms(std::uint64_t ticks) noexcept {
    return ticks * 1000U / 60U;
}

} // namespace battlespades::world
