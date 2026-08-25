#pragma once

#include "battlespades/world/tutorial_session.hpp"

#include <array>
#include <cstddef>

namespace battlespades::world {

/** Three-component value in the retail tool/viewmodel coordinate space. */
struct ViewModelVector final {
    double x{};
    double y{};
    double z{};
};

/**
 * Renderer-neutral input to the recovered Character.draw_fps transform chain.
 *
 * `seconds_since_primary` is zero on the frame an animation starts. The
 * animation functions intentionally preserve the distinct start pose used by
 * the retail Animation.start() methods before their first update().
 */
struct RetailViewModelInput final {
    TutorialTool tool{TutorialTool::pistol};
    double seconds_since_primary{1.0e9};
    double sway_x{};
    double sway_y{};
    double sway_z{};
};

/** Full-catalog equivalent used by multiplayer and weapon parity previews. */
struct WeaponViewModelInput final {
    std::uint8_t tool_id{17U};
    double seconds_since_primary{1.0e9};
    /** Accepted action count, used by alternating multipart tools. */
    std::uint64_t action_serial{};
    double sway_x{};
    double sway_y{};
    double sway_z{};
    /** 0..1 accumulated mechanism rotation, currently Minigun AnimRoll. */
    double mechanism_phase{};
};

/**
 * Compose full-catalog FPS input with Character's recovered pullout motion.
 * Pullout moves X/Y down-left and must never be wired into depth (sway Z).
 */
[[nodiscard]] WeaponViewModelInput compose_weapon_view_model_input(
    std::uint8_t tool_id, double seconds_since_primary,
    std::uint64_t action_serial, ViewModelVector base_sway,
    double pullout_remaining, double mechanism_phase) noexcept;

/** One model-space pose before the outer Character.draw_fps transforms. */
struct RetailModelPose final {
    ViewModelVector position{};
    ViewModelVector orientation_degrees{};
};

/** Character.draw_sight's independent identity-space sight transform. */
struct RetailSightPose final {
    double model_scale{0.05};
    ViewModelVector position{};
    double yaw_degrees{180.0};
    /**
     * Weapon.pin: the second draw_scaled() call at character.pyx:2085-2089.
     *
     * Only the classic rifle sets one, and it shares the sight's yaw and
     * identity root while carrying its own scale and its own offset from the
     * already-offset sight position.
     */
    bool has_pin{};
    double pin_scale{};
    ViewModelVector pin_position{};
};

enum class RetailArmPart : std::size_t {
    left_upper,
    left_lower,
    right_lower,
    count,
};

/** Recovered fixed local transform for one first-person soldier arm mesh. */
struct RetailArmPartPose final {
    ViewModelVector position{};
    double yaw_degrees{};
};

/**
 * Complete semantic pose produced by the retail Python tool classes and the
 * native Cython Character.draw_fps routine.
 *
 * This type deliberately contains no bgfx/OpenGL matrices. Tests can compare
 * the exact recovered values without depending on a renderer convention, and
 * each backend composes the same transform chain from this canonical pose.
 */
struct RetailViewModelPose final {
    double model_scale{0.05};
    bool draws_player_arms{true};
    /** Character.draw_fps outer translation and handedness flip. */
    ViewModelVector character_offset{-0.4, -0.55, 0.9};
    double character_yaw_degrees{180.0};
    /** Shared Character.draw_fps translation applied to tool and arm chain. */
    ViewModelVector tool_sway{};
    /** Shared arm hierarchy anchor recovered from character.pyd. */
    ViewModelVector arms_anchor{0.401, -0.01, -0.801};
    RetailModelPose tool{};
    /** Per-part overrides; currently required by ZombieHandTool. */
    std::array<RetailModelPose, 3U> tool_parts{};
    std::size_t tool_part_count{};
    ViewModelVector arms_position{};
    ViewModelVector arms_orientation_degrees{};
    double arm_rotation_ratio{1.0};
    double digging_pitch_degrees{};
    std::array<RetailArmPartPose, static_cast<std::size_t>(RetailArmPart::count)>
        arm_parts{};
};

/**
 * Evaluate the recovered BlockTool, SpadeTool/DiggingTool, Weapon animation,
 * and Character.draw_fps arm contracts for one frame.
 */
[[nodiscard]] RetailViewModelPose
evaluate_retail_view_model(const RetailViewModelInput& input) noexcept;

/** Evaluate recovered common/special animation families for tools 0..64. */
[[nodiscard]] RetailViewModelPose
evaluate_weapon_view_model(const WeaponViewModelInput& input) noexcept;

/** Evaluate the exact sight-only transform used after Character.set_zoom(). */
[[nodiscard]] RetailSightPose
evaluate_weapon_sight(std::uint8_t tool_id) noexcept;

} // namespace battlespades::world
