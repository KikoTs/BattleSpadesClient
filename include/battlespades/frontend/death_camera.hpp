#pragma once

#include "battlespades/world/player_movement.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace battlespades::world {
class VxlMap;
struct LocalEntity;
}

namespace battlespades::frontend {

/**
 * The camera state owned by a dead or spectator local player.
 *
 * Protocol handlers only announce life-boundary facts to this controller.
 * Mouse input and the fixed gameplay tick then advance it without mutating
 * server-owned player/entity state.
 */
enum class DeathCameraMode : std::uint8_t {
    inactive,
    /**
     * Retail DeathController (camera id 5): an orbit around the body turning
     * to face a repeat killer, optionally flying toward their initial position.
     */
    killer_view,
    /**
     * Retail ChaseController (camera id 1) on the local player's own body:
     * mouse orbit around the corpse/grave, pulled in front of walls.
     */
    grave,
    /** ChaseController following a replicated character, including its corpse. */
    chase,
    /** Spectator FlyController, selected by movement keys or no chase target. */
    spectator_free,
};

/** One replicated character that the camera may follow. */
struct DeathCameraTarget final {
    std::uint8_t player_id{};
    world::Vec3 position{};
    world::Vec3 orientation{-1.0, 0.0, 0.0};
};

/**
 * DeathController.set_killer_info(kill_type, killer_id, killer_position,
 * killer.running_local_player_kills). `streak` is how many times in a row this
 * killer has killed the local player; it resets when the local player kills
 * them.
 */
struct DeathKillerInfo final {
    std::uint8_t player_id{};
    world::Vec3 position{};
    std::uint8_t kill_type{};
    std::uint32_t streak{};
};

/** Renderer-neutral result in the client's canonical z-down world basis. */
struct DeathCameraPose final {
    world::Vec3 eye{};
    double yaw_degrees{};
    double pitch_degrees{};
};

/** Held FlyController inputs (flyController.py update). */
enum class FlyCameraKey : std::uint8_t { forward, backward, left, right, jump, crouch };

/** DEATHCAM_VALID_TYPES = [0..6, 21..24]. */
[[nodiscard]] bool deathcam_valid_kill_type(std::uint8_t kill_type) noexcept;

/**
 * ChaseController.validate_position (gameScene.pyd 0x10031d10): the eye sits
 * up to five blocks behind `focus` along `-forward`, pulled in to the first
 * solid cell (cell centre distance minus sqrt(3)/2 minus 0.5, never below 0)
 * and scaled back inside the map bounds (512, 512, 238).
 */
[[nodiscard]] world::Vec3 chase_camera_eye(const world::VxlMap* map, world::Vec3 focus,
                                           double yaw_degrees, double pitch_degrees) noexcept;

/**
 * Where the death camera aims at the local player's grave (entity type 11):
 * the centre of the tombstone as the renderer draws it.
 *
 * GraveEntity draws its model at (x-0.5, y-0.5, z-0.5) of its movement object
 * (entity_presentation_position), with offset_pivots(0, 0, 11) moving the
 * centred 16x4x22 grave.kv6 pivot to the base, and the terrain contact rule
 * standing that base on the support surface. Half of the 22-voxel height
 * (the recovered pivot offset, 11 voxels at model size 0.1) lies above it.
 * Feeding the raw packet position instead aimed the camera at the stone's
 * corner, below and beside it.
 */
[[nodiscard]] world::Vec3 grave_camera_focus(const world::LocalEntity& grave) noexcept;

/**
 * Retail-compatible death/spectator camera.
 *
 * Recovered from gameScene.pyd DeathController (activate 0x1003e250, update
 * 0x1003f270, set_killer_view 0x10040ca0, switch_to_chase_cam 0x10040850),
 * ChaseController (update 0x10031430, validate_position 0x10031d10,
 * on_mouse_press 0x100310a0, chase_next_player 0x100346b0), FlyController
 * (update 0x10037670) and character.pyd Character.update_dead 0x100462d0:
 *
 * - update_dead activates DEATH when manager.enable_deathcam, else CHASE, on
 *   the local player, with `locked = not never_respawn`;
 * - DeathController switches straight to chase unless the killer is known,
 *   the kill type is valid and the killer's streak is >= 2; otherwise it keeps
 *   the body orbit, faces the killer (angle lerp 10) and, from a streak of 3
 *   with the killer more than 7 blocks away, flies toward them after 0.25 s,
 *   stopping 5 short (position lerp 20);
 * - chase becomes available at 1.5 s (a click, or more than 100 counts of
 *   accumulated dx+dy, switches), and is forced at 5 s;
 * - chase follows the local player's own body. LMB/RMB cycle living teammates
 *   only when not locked (never_respawn: the dead VIP camera), everyone for a
 *   spectator.
 *
 * All methods run on the fixed gameplay thread.
 */
class DeathCameraController final {
public:
    /**
     * The scene camera's r_y/r_x at the moment of death. Retail has one
     * Camera: GameScene.mouse_move feeds the first-person look into
     * Camera.add_mouse_motion, so the death controllers start from exactly
     * where the player was looking (and the killer view turns from there).
     */
    void set_view_angles(double yaw_degrees, double pitch_degrees) noexcept;
    void begin_death(world::Vec3 death_eye, std::optional<DeathKillerInfo> killer,
                     bool deathcam_enabled,
                     bool never_respawn = false) noexcept;
    /** A KillAction that arrives after the SetHp-driven death began. */
    void set_killer_info(std::optional<DeathKillerInfo> killer) noexcept;
    /** A later KillAction carried NEVER_RESPAWN_TIME: unlock teammate cycling. */
    void set_never_respawn(bool never_respawn) noexcept;
    /** The killer's live interpolated position, or nullopt once they are gone. */
    void set_killer_position(std::optional<world::Vec3> position) noexcept;
    void enter_spectator(world::Vec3 fallback_anchor,
                         std::optional<DeathCameraTarget> target) noexcept;
    void end_life() noexcept;

    /**
     * Bind or update the server-created grave belonging to the local player.
     * `focus` is grave_camera_focus(entity): the drawn tombstone's centre.
     */
    void bind_grave(std::uint64_t entity_id, world::Vec3 position) noexcept;
    void update_grave(std::uint64_t entity_id, world::Vec3 position) noexcept;
    /** Follow a client-simulated corpse until a server grave takes ownership. */
    void update_body_position(world::Vec3 position) noexcept;

    /**
     * Follow a replicated character, or nullopt to fall back to the
     * local player's own body (spectators: the free fly camera).
     */
    void set_chase_target(std::optional<DeathCameraTarget> target) noexcept;
    /** DeathController.on_mouse_press: switch once chase is available. */
    void request_chase() noexcept;
    void tick(double dt) noexcept;
    void on_mouse_move(double delta_x, double delta_y,
                       double degrees_per_count = 0.1) noexcept;
    void on_mouse_press() noexcept;
    void set_fly_key(FlyCameraKey key, bool held) noexcept;
    /** Terrain for the chase camera's wall pull-in; nullptr disables it. */
    void set_terrain(const world::VxlMap* map) noexcept;

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] DeathCameraMode mode() const noexcept;
    [[nodiscard]] bool chase_available() const noexcept;
    /** ChaseController.on_mouse_press cycles only when not locked. */
    [[nodiscard]] bool can_cycle_targets() const noexcept;
    [[nodiscard]] bool spectating() const noexcept;
    /** A deliberate switch to fly must not be undone by a roster refresh. */
    [[nodiscard]] bool wants_chase_target() const noexcept;
    [[nodiscard]] std::optional<std::uint8_t> chase_player_id() const noexcept;
    [[nodiscard]] std::optional<std::uint8_t> killer_player_id() const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> grave_entity_id() const noexcept;
    [[nodiscard]] DeathCameraPose pose() const noexcept;

private:
    void activate_death_controller() noexcept;
    void set_killer_view(world::Vec3 killer_position, bool initial) noexcept;
    void switch_to_chase() noexcept;
    void tick_fly(double dt) noexcept;
    [[nodiscard]] world::Vec3 own_body_focus() const noexcept;

    DeathCameraMode mode_{DeathCameraMode::inactive};
    world::Vec3 fallback_anchor_{};
    world::Vec3 grave_position_{};
    std::optional<std::uint64_t> grave_entity_id_;
    std::optional<DeathCameraTarget> chase_target_;
    std::optional<DeathKillerInfo> killer_;
    bool killer_present_{};
    world::Vec3 working_position_{};
    world::Vec3 killer_eye_{};
    /** DeathController.update's validate_position orbit, not the zoom path. */
    bool killer_eye_orbits_body_{true};
    world::Vec3 target_position_{};
    bool zoom_possible_{};
    double target_yaw_{};
    double target_pitch_{};
    double elapsed_{};
    bool chase_available_{};
    // Camera r_x/r_y start at zero: native yaw = 90 - r_y, pitch = -r_x.
    // These angles belong to the scene camera; set_view_angles hands over the
    // first-person look each time the local player dies.
    double yaw_{90.0};
    double pitch_{};
    double mouse_movement_{};
    bool deathcam_enabled_{true};
    bool locked_{true};
    bool spectator_{};
    world::Vec3 fly_position_{};
    world::Vec3 fly_draw_position_{};
    bool fly_requested_{};
    double fly_speed_{};
    std::array<bool, 6U> fly_keys_{};
    const world::VxlMap* terrain_{};
};

} // namespace battlespades::frontend
