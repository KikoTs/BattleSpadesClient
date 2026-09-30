#pragma once

#include "battlespades/world/chunk_mesh.hpp"
#include "battlespades/world/footstep_audio.hpp"
#include "battlespades/world/flight_profile.hpp"
#include "battlespades/world/local_entity.hpp"
#include "battlespades/world/machine_gun_deployment.hpp"
#include "battlespades/world/player_inventory.hpp"
#include "battlespades/world/player_movement.hpp"
#include "battlespades/world/retail_inventory.hpp"
#include "battlespades/world/terrain_effects.hpp"
#include "battlespades/world/tutorial_lessons.hpp"
#include "battlespades/world/voxel_collapse.hpp"
#include "battlespades/world/vxl_map.hpp"
#include "battlespades/world/weapon_catalog.hpp"

#include <array>
#include <bitset>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace battlespades::world {

/** Semantic movement actions owned by the session's held-input state. */
enum class TutorialAction : std::uint8_t {
    forward,
    backward,
    left,
    right,
    jump,
    crouch,
    sneak,
    sprint,
    /** ClientData action bit 0x80 (retail default Z). */
    hover,
    count,
};

/** Recovered tutorial loadout items (retail tool ids 5/2/17). */
enum class TutorialTool : std::uint8_t {
    block,
    spade,
    pistol,
};

/** One tick's primary-attack results, consumed by the frontend for feedback. */
struct TutorialAttackEvents final {
    bool pistol_fired{};
    bool pistol_dry{};
    bool reload_started{};
    bool spade_swung{};
    bool spade_hit_block{};
    bool block_placed{};
    int blocks_collapsed{};
    int targets_destroyed{};
    /** The shot that emptied the magazine dropped the sight (retail zoom_out cue). */
    bool zoom_dropped{};
};

enum class TutorialProjectileBehavior : std::uint8_t {
    bounce,
    contact,
    stick,
    deploy,
    /**
     * Bores straight through terrain instead of stopping at it.
     *
     * Retail implements this as one deliberate line in `Drill.update`: the
     * mover is set to stop on collision, and the drill saves its velocity
     * before stepping and writes it straight back whenever a collision is
     * reported. So it never stops and never bounces -- it grinds forward at a
     * constant speed through solid rock until its lifespan runs out. That
     * single trick is the entire feel of the weapon.
     */
    drill,
};

/** A bouncing grenade struck terrain (GRENADE_BOUNCE_SOUND source). */
struct ProjectileBounceEvent final {
    std::uint64_t projectile_id{};
    std::uint8_t tool_id{};
    Vec3 position{};
    /** Largest |velocity| component (blocks/s) before the 0.36 restitution. */
    double impact_speed{};
};

/** One locally simulated projectile emitted by the all-weapons test loadout. */
struct TutorialProjectile final {
    std::uint64_t id{};
    std::uint8_t tool_id{};
    Vec3 position{};
    /** Untouched packet/local launch origin used only by FPS muzzle presentation. */
    Vec3 spawn_position{};
    Vec3 velocity{};
    double presentation_age{};
    /** Autonomous launchers have no first-person weapon muzzle. */
    bool autonomous_source{};
    /**
     * Tool whose explosion bank the blast plays; 0 means `tool_id`. Rocket.delete
     * picks turr_rocketexplode (tool 20) for a turret rocket although entity 21
     * otherwise maps to the RPG (tool 12).
     */
    std::uint8_t explosion_sound_tool{};
    double remaining{};
    /**
     * Seconds for which retail's contact-only ``drill_loop`` remains live.
     *
     * The flight loop belongs to the projectile's whole lifetime. Each bore
     * contact refreshes this independent half-second tail; keeping the timer on
     * the projectile prevents a terrain packet from opening a detached global
     * loop after the projectile has already disappeared.
     */
    double drilling_audio_remaining{};
    double gravity_multiplier{1.0};
    TutorialProjectileBehavior behavior{TutorialProjectileBehavior::contact};
    double block_damage{};
    std::uint8_t crater_radius{1U};
    bool stuck{};
    /**
     * Spin about the projectile's own long axis, in degrees.
     *
     * Only the drill uses it: retail's Drill.update does `self.roll += 10.0`
     * every frame and feeds it in as a third rotation after yaw and pitch.
     * Without it the drill flies as a rigid dart, which is most of why it does
     * not read as a drill.
     */
    double roll{};
    /**
     * Player that originated a replicated packet-10 projectile.
     *
     * Local sandbox
     * projectiles leave this empty. The id is presentation
     * metadata only: damage and terrain
     * remain authoritative in a live match.
     */
    std::optional<std::uint8_t> source_player;
};

/**
 * Retail placement target shared by local simulation and Protocol 168.
 *
 * Deployable packets
 * carry the solid supporting voxel, not the adjacent air
 * cell used by block placement. `face`
 * identifies the selected surface; face
 * 4 is the top and is the only legal face for ground-only
 * gadgets.
 */
struct DeployableTarget final {
    std::array<std::int16_t, 3U> cell{};
    std::uint8_t face{4U};
};

struct TutorialSessionConfig final {
    /**
     * InitialInfo.block_wallet_multiplier (RULE_CHARACTER_BLOCK_WALLETS):
     * scales the class's initial and maximum block wallet.
     */
    double block_wallet_multiplier{1.0};
    /** Retail Controls slider value; 0.1 is the recovered default. */
    double mouse_sensitivity{0.1};
    bool invert_mouse{false};
    double fixed_dt{1.0 / 60.0};
    /** StateData(45) world gravity; LunarBase advertises 26/64. */
    double gravity{1.0};
    /** Live matches reuse the proven movement/camera shell without lessons. */
    bool network_authoritative{};
    Vec3 initial_position{140.5, 76.5, 230.75};
    Vec3 initial_orientation{-1.0, 0.0, 0.0};
    /** Latest owner WorldUpdate motion when entering an already-live scene. */
    Vec3 initial_velocity{};
    std::optional<bool> initial_airborne;
    bool initial_crouch{};
    bool initial_wade{};
    std::uint8_t initial_class_id{1U};
    /** RULE_CHARACTER_SPEED recovered from InitialInfo's class multiplier. */
    double movement_speed_scale{1.0};
    /** InitialInfo RULE_ENABLE_FALL_ON_WATER_DAMAGE (off zeroes the water multiplier). */
    bool fall_on_water_damage{true};
    std::vector<std::uint8_t> initial_loadout;
    std::vector<std::string> initial_prefabs;
    std::vector<std::uint8_t> initial_ugc_tools;
    std::optional<std::uint8_t> initial_tool;
    FlightProfile flight_profile;
};

struct TutorialDiagnostics final {
    Vec3 position{};
    Vec3 velocity{};
    double yaw{};
    double pitch{};
    bool airborne{};
    bool grounded{};
    bool wade{};
    bool crouch{};
    std::uint32_t chunk_x{};
    std::uint32_t chunk_y{};
};

/**
 * Owner of the playable Tutorial world state: the canonical map, the local
 * player, held input and the first-person camera.
 *
 * The session is renderer- and platform-free so its fixed 60 Hz simulation
 * is fully unit-testable. The frontend maps raw window input to semantic
 * actions and look deltas at its boundary; losing window focus must call
 * clear_input() so no movement or look state sticks.
 *
 * Spawn matches the recovered tutorial mode: lane origin (0,0) plus the
 * authored SPAWN_LOCAL (140.5, 76.5, 230.75), facing -x down the course.
 */
class TutorialWorldSession final {
public:
    TutorialWorldSession(std::shared_ptr<VxlMap> map, TutorialSessionConfig config = {});

    void set_action_held(TutorialAction action, bool held) noexcept;
    [[nodiscard]] bool action_held(TutorialAction action) const noexcept;
    void clear_input() noexcept;

    /** Replace authoritative nearby peer bodies for the next fixed step. */
    void set_player_collision_bodies(std::span<const PlayerCollisionBody> bodies);

    /** Replace the local prediction clamp from authoritative LockToZone(108). */
    [[nodiscard]] bool
    set_server_movement_bounds(PlayerMovementBounds bounds) noexcept;

    /** Applies one relative mouse motion in window counts. */
    void apply_look_delta(double delta_x, double delta_y) noexcept;

    /**
     * Replaces the look angles outright (retail degrees: yaw 0 faces -x,
     * positive pitch looks down). Used by the POIFocus(18) LookAtController
     * lock; the pitch is clamped exactly like mouse look.
     */
    void set_look_angles(double yaw_degrees, double pitch_degrees) noexcept;

    /**
     * Applies confirmed Settings input preferences to the already-live world.
     *
     * The session owns a copy of its launch configuration, so changing the
     * Settings draft cannot otherwise affect look until the next map. Values
     * are bounded here as well as by Settings validation because network and
     * standalone sessions share this runtime boundary.
     */
    void set_look_preferences(double mouse_sensitivity, bool invert_mouse) noexcept;

    /**
     * Primary attack (left mouse) held state. The pistol and block tool are
     * edge-triggered per press; the spade auto-swings while held at its
     * recovered 0.4 s interval.
     */
    void set_primary_held(bool held) noexcept;
    /** Right mouse: recovered iron sights, or a real secondary-tool action. */
    void set_secondary_held(bool held) noexcept;
    /** Right mouse currently held (PaintbrushTool's spray loop owner). */
    [[nodiscard]] bool secondary_held() const noexcept { return secondary_held_; }
    /** Weapon-custom input (retail default E; middle mouse is a test alias). */
    void trigger_weapon_custom() noexcept;
    void set_weapon_custom_held(bool held) noexcept;
    [[nodiscard]] WeaponStateResult request_reload() noexcept;
    void restock_ammunition() noexcept;
    /** Apply Restock(69) type 3 using retail's per-tool partial crate top-up. */
    void restock_from_ammo_crate() noexcept;
    void restock_blocks() noexcept;
    void restock_jetpack_fuel() noexcept;
    [[nodiscard]] double jetpack_fuel() const noexcept { return jetpack_prediction_.fuel; }
    /** Apply the local player's current TeamInfiniteBlocks authority bit. */
    void set_infinite_blocks(bool enabled) noexcept { infinite_blocks_ = enabled; }
    /** InitialInfo.can_shoot_holding_intel: Classic CTF keeps the gun. */
    void set_can_shoot_holding_intel(bool enabled) noexcept {
        can_shoot_holding_intel_ = enabled;
    }
    [[nodiscard]] bool infinite_blocks() const noexcept { return infinite_blocks_; }
    /** Apply BLOCK_GRANTING_DAMAGES (notably Block Sucker damage type 42). */
    void grant_blocks(std::uint16_t amount = 1U) noexcept;
    /**
     * Debit the shared wallet only after the server echoes accepted terrain.
     *
     *
     * Ordinary blocks, BlockLine cells, and expanded prefab voxels all pass
     * through this
     * acknowledgement boundary. Returns the amount applied and
     * clamps a stale local wallet
     * to zero instead of wrapping.
     */
    [[nodiscard]] std::uint16_t spend_server_confirmed_blocks(std::uint16_t amount = 1U) noexcept;
    /** Applies the server-acknowledged SetColor palette to held/built blocks. */
    void set_block_color(VxlColor color) noexcept;
    [[nodiscard]] VxlColor block_color() const noexcept {
        return block_color_;
    }
    /** Attack results from ticks since the last call, consumed once. */
    [[nodiscard]] TutorialAttackEvents take_attack_events() noexcept;
    /**
     * Chunk keys whose voxels changed since the last call (deduplicated,
     * neighbor-aware), consumed once; the frontend re-meshes exactly these.
     */
    [[nodiscard]] std::vector<ChunkKey> take_dirty_chunks();
    /** Unsupported terrain captured for a falling-structure presentation. */
    [[nodiscard]] std::vector<FallingComponent> take_falling_components();
    /** Block-hit particles/sounds captured independently of destruction. */
    [[nodiscard]] std::vector<TerrainImpactEvent> take_terrain_impacts();
    /** Bouncing-grenade terrain contacts since the last call (audio only). */
    [[nodiscard]] std::vector<ProjectileBounceEvent> take_projectile_bounces();

    [[nodiscard]] int pistol_clip() const noexcept;
    [[nodiscard]] int pistol_stock() const noexcept;
    [[nodiscard]] bool pistol_reloading() const noexcept;
    [[nodiscard]] int blocks_remaining() const noexcept;
    [[nodiscard]] int targets_destroyed() const noexcept;
    /** Seconds since the last primary use; drives the recoil animation. */
    [[nodiscard]] double seconds_since_primary() const noexcept;
    /**
     * Retail pullout timer: 0.5 s on every tool switch, decaying to zero;
     * the viewmodel rises from below while it runs.
     */
    [[nodiscard]] double pullout_remaining() const noexcept;
    [[nodiscard]] bool weapon_sandbox_enabled() const noexcept;
    [[nodiscard]] std::optional<std::uint8_t> selected_tool_id() const noexcept;
    /** Pixel radius used by retail's four accuracy-driven crosshair corners. */
    [[nodiscard]] double weapon_crosshair_radius_pixels(double viewport_height) const noexcept;
    /**
     * Minigun barrel speed, normalised to 0..1. Zero for every other weapon.
     *
     * Exposed for audio: retail's spin loop is pitched by exactly this value, so
     * the wind-on the player hears is a pitch ramp rather than a spin-up sample.
     */
    [[nodiscard]] double weapon_spin_fraction() const noexcept;
    /**
     * Whether the fire sound should still be sustained, matching retail's
     * `can_shoot_primary() && can_fire()`.
     *
     * The sprint term is not optional: sprinting blocks firing but does not
     * release the trigger, so without it a player who sprints mid-burst holds
     * the fire loop open indefinitely with no shots under it.
     */
    [[nodiscard]] bool weapon_trigger_live() const noexcept;
    /** Retail hides non-melee first-person tools for the whole sprint. */
    [[nodiscard]] bool weapon_view_model_visible() const noexcept;
    /**
     * Map Creator ScreenshotHud: retail flies a camera with the player's input
     * disabled, so no first-person tool is drawn while the preview is framed.
     */
    void set_view_model_suppressed(bool suppressed) noexcept { view_model_suppressed_ = suppressed; }
    /** Mounted gun deployment; it switches the weapon's whole sound shape. */
    [[nodiscard]] bool machine_gun_deployed() const noexcept;
    /** Character.is_deploying_weapon: the gun is unfolding. */
    [[nodiscard]] bool machine_gun_deploying() const noexcept;
    /**
     * HUD.draw_weapon_deployment_hud: MGWeapon.get_deployment_progress while
     * it is below 1, i.e. while the deploy or withdraw timer counts.
     */
    [[nodiscard]] std::optional<double> machine_gun_deployment_progress() const noexcept;
    /** Character.weapon_deployment_yaw for ClientData, in retail degrees. */
    [[nodiscard]] double weapon_deployment_yaw() const noexcept;
    /**
     * Movement state for footstep selection, as a ready-made input.
     *
     * Returned whole rather than as six accessors so the caller cannot assemble
     * a partial picture -- forgetting the sprint flag, say, would leave sprinting
     * players sounding like walkers with no obvious symptom.
     */
    [[nodiscard]] FootstepInput footstep_input() const noexcept;
    /**
     * Consume exact semantic movement edges emitted by the fixed-step solver.
     *
     *
     * Audio must use these instead of inferring a jump or landing from the
     * airborne bit:
     * retail's one-block auto-climb briefly changes contact
     * state and otherwise sounds like
     * a fall. Multiple unconsumed fixed steps
     * are coalesced, with the strongest landing
     * result retained.
     */
    [[nodiscard]] MovementStepResult take_movement_events() noexcept;
    [[nodiscard]] const ToolAmmoState* selected_ammo() const noexcept;
    [[nodiscard]] double weapon_reload_remaining() const noexcept;
    /** True for either a non-magnified sight or a magnified sniper scope. */
    [[nodiscard]] bool zoomed() const noexcept;
    /** True only for the sniper-family magnified scope behavior. */
    [[nodiscard]] bool magnified_scope() const noexcept;
    /**
     * Retail `character.zoom`: the multiplier the sight ramp is heading for.
     *
     * Flips the instant right mouse is pressed, which is what retail gates
     * aimed mouse sensitivity on. Zero means hip fire; it is NOT 1.0, because
     * a multiplier of 1.0 is already a 2x view.
     */
    [[nodiscard]] double zoom_target() const noexcept;
    /** Local presentation preference, scoped to the held tool; never changes weapon stats. */
    void set_skin_zoom(std::uint8_t tool, std::optional<double> target) noexcept;
    /**
     * Retail `zoom_level`: where the ramp has actually reached.
     *
     * This, not the target, is what the projection must be built from. Reading
     * the target instead is what turns the transition into a snap.
     */
    [[nodiscard]] double zoom_level() const noexcept;
    [[nodiscard]] double weapon_mechanism_phase() const noexcept;
    /** Retail Block Sucker shake shared by the first-person tool and hands. */
    [[nodiscard]] std::array<double, 3U> weapon_view_model_shake() const noexcept;
    [[nodiscard]] std::uint8_t weapon_block_sucker_state() const noexcept;
    [[nodiscard]] std::span<const TutorialProjectile> projectiles() const noexcept;

    /**
     * Materialize a server-relayed UseOrientedItem(10) as a cosmetic projectile.
     *

     * * The packet already carries its canonical position, velocity and remaining
     * fuse, so
     * this path must not recompute any of them from the observing
     * client's camera. In a
     * network-authoritative session the projectile emits
     * flight/impact presentation but
     * never mutates terrain.
     */
    [[nodiscard]] bool apply_server_oriented_item(
        std::uint8_t player_id, std::uint8_t tool_id, double value, Vec3 position, Vec3 velocity);

    /**
     * Refresh retail's half-second Drill contact loop from Damage(37).
     *
     * ``causer_id`` is the authoritative CreateEntity id carried by the
     * packet. Older/UGC relays may omit a usable entity id, so the method falls
     * back to the nearest packet-10 Drill owned by ``player_id``. It changes
     * presentation state only and never mutates terrain.
     */
    [[nodiscard]] bool apply_server_drill_contact(
        std::int16_t causer_id, std::uint8_t player_id, Vec3 position) noexcept;

    // -- Local entities -----------------------------------------------------
    //
    // Deliberately named `spawn_entity` rather than anything that reads as
    // authoritative: a later network stage adds a separate
    // `apply_server_entity`, exactly as `apply_authoritative_transform` is kept
    // distinct from local movement, so the debug spawner can never be mistaken
    // for the server path.

    /** Places one entity. Returns 0 when the renderer slot band is full. */
    std::uint64_t
    spawn_entity(std::uint8_t type, Vec3 position, std::uint8_t team = 0U, std::uint8_t face = 4U);
    /**
     * Install one authoritative packet-21 entity without allocating a local id.
     *

     * * Runs on the fixed gameplay thread. Duplicate wire ids are deliberately
     * ignored,
     * matching retail; malformed types/faces or a full renderer-part
     * budget fail closed and
     * leave the existing world untouched.
     */
    bool apply_server_entity(LocalEntity entity);
    /**
     * Refresh a live entity from WorldUpdate's full Entity row.
     *
     * Unlike
     * duplicate CreateEntity, this path intentionally updates an
     * existing id. It never runs
     * entity behaviour and never creates a missing
     * object; reliable packet 21 remains the
     * sole creation edge.
     */
    bool apply_server_entity_snapshot(
        const LocalEntity& entity, std::optional<std::int32_t> world_loop = std::nullopt);
    /** Apply the compact server-owned rocket-turret yaw/pitch row. */
    bool apply_server_turret_aim(
        std::uint64_t id, double yaw, double pitch,
        std::optional<std::int32_t> world_loop = std::nullopt) noexcept;
    /**
     * Apply one validated ChangeEntity(16) field mutation.
     *
     * Runs on the fixed
     * gameplay thread. Unknown ids, non-finite vectors, and
     * invalid target/ammo values fail
     * closed without partially changing the
     * entity. Position updates also move a pickup's
     * respawn home.
     */
    bool apply_server_entity_update(const ServerEntityMutation& mutation);
    bool despawn_entity(std::uint64_t id) noexcept;
    /**
     * Consume DestroyEntity(19), emitting the retail delete VFX for projectiles.
     *

     * * Projectile position is locally integrated between CreateEntity and this
     *
     * authoritative destruction edge. Other entity types retain the ordinary
     * silent despawn
     * contract. `explosion_sound_tool` overrides the blast's sound bank (a
     * turret-owned entity 21 explodes with turr_rocketexplode, tool 20).
     */
    bool destroy_server_entity(std::uint64_t id,
                               std::optional<std::uint8_t> explosion_sound_tool = std::nullopt);
    /**
     * Remove a server entity without any delete effect and hand it back. The
     * flying sticky grenade (34) has no on_delete in retail: the server
     * destroys it when it sticks and the stuck grenade (35) explodes later.
     */
    [[nodiscard]] std::optional<LocalEntity> take_server_entity(std::uint64_t id);
    /** The delete effect `destroy_server_entity` gives a projectile. */
    void present_server_entity_blast(const LocalEntity& entity);
    /**
     * Carry an attached entity (AttachedStickyGrenadeEntity.update,
     * RiotShieldEntity.get_position) to where its player now is. Position only.
     */
    bool carry_server_entity(std::uint64_t id, Vec3 position) noexcept;
    void clear_entities() noexcept;
    [[nodiscard]] std::span<const LocalEntity> entities() const noexcept;
    [[nodiscard]] std::vector<EntityEvent> take_entity_events();
    /**
     * Hard ceiling on live entities, in PARTS.
     *
     * Budgeted per part rather than per entity because the rocket turret is
     * three meshes and UGC markers are two. Exceeding it refuses the spawn
     * outright instead of silently dropping the mesh, because an entity that
     * simulates but never appears is a debugging trap.
     */
    [[nodiscard]] std::size_t entity_part_budget() const noexcept;
    void set_entity_part_budget(std::size_t parts) noexcept;
    [[nodiscard]] std::size_t entity_parts_in_use() const noexcept;

    /** Debug spawn-menu selection, kept here so it is unit-testable. */
    [[nodiscard]] std::uint8_t debug_selected_entity_type() const noexcept;
    void debug_select_entity_index(std::size_t index) noexcept;
    [[nodiscard]] std::size_t debug_selected_entity_index() const noexcept;
    void debug_cycle_entity_type(int direction) noexcept;
    /** One of every spawnable entity, laid out in a row for a single screenshot. */
    std::size_t debug_spawn_every_entity();

    // -- Local player health ------------------------------------------------

    [[nodiscard]] double health() const noexcept;
    [[nodiscard]] bool alive() const noexcept;
    /** Returns the amount actually applied after clamping to 0..100. */
    double heal(double amount);
    double apply_damage(double amount, std::uint8_t kill_type = 0U);
    void reset_health() noexcept;
    /**
     * Replace local health from SetHP/WorldUpdate/CreatePlayer without
     * fabricating
     * an offline grave or mutating authoritative terrain.
     *
     * Crossing into death clears
     * every held gameplay input immediately.
     * Crossing into a new life clears the deferred
     * offline-death latch. This
     * method runs on the fixed gameplay thread.
     */
    void set_server_health(double health) noexcept;

    [[nodiscard]] std::vector<WeaponAction> take_weapon_actions();
    [[nodiscard]] std::uint64_t weapon_action_sequence() const noexcept;
    [[nodiscard]] std::string_view selected_prefab() const noexcept;
    [[nodiscard]] std::optional<std::uint8_t> selected_ugc_item() const noexcept;
    /** Cycle the currently selected UGC Game Data slot within its RMB group. */
    [[nodiscard]] bool cycle_selected_ugc_item_variant() noexcept;
    /**
     * Adjacent empty voxel under the crosshair. Unlike placement_position(),
     * a miss
     * stays empty so a placement packet can never use player position.
     */
    [[nodiscard]] std::optional<std::array<std::int16_t, 3U>>
    placement_cell(double range = 10.0) const noexcept;
    /**
     * UGCTool ghost cell: can_place_object(..., can_place_vertical=False)
     * (gameScene 0x101270b0) accepts only the TOP face of the pointed solid,
     * so Game Data markers can never hang on walls or ceilings.
     */
    [[nodiscard]] std::optional<std::array<std::int16_t, 3U>>
    ugc_marker_cell(double range = 10.0) const noexcept;
    /** Color of the solid voxel under the crosshair for retail's eyedropper. */
    [[nodiscard]] std::optional<VxlColor> looked_at_block_color(double range = 10.0) const noexcept;
    /**
     * Resolve the exact raw-voxel placement encoded by retail gadget packets.
     *
     * Returns empty on a miss, illegal face/range, or nearby entity overlap.
     * Mounted-gun
     * deployment is the one exception: packet 87 uses the
     * player's current voxel because the
     * gun unfolds around its carrier.
     */
    [[nodiscard]] std::optional<DeployableTarget>
    deployable_target(std::uint8_t tool_id) const noexcept;
    /** Adjacent solid-face target used by block/prefab/deployable packets. */
    [[nodiscard]] Vec3 placement_position(double range = 10.0) const noexcept;
    /** Retail throw/launcher velocity including the player's live velocity. */
    [[nodiscard]] Vec3 action_velocity(const WeaponAction& action) const noexcept;

    /** Advances exactly one fixed simulation step. */
    void tick();

    [[nodiscard]] const VxlMap& map() const noexcept;

    /**
     * Installs the server's ground colour table (InitialInfo / packet 118)
     * on the shared map. Presentation only: it colours implicit interior
     * voxels and never changes solidity.
     */
    void set_ground_colors(std::span<const std::array<std::uint8_t, 4U>> rows) noexcept {
        map_->set_ground_colors(rows);
    }

    /**
     * Point lights placed by flare blocks, baked into terrain at mesh time.
     *
     * The renderer never sees this: the mesher samples it, exactly as retail
     * folded its static lights into vertex colours.
     */
    [[nodiscard]] const StaticLightField& static_lights() const noexcept {
        return static_lights_;
    }
    [[nodiscard]] const PlayerMovementState& player() const noexcept;
    /** Retail look angles in degrees; yaw 0 faces -x, pitch positive down. */
    [[nodiscard]] double yaw() const noexcept;
    [[nodiscard]] double pitch() const noexcept;

    /** Recovered lesson progression driven by this session's movement. */
    [[nodiscard]] const TutorialLessons& lessons() const noexcept;
    /** Stage entered during the most recent tick, consumed by the caller. */
    [[nodiscard]] std::optional<TutorialLessonStage> take_entered_stage() noexcept;

    /**
     * Recovered loadout grants: empty hands through the movement lessons,
     * the pistol at SHOOTING, block tool and spade at CLIMB; each grant
     * auto-equips its final item exactly like SetClassLoadout instant=1.
     */
    [[nodiscard]] std::vector<TutorialTool> unlocked_tools() const;
    [[nodiscard]] std::optional<TutorialTool> equipped_tool() const noexcept;
    void
    equip_tool(TutorialTool tool,
               InventorySelectionOrigin origin = InventorySelectionOrigin::direct_slot) noexcept;
    /** Direct retail number-key slot selection (1..0 maps to indices 0..9). */
    [[nodiscard]] bool equip_inventory_slot(std::size_t index) noexcept;
    void cycle_tool(int direction) noexcept;
    [[nodiscard]] const RetailInventory& inventory() const noexcept;
    /** Concrete prefab names parallel to InventorySlotKind::prefab variants. */
    [[nodiscard]] std::span<const std::string> inventory_prefabs() const noexcept;
    [[nodiscard]] std::optional<InventorySelectionEvent> take_inventory_event() noexcept;
    /** Developer shortcut for visual iteration; not a retail path. */
    void debug_grant_full_loadout() noexcept;
    /** Cycle the active first-person class while retaining the selected tool. */
    void debug_cycle_class(int direction) noexcept;
    [[nodiscard]] std::uint8_t debug_class_id() const noexcept;
    /** First-person eye in canonical coordinates (the retail anchor). */
    [[nodiscard]] std::array<double, 3U> eye_position() const noexcept;
    [[nodiscard]] TutorialDiagnostics diagnostics() const noexcept;
    [[nodiscard]] std::uint64_t ticks_simulated() const noexcept;
    [[nodiscard]] bool network_authoritative() const noexcept;
    /** Exact ClientData input bytes for the current fixed-tick held state. */
    [[nodiscard]] std::uint8_t movement_flags() const noexcept;
    /** action_held OR the latched value the last tick simulated. */
    [[nodiscard]] bool sent_held(TutorialAction action) const noexcept;
    [[nodiscard]] std::uint8_t action_flags() const noexcept;
    /**
     * Start a new authoritative movement generation for spawn/respawn.
     * Clears prior-life collision, camera and fixed-step input-latch state.
     */
    void
    apply_authoritative_transform(Vec3 position, Vec3 orientation, Vec3 velocity = {}) noexcept;
    /**
     * Cache the newest owner WorldUpdate position used by retail's jump
     * launch path. Rows are ACK ordered so a delayed packet cannot rewind
     * the launch anchor.
     */
    void note_authoritative_snapshot(std::int32_t acknowledged_loop, Vec3 position) noexcept;
    /**
     * Journal the movement frame just sent as ClientData.
     *
     * Retail keeps the
     * consumed input and complete post-step state for every
     * unacknowledged loop. WorldUpdate
     * correction restores the ACKed server
     * state and replays the later frames; adding one
     * coordinate delta to every
     * sample is not equivalent when a jump, crouch, edge or voxel
     * collision is
     * involved.
     */
    void record_network_prediction(std::int32_t client_loop);
    /**
     * Apply one owner WorldUpdate using retail's ACK-aligned replay.
     *
     * Returns
     * false when the exact loop has already fallen out of the bounded
     * journal. Sub-0.1
     * position errors are deliberately ignored, including
     * velocity-only differences,
     * matching Character.pyx.
     */
    [[nodiscard]] bool
    reconcile_authoritative(std::int32_t acknowledged_loop, Vec3 position, Vec3 velocity);
    /**
     * Apply an ACK-aligned correction without rewinding current look/input.
     * Simulation moves immediately; the first-person camera eases out the
     * inverse visual offset so ordinary network corrections never teleport.
     */
    void apply_authoritative_delta(Vec3 position_delta, Vec3 velocity_delta) noexcept;
    /**
     * ExplosionDamageManager.handle_damage's push on the local character,
     * predicted on receipt of a stock Damage(37) blast (world/retail_blast.hpp).
     * Returns the impulse applied, if any.
     */
    std::optional<Vec3> apply_blast_push(std::uint8_t damage_type, Vec3 explosion) noexcept;
    /** Replace the complete local inventory with the server-normalized transaction. */
    [[nodiscard]] bool
    apply_server_selection(std::uint8_t class_id,
                           std::span<const std::uint8_t> loadout,
                           std::span<const std::string> prefabs,
                           std::span<const std::uint8_t> ugc_tools = {},
                           std::optional<std::uint8_t> selected_tool = std::nullopt);
    /** Apply an ACK-covered owner WorldUpdate tool without trusting stale rows. */
    [[nodiscard]] bool apply_server_tool(std::uint8_t tool_id) noexcept;
    /**
     * Apply replicated movement abilities from an ACKed owner WorldUpdate.
     * Action bit 0x04 is active jetpack, state bit 0x01 is parachute, and a
     * 0xFF pickup clears burden. Equipment still comes only from loadout 13.
     */
    void apply_server_movement_state(std::uint8_t action_flags,
                                     std::uint8_t state_flags,
                                     std::uint8_t pickup_id,
                                     std::optional<std::int32_t> acknowledged_loop = std::nullopt) noexcept;
    /** Decoded 0..100 fuel, advanced through retained frames after this ACK. */
    void apply_server_jetpack_fuel(
        double fuel, std::optional<std::int32_t> acknowledged_loop = std::nullopt) noexcept;
    /** Apply PickPickup(70)'s authoritative burdensome byte. */
    void apply_server_pickup_burden(bool burdened) noexcept;
    /** Apply the server-normalized class movement profile after respawn. */
    /** Apply the class and its InitialInfo-derived RULE_CHARACTER_SPEED scale atomically. */
    void apply_server_class(std::uint8_t class_id, double movement_speed_scale) noexcept;

private:
    /** Flare-block point lights, baked into terrain vertex colours. */
    StaticLightField static_lights_;

    /**
     * Authoritative type-13 FlareBlockEntity rows.
     *
     * Retail `FlareBlockEntity.post_initialize` is terrain, not a model: it
     * calls `add_user_block(x,y,z,RGB,5)` and `add_static_point_light(x,y,z,
     * RGB,FLAREBLOCK_LIGHT_RADIUS)`. Kept outside `entities_` so hundreds of
     * map flare markers (524 on 20thCenturyTown) never consume renderer part
     * slots, and so DestroyEntity can take the voxel and the light back.
     */
    struct ServerFlare final {
        std::uint64_t id{};
        std::array<std::uint32_t, 3U> cell{};
        bool lit{true};
    };
    std::vector<ServerFlare> server_flares_;
    /** Re-mesh every chunk column a static light can reach. */
    void mark_light_dirty(const StaticLight& light);
    /** Install one networked FlareBlockEntity (voxel + static light). */
    bool apply_server_flare(const LocalEntity& entity);
    /** Drop a flare light whose voxel the terrain replica has destroyed. */
    void refresh_server_flares();
    /**
     * BlockFire (28) and BlockGoo (31) static point lights.
     *
     * Retail registers add_static_point_light(BLOCKFIRE_LIGHT_RADIUS) and
     * re-colours it with update_static_light_colour as calculate_colour
     * ramps. Baked like flares, so every shader tier (Retail included) sees
     * the glow. Re-coloured four times per second to bound re-meshing.
     */
    struct PatchLight final {
        std::array<std::uint32_t, 3U> cell{};
        double clock{};
    };
    std::unordered_map<std::uint64_t, PatchLight> patch_lights_;
    void refresh_patch_lights();

    void handle_stage_entered();
    void update_combat();
    void update_weapon_sandbox();
    void update_reload_aim() noexcept;
    void process_weapon_action(const WeaponAction& action);
    void spawn_projectile(const WeaponAction& action, const WeaponDefinition& weapon);
    void update_projectiles();
    void update_entities();
    /** Places a deployable tool's entity at the crosshair. Returns false if refused. */
    bool place_deployable(const WeaponAction& action);
    /** Throws the carried objective back into the world. */
    bool drop_carried_objective(const WeaponAction& action);
    /** Detonates every live C4 this player owns. */
    std::size_t detonate_all_c4();
    /** Spawns the death grave and drops anything the player was carrying. */
    void handle_player_death();
    /** Respawn, fixed-step terrain physics, fuse/arm/lifetime timers. */
    void step_entity_timers(LocalEntity& entity, const EntityDefinition& definition);
    void step_entity_behaviour(LocalEntity& entity, const EntityDefinition& definition);
    void step_turret(LocalEntity& entity, const EntityDefinition& definition);
    /**
     * One shared blast for every entity that explodes.
     *
     * Reuses the projectile crater path so craters, falling structures and
     * explosion VFX come free, and adds the player falloff term that
     * `explode_projectile` has never had.
     */
    void detonate_entity(LocalEntity& entity, const EntityDefinition& definition);
    void explode_projectile(const TutorialProjectile& projectile, std::optional<VoxelCell> contact);
    void apply_melee_terrain(const WeaponAction& action,
                             const WeaponDefinition& weapon,
                             const VoxelCell& center,
                             const TerrainImpactEvent& impact);
    void apply_weapon_recoil(const WeaponAction& action, const WeaponDefinition& weapon) noexcept;
    void fire_pistol();
    void swing_spade();
    /** `emits_light` registers a flare-block static point light at the voxel. */
    void place_block(bool emits_light = false);
    [[nodiscard]] bool damage_voxel(
        std::uint32_t x, std::uint32_t y, std::uint32_t z, double damage, bool collapse = true);
    void destroy_target(std::size_t index);
    void mark_dirty(std::uint32_t x, std::uint32_t y);
    /** Cancel outgoing input and viewmodel state after an accepted selection. */
    void reset_tool_transition_state() noexcept;
    void sync_inventory_slots(std::optional<TutorialTool> preferred,
                              InventorySelectionOrigin origin);
    /** Resolve non-selectable movement equipment from one normalized loadout. */
    void sync_movement_equipment(std::span<const std::uint8_t> loadout) noexcept;
    void conceal_authoritative_correction(Vec3 position_delta) noexcept;

    struct JetpackPredictionState final {
        double fuel{100.0};
        double held_seconds{};
        double refill_delay_remaining{};
        double idle_seconds{};
        bool advertised_active{};
        bool physics_active{};
        bool requires_release{};
        std::uint8_t activation_defer{};
        std::uint8_t exhaustion_tail{};
    };
    void advance_jetpack_prediction(JetpackPredictionState& state,
                                          std::uint8_t pack,
                                          const PlayerInputState& input,
                                          double dt, bool damaged, bool grounded) noexcept;
    void replay_jetpack_prediction(std::int32_t acknowledged_loop) noexcept;
    void replay_parachute_prediction(std::int32_t acknowledged_loop, bool active) noexcept;

    struct NetworkPredictionSample final {
        std::int32_t loop{};
        PlayerMovementState state{};
        PlayerInputState consumed_input{};
        Vec3 consumed_orientation{};
        /** Thrust consumed before the frame's fuel drain can deactivate it. */
        bool consumed_jetpack_active{};
        bool consumed_jetpack_damage{};
        bool consumed_parachute_active{};
        bool parachute_deploy_pressed{};
        JetpackPredictionState jetpack{};
        /** A restock received after this frame survives older ACK replay. */
        bool jetpack_restocked{};
        MovementClassConfig movement_class{};
        std::vector<PlayerCollisionBody> collision_bodies;
    };

    std::shared_ptr<VxlMap> map_;
    TutorialSessionConfig config_;
    MovementClassConfig movement_class_{};
    PlayerMovementState player_{};
    TutorialLessons lessons_{};
    std::optional<TutorialLessonStage> entered_stage_{};
    RetailInventory inventory_{};
    PlayerInventory sandbox_inventory_{};
    bool debug_full_loadout_{};
    std::array<bool, static_cast<std::size_t>(TutorialAction::count)> held_{};
    /**
     * Key-down edges latched until the next fixed tick consumes them. A tap
     * whose press and release land in the same event poll (a stalled frame,
     * a sub-16 ms tap) still reaches one simulated tick and one ClientData,
     * as retail's latched key requests do (an 80 ms airborne SPACE tap
     * deploys the parachute).
     */
    std::array<bool, static_cast<std::size_t>(TutorialAction::count)> press_latch_{};
    /** held_ OR press_latch_ as sampled by the most recent tick. */
    std::array<bool, static_cast<std::size_t>(TutorialAction::count)> tick_held_{};
    std::vector<PlayerCollisionBody> player_collision_bodies_;
    std::optional<PlayerMovementBounds> server_movement_bounds_{};
    bool jump_requested_{};
    MovementStepResult movement_events_{};
    /** Retail sends current flags after simulating the preceding held frame. */
    PlayerInputState network_latched_input_{};
    std::optional<Vec3> network_latched_orientation_;
    /** Input actually consumed by the most recently completed native step. */
    PlayerInputState last_simulated_input_{};
    bool last_simulated_jetpack_active_{};
    bool parachute_deploy_last_held_{};
    /** Airborne SPACE edge: retail's parachute trigger (P1-19). */
    bool parachute_jump_last_held_{};
    /** Presentation only: this fall used a canopy (landing-damage cue). */
    bool parachute_fall_touched_{};
    bool last_simulated_parachute_active_{};
    bool last_simulated_parachute_pressed_{};
    JetpackPredictionState jetpack_prediction_{};
    bool jetpack_damage_pending_{};
    bool last_simulated_jetpack_damage_{};
    bool jetpack_replay_required_{};
    std::int32_t last_jetpack_fuel_loop_{std::numeric_limits<std::int32_t>::min()};
    std::int32_t last_movement_state_loop_{std::numeric_limits<std::int32_t>::min()};
    Vec3 last_simulated_orientation_{};
    /** Exact bounded movement journal used by owner WorldUpdate replay. */
    std::deque<NetworkPredictionSample> network_predictions_;
    double yaw_{};
    double pitch_{};
    std::uint64_t ticks_{};
    /**
     * Retail Character.interpolated_position and its 0.1-second correction
     * window.
     * Physics remains authoritative; only the presented eye follows
     * this position while the
     * timer is live.
     */
    Vec3 network_interpolated_position_{};
    double position_lerp_timer_{};
    /** Latest ACK-ordered owner row retained for diagnostics only. */
    Vec3 last_authoritative_position_{};
    std::int32_t last_authoritative_position_loop_{std::numeric_limits<std::int32_t>::min()};
    /** Last owner ACK whose state was applied; duplicate unreliable rows are inert. */
    std::int32_t last_reconciled_loop_{std::numeric_limits<std::int32_t>::min()};

    bool primary_held_{};
    bool primary_edge_{};
    /** Primary press latched until a tick consumes it (see press_latch_). */
    bool primary_press_latch_{};
    /** primary_held_ OR primary_press_latch_ as sampled by the last tick. */
    bool primary_tick_held_{};
    bool secondary_held_{};
    bool custom_edge_{};
    bool custom_held_{};
    bool zoomed_{};
    bool empty_magazine_unzoom_pending_{};
    std::optional<std::uint8_t> reload_aim_tool_;
    /** Ramped sight state; 0 is hip fire. Advanced on the fixed tick only. */
    double zoom_level_{};
    std::optional<std::pair<std::uint8_t,double>> skin_zoom_;
    void present_projectile_blast(const LocalEntity& entity,
                                  std::optional<std::uint8_t> explosion_sound_tool);
    /** MGWeapon deployment of the held mounted gun (tool 15). */
    MachineGunDeployment machine_gun_;
    void advance_machine_gun_deployment();
    /** MGWeapon.on_unset: a tool change or a death folds the gun at once. */
    void fold_machine_gun();
    /** Last server-authoritative WorldUpdate disguise bit for action gating. */
    bool disguise_active_{};
    double tool_cooldown_{};
    double reload_remaining_{};
    /** Character.pullout=0.5 when a non-melee sprint ends. */
    double sprint_pullout_remaining_{};
    double since_primary_{1e9};
    int pistol_clip_{};
    int pistol_stock_{};
    int blocks_remaining_{};
    bool infinite_blocks_{};
    bool can_shoot_holding_intel_{};
    int blocks_built_{};
    bool view_model_suppressed_{};
    VxlColor block_color_{110U, 110U, 110U, 255U};
    std::array<bool, 5U> target_down_{};
    bool shooting_gate_reported_{};
    bool climb_gate_reported_{};
    TutorialAttackEvents attack_events_{};
    std::vector<ChunkKey> dirty_chunks_;
    static constexpr std::uint32_t dirty_chunk_edge{16U};
    static constexpr std::uint32_t dirty_chunk_columns{
        (VxlMap::width + dirty_chunk_edge - 1U) / dirty_chunk_edge};
    static constexpr std::uint32_t dirty_chunk_rows{
        (VxlMap::depth + dirty_chunk_edge - 1U) / dirty_chunk_edge};
    std::bitset<dirty_chunk_columns * dirty_chunk_rows> dirty_chunk_membership_;
    std::vector<FallingComponent> falling_components_;
    std::vector<TerrainImpactEvent> terrain_impacts_;
    std::vector<ProjectileBounceEvent> projectile_bounces_;
    std::vector<WeaponAction> weapon_actions_;
    std::vector<TutorialProjectile> projectiles_;
    std::uint64_t next_projectile_id_{1U};
    std::vector<LocalEntity> entities_;
    std::vector<EntityEvent> entity_events_;
    std::uint64_t next_entity_id_{1U};
    /** Matches render::WorldRenderer::entity_slot_count; see the header note. */
    std::size_t entity_part_budget_{256U};
    std::size_t debug_entity_index_{};
    /** The objective entity type currently in hand, or 0 for none. */
    std::uint8_t carried_objective_{};
    /** Seconds before a just-dropped objective may be picked up again. */
    double objective_pickup_lockout_{};
    /** Set the tick the player's health reached zero, consumed by the frontend. */
    bool death_pending_{};
    double health_{maximum_player_health};
    std::uint64_t weapon_action_sequence_{};
    /** Accumulated block damage keyed by packed voxel coordinate. */
    std::unordered_map<std::uint32_t, double> block_damage_;
};

} // namespace battlespades::world
