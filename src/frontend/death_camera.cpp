#include "battlespades/frontend/death_camera.hpp"

#include "battlespades/render/camera_basis.hpp"
#include "battlespades/shared/retail_constants.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace battlespades::frontend {
namespace {

constexpr double grave_camera_range{
    static_cast<double>(retail::DEATHCAM_RANGE_FROM_KILLER)};
constexpr double chase_available_time{
    retail::DEATHCAM_TIME_TILL_CHASE_CAM_AVAIL};
constexpr double chase_forced_time{
    static_cast<double>(retail::DEATHCAM_TIME_TILL_CHASE_CAM_FORCED)};
constexpr double chase_mouse_threshold{8.0};
constexpr double focus_height{0.9};
constexpr double pitch_limit{75.0};

[[nodiscard]] double wrap_degrees(double value) noexcept {
    value = std::fmod(value + 180.0, 360.0);
    if (value < 0.0) value += 360.0;
    return value - 180.0;
}

[[nodiscard]] double orientation_yaw(const world::Vec3& orientation) noexcept {
    if (!std::isfinite(orientation.x) || !std::isfinite(orientation.y) ||
        std::hypot(orientation.x, orientation.y) <= 1.0e-9) {
        return 0.0;
    }
    return std::atan2(-orientation.y, -orientation.x) *
           180.0 / std::numbers::pi;
}

[[nodiscard]] double nearest_grave_front_yaw(double yaw) noexcept {
    const auto positive = std::abs(wrap_degrees(90.0 - yaw));
    const auto negative = std::abs(wrap_degrees(-90.0 - yaw));
    return positive <= negative ? 90.0 : -90.0;
}

[[nodiscard]] DeathCameraPose orbit_pose(world::Vec3 anchor, double yaw,
                                         double pitch) noexcept {
    // Player/entity positions sit at ground/body origin in the z-down map.
    // Looking at a point 0.9 blocks above it frames the complete gravestone
    // instead of staring at the surface voxel under it.
    const world::Vec3 focus{anchor.x, anchor.y, anchor.z - focus_height};
    const auto basis = render::world_camera_basis(yaw, pitch);
    return {
        {focus.x - basis.forward[0U] * grave_camera_range,
         focus.y - basis.forward[1U] * grave_camera_range,
         focus.z - basis.forward[2U] * grave_camera_range},
        yaw,
        pitch,
    };
}

} // namespace

void DeathCameraController::begin_death(
    world::Vec3 fallback_anchor, double yaw_degrees,
    std::optional<DeathCameraTarget> killer, bool deathcam_enabled) noexcept {
    fallback_anchor_ = fallback_anchor;
    grave_position_ = fallback_anchor;
    grave_entity_id_.reset();
    chase_target_ = std::move(killer);
    elapsed_ = 0.0;
    mouse_movement_ = 0.0;
    deathcam_enabled_ = deathcam_enabled;
    // Retail keeps the killer position specifically for the initial death
    // view. Start on that side of the grave, looking back at it. Self/world
    // deaths have no distinct killer, so choose the nearest front/back normal
    // of grave.kv6 instead of allowing its four-voxel edge to fill the view.
    if (killer.has_value()) {
        const world::Vec3 toward_grave{
            fallback_anchor.x - killer->position.x,
            fallback_anchor.y - killer->position.y,
            fallback_anchor.z - killer->position.z};
        orbit_yaw_ = orientation_yaw(toward_grave);
    } else {
        orbit_yaw_ = nearest_grave_front_yaw(yaw_degrees);
    }
    orbit_pitch_ = 15.0;
    mode_ = DeathCameraMode::grave;
}

void DeathCameraController::enter_spectator(
    world::Vec3 fallback_anchor, double yaw_degrees,
    std::optional<DeathCameraTarget> target) noexcept {
    fallback_anchor_ = fallback_anchor;
    grave_position_ = fallback_anchor;
    grave_entity_id_.reset();
    chase_target_ = std::move(target);
    elapsed_ = chase_available_time;
    mouse_movement_ = 0.0;
    deathcam_enabled_ = false;
    orbit_yaw_ = wrap_degrees(yaw_degrees);
    orbit_pitch_ = 12.0;
    if (chase_target_.has_value()) {
        seed_orbit_from_target(*chase_target_);
        mode_ = DeathCameraMode::chase;
    } else {
        mode_ = DeathCameraMode::spectator_free;
    }
}

void DeathCameraController::end_life() noexcept {
    mode_ = DeathCameraMode::inactive;
    grave_entity_id_.reset();
    chase_target_.reset();
    elapsed_ = 0.0;
    mouse_movement_ = 0.0;
}

void DeathCameraController::bind_grave(std::uint64_t entity_id,
                                       world::Vec3 position) noexcept {
    grave_entity_id_ = entity_id;
    grave_position_ = position;
}

void DeathCameraController::update_grave(std::uint64_t entity_id,
                                         world::Vec3 position) noexcept {
    if (grave_entity_id_ == entity_id) {
        grave_position_ = position;
    }
}

void DeathCameraController::set_chase_target(
    std::optional<DeathCameraTarget> target) noexcept {
    const auto previous = chase_target_.has_value()
                              ? std::optional<std::uint8_t>{
                                    chase_target_->player_id}
                              : std::nullopt;
    chase_target_ = std::move(target);
    if (!chase_target_.has_value()) {
        if (mode_ == DeathCameraMode::chase) {
            mode_ = grave_entity_id_.has_value()
                        ? DeathCameraMode::grave
                        : DeathCameraMode::spectator_free;
        }
        return;
    }
    if (mode_ == DeathCameraMode::spectator_free) {
        seed_orbit_from_target(*chase_target_);
        mode_ = DeathCameraMode::chase;
    } else if (mode_ == DeathCameraMode::chase &&
               previous != chase_target_->player_id) {
        seed_orbit_from_target(*chase_target_);
    }
}

void DeathCameraController::request_chase() noexcept {
    if (chase_available()) {
        switch_to_chase_if_possible();
    }
}

void DeathCameraController::tick(double dt) noexcept {
    if (!active() || !std::isfinite(dt) || dt <= 0.0) return;
    elapsed_ += dt;
    if (mode_ == DeathCameraMode::grave &&
        elapsed_ >= (deathcam_enabled_ ? chase_forced_time
                                      : chase_available_time)) {
        switch_to_chase_if_possible();
    }
}

void DeathCameraController::on_mouse_move(double delta_x, double delta_y,
                                          double degrees_per_count) noexcept {
    if (!active() || !std::isfinite(delta_x) || !std::isfinite(delta_y) ||
        !std::isfinite(degrees_per_count)) {
        return;
    }
    orbit_yaw_ =
        wrap_degrees(orbit_yaw_ + delta_x * degrees_per_count);
    orbit_pitch_ = std::clamp(
        orbit_pitch_ + delta_y * degrees_per_count, -pitch_limit, pitch_limit);
    if (mode_ != DeathCameraMode::grave || !chase_available()) return;
    mouse_movement_ += std::abs(delta_x) + std::abs(delta_y);
    if (mouse_movement_ >= chase_mouse_threshold) {
        switch_to_chase_if_possible();
    }
}

void DeathCameraController::on_mouse_press() noexcept {
    request_chase();
}

bool DeathCameraController::active() const noexcept {
    return mode_ != DeathCameraMode::inactive;
}

DeathCameraMode DeathCameraController::mode() const noexcept {
    return mode_;
}

bool DeathCameraController::chase_available() const noexcept {
    return active() && chase_target_.has_value() &&
           elapsed_ >= chase_available_time;
}

std::optional<std::uint8_t>
DeathCameraController::chase_player_id() const noexcept {
    return chase_target_.has_value()
               ? std::optional<std::uint8_t>{chase_target_->player_id}
               : std::nullopt;
}

std::optional<std::uint64_t>
DeathCameraController::grave_entity_id() const noexcept {
    return grave_entity_id_;
}

DeathCameraPose DeathCameraController::pose() const noexcept {
    if (mode_ == DeathCameraMode::chase && chase_target_.has_value()) {
        return orbit_pose(chase_target_->position, orbit_yaw_, orbit_pitch_);
    }
    if (mode_ == DeathCameraMode::grave) {
        return orbit_pose(grave_entity_id_.has_value() ? grave_position_
                                                       : fallback_anchor_,
                          orbit_yaw_, orbit_pitch_);
    }
    return {fallback_anchor_, orbit_yaw_, orbit_pitch_};
}

void DeathCameraController::switch_to_chase_if_possible() noexcept {
    if (!chase_target_.has_value()) return;
    seed_orbit_from_target(*chase_target_);
    mode_ = DeathCameraMode::chase;
}

void DeathCameraController::seed_orbit_from_target(
    const DeathCameraTarget& target) noexcept {
    orbit_yaw_ = wrap_degrees(orientation_yaw(target.orientation));
    orbit_pitch_ = 12.0;
}

} // namespace battlespades::frontend
