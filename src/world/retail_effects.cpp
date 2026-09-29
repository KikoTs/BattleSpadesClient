#include "battlespades/world/retail_effects.hpp"

#include <algorithm>
#include <cmath>

namespace battlespades::world {
namespace {

using Rgb = std::array<float, 3U>;

[[nodiscard]] constexpr Rgb rgb(float red, float green, float blue) noexcept {
    return {red / 255.0F, green / 255.0F, blue / 255.0F};
}

[[nodiscard]] Rgb lerp(const Rgb& from, const Rgb& to, float t) noexcept {
    return {from[0U] + (to[0U] - from[0U]) * t, from[1U] + (to[1U] - from[1U]) * t,
            from[2U] + (to[2U] - from[2U]) * t};
}

/** Hot at a full fuse, mid at half, cold at zero. */
[[nodiscard]] Rgb three_stop_ramp(double fuse, const Rgb& hot, const Rgb& mid, const Rgb& cold) {
    const double ratio =
        std::isfinite(fuse) ? std::clamp(fuse / block_patch_max_lifespan, 0.0, 1.0) : 0.0;
    const auto t = static_cast<float>(ratio);
    if (t >= 0.5F) {
        return lerp(mid, hot, (t - 0.5F) * 2.0F);
    }
    return lerp(cold, mid, t * 2.0F);
}

[[nodiscard]] std::uint32_t mix(std::uint32_t value) noexcept {
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    return value ^ (value >> 16U);
}

} // namespace

std::array<float, 3U> block_fire_colour(double fuse) noexcept {
    return three_stop_ramp(fuse, rgb(255.0F, 255.0F, 255.0F), rgb(255.0F, 255.0F, 0.0F),
                           rgb(255.0F, 0.0F, 0.0F));
}

std::array<float, 3U> block_goo_colour(double fuse) noexcept {
    return three_stop_ramp(fuse, rgb(255.0F, 255.0F, 255.0F), rgb(20.0F, 255.0F, 50.0F),
                           rgb(0.0F, 255.0F, 0.0F));
}

std::optional<VoxelCell> surface_patch_voxel(const VxlMap& map, Vec3 anchor) noexcept {
    if (!std::isfinite(anchor.x) || !std::isfinite(anchor.y) || !std::isfinite(anchor.z)) {
        return std::nullopt;
    }
    // The anchor sits 0.01 outside one face; probe that cell first, then the
    // six cells 0.05 away so whichever face was chosen resolves to its voxel.
    constexpr double probe{0.05};
    const std::array<std::array<double, 3U>, 7U> offsets{{{0.0, 0.0, 0.0},
                                                          {0.0, 0.0, probe},
                                                          {-probe, 0.0, 0.0},
                                                          {probe, 0.0, 0.0},
                                                          {0.0, -probe, 0.0},
                                                          {0.0, probe, 0.0},
                                                          {0.0, 0.0, -probe}}};
    for (const auto& offset : offsets) {
        const double x = std::floor(anchor.x + offset[0U]);
        const double y = std::floor(anchor.y + offset[1U]);
        const double z = std::floor(anchor.z + offset[2U]);
        if (x < 0.0 || y < 0.0 || z < 0.0 || x >= static_cast<double>(VxlMap::width) ||
            y >= static_cast<double>(VxlMap::depth) || z >= static_cast<double>(VxlMap::height)) {
            continue;
        }
        const VoxelCell cell{static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                             static_cast<std::uint32_t>(z)};
        if (map.solid(cell.x, cell.y, cell.z)) {
            return cell;
        }
    }
    return std::nullopt;
}

SetHpFeedback set_hp_feedback(std::uint8_t damage_type,
                              int previous_health,
                              int health) noexcept {
    switch (damage_type) {
    case 1U:
        return SetHpFeedback::hit;
    case 2U:
        return health > previous_health ? SetHpFeedback::heal : SetHpFeedback::none;
    case 3U:
        return SetHpFeedback::burn;
    case 4U:
        return SetHpFeedback::sudden_death;
    default:
        return SetHpFeedback::none;
    }
}

StatusTints status_tints(double burn_remaining, double sudden_death_remaining,
                         std::array<std::uint8_t, 3U> team_color) noexcept {
    const auto alpha = [](double remaining, double duration) -> std::optional<std::uint8_t> {
        if (!std::isfinite(remaining) || remaining <= 0.0) return std::nullopt;
        return static_cast<std::uint8_t>(
            std::clamp(remaining / duration * 255.0, 0.0, 255.0));
    };
    StatusTints tints;
    if (const auto burn = alpha(burn_remaining, burn_indicator_time); burn.has_value()) {
        tints.burn = StatusTint{{255U, 0U, 0U}, *burn};
    }
    if (const auto sudden = alpha(sudden_death_remaining, sudden_death_indicator_time);
        sudden.has_value()) {
        tints.sudden_death = StatusTint{team_color, *sudden};
    }
    return tints;
}

CharacterStatusEdges character_status_edges(bool was_on_fire,
                                            bool on_fire,
                                            bool was_touching_goo,
                                            bool touching_goo) noexcept {
    return {!was_on_fire && on_fire, was_on_fire && !on_fire,
            !was_touching_goo && touching_goo, was_touching_goo && !touching_goo};
}

bool character_in_water(const Vec3& position) noexcept {
    // The movement core's wade test: the origin below the z=237 water plane.
    return position.z > 237.0;
}

std::optional<TracerState> make_tracer(std::array<float, 3U> muzzle,
                                       std::array<float, 3U> target,
                                       std::uint8_t tool_id) noexcept {
    const std::array<float, 3U> delta{target[0U] - muzzle[0U], target[1U] - muzzle[1U],
                                      target[2U] - muzzle[2U]};
    const float length =
        std::sqrt(delta[0U] * delta[0U] + delta[1U] * delta[1U] + delta[2U] * delta[2U]);
    if (!std::isfinite(length) || length < 0.01F) {
        return std::nullopt;
    }
    TracerState tracer;
    tracer.position = muzzle;
    tracer.direction = {delta[0U] / length, delta[1U] / length, delta[2U] / length};
    tracer.remaining = length;
    tracer.tool_id = tool_id;
    return tracer;
}

bool advance_tracer(TracerState& tracer, float dt) noexcept {
    if (!std::isfinite(dt) || dt <= 0.0F) {
        return tracer.remaining > 0.0F;
    }
    const float travel = std::min(weapon_tracer_speed * dt, tracer.remaining);
    for (std::size_t axis{}; axis < 3U; ++axis) {
        tracer.position[axis] += tracer.direction[axis] * travel;
    }
    tracer.remaining -= travel;
    return tracer.remaining > 0.0F;
}

std::optional<TracerLaunch> plan_tracer_launch(bool retail_look, std::array<float, 3U> eye,
                                               std::array<float, 3U> direction,
                                               std::optional<std::array<float, 3U>> muzzle,
                                               std::array<float, 3U> target) noexcept {
    const float length = std::sqrt(direction[0U] * direction[0U] + direction[1U] * direction[1U] +
                                   direction[2U] * direction[2U]);
    if (!std::isfinite(length) || length < 1.0e-6F) {
        return std::nullopt;
    }
    const std::array<float, 3U> forward{direction[0U] / length, direction[1U] / length,
                                        direction[2U] / length};
    const auto along = [&](std::array<float, 3U> point) {
        return (point[0U] - eye[0U]) * forward[0U] + (point[1U] - eye[1U]) * forward[1U] +
               (point[2U] - eye[2U]) * forward[2U];
    };
    const float target_distance = along(target);
    if (!std::isfinite(target_distance)) {
        return std::nullopt;
    }
    if (retail_look || !muzzle.has_value()) {
        // Tracer.update(1) deletes the tracer when the hit is inside the step.
        if (target_distance <= retail_tracer_prestep) {
            return std::nullopt;
        }
        TracerLaunch launch;
        for (std::size_t axis{}; axis < 3U; ++axis) {
            launch.start[axis] = eye[axis] + forward[axis] * retail_tracer_prestep;
        }
        launch.end = target;
        return launch;
    }
    for (const float value : *muzzle) {
        if (!std::isfinite(value)) {
            return std::nullopt;
        }
    }
    // A tracer must fly away from the shooter; skip it when the contact is
    // no further down the aim line than the muzzle itself.
    if (target_distance <= along(*muzzle) + 0.05F) {
        return std::nullopt;
    }
    return TracerLaunch{*muzzle, target};
}

float muzzle_flash_duration_for(std::uint8_t tool_id) noexcept {
    switch (tool_id) {
    case 15U: // MGWeapon
    case 35U: // TommyGunWeapon
    case 38U: // ClassicSmgWeapon
        return 0.01F;
    default:
        return muzzle_flash_duration;
    }
}

std::array<float, 16U> muzzle_flash_world_transform(std::array<float, 3U> position,
                                                     std::array<float, 3U> forward,
                                                     float roll_degrees,
                                                     float scale) noexcept {
    const auto normalize = [](std::array<float, 3U> value) {
        const float length =
            std::sqrt(value[0U] * value[0U] + value[1U] * value[1U] + value[2U] * value[2U]);
        if (!std::isfinite(length) || length < 1.0e-6F) {
            return std::array<float, 3U>{0.0F, 1.0F, 0.0F};
        }
        return std::array<float, 3U>{value[0U] / length, value[1U] / length, value[2U] / length};
    };
    const auto cross = [](std::array<float, 3U> a, std::array<float, 3U> b) {
        return std::array<float, 3U>{a[1U] * b[2U] - a[2U] * b[1U], a[2U] * b[0U] - a[0U] * b[2U],
                                     a[0U] * b[1U] - a[1U] * b[0U]};
    };
    const auto f = normalize(forward);
    // Map z grows downward; any non-parallel reference works for the basis.
    const std::array<float, 3U> reference =
        std::abs(f[2U]) > 0.95F ? std::array<float, 3U>{1.0F, 0.0F, 0.0F}
                                : std::array<float, 3U>{0.0F, 0.0F, -1.0F};
    const auto right = normalize(cross(f, reference));
    const auto up = normalize(cross(right, f));
    const float radians = roll_degrees * 0.01745329251994329577F;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    const std::array<float, 3U> rolled_right{right[0U] * c + up[0U] * s,
                                             right[1U] * c + up[1U] * s,
                                             right[2U] * c + up[2U] * s};
    const std::array<float, 3U> rolled_up{up[0U] * c - right[0U] * s, up[1U] * c - right[1U] * s,
                                          up[2U] * c - right[2U] * s};
    // Row-vector layout (translation at [12..14]); the flash model display
    // z axis (the barrel axis of the weapon offsets) follows `forward`.
    return {rolled_right[0U] * scale,
            rolled_right[1U] * scale,
            rolled_right[2U] * scale,
            0.0F,
            rolled_up[0U] * scale,
            rolled_up[1U] * scale,
            rolled_up[2U] * scale,
            0.0F,
            f[0U] * scale,
            f[1U] * scale,
            f[2U] * scale,
            0.0F,
            position[0U],
            position[1U],
            position[2U],
            1.0F};
}

float muzzle_flash_roll_degrees(std::uint32_t seed) noexcept {
    return static_cast<float>(mix(seed) % 360U);
}

CrateDropStep step_crate_drop(double& z,
                              double& velocity_z,
                              double support_z,
                              CrateDropState& state) noexcept {
    CrateDropStep result;
    if (!state.falling) {
        return result;
    }
    velocity_z += crate_drop_gravity * crate_drop_step;
    z += velocity_z * crate_drop_step;
    if (z >= support_z) {
        result.landed = true;
        result.impact_speed = velocity_z;
        z = support_z;
        return result;
    }
    const double distance = support_z - z;
    if (!state.parachute_deployed && distance < crate_parachute_deployment_height) {
        state.parachute_deployed = true;
        result.chute_opened = true;
    }
    if (state.parachute_deployed && !state.parachute_removed &&
        distance < crate_parachute_removal_height) {
        state.parachute_removed = true;
        result.chute_released = true;
    }
    if (state.parachute_deployed && !state.parachute_removed) {
        velocity_z *= crate_parachute_slowdown;
    }
    return result;
}

} // namespace battlespades::world
