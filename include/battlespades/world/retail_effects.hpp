#pragma once

#include "battlespades/world/entity_catalog.hpp"
#include "battlespades/world/player_movement.hpp"
#include "battlespades/world/voxel_collapse.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace battlespades::world {

/**
 * Renderer-free retail presentation rules for status effects, surface
 * patches, tracers, muzzle flashes and crate air drops.
 *
 * Kept pure so every constant and ramp is unit-testable without a GPU or an
 * audio device; the frontend owns only the drawing and the voices.
 */

/** BLOCKFIRE_MAX_LIFESPAN (A1764): the colour ramp divides the fuse by it. */
inline constexpr double block_patch_max_lifespan{4.0};
/** BLOCKFIRE_LIGHT_RADIUS; BlockGooEntity reads the same constant. */
inline constexpr float block_patch_light_radius{3.0F};

/**
 * BlockFireEntity.calculate_colour: BLOCKFIRE_HOT (255,255,255) at a fresh
 * fuse, BLOCKFIRE_MID (255,255,0) half way and BLOCKFIRE_COLD (255,0,0) at
 * burnout, linear in `fuse / BLOCKFIRE_MAX_LIFESPAN`. Channels are 0..1.
 */
[[nodiscard]] std::array<float, 3U> block_fire_colour(double fuse) noexcept;
/** BlockGooEntity.calculate_colour: A2432 white, A2433 (20,255,50), A2434 green. */
[[nodiscard]] std::array<float, 3U> block_goo_colour(double fuse) noexcept;

/**
 * The burning or dissolving voxel behind a server surface anchor.
 *
 * The server places fire and goo 0.01 outside the open face of the voxel it
 * affects (top face first, then the sides), so the voxel is the solid cell
 * the anchor touches. Returns nothing when no neighbour is solid (the
 * voxel has already been dissolved).
 */
[[nodiscard]] std::optional<VoxelCell> surface_patch_voxel(const VxlMap& map,
                                                           Vec3 anchor) noexcept;

/** SetHP damage types (GameScene.process_packet_set_hp). */
enum class SetHpFeedback : std::uint8_t {
    none,
    /** Type 1: hitplayer plus a directional damage indicator. */
    hit,
    /** Type 2: Character.heal_hp_added (medpack healing cue). */
    heal,
    /** Type 3: hitplayer plus `burn_time` (BURN_INDICATOR_TIME). */
    burn,
    /** Type 4: hitplayer plus `sudden_death_damage_time`. */
    sudden_death,
};

/**
 * Maps one SetHP row to its retail feedback. Recovered from the stock
 * gameScene.pyd `process_packet_set_hp`: types 1, 3 and 4 all play the same
 * `hitplayer` cue; 3 and 4 also start their 1.2-second indicators; type 2
 * calls `heal_hp_added(old, new)`.
 */
[[nodiscard]] SetHpFeedback set_hp_feedback(std::uint8_t damage_type,
                                            int previous_health,
                                            int health) noexcept;

/** BURN_INDICATOR_TIME and SUDDEN_DEATH_INDICATOR_TIME. */
inline constexpr double burn_indicator_time{1.2};
inline constexpr double sudden_death_indicator_time{1.2};

/**
 * HUD.draw (hud.pyd 0x1009EE80): while burn_time > time,
 * draw_fsquad_tex(inside_zone_texture, (255, 0, 0, a)) with
 * a = (burn_time - time) / BURN_INDICATOR_TIME * 255; sudden death draws the
 * same full-screen quad tinted (*player.team.color, a) over
 * SUDDEN_DEATH_INDICATOR_TIME. Both layers draw independently.
 */
struct StatusTint final {
    std::array<std::uint8_t, 3U> color{};
    /** 0..255, the retail alpha before the texture's own alpha pattern. */
    std::uint8_t alpha{};
};
struct StatusTints final {
    std::optional<StatusTint> burn;
    std::optional<StatusTint> sudden_death;
};
[[nodiscard]] StatusTints status_tints(double burn_remaining, double sudden_death_remaining,
                                       std::array<std::uint8_t, 3U> team_color) noexcept;

/** Character state bits carried by WorldUpdate. */
inline constexpr std::uint8_t world_action_on_fire_bit{0x20U};
inline constexpr std::uint8_t world_state_touching_goo_bit{0x08U};

/** Edge detector for Character.set_is_on_fire / set_touching_goo. */
struct CharacterStatusEdges final {
    bool ignited{};
    bool extinguished{};
    bool goo_started{};
    bool goo_ended{};
};
[[nodiscard]] CharacterStatusEdges character_status_edges(bool was_on_fire,
                                                          bool on_fire,
                                                          bool was_touching_goo,
                                                          bool touching_goo) noexcept;

/** A character counts as in water for the end cues below the water plane. */
[[nodiscard]] bool character_in_water(const Vec3& position) noexcept;

/** WEAPON_TRACER_SPEED (and SHRAPNEL_TRACER_SPEED): 200 blocks per second. */
inline constexpr float weapon_tracer_speed{200.0F};

/** Character.shoot: Tracer ttl = min(weapon.range / WEAPON_TRACER_SPEED, 0.5). */
[[nodiscard]] constexpr float retail_tracer_ttl(double weapon_range) noexcept {
    const double ttl = weapon_range / static_cast<double>(weapon_tracer_speed);
    return static_cast<float>(ttl < 0.5 ? (ttl > 0.0 ? ttl : 0.0) : 0.5);
}

/** One flying Tracer: starts at the muzzle and dies at the hit point. */
struct TracerState final {
    std::array<float, 3U> position{};
    std::array<float, 3U> direction{1.0F, 0.0F, 0.0F};
    float remaining{};
    std::uint8_t tool_id{};
};

/** Build a tracer from muzzle to target; nothing for a zero-length shot. */
[[nodiscard]] std::optional<TracerState> make_tracer(std::array<float, 3U> muzzle,
                                                     std::array<float, 3U> target,
                                                     std::uint8_t tool_id) noexcept;
/** Advance at WEAPON_TRACER_SPEED; returns false once it reached its end. */
[[nodiscard]] bool advance_tracer(TracerState& tracer, float dt) noexcept;

/**
 * Character.shoot passes prestep=16 to gameScene Tracer.initialize, which sets
 * velocity = direction * prestep and calls update(1) before restoring the real
 * velocity: the tracer is born 16 blocks down the shot line from the
 * shooter's world_object.position (the eye), and dies in that first update
 * when the hitscan contact lies within those 16 blocks.
 */
inline constexpr float retail_tracer_prestep{16.0F};

/** Where one tracer starts and ends, in canonical map space. */
struct TracerLaunch final {
    std::array<float, 3U> start{};
    std::array<float, 3U> end{};
};

/**
 * Plans the visible tracer for one shot. `target` is the first contact, or
 * the shot line's far end when nothing was hit.
 *
 * Retail tier: retail exactly -- eye + direction * 16, nothing when the
 * contact is nearer. Enhanced tiers: from the weapon muzzle (first-person
 * muzzle for the local player, third-person muzzle for others) to the
 * target, i.e. out of the gun towards the crosshair; nothing when the muzzle
 * is already at or past the target (a wall in the gun's face). Without a
 * muzzle the enhanced plan falls back to the retail pre-step.
 */
[[nodiscard]] std::optional<TracerLaunch>
plan_tracer_launch(bool retail_look, std::array<float, 3U> eye,
                   std::array<float, 3U> direction,
                   std::optional<std::array<float, 3U>> muzzle,
                   std::array<float, 3U> target) noexcept;

/** Weapon.draw_muzzle: muzzleflash_default.kv6 for 0.05 s with random roll. */
inline constexpr float muzzle_flash_duration{0.05F};
/** Per-weapon muzzle_flash_duration: 0.01 for the tommy gun, classic SMG and MG. */
[[nodiscard]] float muzzle_flash_duration_for(std::uint8_t tool_id) noexcept;
[[nodiscard]] float muzzle_flash_roll_degrees(std::uint32_t seed) noexcept;
/** models.py: every MUZZLE_FLASH_* display loads muzzleflash_default. */
inline constexpr std::string_view muzzle_flash_model_asset{"kv6/muzzleflash_default.kv6"};
/**
 * Retail third-person weapon display size (Tool.apply_transform), used to
 * scale the local world-space flash and tracer KV6s. Tracer size is VERIFY.
 */
inline constexpr float weapon_display_size{0.065F};
inline constexpr float tracer_model_scale{0.065F};
/** Muzzle flash drawn in world space along `forward` (row-vector matrix). */
[[nodiscard]] std::array<float, 16U> muzzle_flash_world_transform(std::array<float, 3U> position,
                                                                  std::array<float, 3U> forward,
                                                                  float roll_degrees,
                                                                  float scale) noexcept;

/** CRATE_PARACHUTE_MODEL (models.py:387). */
inline constexpr std::string_view crate_parachute_model_asset{"kv6/Crate_Parachute.kv6"};
/**
 * The canopy is drawn with the crate's own display transform, lifted above
 * the crate and at twice the crate's model size. Size and lift are VERIFY
 * (Crate.draw); the deployment rules above are recovered.
 */
inline constexpr EntityModelPart crate_parachute_part{
    "Crate_Parachute.kv6", {0.0F, 0.0F, -0.9F}, 2.0F, 0U, {0.0F, 0.0F, 0.0F}};

// The spawn-protection flash is Character.spawn_color_blink_timer; see
// RetailSpawnBlink in jetpack_death.hpp (team-colour voxels only).

/** Crate air-drop state carried by crate entities (types 3..6). */
struct CrateDropState final {
    bool parachute_deployed{};
    bool parachute_removed{};
    bool falling{};
};

/** CRATE_PARACHUTE_* (constants.py 3043-3049) and the GenericMovement drop. */
inline constexpr double crate_drop_gravity{30.0};
inline constexpr double crate_parachute_deployment_height{10.0};
inline constexpr double crate_parachute_removal_height{2.0};
inline constexpr double crate_parachute_slowdown{0.75};
inline constexpr double crate_drop_step{1.0 / 60.0};

/** Result of one fixed 1/60 s crate step. */
struct CrateDropStep final {
    bool chute_opened{};
    bool chute_released{};
    bool landed{};
    double impact_speed{};
};

/**
 * One retail Crate.update frame over a column whose support voxel is at
 * `support_z`: GenericMovement gravity 30, then the ground hit-scan opens the
 * chute below 10 blocks and releases it below 2, and while it is open the
 * stored velocity is multiplied by 0.75 after the move.
 */
[[nodiscard]] CrateDropStep step_crate_drop(double& z,
                                            double& velocity_z,
                                            double support_z,
                                            CrateDropState& state) noexcept;

} // namespace battlespades::world
