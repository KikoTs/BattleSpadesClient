#pragma once

#include "battlespades/world/chunk_mesh.hpp"
#include "battlespades/world/dynamic_light.hpp"
#include "battlespades/world/kv6_model.hpp"
#include "battlespades/world/particle_system.hpp"
#include "battlespades/world/voxel_collapse.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace battlespades::world {

enum class TerrainImpactKind : std::uint8_t {
    bullet,
    melee,
    explosion,
    /** ExplodeCorpse(36): 40 large bright-red body particles and death audio. */
    corpse_explosion,
    /** Entity 11 deletion: explode its displayed grave model. */
    grave_explosion,
    fire,
    chemical,
    /**
     * One bore step of a drill tunnelling through terrain.
     *
     * Distinct from `explosion` because retail gives the drill TWO different
     * samples: `drill_drilling_exp` fires once per bore contact, while
     * `drillexplode` is reserved for the drill's own death. Routing the bore
     * through the explosion cue would play the death sound continuously.
     */
    drill,
    /**
     * SnowBlower/Block Cannon projectile contact.
     *
     * Retail presents a compact grey smoke puff and `snowcan_impact`; it is a
     * placement acknowledgement, not an explosive terrain hit and therefore
     * must never create an ordnance flash, debris shell, or dynamic light.
     */
    block_cannon,
};

/** A committed terrain hit, consumed by the client-only feedback simulation. */
struct TerrainImpactEvent final {
    constexpr TerrainImpactEvent() noexcept = default;

    /**
     * Builds a terrain event while making the presentation-only tail explicit.
     *
     * AppleClang diagnoses omitted aggregate members even when they have safe
     * default member initializers. Keeping this small value constructor also
     * prevents packet adapters from accidentally inheriting indeterminate
     * projectile position or velocity state on a new platform.
     */
    constexpr TerrainImpactEvent(
        TerrainImpactKind event_kind,
        VoxelCell event_cell,
        VxlColor event_color,
        std::array<std::int32_t, 3U> event_normal,
        bool event_destroyed,
        float event_radius = 1.0F,
        std::uint8_t event_source_tool = 0U,
        std::optional<std::array<float, 3U>> event_position = std::nullopt,
        std::array<float, 3U> event_source_velocity = {}) noexcept
        : kind{event_kind},
          cell{event_cell},
          color{event_color},
          normal{event_normal},
          destroyed{event_destroyed},
          radius{event_radius},
          source_tool{event_source_tool},
          position{event_position},
          source_velocity{event_source_velocity} {}

    TerrainImpactKind kind{TerrainImpactKind::bullet};
    VoxelCell cell{};
    VxlColor color{};
    std::array<std::int32_t, 3U> normal{};
    bool destroyed{};
    /** Authoritative terrain radius; presentation scatter is bounded by it. */
    float radius{1.0F};
    /**
     * The canonical tool that caused this impact when the packet identifies it.
     *
     * Tool zero is the Pickaxe, so callers must use `kind` rather than treating
     * zero as an "unknown" sentinel. This is presentation-only metadata used to
     * recover the per-tool melee and explosion samples.
     */
    std::uint8_t source_tool{};
    /**
     * Exact packet/entity-space origin when one exists.
     *
     * Voxel hits intentionally leave this empty and use the cell centre. A
     * moving projectile or grave can be between cells when the authoritative
     * destroy edge arrives; retaining that position prevents the blast from
     * visibly snapping as much as half a block before it is rendered.
     */
    std::optional<std::array<float, 3U>> position;
    /** Incoming projectile velocity, used only to orient directional VFX. */
    std::array<float, 3U> source_velocity{};
};

/** Exact event origin when available, otherwise the canonical voxel centre. */
[[nodiscard]] inline std::array<float, 3U> terrain_impact_position(
    const TerrainImpactEvent& impact) noexcept {
    return impact.position.value_or(std::array<float, 3U>{
        static_cast<float>(impact.cell.x) + 0.5F,
        static_cast<float>(impact.cell.y) + 0.5F,
        static_cast<float>(impact.cell.z) + 0.5F});
}

enum class TerrainEffectKind : std::uint8_t {
    falling_structure,
    block_debris,
    impact_chip,
    explosion_glow,
    fire_flame,
    chemical_cloud,
};

enum class TerrainSoundKind : std::uint8_t {
    bullet_impact,
    melee_impact,
    /** Per-bore drill contact; re-arms the drilling loop in retail. */
    drill_bore,
    /** Retail `snowcan_impact`, spatialized at the placed block. */
    block_cannon_impact,
    structure_break,
    explosion,
    /** Retail DEATH_EXPLODE_SOUND / DEATH_EXPLODE_WATER_SOUND. */
    death_explosion,
    fire_explosion,
    /** Retail des_split_*: played when a component detaches, not when it lands. */
    structure_split,
    /** Retail bullet_break_*: a bullet destroyed the block outright. */
    block_break,
};

/** Retail selects the des_split_/des_imp_ group by component block count. */
inline constexpr std::size_t falling_sound_medium_blocks{15U};
inline constexpr std::size_t falling_sound_large_blocks{80U};

enum class FallingSoundTier : std::uint8_t {
    small,
    medium,
    large,
};

/** Retail's 15/80-block split used by both des_split_* and des_imp_*. */
[[nodiscard]] constexpr FallingSoundTier falling_sound_tier(
    std::size_t block_count) noexcept {
    return block_count >= falling_sound_large_blocks
               ? FallingSoundTier::large
           : block_count >= falling_sound_medium_blocks
               ? FallingSoundTier::medium
               : FallingSoundTier::small;
}

/**
 * Presentation duration for a detached component.
 *
 * The three audible weights anchor at 0.5/0.9/1.2 seconds. Smoothstep between
 * the 15- and 80-block retail thresholds avoids a one-block size change
 * producing a visible timing pop; very large components ease toward 1.2 s at
 * the renderer's hard 2,048-voxel cap.
 */
[[nodiscard]] float falling_animation_duration(std::size_t block_count) noexcept;

/** Returns the authored retail sound group for a split or landing event. */
[[nodiscard]] std::string_view falling_sound_group(TerrainSoundKind kind,
                                                   std::size_t block_count,
                                                   bool submerged) noexcept;

struct TerrainSoundEvent final {
    TerrainSoundKind kind{TerrainSoundKind::bullet_impact};
    std::array<float, 3U> position{};
    float gain{1.0F};
    std::uint8_t variant{};
    /** Mirrors TerrainImpactEvent::source_tool; selects the explosion cue. */
    std::uint8_t source_tool{};
    /** Captured component size for selecting the retail collapse sound bank. */
    std::uint16_t structure_blocks{};
};

/** Audio ownership for one visual terrain impact. */
enum class TerrainImpactSoundPolicy : std::uint8_t {
    /** Contact sound, plus the separate destruction cue when applicable. */
    full,
    /** A predicted contact already sounded; emit only its later break cue. */
    destruction_only,
    /** Another authoritative packet owns every sound for this impact. */
    silent,
};

/** Immutable mesh plus the current presentation transform of one effect. */
struct TerrainEffectInstance final {
    std::uint64_t id{};
    TerrainEffectKind kind{TerrainEffectKind::impact_chip};
    ChunkMesh mesh;
    std::array<float, 3U> position{};
    std::array<float, 3U> rotation_degrees{};
};

/** One renderer-neutral voxel used by block-line placement ghosting. */
[[nodiscard]] ChunkMesh placement_preview_cube(VxlColor color);

/**
 * Bounded client-only presentation of terrain hits and disconnected structures.
 *
 * Collision has already been removed atomically by the authoritative VXL map.
 * This class therefore cannot affect movement or networking: it animates the
 * captured colors as a collisionless body, matching retail's ability to phase
 * through remaining terrain, and replaces it after a size-weighted window with
 * one shrinking image per captured voxel. All methods run on the fixed-rate
 * gameplay thread and perform no file I/O.
 */
class TerrainEffectSimulation final {
public:
    static constexpr std::size_t maximum_instances{40U};
    /** Hard visual cap requested for one detached structure. */
    static constexpr std::size_t maximum_falling_voxels{2'048U};
    static constexpr std::size_t maximum_dynamic_lights{16U};

    /**
     * Attaches the particle sink that renders the high-fidelity bursts.
     *
     * Optional by design: with no sink this class behaves exactly as it did
     * before particles existed, which keeps its recovered instance counts
     * under test independently of presentation.
     */
    void set_particle_sink(ParticleSystem* particles) noexcept;

    /**
     * Installs the already-decoded retail grave display used by entity 11.
     *
     * Called once on the frontend thread before play. The model is retained by
     * value so a later map/server transition cannot invalidate the particles'
     * source while an entity-destroy packet is being handled.
     */
    void set_grave_model(Kv6Model model);

    void spawn_falling(FallingComponent component);
    /**
     * Spawn visual feedback for one committed hit.
     *
     * `sound_policy` separates contact sound from the later destruction sound.
     * Live melee Damage(37) is silent because retail follows it with an
     * authoritative positioned PlaySound(23). A locally replayed bullet contact
     * uses `destruction_only` when Damage(37) later removes the same block, so
     * the contact does not play twice but the break remains audible.
     */
    void spawn_impact(
        const TerrainImpactEvent& impact,
        TerrainImpactSoundPolicy sound_policy = TerrainImpactSoundPolicy::full);
    /**
     * Adds the restrained world-light flash owned by a visible weapon muzzle.
     *
     * This is presentation-only and intentionally rejects tools whose retail
     * shot has no bright propellant flash. Both local actions and replicated
     * ShootFeedback use it, without touching gameplay authority.
     */
    void spawn_weapon_flash(std::array<float, 3U> position,
                            std::uint8_t tool_id);
    void tick(double dt, const VxlMap& map);
    void clear() noexcept;

    [[nodiscard]] std::span<const TerrainEffectInstance> instances() const noexcept;
    /** Current blast lights, already evaluated at this tick's envelope. */
    [[nodiscard]] std::span<const DynamicLight> lights() const noexcept;
    [[nodiscard]] std::vector<TerrainSoundEvent> take_sound_events();

private:
    struct ActiveEffect final {
        TerrainEffectInstance presented;
        FallingComponent source;
        std::array<float, 3U> source_pivot{};
        std::array<float, 3U> velocity{};
        std::array<float, 3U> angular_velocity{};
        std::array<float, 3U> target_rotation_degrees{};
        float age{};
        float lifetime{1.0F};
    };
    struct ActiveLight final {
        DynamicLight presented;
        std::array<float, 3U> hot_color{1.0F, 0.92F, 0.72F};
        std::array<float, 3U> cool_color{1.0F, 0.22F, 0.025F};
        std::array<float, 3U> velocity{};
        float peak_radius{};
        float peak_intensity{};
        float age{};
        float lifetime{0.65F};
    };
    void make_debris(const ActiveEffect& source);
    void enforce_limit();

    std::vector<ActiveEffect> active_;
    std::vector<TerrainEffectInstance> public_instances_;
    std::vector<ActiveLight> active_lights_;
    std::vector<DynamicLight> public_lights_;
    std::vector<TerrainSoundEvent> sound_events_;
    std::uint64_t next_id_{1U};
    ParticleSystem* particles_{};
    std::optional<Kv6Model> grave_model_;
};

} // namespace battlespades::world
