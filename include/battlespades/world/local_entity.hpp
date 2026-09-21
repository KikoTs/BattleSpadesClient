#pragma once

#include "battlespades/world/entity_catalog.hpp"
#include "battlespades/world/player_movement.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace battlespades::world {

class VxlMap;

/**
 * One locally simulated entity.
 *
 * Deliberately a superset of `CreateEntityPacket`'s record so a future network
 * adapter is a field copy rather than a redesign, and keyed by the RETAIL type
 * id rather than a local enum for the same reason.
 */
struct LocalEntity final {
    std::uint64_t id{};
    /** Retail id 0..39; indexes the catalog and is the wire id. */
    std::uint8_t type{};
    Vec3 position{};
    Vec3 velocity{};
    /** Where a consumed pickup returns to. */
    Vec3 home{};
    /** Degrees: ordinary rigs use -X/down; turret packets use +Y/up. */
    double yaw{};
    double pitch{};
    /** Where the rig is currently pointing, which lags `yaw`/`pitch`. */
    double aim_yaw{};
    double aim_pitch{};
    std::uint8_t team{};
    std::uint8_t owner{};
    /** Optional authored packet-21 RGB tint; zero RGB selects team material. */
    std::array<std::uint8_t, 3U> color{};
    bool has_color{};
    /** Voxel face a stuck entity clings to, 0..5. */
    std::uint8_t face{4U};

    double health{};
    /** Seconds left on the fuse; negative means no fuse is running. */
    double fuse{-1.0};
    /** Seconds until a trap arms. */
    double arm_remaining{};
    /** Seconds until self-removal; negative means it never expires. */
    double lifetime_remaining{-1.0};
    /** Client-only age used to settle spawn presentation onto packet motion. */
    double presentation_age{};
    /** Matched at creation to an explicitly sent local launcher action. */
    bool local_launcher_muzzle{};
    /**
     * Remaining presentation time for the Drill's contact-only loop.
     * Damage(37) refreshes it to the recovered 0.5 seconds while the separate
     * projectile-flight loop remains active for the entity's whole lifetime.
     */
    double drilling_audio_remaining{};
    /** Seconds until a consumed pickup returns; only read while !alive. */
    double respawn_remaining{};

    std::uint16_t ammo{};
    std::uint16_t uses{};
    /** Packet-97/98 variant for polymorphic UGC entity 29; 0xFF otherwise. */
    std::uint8_t ugc_item_id{0xFFU};
    /** Seconds until this entity may fire again. */
    double shoot_cooldown{};
    std::optional<std::uint8_t> target;

    bool alive{true};
    /**
     * Finished for good; the update pass erases it.
     *
     * Distinct from `!alive`, which is the temporary state of a consumed pickup
     * waiting out its respawn -- those must keep their slot and their home.
     */
    bool retired{};
    /**
     * Stuck to a voxel face rather than resting on the ground.
     *
     * Suppresses terrain gravity while the attachment voxel exists. Cleared
     * automatically when that voxel is destroyed, so a charge on a dug-out
     * wall still falls and settles on the VXL below it.
     */
    bool attached{};
    /**
     * Resting on the voxel stored in `position`.
     *
     * `position` remains the authoritative solid support voxel. Retail's
     * GenericMovement collision body is not the KV6 pivot, however, so the
     * renderer additionally seats the measured bottom of the model on that
     * support plane. Keeping that correction presentation-only means a model
     * pivot can never change packet positions, pickup radii, or blast centres.
     */
    bool grounded{};
    /** A trap that has finished arming and will now trip. */
    bool armed{};
    /** Set the tick it should detonate; the blast pass consumes it. */
    bool detonating{};
    /**
     * Independent per-behaviour accumulators.
     *
     * Blockfire is why these are separate rather than one shared clock: retail
     * damages players every 0.3 s, blocks every 0.4 s and spreads every 0.5 s.
     * Driving all three off one timer silently locks them to a single cadence
     * and the fire stops feeling like fire.
     */
    std::array<double, 3U> accumulators{};
};

/**
 * World-space countdown presentation for retail's timed dynamite.
 *
 * C4 (entity 38) is deliberately excluded: retail C4 has no fuse and is
 * remote-detonated. Calling it a timed C4 would fabricate both UI and audio.
 */
struct TimedExplosivePresentation final {
    bool visible{};
    std::uint8_t seconds{};
    Vec3 label_position{};
    std::string_view ticking_sound{};
};

/** Derive the floating fuse label and attached ticking-loop sound. */
[[nodiscard]] TimedExplosivePresentation timed_explosive_presentation(
    const LocalEntity& entity) noexcept;

/** Field selector for one authoritative ChangeEntity(16) mutation. */
enum class ServerEntityProperty : std::uint8_t {
    state,
    position,
    velocity,
    owner,
    forward,
    target,
    fuse,
    ammo,
};

/**
 * Network-neutral representation of a packet-16 entity update.
 *
 * The protocol adapter fills only the member selected by `property`. Keeping
 * this type in the world layer prevents simulation code from depending on wire
 * packet classes while retaining one atomic, validated mutation boundary.
 */
struct ServerEntityMutation final {
    std::uint64_t entity_id{};
    ServerEntityProperty property{ServerEntityProperty::state};
    Vec3 vector{};
    double scalar{};
    std::int32_t integer{};
};

/** What happened to an entity this tick, for sound, VFX and HUD. */
enum class EntityEventKind : std::uint8_t {
    spawned,
    /** A pickup was walked into and consumed. */
    collected,
    /** A trap finished its arm delay. */
    armed,
    detonated,
    /** Removed by lifetime or by the debug clear, with no blast. */
    expired,
    /** A consumed pickup came back. */
    respawned,
    /** A turret or similar fired. */
    fired,
    /** A turret acquired or lost a target. */
    target_changed,
    /** The local player's health moved; the frontend forwards it to the HUD. */
    health_changed,
};

struct EntityEvent final {
    EntityEventKind kind{EntityEventKind::spawned};
    std::uint64_t entity_id{};
    std::uint8_t type{};
    Vec3 position{};
    /** health_changed carries the new value here. */
    double value{};
    /** True when the blast centre sits at or below the water bed. */
    bool in_water{};
};

/** Result of one fixed-step terrain-physics update. */
struct EntityPhysicsStep final {
    bool moved{};
    bool landed{};
    /** Contact retained upward velocity instead of settling this tick. */
    bool bounced{};
    bool support_lost{};
    /** Downward speed immediately before contact; zero without a landing. */
    double impact_speed{};
};

/**
 * Whether this entity owns ordinary VXL gravity.
 *
 * Flying ordnance has its own recovered projectile integrator; map-authoring
 * markers, capture volumes, block fire and player-attached props are anchored.
 * Pickups, objectives, graves and terrain deployables use this path.
 */
[[nodiscard]] bool uses_entity_terrain_gravity(const EntityDefinition& definition) noexcept;

/**
 * Integrate a gravity-owned entity against the current voxel map.
 *
 * Runs on the fixed gameplay thread. Motion is swept in bounded substeps so a
 * fast falling grave or dropped objective cannot tunnel through a one-voxel
 * floor. On downward contact `position.z` becomes the SOLID support voxel,
 * matching retail Entity.set_position's face-4 half-block standoff. Graves and
 * corpses retain their recovered diminishing bounce; other rows settle.
 * Attached charges remain fixed until their supporting voxel is removed, then
 * rotate to the ground face and enter the same falling simulation.
 */
[[nodiscard]] EntityPhysicsStep step_entity_terrain_physics(LocalEntity& entity,
                                                            const EntityDefinition& definition,
                                                            const VxlMap& map,
                                                            double dt) noexcept;

/**
 * Whether a player at `player` is inside `entity`'s touch sphere.
 *
 * Retail's crate test is pure proximity -- no line of sight, no facing, no use
 * key -- and it deliberately shrank from 3.0 to 2.5 because the wider sphere
 * let a single walk-through eat two adjacent crates.
 */
[[nodiscard]] bool
within_touch_radius(const LocalEntity& entity, const Vec3& player, float radius) noexcept;

/**
 * Whether a landmine at `entity` would trip on a player at `player`.
 *
 * The trip volume is a CYLINDER, not a sphere: `radius` horizontally but only
 * `layers` voxels vertically, so walking over a mine on the roof above it is
 * safe while standing beside it at the same height is not. Line of sight is
 * deliberately ignored, which is what makes a re-buried mine still lethal.
 *
 * `layers` is an INTERPRETATION, not recovered behaviour. Retail names the
 * constant a layer count (LANDMINE_DETECTION_LAYERS = 3) but never shows what
 * it does with it; treating it as +/- 3 blocks of vertical tolerance is our
 * reading, shared with the BattleSpades server.
 */
[[nodiscard]] bool within_trip_volume(const LocalEntity& entity,
                                      const Vec3& player,
                                      float radius,
                                      float layers) noexcept;

/**
 * Explosive falloff at `distance` from a blast of `radius`.
 *
 * Returns 0..1, linear to the edge and exactly zero beyond it, so a blast can
 * never heal or reach further than its radius through floating-point slack.
 */
[[nodiscard]] double blast_falloff(double distance, double radius) noexcept;

/**
 * Steps a turret's aim toward its target at a fixed angular rate.
 *
 * Both angles are degrees and yaw wraps, so the turret always takes the short
 * way around rather than unwinding through 350 degrees. Returns true once both
 * axes are inside `tolerance`, which is the firing gate.
 */
struct TurretAim final {
    double yaw{};
    double pitch{};
    bool on_target{};
    /** Degrees actually moved this tick; the aim-loop audio is gated on it. */
    double angular_rate{};
};

[[nodiscard]] TurretAim step_turret_aim(double current_yaw,
                                        double current_pitch,
                                        double desired_yaw,
                                        double desired_pitch,
                                        double degrees_per_second,
                                        double dt,
                                        double tolerance) noexcept;

/** Shortest signed difference between two degree angles, in (-180, 180]. */
[[nodiscard]] double shortest_angle_delta(double from, double to) noexcept;

/**
 * Outward normal of the voxel face an entity is attached to, in MAP space.
 *
 * Map Z grows DOWNWARD, so face 4 -- the default, and the only one a
 * ground-placed entity ever gets -- points at (0,0,-1), which is UP.
 *
 * Recovered from retail's `Entity.set_position` face table, corroborated by
 * the placement ghost's voxel-face-centre offsets: face 0 is the -X face,
 * 1 is +X, 2 is -Y, 3 is +Y, 4 is -Z (the block's top) and 5 is +Z (its
 * underside).
 */
[[nodiscard]] Vec3 entity_face_normal(std::uint8_t face) noexcept;

/**
 * The single axis-angle retail applies for a stuck entity's face.
 *
 * `axis` is 0=X, 1=Y, 2=Z in MAP space; `degrees` is the rotation about it.
 * One axis-angle per face means there is no euler-order ambiguity here.
 *
 * Face 4 is the identity, which is why a ground-placed landmine needs no
 * rotation at all -- and why a landmine that LOOKED rotated was really a
 * missing basis conversion, not a missing face rotation.
 */
struct EntityFaceRotation final {
    std::uint8_t axis{};
    float degrees{};
};

[[nodiscard]] EntityFaceRotation entity_face_rotation(std::uint8_t face) noexcept;

/** Map-space articulation after the KV6 basis conversion; pitch precedes yaw. */
struct EntityAimRotation final {
    std::uint8_t pitch_axis{1U};
    double pitch_degrees{};
    double yaw_degrees{};
};

/** Turret packets use retail +Y forward/positive-up angles, unlike characters. */
[[nodiscard]] EntityAimRotation entity_aim_rotation(std::uint8_t entity_type,
                                                    double yaw,
                                                    double pitch) noexcept;

/** Correlates owned rockets with sent launcher actions, never merely ownership. */
class LauncherMuzzleTracker final {
public:
    using Clock = std::chrono::steady_clock;
    void remember(std::uint8_t tool, Vec3 position, Vec3 velocity,
                  Clock::time_point now) noexcept;
    [[nodiscard]] bool consume(const LocalEntity& entity, Clock::time_point now) noexcept;
    void clear() noexcept;

private:
    struct Shot final {
        Vec3 position{};
        Vec3 velocity{};
        Clock::time_point created{};
        std::uint8_t entity_type{};
    };
    std::array<Shot, 8U> shots_{};
    std::size_t next_{};
};

/**
 * Exact retail display origin for one entity model part in map coordinates.
 *
 * `Entity.set_position` rotates its authored part offset with the attachment
 * face instead of merely adding a face normal. Rocket turrets then add their
 * own half-block XY centring, while graves bypass the generic entity display
 * and follow their movement object at (-0.5,-0.5,-0.5).
 */
[[nodiscard]] Vec3 entity_presentation_position(const LocalEntity& entity,
                                                const EntityModelPart& part) noexcept;

/** Shared row-vector transform for a placed entity and its placement ghost. */
[[nodiscard]] std::array<float, 16U> entity_presentation_transform(
    const LocalEntity& entity,
    const EntityDefinition& definition,
    const EntityModelPart& part,
    double contact_adjustment = 0.0) noexcept;

/**
 * Vertical contact offset that keeps a face-up KV6 rig on its physics anchor.
 *
 * `mesh_minimum_y` is the minimum render-space Y bound returned by
 * `Kv6Model::mesh` after applying the part's recovered pivot offset. Entity
 * models are subsequently rotated by Rx(-90), so that bound becomes the
 * greatest (lowest, in z-down map space) local Z value. The returned value is
 * added to every part of the entity, preserving multi-part rigs such as the
 * rocket turret.
 *
 * This is an intrinsic model/contact offset, so it stays active through falls
 * and rebounds. Enabling it only after `grounded` makes a grave hover by its
 * loader-pivot gap while airborne and then snap down on the final contact.
 * Wall/ceiling attachments keep their authored face transform and receive no
 * vertical floor correction. The server-owned entity anchor and every
 * gameplay calculation remain untouched.
 */
[[nodiscard]] double entity_vertical_contact_adjustment(
    const LocalEntity& entity,
    const EntityDefinition& definition,
    const EntityModelPart& contact_part,
    float mesh_minimum_y) noexcept;

/** Seat all non-pitching parts together; imported rigs may keep the real
 * feet in the yawing body and use a placeholder for the nominal base. */
[[nodiscard]] double entity_rig_vertical_contact_adjustment(
    const LocalEntity& entity,
    const EntityDefinition& definition,
    std::span<const EntityModelPart> parts,
    std::span<const float> mesh_minimum_y) noexcept;

/**
 * Terrain-projected centre for retail's health-crate spot shadow.
 *
 * HealthCrate is the only entity with `needs_shadow = True`; its recovered
 * query offset is `(0.5, 0.5, 0.0)`. The original renderer projects that query
 * down onto the first solid voxel. Returning the support plane rather than the
 * entity transform keeps the decal on the ground while a dropped crate falls.
 * Other entity types, consumed crates and columns without terrain return
 * `std::nullopt`.
 */
[[nodiscard]] std::optional<Vec3>
health_crate_spot_shadow_position(const LocalEntity& entity, const VxlMap& map) noexcept;

/**
 * How far a drawn entity sits from its wire position: half a block along the
 * face normal, seating it on the surface rather than inside it.
 */
inline constexpr double entity_face_standoff{0.5};

/** Local player health, clamped to retail's 0..100. */
inline constexpr double maximum_player_health{100.0};

} // namespace battlespades::world
