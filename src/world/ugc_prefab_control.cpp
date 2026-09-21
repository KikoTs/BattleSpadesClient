#include "battlespades/world/ugc_prefab_control.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace battlespades::world {
namespace {

constexpr std::array<double, 4U> input_repeat{0.0, 0.5, 0.1, 0.05};
constexpr double erase_delay{0.1};
constexpr double sprint_speed{10.0};
constexpr double maximum_zoom{9'999'999.0};
constexpr double color_minimum{0.9};
constexpr double color_maximum{2.0};

[[nodiscard]] constexpr std::size_t index(UgcPrefabControlInput input) noexcept {
    return static_cast<std::size_t>(input);
}

[[nodiscard]] std::array<std::int32_t, 3U>
rotate_size(std::array<std::int32_t, 3U> value,
            std::uint8_t yaw,
            std::uint8_t pitch,
            std::uint8_t roll) noexcept {
    // shared.common.rotate_point applies Y (roll), X (pitch), then Z (yaw).
    for (std::uint8_t step{}; step < (roll & 3U); ++step) {
        const auto previous_x = value[0U];
        value[0U] = -value[2U];
        value[2U] = previous_x;
    }
    for (std::uint8_t step{}; step < (pitch & 3U); ++step) {
        const auto previous_y = value[1U];
        value[1U] = value[2U];
        value[2U] = -previous_y;
    }
    for (std::uint8_t step{}; step < (yaw & 3U); ++step) {
        const auto previous_x = value[0U];
        value[0U] = value[1U];
        value[1U] = -previous_x;
    }
    return value;
}

[[nodiscard]] std::uint8_t wrap_quarter_turn(std::int32_t value) noexcept {
    return static_cast<std::uint8_t>((value % 4 + 4) % 4);
}

} // namespace

void UgcPrefabControl::prime(const UgcPrefabPlacement& placement) noexcept {
    if (active_) {
        return;
    }
    const bool changed_size = !has_placement_ || placement.authored_size != placement_.authored_size;
    placement_ = placement;
    placement_.yaw &= 3U;
    placement_.pitch &= 3U;
    placement_.roll &= 3U;
    has_placement_ = true;
    const auto x = static_cast<double>(std::max(0, placement_.authored_size[0U]));
    const auto y = static_cast<double>(std::max(0, placement_.authored_size[1U]));
    const auto z = static_cast<double>(std::max(0, placement_.authored_size[2U]));
    prefab_radius_ = std::sqrt(x * x + y * y + z * z) * 0.5;
    if (changed_size) {
        // UGCPrefabTool.set_prefab_radius uses size_x rather than the radius.
        zoom_level_ = x * 1.73205080757 * 0.5;
    }
}

bool UgcPrefabControl::activate() noexcept {
    if (!has_placement_ || deactivate_remaining_ > 0.0) {
        return false;
    }
    active_ = true;
    clear_inputs();
    return true;
}

void UgcPrefabControl::deactivate() noexcept {
    active_ = false;
    clear_inputs();
    deactivate_remaining_ = 0.5;
}

void UgcPrefabControl::reset() noexcept {
    placement_ = {};
    clear_inputs();
    has_placement_ = false;
    active_ = false;
    carve_pending_ = false;
    erase_remaining_ = 0.0;
    deactivate_remaining_ = 0.0;
    prefab_radius_ = 0.0;
    zoom_level_ = 0.0;
    color_animation_ = color_minimum;
    color_animation_delta_ = 0.5;
}

void UgcPrefabControl::set_input(UgcPrefabControlInput value, bool held) noexcept {
    if (index(value) >= inputs_.size() || inputs_[index(value)] == held) return;
    inputs_[index(value)] = held;
    if (held && value != UgcPrefabControlInput::sprint && value != UgcPrefabControlInput::carve) {
        // A fresh direction/rotation key is a new nudge. Do not make reversals
        // wait for the old direction's initial half-second repeat delay.
        repeat_index_ = 0U;
        repeat_remaining_ = 0.0;
    }
}

bool UgcPrefabControl::input(UgcPrefabControlInput value) const noexcept {
    return inputs_[index(value)];
}

void UgcPrefabControl::clear_inputs() noexcept {
    inputs_.fill(false);
    carve_pending_ = false;
    repeat_index_ = 0U;
    repeat_remaining_ = 0.0;
}

void UgcPrefabControl::tick(double dt, const Vec3& camera_forward) noexcept {
    if (!std::isfinite(dt) || dt < 0.0) return;
    deactivate_remaining_ = std::max(0.0, deactivate_remaining_ - dt);
    erase_remaining_ = std::max(0.0, erase_remaining_ - dt);
    if (has_placement_ && input(UgcPrefabControlInput::carve) && erase_remaining_ == 0.0) {
        erase_remaining_ = erase_delay;
        carve_pending_ = true;
    }

    color_animation_ = std::clamp(color_animation_ + color_animation_delta_ * dt,
                                  color_minimum,
                                  color_maximum);
    if (color_animation_ >= color_maximum || color_animation_ <= color_minimum) {
        color_animation_delta_ = -color_animation_delta_;
    }
    if (!active_) {
        return;
    }

    const bool any_nudge =
        input(UgcPrefabControlInput::forward) || input(UgcPrefabControlInput::backward) ||
        input(UgcPrefabControlInput::left) || input(UgcPrefabControlInput::right) ||
        input(UgcPrefabControlInput::up) || input(UgcPrefabControlInput::down) ||
        input(UgcPrefabControlInput::rotate_left) ||
        input(UgcPrefabControlInput::rotate_right) ||
        input(UgcPrefabControlInput::rotate_up) ||
        input(UgcPrefabControlInput::rotate_down);
    if (!any_nudge) {
        repeat_index_ = 0U;
        repeat_remaining_ = 0.0;
        return;
    }

    const bool first_nudge = repeat_index_ == 0U;
    if (!first_nudge) repeat_remaining_ -= dt;
    if (repeat_remaining_ > 1e-9) {
        return;
    }
    repeat_index_ = static_cast<std::uint8_t>(
        std::min<std::size_t>(repeat_index_ + 1U, input_repeat.size() - 1U));
    // Retain fractional tick time so a 50 ms repeat does not drift to four
    // 60 Hz ticks. Bound catch-up after a stall to one nudge per fixed update.
    repeat_remaining_ = first_nudge ? input_repeat[repeat_index_]
                                    : std::max(0.0, repeat_remaining_ + input_repeat[repeat_index_]);

    const double flat_length =
        std::hypot(camera_forward.x, camera_forward.y);
    const double forward_x = flat_length > 1e-9 ? camera_forward.x / flat_length : 1.0;
    const double forward_y = flat_length > 1e-9 ? camera_forward.y / flat_length : 0.0;
    std::uint8_t camera_direction{}; // NORTH
    if (std::abs(forward_x) > std::abs(forward_y)) {
        camera_direction = forward_x > 0.0 ? 1U : 3U; // EAST / WEST
    } else if (forward_y < 0.0) {
        camera_direction = 0U; // NORTH
    } else {
        camera_direction = 2U; // SOUTH
    }
    apply_camera_relative_rotation(
        static_cast<std::uint8_t>((placement_.yaw + camera_direction) & 3U));

    double movement_x{};
    double movement_y{};
    double movement_z{};
    if (input(UgcPrefabControlInput::forward)) movement_y += 1.0;
    if (input(UgcPrefabControlInput::backward)) movement_y -= 1.0;
    if (input(UgcPrefabControlInput::right)) movement_x += 1.0;
    if (input(UgcPrefabControlInput::left)) movement_x -= 1.0;

    // Vector3(0,0,1).cross(camera_forward) == (-forward_y, forward_x, 0).
    double world_x = forward_x * movement_y + (-forward_y) * movement_x;
    double world_y = forward_y * movement_y + forward_x * movement_x;
    if (input(UgcPrefabControlInput::up)) movement_z -= 1.0;
    if (input(UgcPrefabControlInput::down)) movement_z += 1.0;

    if (std::abs(world_x) > std::abs(world_y) &&
        std::abs(world_x) > std::abs(movement_z)) {
        world_x = std::copysign(1.0, world_x);
        world_y = 0.0;
        movement_z = 0.0;
    } else if (std::abs(world_y) > std::abs(world_x) &&
               std::abs(world_y) > std::abs(movement_z)) {
        world_x = 0.0;
        world_y = std::copysign(1.0, world_y);
        movement_z = 0.0;
    } else if (movement_z != 0.0) {
        world_x = 0.0;
        world_y = 0.0;
        movement_z = std::copysign(1.0, movement_z);
    }
    const double speed = input(UgcPrefabControlInput::sprint) ? sprint_speed : 1.0;
    placement_.center[0U] += world_x * speed;
    placement_.center[1U] += world_y * speed;
    placement_.center[2U] += movement_z * speed;
    recenter_anchor();
}

void UgcPrefabControl::rotate_yaw(int quarter_turns) noexcept {
    if (!has_placement_) {
        return;
    }
    rotate(quarter_turns, 0, 0);
}

void UgcPrefabControl::adjust_zoom(double wheel_y) noexcept {
    zoom_level_ =
        std::clamp(zoom_level_ - wheel_y * prefab_radius_ * 0.1, 0.0, maximum_zoom);
}

bool UgcPrefabControl::take_carve_request() noexcept {
    const bool pending = carve_pending_;
    carve_pending_ = false;
    return pending;
}

void UgcPrefabControl::rotate(std::int32_t yaw,
                              std::int32_t pitch,
                              std::int32_t roll) noexcept {
    placement_.yaw = wrap_quarter_turn(static_cast<std::int32_t>(placement_.yaw) + yaw);
    placement_.pitch = wrap_quarter_turn(static_cast<std::int32_t>(placement_.pitch) + pitch);
    placement_.roll = wrap_quarter_turn(static_cast<std::int32_t>(placement_.roll) + roll);
    recenter_anchor();
}

void UgcPrefabControl::recenter_anchor() noexcept {
    auto world_size = rotate_size(placement_.authored_size,
                                  placement_.yaw,
                                  placement_.pitch,
                                  placement_.roll);
    for (std::size_t axis{}; axis < 3U; ++axis) {
        placement_.anchor[axis] = static_cast<std::int32_t>(placement_.center[axis]) -
                                  world_size[axis] / 2;
    }

    // UGCPrefabTool.rotate_prefab_centered's even-size yaw compensation.
    if ((placement_.authored_size[0U] & 1) == 0 &&
        (placement_.yaw == 1U || placement_.yaw == 2U)) {
        --placement_.anchor[1U];
    }
    if ((placement_.authored_size[1U] & 1) == 0 &&
        (placement_.yaw == 0U || placement_.yaw == 1U)) {
        ++placement_.anchor[0U];
    }
}

void UgcPrefabControl::apply_camera_relative_rotation(
    std::uint8_t camera_relative_yaw) noexcept {
    const bool left = input(UgcPrefabControlInput::rotate_left) &&
                      !input(UgcPrefabControlInput::rotate_right);
    const bool right = input(UgcPrefabControlInput::rotate_right) &&
                       !input(UgcPrefabControlInput::rotate_left);
    const bool up = input(UgcPrefabControlInput::rotate_up) &&
                    !input(UgcPrefabControlInput::rotate_down);
    const bool down = input(UgcPrefabControlInput::rotate_down) &&
                      !input(UgcPrefabControlInput::rotate_up);
    const auto pitch = placement_.pitch;

    // Mechanical port of UGCPrefabTool.apply_prefab_rotation. Several camera
    // facings need compound quarter-turns to keep arrows screen-relative.
    if (left) {
        switch (camera_relative_yaw & 3U) {
        case 0U:
            if (pitch == 3U) { rotate(0, 1, 0); rotate(1, 0, 0); rotate(0, 0, -1); }
            else if (pitch == 1U) { rotate(0, -1, 0); rotate(-1, 0, 0); rotate(0, 0, -1); }
            else rotate(0, 0, -1);
            break;
        case 1U:
            if (pitch == 1U) { rotate(0, -1, 0); rotate(2, 0, 0); rotate(0, 0, 2); }
            else rotate(0, 1, 0);
            break;
        case 2U:
            if (pitch == 3U) { rotate(0, 1, 0); rotate(-1, 0, 0); rotate(0, 0, 1); }
            else if (pitch == 1U) { rotate(0, -1, 0); rotate(1, 0, 0); rotate(0, 0, 1); }
            else rotate(0, 0, 1);
            break;
        default:
            // UGCPrefabTool.py:149-155: WEST + left special-cases
            // pitch 3, not pitch 1.  Swapping this branch mirrors an
            // asymmetric construct after a west-facing screen-left turn.
            if (pitch == 3U) { rotate(0, 1, 0); rotate(2, 0, 0); rotate(0, 0, 2); }
            else rotate(0, -1, 0);
            break;
        }
    }
    if (right) {
        switch (camera_relative_yaw & 3U) {
        case 0U:
            if (pitch == 3U) { rotate(0, 1, 0); rotate(-1, 0, 0); rotate(0, 0, 1); }
            else if (pitch == 1U) { rotate(0, -1, 0); rotate(1, 0, 0); rotate(0, 0, 1); }
            else rotate(0, 0, 1);
            break;
        case 1U:
            // UGCPrefabTool.py:168-174: EAST + right is the inverse
            // asymmetric branch: pitch 3 performs the compound half turns;
            // every other pitch moves one quarter turn down.
            if (pitch == 3U) { rotate(0, 1, 0); rotate(2, 0, 0); rotate(0, 0, 2); }
            else rotate(0, -1, 0);
            break;
        case 2U:
            if (pitch == 3U) { rotate(0, 1, 0); rotate(1, 0, 0); rotate(0, 0, -1); }
            else if (pitch == 1U) { rotate(0, -1, 0); rotate(-1, 0, 0); rotate(0, 0, -1); }
            else rotate(0, 0, -1);
            break;
        default:
            if (pitch == 1U) { rotate(0, -1, 0); rotate(2, 0, 0); rotate(0, 0, 2); }
            else rotate(0, 1, 0);
            break;
        }
    }
    if (up) {
        switch (camera_relative_yaw & 3U) {
        case 0U:
            if (pitch == 1U) { rotate(0, -1, 0); rotate(2, 0, 0); rotate(0, 0, 2); }
            else rotate(0, 1, 0);
            break;
        case 1U:
            if (pitch == 3U) { rotate(0, 1, 0); rotate(-1, 0, 0); rotate(0, 0, 1); }
            else if (pitch == 1U) { rotate(0, -1, 0); rotate(1, 0, 0); rotate(0, 0, 1); }
            else rotate(0, 0, 1);
            break;
        case 2U:
            if (pitch == 3U) { rotate(0, 1, 0); rotate(2, 0, 0); rotate(0, 0, 2); }
            else rotate(0, -1, 0);
            break;
        default:
            if (pitch == 3U) { rotate(0, 1, 0); rotate(1, 0, 0); rotate(0, 0, -1); }
            else if (pitch == 1U) { rotate(0, -1, 0); rotate(-1, 0, 0); rotate(0, 0, -1); }
            else rotate(0, 0, -1);
            break;
        }
    }
    if (down) {
        switch (camera_relative_yaw & 3U) {
        case 0U:
            if (pitch == 3U) { rotate(0, 1, 0); rotate(2, 0, 0); rotate(0, 0, 2); }
            else rotate(0, -1, 0);
            break;
        case 1U:
            if (pitch == 3U) { rotate(0, 1, 0); rotate(1, 0, 0); rotate(0, 0, -1); }
            else if (pitch == 1U) { rotate(0, -1, 0); rotate(-1, 0, 0); rotate(0, 0, -1); }
            else rotate(0, 0, -1);
            break;
        case 2U:
            if (pitch == 1U) { rotate(0, -1, 0); rotate(2, 0, 0); rotate(0, 0, 2); }
            else rotate(0, 1, 0);
            break;
        default:
            if (pitch == 3U) { rotate(0, 1, 0); rotate(1, 0, 0); rotate(0, 0, -1); }
            else if (pitch == 1U) { rotate(0, -1, 0); rotate(-1, 0, 0); rotate(0, 0, -1); }
            else rotate(0, 0, 1);
            break;
        }
    }
}

} // namespace battlespades::world
