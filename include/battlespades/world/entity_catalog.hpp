#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace battlespades::world {

class Kv6Model;
struct VxlColor;

/** Broad behavioural family; `update_entities` switches on this first. */
enum class EntityCategory : std::uint8_t {
    /** Walk-through restock: ammo, health and block crates. */
    pickup,
    /** Carried by equipping a tool: intel, diamond, bomb. */
    objective,
    /** Placed by a player and then acts on its own. */
    deployable,
    /** Fuse or timer, then a blast. */
    hazard,
    /** Renders and nothing else. */
    marker,
    /** Team volume: bases and capture points. */
    structure,
    /** In flight; already owned by TutorialProjectile. */
    projectile,
    /** No art ships, for it or for anything it could borrow. */
    unportable,
};

/**
 * Where a number came from.
 *
 * Recorded per row so the debug readout can mark a value we INVENTED rather
 * than recovered. Retail contains two parallel constant blocks -- named
 * (`DYNAMITE_EXPLOSION_RADIUS`) and A-aliased (`A1632`) -- which disagree on
 * at least dynamite, and the alias block is the one the weapon modules
 * actually bind. Silently averaging them into "parity" is how a wrong number
 * hardens into a fake fact.
 */
enum class EntityProvenance : std::uint8_t {
    /** From the A-number block that the weapon modules bind. Strongest. */
    retail_alias,
    /** From the named constant block. */
    retail_named,
    /** A bare literal in a retail behaviour module. */
    retail_literal,
    /** Exists only in the BattleSpades server: an invention, not parity. */
    battlespades,
    /** No source found; the field is a placeholder. */
    absent,
};

[[nodiscard]] std::string_view entity_category_name(EntityCategory category) noexcept;
[[nodiscard]] std::string_view entity_provenance_name(EntityProvenance provenance) noexcept;

/**
 * One KV6 in an entity's rig, in draw order.
 *
 * Multi-part is not exotic: the rocket turret is three meshes with independent
 * rotations, and UGC drop points are a baseplate plus an item. Renderer slots
 * are therefore budgeted per PART, never per entity.
 */
struct EntityModelPart final {
    std::string_view kv6;
    /**
     * Offset from the entity origin, in WORLD units.
     *
     * Retail mixes two conventions -- models.py offsets are raw KV6 voxels
     * while the turret's are already premultiplied by its model size -- so the
     * generator normalises both to world units. The renderer applies one rule.
     */
    std::array<float, 3U> offset{};
    /** Per-part scale; parts of one rig do not always share one. */
    float scale{1.0F};
    /**
     * Which rotation this part follows.
     *
     * 0 = static, 1 = yaw only, 2 = yaw and pitch. The turret's base is
     * static, its ball takes yaw, and its gun takes both.
     */
    std::uint8_t rotation_mode{};
    /**
     * load_model(..., offset) passed to KV6.offset_pivots, in raw KV6 voxels.
     *
     * This is intentionally separate from `offset`: the grave's recovered
     * (0,0,11) changes its authored pivot and must never move the packet-owned
     * entity origin 1.1 blocks into the floor.
     */
    std::array<float, 3U> pivot_offset{};
};

/** One immutable entity row, keyed by the retail wire id. */
struct EntityDefinition final {
    /** The retail id 0..39. This is also the wire id; never renumber it. */
    std::uint8_t type_id{};
    std::string_view symbolic_name;
    /** Short human label for the debug spawn menu. */
    std::string_view display_name;
    EntityCategory category{EntityCategory::marker};

    /**
     * The rig, in draw order.
     *
     * EMPTY means no art ships. That is the single source of truth for "not
     * portable" and the asset test pins it, so nobody can quietly substitute a
     * lookalike model for an entity retail never drew.
     */
    std::span<const EntityModelPart> parts;
    float model_size{1.0F};

    /** Sphere the player must enter to trigger a pickup or objective. */
    float touch_radius{};
    float health{};
    /** Seconds from placement to detonation; 0 means no fuse. */
    float fuse{};
    /** Seconds before a trap will trip. */
    float arm_delay{};
    /** Seconds before the entity removes itself; 0 means forever. */
    float lifetime{};
    /** Seconds a consumed pickup stays gone. */
    float respawn_delay{};

    float blast_radius{};
    float blast_damage{};
    float block_damage{};
    std::uint8_t crater_radius{};
    /** Horizontal trip distance and vertical trip layers for mines. */
    float trip_radius{};
    float trip_height{};

    std::uint16_t ammo{};
    std::uint16_t uses{};
    /** Point-light radius for the entities that emit one. */
    float light_radius{};
    /**
     * Detection range for entities that sense rather than explode.
     *
     * Deliberately NOT folded into `blast_radius`: the radar station's 45 is a
     * sensing range, and a reader who found it under a blast field would
     * reasonably conclude the radar detonates for 45 units. It does not
     * detonate at all.
     */
    float sense_radius{};

    std::string_view sound_place;
    std::string_view sound_trigger;
    /** Retail selects this when the blast centre is at or below the water bed. */
    std::string_view sound_trigger_water;

    /** Draws in the owning team's colour rather than its authored palette. */
    bool team_tinted{};
    /**
     * Safe to put on the wire.
     *
     * False for BASE(1): it is absent from retail's `GameScene.ENTITIES` and a
     * clean client dies with `KeyError: 1` on receiving it. Carried from day
     * one so the later network stage physically cannot serialise it. FLAG(0)
     * is flagged defensively by analogy -- only BASE was actually measured.
     */
    bool wire_safe{true};
    /** Offered by the debug spawn menu. */
    bool spawnable{};

    /** The WEAKEST provenance among this row's numbers, so the HUD can warn. */
    EntityProvenance weakest_provenance{EntityProvenance::retail_alias};
};

[[nodiscard]] const EntityDefinition* find_entity_definition(std::uint8_t type_id) noexcept;
[[nodiscard]] std::span<const EntityDefinition> entity_catalog() noexcept;

/**
 * Resolve the two-part retail presentation for packet-97/98 UGC item ids.
 *
 * Entity 29 is polymorphic: drop points use the three crate models, the OCC
 * point uses Bomb, and spawn/base zones use different meshes and authored
 * scales.  Treating it as one fixed spawn-zone mesh makes valid map metadata
 * visually indistinguishable and was the reason placed UGC objects appeared
 * missing in the native client.
 */
[[nodiscard]] std::span<const EntityModelPart>
ugc_entity_model_parts(std::uint8_t item_id) noexcept;

/** Team material selected by a UGC item id: 2 blue, 3 green, 0 neutral. */
[[nodiscard]] std::uint8_t ugc_entity_team(std::uint8_t item_id) noexcept;

/** Retail RMB grouping: cycle small/medium/large or health/ammo/blocks. */
[[nodiscard]] std::uint8_t next_ugc_item_variant(std::uint8_t item_id) noexcept;

/** Retail toolbar icon selected by a tool-41 slot's UGC item variant. */
[[nodiscard]] std::string_view ugc_tool_icon_asset(std::uint8_t item_id) noexcept;

/**
 * Resolve one entity model's server-selected team material.
 *
 * Most entity art uses the original three magenta default-colour bands. The
 * grave asset instead uses exact black for the panel controlled by its packet
 * colour, matching the later Specialist/Medic authoring convention.
 */
void apply_entity_team_material(Kv6Model& model, std::uint8_t type_id,
                                VxlColor team_color) noexcept;

/** Catalog rows the debug menu offers, in menu order. */
[[nodiscard]] std::span<const EntityDefinition* const> spawnable_entities() noexcept;

/**
 * The entity a deployable or objective tool places, or 0 for tools that place
 * nothing.
 *
 * Retail keeps this mapping in the weapon modules rather than in any table --
 * `list.py`'s PICKUPS dict for the objectives, and each deployable weapon's own
 * entity class reference. Centralised here so the session has one place to ask
 * and the network stage can reuse it verbatim.
 *
 * Zero is a safe "nothing" because entity 0 is FLAG, which has no art and can
 * never be placed by a weapon.
 */
[[nodiscard]] std::uint8_t entity_placed_by_tool(std::uint8_t tool_id) noexcept;

/** The tool a carried objective equips, or 0 if the entity is not carryable. */
[[nodiscard]] std::uint8_t tool_carrying_entity(std::uint8_t type_id) noexcept;

/**
 * Retail's `can_place_object` constraints for one deployable tool.
 *
 * The check is a spherical SHELL around the player, not just a max range:
 * `player_min <= distance <= far_radius`. Distance is measured from the
 * player's position to the hit voxel's INTEGER corner, not its centre --
 * reproducing that exactly matters, because centre-to-centre drifts the
 * effective range by up to ~0.87 blocks.
 */
struct DeployablePlacement final {
    /** Maximum distance from the player to the target voxel. */
    float far_radius{};
    /** Minimum distance; zero for most tools, 1 for the turret and radar. */
    float player_min_radius{};
    /** No other visible entity may be within this many blocks of the target. */
    float entity_min_radius{1.0F};
    /**
     * Whether the tool may attach to a wall or ceiling.
     *
     * False means retail refuses any face except 4 (the block's top), so a
     * landmine can only ever go on the ground. Only dynamite and C4 are true.
     */
    bool can_place_vertical{};
    /** False means the tool refuses placement at or below the water plane. */
    bool water_legal{true};
};

/** Placement rules for a deployable tool; null for tools that place nothing. */
[[nodiscard]] const DeployablePlacement*
deployable_placement(std::uint8_t tool_id) noexcept;

/**
 * The voxel face a ray with this surface normal entered through, 0..5.
 *
 * Face 4 is the block's TOP because map z grows downward, which is also why
 * a ground-only deployable tests for exactly this value.
 */
[[nodiscard]] std::uint8_t entity_face_from_normal(int nx, int ny, int nz) noexcept;

/** Largest `parts.size()` in the catalog; sizes the renderer slot budget. */
[[nodiscard]] std::size_t maximum_entity_parts() noexcept;

} // namespace battlespades::world
