#pragma once

#include "battlespades/world/chunk_mesh.hpp"
#include "battlespades/world/tutorial_session.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

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
    /** Held tool KV6 scale: Character.view_weapon.size = tool.view_model_size. */
    double model_scale{0.05};
    /**
     * First-person arm KV6 scale. Character.__init__ fixes every fps_*_arm
     * DisplayList at BODY_PARTS_SIZE (character.pyx:145) and draw_fps calls
     * their draw_scaled() with no argument, so the arms never inherit the
     * tool's view_model_size. Scaling them with the tool made the Medic's
     * riot shield (0.18) draw 3.6x arms across the whole screen.
     */
    double arm_model_scale{0.05};
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
    /**
     * DiggingTool.rotate_arm_ratio (0.25) scales the arm pitch taken from
     * the tool orientation. Character.draw_fps never reads Tool.pitch/
     * get_pitch(): DiggingTool's swing pitch is a third-person arm angle only.
     */
    double arm_rotation_ratio{1.0};
    std::array<RetailArmPartPose, static_cast<std::size_t>(RetailArmPart::count)>
        arm_parts{};
};

/** Which retail Animation objects a tool's use_primary path starts. */
enum class RetailToolAnimationFamily {
    none,
    /** Weapon.use_primary -> AnimWeaponShoot(shoot_interval). */
    weapon_shoot,
    /** AnimPlaceBlock(shoot_interval). */
    place_block,
    /** Deployable Weapon subclasses: both of the above on the same frame. */
    weapon_shoot_and_place_block,
    /** AnimUseSpade (spade, super spade, classic spade, UGC super spade). */
    use_spade,
    /** AnimUsePickAxe/Knife/Crowbar/Machete/RiotStick (identical curves). */
    use_melee,
    /** AnimUseRiotShield: the z-only forward bash. */
    use_riot_shield,
    zombie_hand,
    /** AnimThrowGrenade(fuse): grenade tools, from press to release. */
    throw_cooked,
    /** AnimThrowGrenade(charge, stop_on_end=False): molotov/sticky/chemical. */
    throw_charged,
};

[[nodiscard]] RetailToolAnimationFamily
retail_tool_animation_family(std::uint8_t tool_id) noexcept;

/** True for DiggingTool subclasses (rotate_arm_ratio 0.25, digging pitch). */
[[nodiscard]] bool retail_tool_is_digging_tool(std::uint8_t tool_id) noexcept;

/**
 * One Tool subclass's __init__ hold: initial_position/initial_orientation per
 * view-model part plus arms_position_offset.
 *
 * Almost every class assigns its initial pose inside `if character.main:`, so
 * `main_character=false` (observers, third person) mostly yields zeros; the
 * exceptions are RPG2 (0,0.3,0), the rocket turret parts and the minigun
 * barrel.
 */
struct RetailToolHold final {
    std::array<RetailModelPose, 3U> parts{};
    ViewModelVector arms_position_offset{};
};

[[nodiscard]] RetailToolHold retail_tool_hold(std::uint8_t tool_id,
                                              bool main_character) noexcept;

/**
 * Sum of the tool's playing Animation offsets (Tool.apply_animations) at
 * `seconds_since_primary` after its use_primary. `main_character=false`
 * drops the animations retail only starts on the local placing/cooking path.
 */
[[nodiscard]] RetailModelPose evaluate_retail_tool_animation(
    std::uint8_t tool_id, double seconds_since_primary, bool main_character) noexcept;

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

/**
 * One Weapon subclass's first-person muzzle flash data (aoslib/weapons/xWeapon.py).
 *
 * Only classes that assign `muzzle_flash_view_display` flash in first person;
 * the classic rifle has a third-person flash only. `zoomed_view_offset` keeps
 * the Weapon default (0,0,0) where a class never overrides it (minigun,
 * sniper, sniper 2).
 */
struct RetailViewMuzzleFlash final {
    /** muzzle_flash_view_offset, read by Weapon.draw_fps. */
    ViewModelVector view_offset{};
    /** muzzle_flash_zoomed_view_offset, read by Weapon.draw_sight. */
    ViewModelVector zoomed_view_offset{};
    /** muzzle_flash_scale: flash size = view_weapon.size * scale. */
    double scale{0.5};
    /** muzzle_flash_duration: Weapon.shot_weapon arms the timer with it. */
    double duration{0.05};
};

[[nodiscard]] std::optional<RetailViewMuzzleFlash>
retail_view_muzzle_flash(std::uint8_t tool_id) noexcept;

/**
 * The first-person muzzleflash_default.kv6 draw for one frame.
 *
 * Weapon.draw_muzzle runs inside Character.draw_fps (after the tool parts,
 * under the outer R_y(180)·T(sway + (-0.4,-0.55,0.9)) but outside
 * Tool.apply_transform, so recoil and the class hold never move it) or inside
 * Character.draw_sight (identity·R_y(180) only). It then issues
 * glTranslatef(offset), glRotatef(roll,0,0,1) and DisplayList.draw with
 * size = view_weapon.size * muzzle_flash_scale and the identity view_weapon
 * matrix. Row-vector composition: S(model_scale)·R_z(roll)·T(position)·
 * R_y(yaw_degrees).
 */
struct RetailViewMuzzleFlashPose final {
    double model_scale{};
    ViewModelVector position{};
    double roll_degrees{};
    double yaw_degrees{180.0};
};

/**
 * Evaluate the local flash. `zoomed` selects Character.draw_sight's branch
 * (the caller must still honour draw_sight's no-sight early return); `sway`
 * is the same draw_fps sway/pullout/bob vector the tool root uses.
 */
[[nodiscard]] std::optional<RetailViewMuzzleFlashPose>
evaluate_retail_view_muzzle_flash(std::uint8_t tool_id, bool zoomed, ViewModelVector sway,
                                  double roll_degrees) noexcept;

/**
 * The muzzle end of a first-person tool part, in the part's own KV6 model
 * space (x right, y up, +z forward along the barrel, voxel units about the
 * pivot, exactly the space Kv6Model::mesh() emits).
 *
 * z is the front-most face; x/y are the centroid of every vertex within one
 * voxel of it, i.e. the barrel mouth rather than the whole silhouette.
 * nullopt for an empty mesh.
 */
[[nodiscard]] std::optional<std::array<float, 3U>>
view_model_muzzle_tip(const ChunkMesh& mesh) noexcept;

/**
 * muzzleflash_default.kv6 extends 6 voxels either side of its pivot along
 * its forward axis. Enhanced tiers centre the flash this many flash voxels
 * ahead of the barrel mouth, so its rear just overlaps the mouth (one voxel) instead
 * of floating in front of it (retail's own view offsets leave the minigun
 * flash 0.15 units clear of the barrel and put the assault rifle/LMG flashes
 * inside their receivers, because they ignore Tool.apply_transform).
 */
inline constexpr double view_muzzle_flash_lead_voxels{5.0};

/**
 * Flash centre in the anchored part's model space: the tip plus
 * lead * flash_scale voxels along +z. The flash matrix is then
 * S(flash_scale)·R_z(roll)·T(centre)·part_matrix, which reproduces retail's
 * size (view_model_size * muzzle_flash_scale) while following the barrel's
 * hold, recoil and spin.
 */
[[nodiscard]] std::array<float, 3U>
anchored_view_muzzle_flash_centre(std::array<float, 3U> tip, double flash_scale) noexcept;

} // namespace battlespades::world
