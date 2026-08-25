#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <optional>
#include <span>
#include <string_view>

namespace battlespades::world {

/** Protocol 168 reserves byte 65 as the exclusive upper tool-id sentinel. */
inline constexpr std::size_t selectable_tool_count{65U};
inline constexpr std::uint8_t no_selectable_tool{65U};

enum class WeaponCategory : std::uint8_t {
    melee,
    rifle,
    smg,
    shotgun,
    sniper,
    pistol,
    machine_gun,
    grenade,
    launcher,
    deployable,
    objective,
    special,
};

/**
 * Input/state-machine family used by the recovered retail implementation.
 *
 * This is intentionally more specific than WeaponCategory: a grenade and a
 * rocket are both projectiles but one cooks on hold/release while the other
 * fires immediately. Every selectable Protocol 168 tool has one mechanism.
 */
enum class WeaponMechanism : std::uint8_t {
    inert,
    melee,
    firearm_semi,
    firearm_automatic,
    firearm_burst,
    firearm_spinup,
    shotgun_semi,
    shotgun_automatic,
    deployed_machine_gun,
    block_builder,
    flare_builder,
    prefab_builder,
    ugc_prefab_editor,
    cooked_throwable,
    charged_throwable,
    oriented_launcher,
    deployable,
    c4,
    objective,
    ugc_entity,
    paintbrush,
    block_sucker,
    disguise,
};

/**
 * Retail meaning of right mouse for an equipped tool.
 *
 * `RetailUseTuning::can_zoom` alone is not sufficient: the recovered Tool
 * base class leaves it enabled for many tools which have no sight at all.
 * This semantic layer keeps input routing independent from that inheritance
 * accident and from the protocol/tool mechanism used by primary fire.
 *
 * The aiming and acting branches are mutually exclusive in retail: a tool has
 * an alternate right-click action OR it aims, never both. `tool_action` is
 * still deliberately coarse and collapses seven distinct recovered behaviours
 * (block-line cancel, prefab rotate, UGC variant cycle, continuous paint, C4
 * detonate and the two alternate digs); splitting it needs trigger shapes this
 * enum does not yet carry. See docs/WEAPON_SECONDARY_RECOVERY.md section 2.
 */
enum class WeaponSecondaryBehavior : std::uint8_t {
    none,
    /** ADS at zoom multiplier 1.0. Twenty tools, not two. */
    iron_sights,
    /** ADS above 1.0: the two snipers only. */
    magnified_scope,
    spin_up,
    tool_action,
    deploy_machine_gun,
};

/** True for both aiming behaviours, which differ only in magnification. */
[[nodiscard]] constexpr bool
aims_down_sights(WeaponSecondaryBehavior behavior) noexcept {
    return behavior == WeaponSecondaryBehavior::iron_sights ||
           behavior == WeaponSecondaryBehavior::magnified_scope;
}

enum class RetailConstantKind : std::uint8_t {
    number,
    boolean,
    numeric_tuple,
};

/** One descriptive constants.py value retained for specialized tool logic. */
struct RetailWeaponConstant final {
    std::string_view name;
    RetailConstantKind kind{RetailConstantKind::number};
    std::array<double, 4U> values{};
    std::uint8_t value_count{1U};
};

/** Exact five-region damage tuple used by retail Weapon subclasses. */
struct RetailDamageTuning final {
    std::optional<std::array<double, 5U>> hit_regions;
    std::optional<double> block;
    /** Server-authoritative melee player damage when the class has no tuple. */
    std::optional<double> melee_player;
};

/** Accuracy growth/recovery and recoil used by local prediction/presentation. */
struct RetailAimTuning final {
    std::optional<double> accuracy;
    std::optional<double> accuracy_zoom;
    std::optional<double> accuracy_min;
    std::optional<double> accuracy_max;
    std::optional<double> spread_min;
    std::optional<double> spread_max;
    std::optional<double> spread_increase_per_shot;
    std::optional<double> spread_reduction_per_second;
    std::optional<double> recoil_up;
    std::optional<double> recoil_side;
    bool variable_accuracy{};
};

/** Exact retail Weapon.ammo tuple plus count-based Tool inventory values. */
struct RetailAmmoTuning final {
    std::optional<std::uint16_t> magazine_capacity;
    std::optional<std::uint16_t> initial_magazine;
    std::optional<std::uint16_t> reserve_capacity;
    std::optional<std::uint16_t> initial_reserve;
    std::optional<std::uint16_t> restock_amount;
    std::optional<std::uint16_t> maximum_count;
    std::optional<std::uint16_t> initial_count;
    std::optional<std::uint16_t> count_restock_amount;
    bool clip_reload{};
};

/** Class-level timing, zoom, crosshair and interaction flags. */
struct RetailUseTuning final {
    std::optional<double> shoot_interval;
    std::optional<double> secondary_shoot_interval;
    std::optional<double> reload_time;
    std::optional<double> input_delay;
    std::optional<double> range;
    std::optional<double> fuse;
    std::optional<double> maximum_fuse;
    std::optional<double> zoom_factor;
    std::optional<double> zoom_sensitivity_factor;
    std::optional<double> model_size;
    std::optional<double> view_model_size;
    /** Weapon.sight_pos, consumed by Character.draw_sight instead of FPS pose. */
    std::array<double, 3U> sight_position{};
    /**
     * Weapon.pin_scale: the draw_scaled() scalar for the front bead.
     *
     * Retail sets it once on the Weapon base (weapon.py:37) and never
     * overrides it, so every firearm carries 0.02 whether or not it owns a
     * pin. Tools outside the Weapon hierarchy have no such attribute and stay
     * at zero, which is harmless because they also have no pin to scale.
     */
    double pin_scale{};
    std::optional<std::int16_t> crosshair_mode;
    std::optional<std::uint8_t> pellets;
    bool can_zoom{};
    bool has_secondary{};
    /** Character.can_shoot_primary's recovered per-tool sprint exception. */
    bool can_shoot_primary_while_sprinting{};
    /** Character.can_shoot_secondary's recovered per-tool sprint exception. */
    bool can_shoot_secondary_while_sprinting{};
    bool stoppable{};
    bool delayed_use{};
    bool show_crosshair_center{};
    bool play_shoot_animation{true};
    /** Retail recolors reserved KV6 material bands with the owning team RGB. */
    bool use_team_color{};
};

/** Values resolved through the actual recovered retail subclass inheritance. */
struct RetailWeaponTuning final {
    std::string_view module;
    std::string_view class_name;
    RetailDamageTuning damage;
    RetailAimTuning aim;
    RetailAmmoTuning ammo;
    RetailUseTuning use;
};

/**
 * One KV6 part and the offset passed to retail models.load_model().
 *
 * The offset is authored in KV6 axes and is deliberately retained here:
 * weapons with the same generic Character attachment still use different
 * pivots (for example pistols, shotguns, RPGs, machetes and deployables).
 */
struct WeaponModelPartDefinition final {
    std::string_view asset;
    std::array<float, 3U> authored_offset{};
};

/**
 * Retail cues that live inside method bodies rather than class attributes.
 *
 * `shoot_sound`/`reload_sound`/`reload_done_sound` are class attributes and an
 * attribute walk recovers them. Everything here is a string literal passed to
 * `self.play_sound(...)` inside `update()` or `use_primary()`, so it can only
 * be recovered by reading the method bodies. Retail deliberately sets
 * `shoot_sound = BLANK_SOUND` on every loop-fire weapon precisely because the
 * sustained loop lives in `update()`; an empty `shoot_sound` on an automatic
 * weapon is therefore correct data, not a generator failure.
 *
 * Empty means retail is genuinely silent in that role. Never substitute.
 */
struct WeaponSoundSet final {
    /** Sustained automatic-fire loop; retail passes loops=0 (infinite). */
    std::string_view fire_loop;
    /** One-shot tail played when the fire loop closes. */
    std::string_view fire_tail;
    /**
     * Minigun barrel spin loop. Its pitch is a raw playback ratio
     * (spin_speed / 5), not the usual 2^(semitones/12) conversion.
     */
    std::string_view spin_loop;
    /** DiggingTool.miss_sound: plays on every swing, hit or miss. */
    std::string_view melee_miss;
    std::string_view melee_hit_block;
    /** Retail plays this on the victim's entity at gain 0.75. */
    std::string_view melee_hit_player;
    /** Retail Weapon.use_primary dry-fire cue when the magazine is empty. */
    std::string_view empty_fire;
    /** Throwables: pin on hold-start, release on throw. */
    std::string_view pin;
    std::string_view throw_release;
    /** Sustained tool loops: block sucker, snow blower, paintbrush. */
    std::string_view tool_loop_start;
    std::string_view tool_loop;
    std::string_view tool_loop_stop;
    /** One extra tool-specific cue, such as the block sucker's pickup. */
    std::string_view tool_extra;
};

/**
 * One generated row shared by rendering, inventory, prediction and protocol.
 *
 * Numeric values originate in BattleSpades/server/game_constants.py. Asset
 * stems and image-presence rules originate in retail shared/constants.py;
 * paths are resolved against the checked-in original asset tree. Do not edit
 * generated rows manually: run tools/generate_weapon_catalog.py instead.
 */
struct WeaponDefinition final {
    std::uint8_t tool_id{};
    std::string_view symbolic_name;
    std::string_view asset_stem;
    WeaponCategory category{WeaponCategory::special};
    WeaponMechanism mechanism{WeaponMechanism::inert};
    double base_damage{};
    double head_damage{};
    double block_damage{};
    double fire_interval{};
    double maximum_range{};
    std::uint16_t clip_size{};
    std::uint16_t reserve_ammo{};
    double reload_time{};
    std::uint8_t pellet_count{1U};
    double spread{};
    double blast_radius{};
    double fuse_time{};
    /** Numeric ids from retail TOOLS_DAMAGE_TYPE / TOOLS_KILL_TYPE; -1=None. */
    std::int16_t damage_type{-1};
    std::int16_t kill_type{-1};
    bool melee{};
    bool projectile{};
    bool selectable_when_empty{};
    bool retail_draws_first_person_image{};
    bool team_colored_first_person_image{};
    std::string_view toolbar_icon_asset;
    std::string_view first_person_image_asset;
    std::string_view first_person_blue_asset;
    std::string_view first_person_green_asset;
    std::string_view first_person_neutral_asset;
    std::string_view voxel_model_asset;
    /** Exact recovered model/view_model composites, including multipart guns. */
    std::span<const WeaponModelPartDefinition> third_person_models;
    std::span<const WeaponModelPartDefinition> first_person_models;
    /** Retail media/model identities used by audiovisual presentation. */
    std::string_view shoot_sound;
    /** Character.play_sound list slots 3/4, expressed in semitones. */
    std::array<float, 2U> shoot_sound_pitch{};
    std::string_view reload_sound;
    std::array<float, 2U> reload_sound_pitch{};
    /** Optional final action/cock sound after the last reload cycle. */
    std::string_view reload_done_sound;
    std::array<float, 2U> reload_done_sound_pitch{};
    /** Cues retail keeps in method bodies rather than class attributes. */
    WeaponSoundSet sounds;
    std::string_view sight_model_asset;
    /**
     * Weapon.pin: the front bead Character.draw_sight draws after the sight.
     *
     * Exactly one tool has one -- classicRifleWeapon.py:33 `pin = SEMI_PIN` is
     * the only override of weapon.py:39 `pin = None` in the whole tree. It is
     * a part definition rather than a bare asset path because
     * models.load_weapon() gives semi_sight_pin a non-zero authored load
     * offset (models.py:230), unlike every sight/casing/tracer.
     */
    WeaponModelPartDefinition pin_model;
    std::string_view casing_model_asset;
    std::string_view tracer_model_asset;
    RetailWeaponTuning retail;
    /** Complete descriptive numeric/bool constants for specialized behavior. */
    std::span<const RetailWeaponConstant> constants;
};

/** All real Protocol 168 tools in exact byte-id order, 0 through 64. */
[[nodiscard]] std::span<const WeaponDefinition> weapon_catalog() noexcept;

/** Return null for the protocol sentinel (65) and every malformed tool id. */
[[nodiscard]] const WeaponDefinition*
find_weapon_definition(std::uint8_t tool_id) noexcept;

[[nodiscard]] bool valid_selectable_tool(std::uint8_t tool_id) noexcept;

/** Resolve the recovered RMB contract for one retail weapon/tool. */
[[nodiscard]] WeaponSecondaryBehavior
weapon_secondary_behavior(const WeaponDefinition& weapon) noexcept;

/** SHA-256 of the server/tool inputs used to generate the checked-in table. */
[[nodiscard]] std::string_view weapon_catalog_contract_sha256() noexcept;

[[nodiscard]] const RetailWeaponConstant*
find_retail_weapon_constant(const WeaponDefinition& weapon,
                            std::string_view name) noexcept;

} // namespace battlespades::world
