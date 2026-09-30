#pragma once

#include "battlespades/world/player_movement.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <cstdint>

namespace battlespades::world {

/** MG_DEPLOYMENT_TIME (A1546): RMB held this long unfolds the gun. */
inline constexpr double machine_gun_deployment_seconds{3.0};
/** MG_WITHDRAWAL_TIME (A1547): RMB held this long folds it again. */
inline constexpr double machine_gun_withdrawal_seconds{0.75};
/** A1587 / A1588: the deployed gun turns 45 degrees each way from its yaw. */
inline constexpr double machine_gun_pitch_limit_degrees{45.0};
inline constexpr double machine_gun_yaw_limit_degrees{45.0};

struct MachineGunDeployInput final {
    /** Character.weapon_custom or shoot_secondary: the key or RMB is down. */
    bool trigger_held{};
    /** MGWeapon.check_deploying: a crouching character cannot start. */
    bool crouching{};
    /** MGWeapon.check_available_space_for_deployment. */
    bool space_available{};
    double yaw_degrees{};
    Vec3 position{};
    /**
     * How far the deployed character may drift before the gun folds. Retail
     * compares the position exactly (0); a network session allows for the
     * 1/64-block wire quantisation of server corrections.
     */
    double position_tolerance{};
};

enum class MachineGunDeployEvent : std::uint8_t {
    none,
    /** The deployment timer ran out: the gun is up. */
    deployed,
    /** The withdrawal timer ran out: the gun is folded. */
    withdrawn,
    /** The deployed character was moved off its spot: folded at once. */
    dislodged,
};

/**
 * MGWeapon (aoslib/weapons/mgWeapon.py) deployment, one instance per character.
 *
 * Holding the trigger counts `deployment_time` down; letting go before it
 * reaches zero restarts it. Completion clears the trigger, so every deployment
 * and every withdrawal needs a press of its own.
 */
class MachineGunDeployment final {
public:
    /** MGWeapon.update for one fixed step. */
    MachineGunDeployEvent tick(const MachineGunDeployInput& input, double dt) noexcept;
    /** MGWeapon.on_unset (tool change, death): folds a deployed gun at once. */
    bool reset() noexcept;

    /** Character.is_weapon_deployed. */
    [[nodiscard]] bool deployed() const noexcept { return deployed_; }
    /** Character.is_deploying_weapon: unfolding (never set while folding). */
    [[nodiscard]] bool deploying() const noexcept { return deploying_; }
    /** check_deploying() held this step: either timer is counting. */
    [[nodiscard]] bool timer_running() const noexcept { return timer_running_; }
    /** Character.set_walk / set_jump / set_crouch ignore input in both states. */
    [[nodiscard]] bool locks_movement() const noexcept { return deployed_ || deploying_; }
    /** MGWeapon.update calls set_primary_shoot(False) while a timer counts. */
    [[nodiscard]] bool blocks_primary() const noexcept { return timer_running_; }
    /** MGWeapon.can_swap. */
    [[nodiscard]] bool blocks_swap() const noexcept { return deployed_ || deploying_; }
    /** Character.weapon_deployment_yaw, in degrees. */
    [[nodiscard]] double deployment_yaw() const noexcept { return deployment_yaw_; }
    /** MGWeapon.get_deployment_progress: 1 at rest, falling to 0 while held. */
    [[nodiscard]] double progress() const noexcept;
    /** The yaw the main character is held at, or turned back to. */
    [[nodiscard]] double constrained_yaw(double yaw_degrees) const noexcept;
    /** The deployed main character's pitch limit. */
    [[nodiscard]] double constrained_pitch(double pitch_degrees) const noexcept;

private:
    void complete(const MachineGunDeployInput& input) noexcept;

    double remaining_{machine_gun_deployment_seconds};
    double deployment_yaw_{};
    Vec3 deployed_position_{};
    bool deployed_{};
    bool deploying_{};
    bool timer_running_{};
    /** Completion cleared weapon_custom / shoot_secondary; wait for a release. */
    bool trigger_spent_{};
};

/**
 * MGWeapon.check_available_space_for_deployment: the two blocks ahead of the
 * character (cube_line from its cell along the flat view direction) must be
 * empty at its own height and the two cells below, and solid three below.
 */
[[nodiscard]] bool machine_gun_deployment_space(const VxlMap& map,
                                                Vec3 position,
                                                double yaw_degrees) noexcept;

} // namespace battlespades::world
