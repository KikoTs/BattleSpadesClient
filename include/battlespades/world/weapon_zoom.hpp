#pragma once

#include <cstdint>

namespace battlespades::world {

/**
 * Retail's aim-down-sights transition, as pure arithmetic.
 *
 * Retail models aiming as a scalar `zoom` MULTIPLIER rather than a field of
 * view. The scene derives the projection from it every frame, and a separate
 * `zoom_level` chases the multiplier over several ticks. Everything an ADS
 * transition needs is therefore two numbers -- where the ramp is now and where
 * it is heading -- which is why this lives outside the session and stays
 * testable without a map, a camera or a weapon instance.
 */

/** Vertical field of view with the sights down, in degrees. */
inline constexpr double hip_fov_y_degrees{75.0};

/**
 * Degrees of vertical field of view removed per unit of zoom multiplier.
 *
 * Retail composes the projection as a LINEAR subtraction, not a division:
 * `fovy = 75.0 - 37.5 * zoom_level`. The difference is not cosmetic. Dividing
 * by a 1.5 multiplier yields 50 degrees where retail yields 18.75, so a
 * divide-based scope is barely over a third as strong as the real one and the
 * sniper never feels like a sniper.
 */
inline constexpr double fov_y_degrees_per_zoom{37.5};

/**
 * The tick the transition divisors are authored against.
 *
 * Retail's ramp has no dt term at all -- it subtracts a fixed fraction of the
 * remaining distance per call -- and gets away with it because the update is
 * scheduled at a fixed 60 Hz independently of the render rate. Running the raw
 * recurrence per rendered frame instead would make every scope 2.4x faster at
 * 144 fps than at 60.
 */
inline constexpr double zoom_transition_tick_hz{60.0};

/**
 * Per-tool transition divisors: a larger divisor is a SLOWER ramp.
 *
 * Retail keys separate rates for entering and leaving the sight, and picks
 * between them by comparing the target against the current level.
 */
struct ZoomTransitionRate final {
    double in_divisor{5.0};
    double out_divisor{5.0};
};

/**
 * The recovered rate for one tool, falling back to retail's shared default.
 *
 * Only three tools have their own entry. That the classic rifle is one of them
 * despite aiming at a multiplier of exactly 1.0 is the strongest single piece
 * of evidence that iron sights are a first-class aim state in retail and not
 * merely a scope too weak to matter.
 */
[[nodiscard]] ZoomTransitionRate zoom_transition_rate(std::uint8_t tool_id) noexcept;

/**
 * The value retail stores in `character.zoom` while a tool is aimed.
 *
 * Zero when not aiming, because retail literally calls `set_zoom(0)` to leave
 * the sight; the ramp then runs back down to hip fire through the same code.
 *
 * THIS IS THE ONE PLACE THE FLOAT-VERSUS-BOOL QUESTION LIVES. The call site
 * that passes a tool's `zoom` into `set_zoom` was never decompiled, so whether
 * `character.zoom` carries the tool's float magnitude or a plain boolean is
 * unresolved. This implements the float reading, which the surrounding
 * evidence favours: `set_zoom` stores its argument verbatim and forwards it to
 * `weapon.on_zoom(value)`, and the sniper's uniquely slow zoom-in divisor only
 * earns its keep over a longer travel. If the boolean reading turns out to be
 * right, return 1.0 for every aiming tool here and nothing else needs to move
 * -- both snipers then collapse to 37.5 degrees and the catalogued 1.5 and 1.2
 * become dead data.
 */
[[nodiscard]] double zoom_target_multiplier(std::uint8_t tool_id, bool aiming) noexcept;

/**
 * Advance one ramp by `dt` seconds and return the new level.
 *
 * Reproduces retail's fixed-fraction approach exactly at a 60 Hz step and
 * matches its elapsed-time behaviour at any other rate, by raising the
 * per-tick retention to the number of ticks `dt` represents. The recurrence is
 * asymptotic and never actually arrives, so this snaps once the remainder
 * stops mattering; that threshold is a termination guard, not a duration knob,
 * and is far below anything the eye resolves through a field of view.
 */
[[nodiscard]] double advance_zoom_level(double zoom_level, double target,
                                        std::uint8_t tool_id, double dt) noexcept;

/** Retail's projection from a ramp position to a vertical field of view. */
[[nodiscard]] constexpr double zoom_fov_y_degrees(double zoom_level) noexcept {
    return hip_fov_y_degrees - fov_y_degrees_per_zoom * zoom_level;
}

/**
 * The same ramp for a player-chosen hip field of view (Graphics tab). The
 * zoom keeps retail's magnification relative to hip fire (half the field of
 * view at a 1.0 ramp), and the retail 75 degrees reproduces the function
 * above exactly.
 */
[[nodiscard]] constexpr double zoom_fov_y_degrees(double zoom_level, double hip_fov) noexcept {
    if (hip_fov == hip_fov_y_degrees) {
        return zoom_fov_y_degrees(zoom_level);
    }
    return hip_fov * (zoom_fov_y_degrees(zoom_level) / hip_fov_y_degrees);
}

} // namespace battlespades::world
