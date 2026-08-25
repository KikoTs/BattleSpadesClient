#pragma once

#include "battlespades/world/player_movement.hpp"

#include <cstdint>
#include <optional>

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
    /** Orbiting the authoritative grave (entity type 11). */
    grave,
    /** Following one living replicated player. */
    chase,
    /** Spectator fallback while no legal chase target exists. */
    spectator_free,
};

/** One generation-safe living player that the camera may follow. */
struct DeathCameraTarget final {
    std::uint8_t player_id{};
    world::Vec3 position{};
    world::Vec3 orientation{-1.0, 0.0, 0.0};
};

/** Renderer-neutral result in the client's canonical z-down world basis. */
struct DeathCameraPose final {
    world::Vec3 eye{};
    double yaw_degrees{};
    double pitch_degrees{};
};

/**
 * Retail-compatible death/spectator transition controller.
 *
 * Recovered invariants:
 * - death begins in camera id 5 (DEATH_CAMERA);
 * - chase becomes available after 1.5 seconds;
 * - any click, or eight accumulated mouse counts, selects chase;
 * - an available chase target is forced after five seconds.
 *
 * Every death opens on the local player's grave for at least 1.5 seconds.
 * The killer is used to choose which side of the grave the camera starts on,
 * but the grave remains the focus; this prevents a server with deathcam
 * disabled from skipping the tombstone presentation entirely. After the
 * hold, disabled deathcam advances automatically while enabled deathcam keeps
 * the recovered mouse/click and five-second transitions. All methods run on
 * the fixed gameplay thread.
 */
class DeathCameraController final {
public:
    void begin_death(world::Vec3 fallback_anchor, double yaw_degrees,
                     std::optional<DeathCameraTarget> killer,
                     bool deathcam_enabled) noexcept;
    void enter_spectator(world::Vec3 fallback_anchor, double yaw_degrees,
                         std::optional<DeathCameraTarget> target) noexcept;
    void end_life() noexcept;

    /** Bind or update the server-created grave belonging to the local player. */
    void bind_grave(std::uint64_t entity_id, world::Vec3 position) noexcept;
    void update_grave(std::uint64_t entity_id, world::Vec3 position) noexcept;

    /** Replace a stale/dead chase target with the current replicated sample. */
    void set_chase_target(std::optional<DeathCameraTarget> target) noexcept;
    void request_chase() noexcept;
    void tick(double dt) noexcept;
    void on_mouse_move(double delta_x, double delta_y,
                       double degrees_per_count = 0.1) noexcept;
    void on_mouse_press() noexcept;

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] DeathCameraMode mode() const noexcept;
    [[nodiscard]] bool chase_available() const noexcept;
    [[nodiscard]] std::optional<std::uint8_t> chase_player_id() const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> grave_entity_id() const noexcept;
    [[nodiscard]] DeathCameraPose pose() const noexcept;

private:
    void switch_to_chase_if_possible() noexcept;
    void seed_orbit_from_target(const DeathCameraTarget& target) noexcept;

    DeathCameraMode mode_{DeathCameraMode::inactive};
    world::Vec3 fallback_anchor_{};
    world::Vec3 grave_position_{};
    std::optional<std::uint64_t> grave_entity_id_;
    std::optional<DeathCameraTarget> chase_target_;
    double elapsed_{};
    double orbit_yaw_{};
    double orbit_pitch_{15.0};
    double mouse_movement_{};
    bool deathcam_enabled_{true};
};

} // namespace battlespades::frontend
