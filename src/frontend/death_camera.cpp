#include "battlespades/frontend/death_camera.hpp"

#include "battlespades/render/camera_basis.hpp"
#include "battlespades/shared/retail_constants.hpp"
#include "battlespades/world/voxel_raycast.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace battlespades::frontend {
namespace {

// gameScene.pyd constants (retail_constants.txt DEATHCAM_*).
constexpr double killer_range{static_cast<double>(retail::DEATHCAM_RANGE_FROM_KILLER)};
constexpr double chase_available_time{retail::DEATHCAM_TIME_TILL_CHASE_CAM_AVAIL};
constexpr double chase_forced_time{
    static_cast<double>(retail::DEATHCAM_TIME_TILL_CHASE_CAM_FORCED)};
constexpr double position_change_time{retail::DEATHCAM_TIME_TILL_POSITION_CHANGE};
constexpr double angle_lerp{static_cast<double>(retail::DEATHCAM_ANGLE_LERP_SPEED)};
constexpr double position_lerp{static_cast<double>(retail::DEATHCAM_POSITION_LERP_SPEED)};
constexpr std::uint32_t streak_for_orientation{
    static_cast<std::uint32_t>(retail::DEATHCAM_STREAK_FOR_ORIENTATION)};
constexpr std::uint32_t streak_for_position{
    static_cast<std::uint32_t>(retail::DEATHCAM_STREAK_FOR_POSITION)};
// DeathController.mouse_movement_to_chase_cam class attribute (0x101b2e5d).
constexpr double mouse_movement_to_chase_cam{100.0};
// ChaseController.validate_position: I5 range and sqrt(3)/2 cell half-diagonal.
constexpr double chase_distance{5.0};
// Camera.add_mouse_motion clamps r_x to +-89.9.
constexpr double pitch_limit{89.9};
// flyController.py: FLYCAMERA_TRAVEL_SPEED 30.0, SPEED_NORMALIZE 10, 0.7 vertical.
constexpr double fly_travel_speed{30.0};
constexpr double fly_speed_normalize{10.0};
constexpr double fly_vertical_factor{0.7};
// A2212/A2213 map extent, A2214 fly floor, A2215 chase z bound.
constexpr double map_x_extent{512.0};
constexpr double map_y_extent{512.0};
constexpr double fly_z_extent{240.0};
constexpr double chase_z_extent{238.0};
constexpr double retail_tick_hz{60.0};

[[nodiscard]] double wrap_degrees(double value) noexcept {
    value = std::fmod(value + 180.0, 360.0);
    if (value < 0.0) value += 360.0;
    return value - 180.0;
}

/** common.interpolate(old, new, div) applied for dt worth of 60 Hz ticks. */
[[nodiscard]] double interpolate(double old_value, double new_value, double div,
                                 double dt) noexcept {
    const double ticks = std::max(0.0, dt * retail_tick_hz);
    const double retain = std::pow(1.0 - 1.0 / div, ticks);
    return new_value + (old_value - new_value) * retain;
}

/** common.interpolate_angle: shortest way round, same per-tick fraction. */
[[nodiscard]] double interpolate_angle(double old_value, double new_value, double div,
                                       double dt) noexcept {
    const double diff = wrap_degrees(new_value - old_value);
    const double ticks = std::max(0.0, dt * retail_tick_hz);
    const double retain = std::pow(1.0 - 1.0 / div, ticks);
    return wrap_degrees(old_value + diff * (1.0 - retain));
}

[[nodiscard]] double length(world::Vec3 value) noexcept {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

} // namespace

bool deathcam_valid_kill_type(std::uint8_t kill_type) noexcept {
    return kill_type <= 6U || (kill_type >= 21U && kill_type <= 24U);
}

world::Vec3 chase_camera_eye(const world::VxlMap* map, world::Vec3 focus, double yaw_degrees,
                             double pitch_degrees) noexcept {
    const auto basis = render::world_camera_basis(yaw_degrees, pitch_degrees);
    const world::Vec3 back{-basis.forward[0U], -basis.forward[1U], -basis.forward[2U]};
    double distance = chase_distance;
    if (map != nullptr) {
        const double half_diagonal = std::sqrt(3.0) * 0.5;
        const auto hit = world::trace_first_solid(
            *map,
            {static_cast<float>(focus.x), static_cast<float>(focus.y),
             static_cast<float>(focus.z)},
            {static_cast<float>(back.x), static_cast<float>(back.y), static_cast<float>(back.z)},
            static_cast<float>(chase_distance + half_diagonal + 1.0));
        if (hit.has_value()) {
            const world::Vec3 centre{static_cast<double>(hit->cell.x) + 0.5,
                                     static_cast<double>(hit->cell.y) + 0.5,
                                     static_cast<double>(hit->cell.z) + 0.5};
            const double to_hit = length({centre.x - focus.x, centre.y - focus.y,
                                          centre.z - focus.z});
            if (to_hit < distance + half_diagonal) {
                distance = std::max(to_hit - half_diagonal - 0.5, 0.0);
            }
        }
    }
    world::Vec3 offset{back.x * distance, back.y * distance, back.z * distance};
    // Scale the offset so the eye stays inside the map box; the focus itself
    // is authoritative and never moved.
    double scale = 1.0;
    const std::array<double, 3U> origin{focus.x, focus.y, focus.z};
    const std::array<double, 3U> delta{offset.x, offset.y, offset.z};
    const std::array<double, 3U> bound{map_x_extent, map_y_extent, chase_z_extent};
    for (std::size_t axis{}; axis < 3U; ++axis) {
        const double end = origin[axis] + delta[axis] * scale;
        if (end > bound[axis] && delta[axis] > 0.0) {
            scale = std::max(0.0, (bound[axis] - origin[axis]) / delta[axis]);
        } else if (end < 0.0 && delta[axis] < 0.0) {
            scale = std::max(0.0, (0.0 - origin[axis]) / delta[axis]);
        }
    }
    return {focus.x + offset.x * scale, focus.y + offset.y * scale, focus.z + offset.z * scale};
}

void DeathCameraController::begin_death(world::Vec3 death_eye, double yaw_degrees,
                                        double pitch_degrees,
                                        std::optional<DeathKillerInfo> killer,
                                        bool deathcam_enabled, bool never_respawn) noexcept {
    fallback_anchor_ = death_eye;
    grave_position_ = death_eye;
    grave_entity_id_.reset();
    chase_target_.reset();
    killer_ = killer;
    killer_present_ = killer.has_value();
    elapsed_ = 0.0;
    chase_available_ = false;
    mouse_movement_ = 0.0;
    deathcam_enabled_ = deathcam_enabled;
    // update_dead: activate_controller(..., locked = not self.never_respawn).
    locked_ = !never_respawn;
    spectator_ = false;
    fly_keys_ = {};
    fly_speed_ = 0.0;
    yaw_ = wrap_degrees(yaw_degrees);
    pitch_ = std::clamp(pitch_degrees, -pitch_limit, pitch_limit);
    if (deathcam_enabled_) {
        activate_death_controller();
    } else {
        mode_ = DeathCameraMode::grave;
    }
}

void DeathCameraController::activate_death_controller() noexcept {
    // DeathController.activate: straight to the chase cam unless the killer is
    // known, distinct, has killed us at least twice running, and the kill
    // type is one DEATHCAM_VALID_TYPES frames.
    if (!killer_.has_value() || killer_->streak < streak_for_orientation ||
        !deathcam_valid_kill_type(killer_->kill_type)) {
        mode_ = DeathCameraMode::grave;
        return;
    }
    working_position_ = fallback_anchor_;
    target_position_ = working_position_;
    zoom_possible_ = false;
    mode_ = DeathCameraMode::killer_view;
    set_killer_view(killer_->position);
}

void DeathCameraController::set_killer_view(world::Vec3 killer_position) noexcept {
    const world::Vec3 diff{killer_position.x - working_position_.x,
                           killer_position.y - working_position_.y,
                           killer_position.z - working_position_.z};
    const double distance = length(diff);
    if (distance <= 1.0e-9 || !std::isfinite(distance)) return;
    const world::Vec3 direction{diff.x / distance, diff.y / distance, diff.z / distance};
    target_yaw_ = std::atan2(-direction.y, -direction.x) * 180.0 / std::numbers::pi;
    target_pitch_ = std::clamp(std::asin(std::clamp(direction.z, -1.0, 1.0)) * 180.0 /
                                   std::numbers::pi,
                               -pitch_limit, pitch_limit);
    if (distance > killer_range + 2.0) {
        const double travel = distance - killer_range;
        target_position_ = {working_position_.x + direction.x * travel,
                            working_position_.y + direction.y * travel,
                            working_position_.z + direction.z * travel};
        zoom_possible_ = true;
    } else {
        target_position_ = working_position_;
    }
}

void DeathCameraController::set_killer_info(std::optional<DeathKillerInfo> killer) noexcept {
    if (!killer.has_value() || spectator_) return;
    const bool already_known = killer_.has_value();
    killer_ = killer;
    killer_present_ = true;
    // Retail stores the kill info before the controller activates. A KillAction
    // that trails the SetHp death still owns that first activation, provided
    // the player has not yet been moved on to the chase camera.
    if (!already_known && deathcam_enabled_ && mode_ == DeathCameraMode::grave &&
        elapsed_ < chase_available_time) {
        const double elapsed = elapsed_;
        activate_death_controller();
        elapsed_ = elapsed;
    }
}

void DeathCameraController::set_never_respawn(bool never_respawn) noexcept {
    if (!spectator_) locked_ = !never_respawn;
}

void DeathCameraController::set_killer_position(std::optional<world::Vec3> position) noexcept {
    killer_present_ = position.has_value();
    if (position.has_value() && killer_.has_value()) {
        killer_->position = *position;
    }
}

void DeathCameraController::enter_spectator(world::Vec3 fallback_anchor, double yaw_degrees,
                                            std::optional<DeathCameraTarget> target) noexcept {
    fallback_anchor_ = fallback_anchor;
    grave_position_ = fallback_anchor;
    grave_entity_id_.reset();
    chase_target_ = std::move(target);
    killer_.reset();
    killer_present_ = false;
    elapsed_ = chase_available_time;
    chase_available_ = true;
    mouse_movement_ = 0.0;
    deathcam_enabled_ = false;
    locked_ = false;
    spectator_ = true;
    fly_position_ = fallback_anchor;
    fly_speed_ = 0.0;
    fly_keys_ = {};
    yaw_ = wrap_degrees(yaw_degrees);
    pitch_ = 0.0;
    mode_ = chase_target_.has_value() ? DeathCameraMode::chase : DeathCameraMode::spectator_free;
}

void DeathCameraController::end_life() noexcept {
    mode_ = DeathCameraMode::inactive;
    grave_entity_id_.reset();
    chase_target_.reset();
    killer_.reset();
    killer_present_ = false;
    elapsed_ = 0.0;
    chase_available_ = false;
    mouse_movement_ = 0.0;
    spectator_ = false;
    fly_keys_ = {};
    fly_speed_ = 0.0;
    terrain_ = nullptr;
}

void DeathCameraController::bind_grave(std::uint64_t entity_id, world::Vec3 position) noexcept {
    grave_entity_id_ = entity_id;
    grave_position_ = position;
}

void DeathCameraController::update_grave(std::uint64_t entity_id, world::Vec3 position) noexcept {
    if (grave_entity_id_ == entity_id) {
        grave_position_ = position;
    }
}

void DeathCameraController::set_chase_target(std::optional<DeathCameraTarget> target) noexcept {
    if (mode_ == DeathCameraMode::inactive) return;
    const auto previous_eye = pose().eye;
    chase_target_ = std::move(target);
    if (mode_ == DeathCameraMode::killer_view) {
        // Target selection belongs to the chase controller; the killer view
        // keeps its own eye until it hands over.
        return;
    }
    if (chase_target_.has_value()) {
        mode_ = DeathCameraMode::chase;
    } else if (mode_ == DeathCameraMode::chase) {
        if (spectator_) {
            fly_position_ = previous_eye;
            mode_ = DeathCameraMode::spectator_free;
        } else {
            mode_ = DeathCameraMode::grave;
        }
    }
}

void DeathCameraController::request_chase() noexcept {
    if (mode_ == DeathCameraMode::killer_view && chase_available_) {
        switch_to_chase();
    }
}

void DeathCameraController::switch_to_chase() noexcept {
    // switch_to_chase_cam -> ChaseController on the local player's body; the
    // camera keeps the angles the killer view had turned to.
    mode_ = chase_target_.has_value() ? DeathCameraMode::chase : DeathCameraMode::grave;
}

void DeathCameraController::tick(double dt) noexcept {
    if (!active() || !std::isfinite(dt) || dt <= 0.0) return;
    if (mode_ == DeathCameraMode::spectator_free) {
        tick_fly(dt);
        return;
    }
    if (mode_ != DeathCameraMode::killer_view) {
        elapsed_ += dt;
        return;
    }
    // DeathController.update, in retail order.
    if (elapsed_ > position_change_time && killer_.has_value() &&
        killer_->streak >= streak_for_position && zoom_possible_) {
        working_position_ = {interpolate(working_position_.x, target_position_.x, position_lerp, dt),
                             interpolate(working_position_.y, target_position_.y, position_lerp, dt),
                             interpolate(working_position_.z, target_position_.z, position_lerp, dt)};
    }
    if (killer_present_ && killer_.has_value()) {
        set_killer_view(killer_->position);
    }
    pitch_ = std::clamp(interpolate_angle(pitch_, target_pitch_, angle_lerp, dt), -pitch_limit,
                        pitch_limit);
    yaw_ = interpolate_angle(yaw_, target_yaw_, angle_lerp, dt);
    elapsed_ += dt;
    if (elapsed_ > chase_available_time) chase_available_ = true;
    if (elapsed_ > chase_forced_time) switch_to_chase();
}

void DeathCameraController::tick_fly(double dt) noexcept {
    const bool has_input =
        std::ranges::any_of(fly_keys_, [](bool held) { return held; });
    fly_speed_ = interpolate(fly_speed_, has_input ? fly_travel_speed : 0.0, fly_speed_normalize,
                             dt);
    const double step = fly_speed_ * dt;
    const auto basis = render::world_camera_basis(yaw_, pitch_);
    const auto held = [this](FlyCameraKey key) {
        return fly_keys_[static_cast<std::size_t>(key)];
    };
    world::Vec3 position = fly_position_;
    const auto add = [&position](const std::array<double, 3U>& axis, double amount) {
        position.x += axis[0U] * amount;
        position.y += axis[1U] * amount;
        position.z += axis[2U] * amount;
    };
    if (held(FlyCameraKey::forward)) add(basis.forward, step);
    if (held(FlyCameraKey::backward)) add(basis.forward, -step);
    if (held(FlyCameraKey::left)) add(basis.right, -step);
    if (held(FlyCameraKey::right)) add(basis.right, step);
    // Jump rises (z-down world) and crouch sinks, at 0.7 of the travel speed.
    if (held(FlyCameraKey::jump)) position.z -= step * fly_vertical_factor;
    if (held(FlyCameraKey::crouch)) position.z += step * fly_vertical_factor;
    position.x = std::clamp(position.x, 0.0, map_x_extent);
    position.y = std::clamp(position.y, 0.0, map_y_extent);
    position.z = std::clamp(position.z, 0.0, fly_z_extent);
    fly_position_ = position;
}

void DeathCameraController::on_mouse_move(double delta_x, double delta_y,
                                          double degrees_per_count) noexcept {
    if (!active() || !std::isfinite(delta_x) || !std::isfinite(delta_y) ||
        !std::isfinite(degrees_per_count)) {
        return;
    }
    if (mode_ == DeathCameraMode::killer_view) {
        // DeathController.on_mouse_move does not look around; it only counts
        // pyglet's signed dx + dy (dy up-positive, so -delta_y here) once the
        // chase cam is available, switching past 100.
        if (!chase_available_) return;
        mouse_movement_ += delta_x - delta_y;
        if (mouse_movement_ > mouse_movement_to_chase_cam) switch_to_chase();
        return;
    }
    yaw_ = wrap_degrees(yaw_ + delta_x * degrees_per_count);
    pitch_ = std::clamp(pitch_ + delta_y * degrees_per_count, -pitch_limit, pitch_limit);
}

void DeathCameraController::on_mouse_press() noexcept {
    request_chase();
}

void DeathCameraController::set_fly_key(FlyCameraKey key, bool held) noexcept {
    fly_keys_[static_cast<std::size_t>(key)] = held;
}

void DeathCameraController::set_terrain(const world::VxlMap* map) noexcept {
    terrain_ = map;
}

bool DeathCameraController::active() const noexcept {
    return mode_ != DeathCameraMode::inactive;
}

DeathCameraMode DeathCameraController::mode() const noexcept {
    return mode_;
}

bool DeathCameraController::chase_available() const noexcept {
    return active() && (mode_ != DeathCameraMode::killer_view || chase_available_);
}

bool DeathCameraController::can_cycle_targets() const noexcept {
    return active() && mode_ != DeathCameraMode::killer_view && !locked_;
}

bool DeathCameraController::spectating() const noexcept {
    return active() && spectator_;
}

std::optional<std::uint8_t> DeathCameraController::chase_player_id() const noexcept {
    return chase_target_.has_value() ? std::optional<std::uint8_t>{chase_target_->player_id}
                                     : std::nullopt;
}

std::optional<std::uint8_t> DeathCameraController::killer_player_id() const noexcept {
    return killer_.has_value() ? std::optional<std::uint8_t>{killer_->player_id} : std::nullopt;
}

std::optional<std::uint64_t> DeathCameraController::grave_entity_id() const noexcept {
    return grave_entity_id_;
}

world::Vec3 DeathCameraController::own_body_focus() const noexcept {
    return grave_entity_id_.has_value() ? grave_position_ : fallback_anchor_;
}

DeathCameraPose DeathCameraController::pose() const noexcept {
    switch (mode_) {
    case DeathCameraMode::killer_view:
        if (!killer_present_) {
            // DeathController.update: without a live killer the controller
            // places itself behind the chase player (our own body).
            return {chase_camera_eye(terrain_, own_body_focus(), yaw_, pitch_), yaw_, pitch_};
        }
        return {working_position_, yaw_, pitch_};
    case DeathCameraMode::chase:
        if (chase_target_.has_value()) {
            return {chase_camera_eye(terrain_, chase_target_->position, yaw_, pitch_), yaw_,
                    pitch_};
        }
        [[fallthrough]];
    case DeathCameraMode::grave:
        return {chase_camera_eye(terrain_, own_body_focus(), yaw_, pitch_), yaw_, pitch_};
    case DeathCameraMode::spectator_free:
        return {fly_position_, yaw_, pitch_};
    case DeathCameraMode::inactive:
        break;
    }
    return {fallback_anchor_, yaw_, pitch_};
}

} // namespace battlespades::frontend
