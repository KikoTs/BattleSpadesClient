#pragma once

#include <array>
#include <cmath>
#include <numbers>

namespace battlespades::render {

/**
 * Orthonormal first-person camera basis in canonical map coordinates.
 *
 * The retail look model drives it: yaw 0 faces -x (the tutorial spawn),
 * pitch positive looks down (+z), world-up is (0,0,-1) and
 * forward = (-cos yaw * cos pitch, -sin yaw * cos pitch, sin pitch).
 * `right` is derived so it equals the recovered movement strafe vector
 * s = (-oy, ox) of the horizontal orientation — the one invariant that keeps
 * the rendered image and A/D strafing agreeing about which way is right.
 * Header-only and bgfx-free so simulation and renderer share one definition
 * and headless tests can pin it.
 */
struct WorldCameraBasis final {
    std::array<double, 3U> forward{};
    std::array<double, 3U> right{};
    std::array<double, 3U> up{};
};

[[nodiscard]] inline WorldCameraBasis world_camera_basis(double yaw_degrees,
                                                         double pitch_degrees) noexcept {
    constexpr double to_radians = std::numbers::pi / 180.0;
    const double yaw = yaw_degrees * to_radians;
    const double pitch = pitch_degrees * to_radians;
    WorldCameraBasis basis;
    basis.forward = {-std::cos(yaw) * std::cos(pitch),
                     -std::sin(yaw) * std::cos(pitch),
                     std::sin(pitch)};
    // Horizontal strafe direction: s = (-oy, ox) of the facing projection,
    // independent of pitch.
    basis.right = {std::sin(yaw), -std::cos(yaw), 0.0};
    // up = right x forward (right-handed) keeps the horizon level and points
    // toward the sky (-z) at zero pitch.
    basis.up = {
        basis.right[1U] * basis.forward[2U] - basis.right[2U] * basis.forward[1U],
        basis.right[2U] * basis.forward[0U] - basis.right[0U] * basis.forward[2U],
        basis.right[0U] * basis.forward[1U] - basis.right[1U] * basis.forward[0U],
    };
    return basis;
}

} // namespace battlespades::render
