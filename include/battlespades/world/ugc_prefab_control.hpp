#pragma once

#include "battlespades/world/player_movement.hpp"

#include <array>
#include <cstdint>

namespace battlespades::world {

/** One retained Construct Builder blueprint in raw VXL coordinates. */
struct UgcPrefabPlacement final {
    std::array<std::int32_t, 3U> anchor{};
    std::array<double, 3U> center{};
    std::array<std::int32_t, 3U> authored_size{};
    std::uint8_t yaw{};
    std::uint8_t pitch{};
    std::uint8_t roll{};
};

/** Inputs consumed by retail's second-stage Construct Builder control mode. */
enum class UgcPrefabControlInput : std::uint8_t {
    forward,
    backward,
    left,
    right,
    up,
    down,
    sprint,
    rotate_left,
    rotate_right,
    rotate_up,
    rotate_down,
    carve,
};

/**
 * Source-recovered UGC Construct Builder fine-placement state machine.
 *
 * The first LMB captures the approximate world ghost and enters this mode.
 * While active, movement bindings nudge the blueprint rather than the player,
 * palette arrows rotate it in camera-relative quarter turns, wheel changes the
 * prefab-camera distance, C repeats erase requests, Q cancels, and the second
 * LMB commits. The class owns no renderer, map, socket, or player object; the
 * frontend validates the exposed placement and sends Protocol 168 packets.
 * It is advanced on the fixed gameplay tick.
 */
class UgcPrefabControl final {
public:
    /** Replace the approximate stage-one ghost unless fine control is active. */
    void prime(const UgcPrefabPlacement& placement) noexcept;

    /** Enter stage two. False means there is no ghost or the 0.5 s exit guard is active. */
    [[nodiscard]] bool activate() noexcept;

    /** Leave stage two and arm retail's 0.5 second re-entry guard. */
    void deactivate() noexcept;

    /** Forget both placement stages, held inputs and timers. */
    void reset() noexcept;

    void set_input(UgcPrefabControlInput input, bool held) noexcept;

    /** Advance repeat/rotation/nudge/carve state using the live camera forward vector. */
    void tick(double dt, const Vec3& camera_forward) noexcept;

    /** Rotate around the world Z axis (the ordinary RMB action). */
    void rotate_yaw(int quarter_turns) noexcept;

    /** Retail wheel formula: zoom -= wheel_y * radius * 0.1, clamped to [0, 9999999]. */
    void adjust_zoom(double wheel_y) noexcept;

    /** Consume one erase request generated at the recovered 0.1 second cadence. */
    [[nodiscard]] bool take_carve_request() noexcept;

    [[nodiscard]] bool has_placement() const noexcept { return has_placement_; }
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] const UgcPrefabPlacement& placement() const noexcept { return placement_; }
    [[nodiscard]] double zoom_level() const noexcept { return zoom_level_; }
    [[nodiscard]] double prefab_radius() const noexcept { return prefab_radius_; }
    [[nodiscard]] double color_animation() const noexcept { return color_animation_; }
    [[nodiscard]] double deactivate_remaining() const noexcept { return deactivate_remaining_; }

private:
    static constexpr std::size_t input_count{12U};

    [[nodiscard]] bool input(UgcPrefabControlInput value) const noexcept;
    void clear_inputs() noexcept;
    void apply_camera_relative_rotation(std::uint8_t camera_relative_yaw) noexcept;
    void rotate(std::int32_t yaw, std::int32_t pitch, std::int32_t roll) noexcept;
    void recenter_anchor() noexcept;

    UgcPrefabPlacement placement_{};
    std::array<bool, input_count> inputs_{};
    bool has_placement_{};
    bool active_{};
    bool carve_pending_{};
    std::uint8_t repeat_index_{};
    double repeat_remaining_{};
    double erase_remaining_{};
    double deactivate_remaining_{};
    double prefab_radius_{};
    double zoom_level_{};
    double color_animation_{0.9};
    double color_animation_delta_{0.5};
};

} // namespace battlespades::world
