#pragma once

#include <array>
#include <cstdint>

namespace battlespades::world {

using ProjectilePresentationMatrix = std::array<float, 16U>;

/**
 * Builds the retail projectile model transform in canonical map space.
 *
 * KV6 loading converts authored (x,y,z) to render (x,-z,y), and the shipped
 * rocket's hot exhaust occupies authored y=37..41, making its nose local -Z.
 * This function owns that otherwise easy-to-regress basis correction plus the
 * recovered display position offset of -0.5 on every axis.
 */
[[nodiscard]] ProjectilePresentationMatrix projectile_presentation_transform(
    std::array<double, 3U> position, std::array<double, 3U> velocity,
    double roll_degrees, float model_scale) noexcept;

/** Transforms and normalizes a model-space direction without translation. */
[[nodiscard]] std::array<float, 3U> projectile_presentation_direction(
    const ProjectilePresentationMatrix& transform,
    std::array<float, 3U> model_direction) noexcept;

/** Transforms one model-space point through the rendered projectile matrix. */
[[nodiscard]] std::array<float, 3U> projectile_presentation_point(
    const ProjectilePresentationMatrix& transform,
    std::array<float, 3U> model_point) noexcept;

/**
 * World-space centre of the shipped rocket's hot exhaust voxels.
 *
 * The smoke emitter must consume this point rather than the raw network
 * anchor; otherwise model rotation leaves the plume visibly detached.
 */
[[nodiscard]] std::array<float, 3U> projectile_exhaust_position(
    std::array<double, 3U> position, std::array<double, 3U> velocity,
    double roll_degrees, float model_scale) noexcept;

/**
 * Packet anchor used while an owning first-person rocket exits its launcher.
 *
 * Retail sends the authoritative world-object position, not a fabricated
 * muzzle position. The local presentation nevertheless needs to begin at the
 * visible FPS barrel. `spawn_position` is that untouched packet origin and
 * `position` is the currently integrated authoritative projectile. Tools
 * other than RPG/RPG2/UGC-RPG2 are returned unchanged.
 */
[[nodiscard]] std::array<double, 3U> local_rocket_presentation_anchor(
    std::uint8_t tool_id,
    std::array<double, 3U> spawn_position,
    std::array<double, 3U> position,
    std::array<double, 3U> velocity,
    double seconds_since_spawn) noexcept;

} // namespace battlespades::world
