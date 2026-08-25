#include "battlespades/world/weapon_fire_sound.hpp"

#include "battlespades/world/weapon_catalog.hpp"

#include <cmath>

namespace battlespades::world {
namespace {

/** Retail tool ids for the two weapons whose sound shape switches at runtime. */
constexpr std::uint8_t minigun_tool_id{8U};
constexpr std::uint8_t mounted_machine_gun_tool_id{15U};

/**
 * The minigun opens its fire loop only at FULL barrel speed.
 *
 * Compared with an epsilon rather than against 1.0 exactly: the fraction is
 * derived by dividing one catalogued double by another, and the divisor is not
 * representable, so a fully spun barrel can land a few ulps short and the loop
 * would never open.
 */
constexpr double full_spin_epsilon{1.0e-6};

[[nodiscard]] double named_value(const WeaponDefinition& weapon, std::string_view name,
                                 double fallback) noexcept {
    const auto* constant = find_retail_weapon_constant(weapon, name);
    return constant != nullptr && constant->value_count > 0U ? constant->values.front()
                                                             : fallback;
}

} // namespace

FireSoundShape fire_sound_shape(const FireSoundInput& input) noexcept {
    // Follow-up burst rounds are silent whatever else is true of the weapon.
    if (input.burst_follow_up) {
        return FireSoundShape::silent;
    }
    if (!input.has_loop_cue) {
        return FireSoundShape::one_shot_per_round;
    }

    if (input.tool_id == minigun_tool_id) {
        // Two modes. Below full spin retail fires a discrete report per round
        // and leaves its per-shot cue enabled; only at full speed does it swap
        // to the sustained loop and suppress the per-shot sample. Running the
        // loop during spin-up is what turns one round into a burst of five: the
        // grain is a tenth of a second long and the loop's tail is another one
        // and three quarter seconds on top.
        return input.spin_fraction >= 1.0 - full_spin_epsilon
                   ? FireSoundShape::sustained_loop
                   : FireSoundShape::one_shot_per_round;
    }

    if (input.tool_id == mounted_machine_gun_tool_id) {
        // Also two modes, switched by deployment rather than by spin: mounted it
        // borrows the SMG loop, carried it fires a discrete report.
        return input.deployed ? FireSoundShape::sustained_loop
                              : FireSoundShape::one_shot_per_round;
    }

    return FireSoundShape::sustained_loop;
}

double fire_loop_quantum(std::uint8_t tool_id, bool deployed) noexcept {
    const auto* weapon = find_weapon_definition(tool_id);
    if (weapon == nullptr) {
        return 0.0;
    }
    if (tool_id == minigun_tool_id) {
        // Deliberately NOT the weapon's fire interval, which ramps with barrel
        // speed; retail quantises this loop to a fixed sound length so the close
        // does not stretch while the barrels are still winding up.
        return named_value(*weapon, "MINIGUN_SHOOT_SOUND_LENGTH", 0.1);
    }
    if (tool_id == mounted_machine_gun_tool_id) {
        // The catalogued fire_interval for this row is the UNDEPLOYED cadence,
        // five times the deployed one the loop actually runs at.
        return deployed ? named_value(*weapon, "MG_DEPLOYED_SHOOT_INTERVAL", 0.1)
                        : weapon->fire_interval;
    }
    return weapon->fire_interval;
}

void FireLoopCadence::open(double quantum) noexcept {
    quantum_ = std::isfinite(quantum) && quantum > 0.0 ? quantum : 0.0;
    elapsed_ = 0.0;
    firing_ = true;
}

void FireLoopCadence::refresh() noexcept {
    firing_ = true;
}

bool FireLoopCadence::advance(double dt, bool trigger_live) noexcept {
    if (!firing_ || !std::isfinite(dt) || dt <= 0.0) {
        return false;
    }
    elapsed_ += dt;
    if (quantum_ <= 0.0) {
        if (!trigger_live) {
            cancel();
            return true;
        }
        return false;
    }
    if (elapsed_ < quantum_) {
        return false;
    }
    elapsed_ = std::fmod(elapsed_, quantum_);
    if (trigger_live) {
        return false;
    }
    cancel();
    return true;
}

void FireLoopCadence::cancel() noexcept {
    firing_ = false;
    elapsed_ = 0.0;
    quantum_ = 0.0;
}

} // namespace battlespades::world
