#include "battlespades/world/machine_gun_deployment.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace battlespades::world {
namespace {

[[nodiscard]] double wrapped_degrees(double value) noexcept {
    value = std::fmod(value + 180.0, 360.0);
    if (value < 0.0) value += 360.0;
    return value - 180.0;
}

/**
 * aoslib.world.cube_line restricted to one height: the face-connected cells
 * from `start` to `target`, both included. Same interval arithmetic and tie
 * order (y before x) as network::cube_line_cells.
 */
template <typename Visit>
void walk_flat_cube_line(std::array<long, 2U> cell, std::array<long, 2U> target, Visit visit) {
    const std::array<long, 2U> delta{target[0U] - cell[0U], target[1U] - cell[1U]};
    const std::array<long, 2U> step{delta[0U] < 0 ? -1L : 1L, delta[1U] < 0 ? -1L : 1L};
    constexpr long unit{1024};
    constexpr long disabled{0x3FFFFFFF / 512};
    const auto ratio = [](long dominant, long axis) {
        return axis == 0 ? disabled : std::labs(dominant * unit / axis);
    };
    std::array<long, 2U> interval{};
    if (std::labs(delta[0U]) >= std::labs(delta[1U])) {
        interval = {unit, ratio(delta[0U], delta[1U])};
    } else {
        interval = {ratio(delta[1U], delta[0U]), unit};
    }
    std::array<long, 2U> distance{interval[0U] / 2, interval[1U] / 2};
    for (std::size_t axis{}; axis < 2U; ++axis) {
        if (step[axis] >= 0) distance[axis] = interval[axis] - distance[axis];
    }
    // Two blocks ahead is at most three cells per axis; the bound is a guard.
    for (int guard{}; guard < 8; ++guard) {
        visit(cell);
        if (cell == target) return;
        const std::size_t axis = distance[0U] < distance[1U] ? 0U : 1U;
        cell[axis] += step[axis];
        distance[axis] += interval[axis];
    }
}

} // namespace

MachineGunDeployEvent MachineGunDeployment::tick(const MachineGunDeployInput& input,
                                                 double dt) noexcept {
    if (!input.trigger_held) trigger_spent_ = false;
    const bool held = input.trigger_held && !trigger_spent_;

    // MGWeapon.check_deploying: a folded gun also needs a standing character
    // and the space ahead; a deployed gun only needs the trigger.
    const bool counting =
        held && (deployed_ || (!input.crouching && input.space_available));
    timer_running_ = counting;

    auto event = MachineGunDeployEvent::none;
    if (counting) {
        remaining_ -= std::max(0.0, dt);
        if (remaining_ > 0.0) {
            if (!deployed_ && !deploying_) {
                deploying_ = true;
                deployment_yaw_ = input.yaw_degrees;
            }
        } else {
            complete(input);
            event = deployed_ ? MachineGunDeployEvent::deployed
                              : MachineGunDeployEvent::withdrawn;
            deploying_ = false;
            timer_running_ = false;
            trigger_spent_ = true;
        }
    } else {
        remaining_ = deployed_ ? machine_gun_withdrawal_seconds
                               : machine_gun_deployment_seconds;
        deploying_ = false;
    }

    // MGWeapon.update, deployed main character: any displacement folds the gun.
    const auto moved = [&input](double now, double then) {
        return std::abs(now - then) > std::max(0.0, input.position_tolerance);
    };
    if (deployed_ && event == MachineGunDeployEvent::none &&
        (moved(input.position.x, deployed_position_.x) ||
         moved(input.position.y, deployed_position_.y) ||
         moved(input.position.z, deployed_position_.z))) {
        deployed_ = false;
        deploying_ = false;
        timer_running_ = false;
        remaining_ = machine_gun_deployment_seconds;
        event = MachineGunDeployEvent::dislodged;
    }
    return event;
}

void MachineGunDeployment::complete(const MachineGunDeployInput& input) noexcept {
    // MGWeapon.deployment_complete toggles the state.
    if (!deployed_) {
        deployed_ = true;
        if (!deploying_) deployment_yaw_ = input.yaw_degrees;
        deployed_position_ = input.position;
        remaining_ = machine_gun_withdrawal_seconds;
    } else {
        deployed_ = false;
        remaining_ = machine_gun_deployment_seconds;
    }
}

bool MachineGunDeployment::reset() noexcept {
    const bool was_deployed = deployed_;
    *this = {};
    return was_deployed;
}

double MachineGunDeployment::progress() const noexcept {
    const double total =
        deployed_ ? machine_gun_withdrawal_seconds : machine_gun_deployment_seconds;
    return std::clamp(remaining_ / total, 0.0, 1.0);
}

double MachineGunDeployment::constrained_yaw(double yaw_degrees) const noexcept {
    // While a timer counts the main character's yaw is weapon_deployment_yaw.
    if (timer_running_ && (deploying_ || deployed_)) return deployment_yaw_;
    if (!deployed_) return yaw_degrees;
    const double difference = wrapped_degrees(yaw_degrees - deployment_yaw_);
    return deployment_yaw_ + std::clamp(difference, -machine_gun_yaw_limit_degrees,
                                        machine_gun_yaw_limit_degrees);
}

double MachineGunDeployment::constrained_pitch(double pitch_degrees) const noexcept {
    if (!deployed_) return pitch_degrees;
    return std::clamp(pitch_degrees, -machine_gun_pitch_limit_degrees,
                      machine_gun_pitch_limit_degrees);
}

bool machine_gun_deployment_space(const VxlMap& map, Vec3 position, double yaw_degrees) noexcept {
    // The session's yaw: orientation = (-cos yaw, -sin yaw) on the flat plane.
    const double radians = yaw_degrees * std::numbers::pi / 180.0;
    const Vec3 target{position.x - std::cos(radians) * 2.0,
                      position.y - std::sin(radians) * 2.0,
                      position.z};
    const auto cell = [](double value) { return static_cast<long>(value); };
    const auto solid = [&map](long x, long y, long z) {
        if (x < 0 || y < 0 || z < 0 || x >= static_cast<long>(VxlMap::width) ||
            y >= static_cast<long>(VxlMap::depth) || z >= static_cast<long>(VxlMap::height)) {
            return false;
        }
        return map.solid(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                         static_cast<std::uint32_t>(z));
    };
    const long z = cell(position.z);
    bool clear{true};
    bool supported{true};
    walk_flat_cube_line({cell(position.x), cell(position.y)}, {cell(target.x), cell(target.y)},
                        [&](const std::array<long, 2U>& point) {
                            for (long delta{}; delta < 3; ++delta) {
                                if (solid(point[0U], point[1U], z + delta)) clear = false;
                            }
                            if (!solid(point[0U], point[1U], z + 3)) supported = false;
                        });
    return clear && supported;
}

} // namespace battlespades::world
