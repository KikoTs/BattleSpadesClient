#pragma once

#include "battlespades/world/weapon_state.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace battlespades::world {

/** Semantic result of a retail tool state machine; sessions add world data. */
enum class WeaponActionKind : std::uint8_t {
    dry_fire,
    hitscan,
    melee,
    oriented_item,
    block_line_begin,
    block_line_cancel,
    block_line_commit,
    flare_place,
    prefab_place,
    prefab_rotate,
    deployable_place,
    c4_detonate,
    objective_use,
    ugc_entity_use,
    paint_single,
    paint_area,
    block_suck,
    block_sucker_state,
    disguise_activate,
    color_pick,
    /** Cooked-grenade pin edge emitted when the hold begins. */
    throwable_primed,
    reload_started,
    reload_completed,
    /**
     * Presentation only: a trigger press refused because the placement
     * target is invalid (BUILD_ERROR_SOUND). Never sent to the server.
     */
    placement_rejected,
};

/**
 * Packet-neutral action emitted by WeaponRuntime.
 *
 * `value` is the remaining fuse for cooked grenades, normalized charge for
 * charged throwables, and Block Sucker state (0/1/2) for state edges.
 */
struct WeaponAction final {
    WeaponActionKind kind{WeaponActionKind::dry_fire};
    std::uint8_t tool_id{};
    std::uint8_t seed{};
    std::uint8_t pellets{1U};
    bool secondary{};
    double value{};
    /**
     * Rounds 2..N of a burst. PRESENTATION ONLY.
     *
     * Retail plays one whole three-round recording when the burst STARTS and
     * nothing at all on the follow-up rounds, because the burst weapon clears
     * its own play_shoot_sound flag. The rounds are identical to the opener in
     * every other respect, so this exists purely so the audio layer can stay
     * silent for them -- playing a three-round sample per round stacks three
     * bursts of audio over one trigger pull.
     *
     * Appended last and default-false so the other emit paths are untouched,
     * and never serialised: the wire marshals its own context struct.
     */
    bool burst_follow_up{};
    /**
     * Weapon.accuracy at the moment of the shot (hitscan only).
     *
     * Retail prep_shoot() derives it from the current accuracy_spread BEFORE
     * shot_weapon() grows the spread, and shoot_bullet() scatters with it, so
     * the local pellet/impact FX must bloom exactly like the server's cloud.
     * Never serialised.
     */
    double accuracy{};
};

struct WeaponRuntimeContext final {
    bool sprinting{};
    bool deployed{};
    bool disguise_active{};
    /** False suppresses a placement edge before it can spend local stock. */
    bool deployable_target_valid{true};
};

/**
 * Shared implementation of all 65 retail tool input/state machines.
 *
 * It owns timing, ammo, reload, automatic fire, assault burst, minigun
 * spin-up, grenade cooking, charged throws and special tool edges. It never
 * raycasts or mutates terrain. The offline or network session validates the
 * emitted semantic action and supplies positions/orientations to Protocol 168.
 */
class WeaponRuntime final {
public:
    explicit WeaponRuntime(std::uint32_t random_seed = 0xA05B168U) noexcept;
    void set_classic_protocol(std::uint8_t protocol) noexcept;
    void classic_reload_completed(std::uint8_t magazine, std::uint8_t reserve) noexcept;

    void replace_loadout(std::span<const std::uint8_t> tool_ids,
                         std::optional<std::uint8_t> selected = std::nullopt);
    [[nodiscard]] WeaponStateResult select(std::uint8_t tool_id) noexcept;

    void set_primary(bool held) noexcept;
    void set_secondary(bool held) noexcept;
    /** Retail weapon-custom held state (default E; distinct from RMB). */
    void set_custom(bool held) noexcept;
    void set_context(WeaponRuntimeContext context) noexcept;
    [[nodiscard]] WeaponStateResult request_reload() noexcept;
    /** Refill only tool ammunition/counts; blocks and health live elsewhere. */
    void restock_ammunition() noexcept;
    /** Partial ammo-crate top-up; see WeaponReplicationState for the split. */
    [[nodiscard]] bool restock_from_ammo_crate() noexcept;
    /**
     * Retail Tool.on_unset lifecycle boundary.
     *
     * Unlike cancel_interaction(), this is a hard reset used by tool changes,
     * death, class replacement, and scene teardown. It cancels reload state,
     * pending actions, cooldowns, spread, and mechanism-specific motors.
     */
    void on_unset() noexcept;
    void cancel_interaction() noexcept;
    void tick(double dt) noexcept;

    [[nodiscard]] std::vector<WeaponAction> take_actions();
    [[nodiscard]] const WeaponReplicationState& replication() const noexcept;
    [[nodiscard]] double cooldown_remaining() const noexcept;
    [[nodiscard]] double reload_remaining() const noexcept;
    [[nodiscard]] double charge_fraction() const noexcept;
    /** Active hold duration for grenade/viewmodel presentation, or -1. */
    [[nodiscard]] double interaction_elapsed() const noexcept;
    /** Original spade charge clock, shared by presentation and the dig action. */
    [[nodiscard]] double classic_dig_progress() const noexcept;
    [[nodiscard]] bool classic_block_dragging() const noexcept { return classic_timing_.block_dragging; }
    /** A queued underfoot placement starts its cooldown when it actually builds. */
    void classic_block_placed() noexcept { classic_timing_.block = classic_timing_.time + 0.5; }
    [[nodiscard]] double current_accuracy() const noexcept;
    /**
     * Retail crosshair corner radius for the current weapon and projection.
     *
     * Weapon.get_accuracy uses two deliberately different paths. Tools whose
     * accuracy and aimed accuracy are both absent use the fixed
     * `accuracy_spread * 6` radius (melee, blocks and throwables). Firearms
     * project their current shot cone through the vertical field of view, so
     * recoil growth and recovery move the four corner sprites by the same
     * amount as the bullets. The returned value is already in window pixels.
     */
    [[nodiscard]] double crosshair_radius_pixels(double viewport_height,
                                                 double fov_y_degrees,
                                                 bool zoomed) const noexcept;
    [[nodiscard]] double spin_fraction() const noexcept;
    /** Accumulated AnimRoll barrel orientation normalized to one revolution. */
    [[nodiscard]] double spin_rotation_fraction() const noexcept;
    /** Authoritative local Block Sucker state: 0=off, 1=warm-up, 2=full power. */
    [[nodiscard]] std::uint8_t block_sucker_state() const noexcept;
    /**
     * Retail AnimBlockSucker translation applied to both the tool and hands.
     * Values are in first-person model-space units and include the one-second
     * release settle-down.
     */
    [[nodiscard]] std::array<double, 3U> block_sucker_shake() const noexcept;
    /**
     * Retail Character.can_shoot_* sprint predicate, without character state.
     *
     * Both native methods exempt ALL_MELEE_WEAPONS before consulting the
     * matching Tool.can_shoot_*_while_sprinting class attribute. Keeping this
     * public lets audio and presentation use the exact same eligibility rule
     * as packet-producing actions.
     */
    [[nodiscard]] static bool can_use_while_sprinting(
        const WeaponDefinition& weapon, bool secondary = false) noexcept;
    /**
     * Retail Tool.can_swap: false while the SpadeTool's inert secondary lock
     * (active_secondary, SPADE secondary_shoot_interval 1.0 s) is running.
     */
    [[nodiscard]] bool swap_locked() const noexcept;
    /**
     * One-shot Character.auto_switch_tool request: the held tool was fired dry
     * with nothing left to reload (or a throwable with no count).
     */
    [[nodiscard]] bool take_auto_switch_request() noexcept;

private:
    void tick_classic(double dt) noexcept;
    std::uint8_t classic_protocol_{};
    struct ClassicTiming {
        double time{}, gun{}, spade{}, dig{}, block{}, grenade{};
        bool shooting{}, digging{}, block_dragging{};
    } classic_timing_;
    void process_edges(const WeaponDefinition& weapon) noexcept;
    void process_held(const WeaponDefinition& weapon, double dt) noexcept;
    void update_minigun_motor(const WeaponDefinition& weapon, double dt,
                              bool input_enabled) noexcept;
    void activate(const WeaponDefinition& weapon, bool secondary) noexcept;
    void emit(WeaponActionKind kind, const WeaponDefinition& weapon,
              bool secondary = false, double value = 0.0) noexcept;
    void report_placement_rejected(const WeaponDefinition& weapon, bool secondary) noexcept;
    [[nodiscard]] bool consume(const WeaponDefinition& weapon) noexcept;
    [[nodiscard]] std::uint8_t next_seed() noexcept;
    [[nodiscard]] double named_value(const WeaponDefinition& weapon,
                                     std::string_view name,
                                     double fallback) const noexcept;
    [[nodiscard]] double charged_duration(const WeaponDefinition& weapon) const noexcept;
    [[nodiscard]] double launcher_speed(const WeaponDefinition& weapon) const noexcept;
    void reset_selected_runtime() noexcept;
    void begin_block_sucker_settle() noexcept;
    void update_block_sucker_animation(double dt,
                                       const WeaponDefinition& weapon) noexcept;

    WeaponReplicationState replication_;
    WeaponRuntimeContext context_{};
    std::vector<WeaponAction> actions_;
    std::uint32_t random_state_{};
    bool primary_held_{};
    bool secondary_held_{};
    bool custom_held_{};
    bool primary_pressed_{};
    /**
     * Character.shoot_primary_held: the shot that emptied the magazine had
     * the trigger down. end_reload stops a shell chain on it and resumes fire
     * (set_primary_shoot(True)) once rounds are back. Releasing the trigger
     * clears it (set_primary_shoot(False), character.pyd 0x10028290), so a
     * tapped single-shot weapon never re-fires on its own after the reload.
     */
    bool shoot_primary_held_{};
    /** set_primary_shoot(True) from end_reload, honoured once cooldown allows. */
    bool resume_fire_pending_{};
    bool primary_released_{};
    bool secondary_pressed_{};
    bool secondary_released_{};
    bool custom_pressed_{};
    bool custom_released_{};
    bool interaction_active_{};
    /**
     * Retail empty-magazine cue latch. PRESENTATION ONLY: it gates the dry_fire
     * emission and nothing else.
     *
     * Retail plays `empty` once per trigger press and is silent while the
     * trigger stays held. It gets there by clearing `Character.shoot_primary`
     * inside the fire call, after which `can_shoot_primary` short-circuits on
     * that attribute and the weapon update stops calling the fire path at all.
     *
     * We cannot copy that literally: `set_primary` is edge-guarded and the
     * session re-asserts the held flag every tick, so clearing `primary_held_`
     * from in here would synthesise a fresh press edge and re-emit at the same
     * 60 Hz it is meant to stop. Latching the cue instead reproduces the audible
     * behaviour without touching an input field.
     */
    bool dry_fire_latched_{};
    bool secondary_dry_fire_latched_{};
    double cooldown_{};
    double secondary_cooldown_{};
    double reload_remaining_{};
    double interaction_elapsed_{};
    double interaction_value_{};
    double spread_{};
    std::uint8_t burst_remaining_{};
    double burst_cooldown_{};
    double minigun_interval_{};
    double minigun_rotation_{};
    double block_sucker_reactivate_{};
    std::uint8_t block_sucker_state_{};
    std::uint32_t block_sucker_random_state_{0xB10C5ACCU};
    double block_sucker_horizontal_phase_{};
    double block_sucker_vertical_phase_{};
    double block_sucker_shake_amplitude_{};
    double block_sucker_settle_remaining_{};
    double block_sucker_settle_start_amplitude_{};
    /**
     * Retail Character.reload_next_update, set by the shot that empties a
     * reloadable magazine (Weapon.use_primary) and by an ammo-crate restock.
     * Character.update_alive starts the reload only once the weapon_shoot
     * animation has stopped playing.
     */
    bool reload_next_update_{};
    /**
     * Remaining time of the retail weapon_shoot animation (AnimWeaponShoot,
     * started with the shot's shoot_interval). It gates reload_next_update.
     */
    double shoot_animation_remaining_{};
    /**
     * Weapon.use_primary cleared Character.shoot_primary after a refused
     * Character.shoot (invalid deployable ghost): the held trigger stops
     * repeating until it is pressed again.
     */
    bool primary_repeat_blocked_{};
    bool auto_switch_requested_{};
    double swap_lock_remaining_{};
    std::array<double, 3U> block_sucker_shake_{};
};

} // namespace battlespades::world
