#include "battlespades/world/projectile_presentation.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <ranges>

namespace battlespades::world {
namespace {

[[nodiscard]] ProjectilePresentationMatrix identity() noexcept {
    return {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
}

[[nodiscard]] ProjectilePresentationMatrix multiply(
    const ProjectilePresentationMatrix& left,
    const ProjectilePresentationMatrix& right) noexcept {
    ProjectilePresentationMatrix result{};
    for (std::size_t row{}; row < 4U; ++row) {
        for (std::size_t column{}; column < 4U; ++column) {
            for (std::size_t inner{}; inner < 4U; ++inner) {
                result[row * 4U + column] +=
                    left[row * 4U + inner] * right[inner * 4U + column];
            }
        }
    }
    return result;
}

[[nodiscard]] ProjectilePresentationMatrix scale(float value) noexcept {
    auto result = identity();
    result[0U] = value;
    result[5U] = value;
    result[10U] = value;
    return result;
}

[[nodiscard]] ProjectilePresentationMatrix translate(float x, float y,
                                                      float z) noexcept {
    auto result = identity();
    result[12U] = x;
    result[13U] = y;
    result[14U] = z;
    return result;
}

[[nodiscard]] ProjectilePresentationMatrix rotate_x(float degrees) noexcept {
    const float angle =
        degrees * static_cast<float>(std::numbers::pi / 180.0);
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    auto result = identity();
    result[5U] = cosine;
    result[6U] = sine;
    result[9U] = -sine;
    result[10U] = cosine;
    return result;
}

[[nodiscard]] ProjectilePresentationMatrix rotate_y(float degrees) noexcept {
    const float angle =
        degrees * static_cast<float>(std::numbers::pi / 180.0);
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    auto result = identity();
    result[0U] = cosine;
    result[2U] = -sine;
    result[8U] = sine;
    result[10U] = cosine;
    return result;
}

[[nodiscard]] ProjectilePresentationMatrix rotate_z(float degrees) noexcept {
    const float angle =
        degrees * static_cast<float>(std::numbers::pi / 180.0);
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    auto result = identity();
    result[0U] = cosine;
    result[1U] = sine;
    result[4U] = -sine;
    result[5U] = cosine;
    return result;
}

} // namespace

ProjectilePresentationMatrix projectile_presentation_transform(
    std::array<double, 3U> position, std::array<double, 3U> velocity,
    double roll_degrees, float model_scale) noexcept {
    const double horizontal = std::hypot(velocity[0U], velocity[1U]);
    const float yaw = static_cast<float>(
        std::atan2(-velocity[1U], -velocity[0U]) * 180.0 /
        std::numbers::pi);
    const float pitch = static_cast<float>(
        std::atan2(velocity[2U], horizontal) * 180.0 / std::numbers::pi);

    auto model = scale(model_scale);
    model = multiply(model, rotate_x(-90.0F));
    // Authored nose is -Z after the KV6 asset-boundary conversion.
    model = multiply(model, rotate_z(-90.0F));
    if (roll_degrees != 0.0) {
        model = multiply(model, rotate_x(static_cast<float>(roll_degrees)));
    }
    model = multiply(model, rotate_y(pitch));
    model = multiply(model, rotate_z(yaw));
    return multiply(
        model,
        translate(static_cast<float>(position[0U] - 0.5),
                  static_cast<float>(position[1U] - 0.5),
                  static_cast<float>(position[2U] - 0.5)));
}

std::array<float, 3U> projectile_presentation_direction(
    const ProjectilePresentationMatrix& transform,
    std::array<float, 3U> model_direction) noexcept {
    std::array<float, 3U> result{
        model_direction[0U] * transform[0U] +
            model_direction[1U] * transform[4U] +
            model_direction[2U] * transform[8U],
        model_direction[0U] * transform[1U] +
            model_direction[1U] * transform[5U] +
            model_direction[2U] * transform[9U],
        model_direction[0U] * transform[2U] +
            model_direction[1U] * transform[6U] +
            model_direction[2U] * transform[10U]};
    const float length =
        std::sqrt(result[0U] * result[0U] + result[1U] * result[1U] +
                  result[2U] * result[2U]);
    if (length <= 0.000001F) {
        return {};
    }
    for (float& axis : result) {
        axis /= length;
    }
    return result;
}

std::array<float, 3U> projectile_presentation_point(
    const ProjectilePresentationMatrix& transform,
    std::array<float, 3U> model_point) noexcept {
    return {
        model_point[0U] * transform[0U] + model_point[1U] * transform[4U] +
            model_point[2U] * transform[8U] + transform[12U],
        model_point[0U] * transform[1U] + model_point[1U] * transform[5U] +
            model_point[2U] * transform[9U] + transform[13U],
        model_point[0U] * transform[2U] + model_point[1U] * transform[6U] +
            model_point[2U] * transform[10U] + transform[14U],
    };
}

std::array<float, 3U> projectile_exhaust_position(
    std::array<double, 3U> position, std::array<double, 3U> velocity,
    double roll_degrees, float model_scale) noexcept {
    const auto transform = projectile_presentation_transform(
        position, velocity, roll_degrees, model_scale);
    // rocket.kv6/rocket2.kv6: authored Y size 42, pivot Y=22 and the hot
    // exhaust end is Y=41. Asset conversion makes the tail local +Z, 19
    // voxels behind the pivot. Deriving the point from the render matrix keeps
    // it attached over yaw, pitch and roll instead of guessing a world offset.
    return projectile_presentation_point(transform, {0.0F, 0.0F, 19.0F});
}

std::array<double, 3U> local_rocket_presentation_anchor(
    std::uint8_t tool_id,
    std::array<double, 3U> spawn_position,
    std::array<double, 3U> position,
    std::array<double, 3U> velocity,
    double seconds_since_spawn) noexcept {
    // rpgWeapon.py/rpg2Weapon.py send world_object.position verbatim. These
    // offsets are instead the recovered view-space centres of each launcher
    // mouth: KV6 tip centre, authored pivot, 0.065 view scale, the tool's
    // (0,.1,0) initial position and Character.draw_fps (-.4,-.55,.9) root.
    // Values are (right, up, forward) in camera units.
    std::array<double, 3U> muzzle{};
    switch (tool_id) {
    case 12U:
        muzzle = {0.4, -0.2875, 1.7775};
        break;
    case 13U:
    case 46U:
        muzzle = {0.4975, -0.385, 1.9075};
        break;
    default:
        return position;
    }
    if (!std::isfinite(seconds_since_spawn) ||
        !std::ranges::all_of(spawn_position, [](double value) { return std::isfinite(value); }) ||
        !std::ranges::all_of(position, [](double value) { return std::isfinite(value); }) ||
        !std::ranges::all_of(velocity, [](double value) { return std::isfinite(value); })) {
        return position;
    }

    const double speed = std::sqrt(velocity[0U] * velocity[0U] +
                                   velocity[1U] * velocity[1U] +
                                   velocity[2U] * velocity[2U]);
    if (speed <= 1.0e-9) {
        return position;
    }
    const std::array<double, 3U> forward{
        velocity[0U] / speed, velocity[1U] / speed, velocity[2U] / speed};
    const double horizontal = std::hypot(forward[0U], forward[1U]);
    const std::array<double, 3U> right =
        horizontal > 1.0e-9
            ? std::array<double, 3U>{-forward[1U] / horizontal,
                                     forward[0U] / horizontal, 0.0}
            : std::array<double, 3U>{1.0, 0.0, 0.0};
    const std::array<double, 3U> up{
        right[1U] * forward[2U] - right[2U] * forward[1U],
        right[2U] * forward[0U] - right[0U] * forward[2U],
        right[0U] * forward[1U] - right[1U] * forward[0U]};

    std::array<double, 3U> muzzle_anchor{};
    for (std::size_t axis{}; axis < muzzle_anchor.size(); ++axis) {
        const double centre = spawn_position[axis] +
                              right[axis] * muzzle[0U] +
                              up[axis] * muzzle[1U] +
                              forward[axis] * muzzle[2U];
        // projectile_presentation_transform applies retail's (-.5,-.5,-.5)
        // display offset. Counter it only at the authored muzzle; interpolation
        // restores the untouched authoritative transform on exit.
        muzzle_anchor[axis] = centre + 0.5;
    }

    constexpr double exit_time{0.05};
    const double linear = std::clamp(seconds_since_spawn / exit_time, 0.0, 1.0);
    const double blend = linear * linear * (3.0 - 2.0 * linear);
    std::array<double, 3U> result{};
    for (std::size_t axis{}; axis < result.size(); ++axis) {
        result[axis] = muzzle_anchor[axis] +
                       (position[axis] - muzzle_anchor[axis]) * blend;
    }
    return result;
}

} // namespace battlespades::world
