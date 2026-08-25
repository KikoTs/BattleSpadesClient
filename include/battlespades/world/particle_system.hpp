#pragma once

#include "battlespades/world/vxl_map.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace battlespades::world {

/**
 * Source blend for one particle draw.
 *
 * Retail exposes only NONE/BLEND/ADDITIVE/SUBTRACTIVE through
 * `shared/constants.py` ALPHA_BLEND_MODE_*; `premultiplied` is NONRETAIL and
 * exists because sorted smoke reads far better when the texture carries its
 * own coverage.
 */
enum class ParticleBlend : std::uint8_t {
    alpha,
    additive,
    premultiplied,
};

/**
 * Fragment-colour path used by one particle draw.
 *
 * Most retail particles multiply their atlas by the authored RGBA tint.
 * `glow_lut` reproduces `use_lut_particles_for_effect` for the tumbling glow
 * cube atlas. `smoke_lut` is the distinct LUT path stored in retail's
 * GLOW_SMOKE_TRAIL_SPAWN_POINT: `SmokeTrail_anim_8x8.png` is indexed through
 * `particle_lut.png`. The two LUTs are not interchangeable.
 */
enum class ParticleColorMode : std::uint8_t {
    tinted,
    glow_lut,
    smoke_lut,
};

/**
 * Native child-emitter attached to a moving particle.
 *
 * Retail's GlowBlockParticles are not eight isolated sprites.  Each parent
 * calls GLOW_SMOKE_TRAIL_SPAWN_POINT once from its native update, leaving one
 * independently animated LUT sprite at the parent's new position.  Keeping
 * that contract in the particle simulation (instead of drawing fake history
 * copies) preserves lifetime, animation and pool behaviour.
 */
enum class ParticleChildEmitter : std::uint8_t {
    none,
    glow_smoke_trail,
};

/** Sprite sheet selection; each maps to one authored retail atlas. */
enum class ParticleAtlas : std::uint8_t {
    tumbling_cube,
    glow_cube,
    smoke_trail,
    pickup_twinkle,
    soft_round,
};

inline constexpr std::size_t particle_atlas_count{5U};
inline constexpr std::size_t particle_blend_count{3U};
inline constexpr std::size_t particle_color_mode_count{3U};

/**
 * One authored emission.
 *
 * Velocities use the native particle convention. The retail integrator moves
 * each particle by `velocity * 32 * dt`; they are neither metres per second
 * nor per-60 Hz displacements.
 */
struct ParticleSpawn final {
    std::array<float, 3U> position{};
    std::array<float, 3U> velocity{};
    /** The block's own captured colour, or an authored effect tint. */
    VxlColor color{255U, 255U, 255U, 255U};
    /** Retail `explode_velocity`: extra outward speed applied per particle. */
    float explode_velocity{};
    float size_begin{0.25F};
    float size_end{0.25F};
    float alpha_begin{1.0F};
    float alpha_end{0.0F};
    float rotation_degrees{180.0F};
    /** Degrees per second, exactly as the native particle update applies it. */
    float rotation_speed{};
    float lifetime{1.0F};
    /** Scales the recovered world gravity of 1.0; 0 exempts smoke and fire. */
    float gravity_scale{1.0F};
    /** Per-tick velocity retention; 0 disables drag. */
    float drag{};
    ParticleAtlas atlas{ParticleAtlas::tumbling_cube};
    ParticleBlend blend{ParticleBlend::alpha};
    ParticleColorMode color_mode{ParticleColorMode::tinted};
    std::uint8_t frames_x{8U};
    std::uint8_t frames_y{8U};
    /** Retail treats 0 as "pick a random start frame". */
    std::uint8_t start_frame{};
    std::uint8_t framerate{30U};
    /** Direction selected once for the whole retail emission. */
    bool forward_animate{true};
    bool loop{true};
    bool collide{true};
    ParticleChildEmitter child_emitter{ParticleChildEmitter::none};
};

/**
 * One GPU instance. 48 bytes is exactly three `vec4` instance-data slots.
 */
struct ParticleInstance final {
    std::array<float, 3U> position{};
    float size{};
    std::array<float, 4U> rgba{};
    float rotation_radians{};
    /** 0 at spawn, 1 at death. */
    float life01{};
    /** Float so the shader may cross-fade between sheet cells. */
    float frame{};
    float atlas_grid_x{};
};

static_assert(sizeof(ParticleInstance) == 48U);

/** One contiguous run of instances sharing an atlas and a blend mode. */
struct ParticleBatch final {
    ParticleAtlas atlas{ParticleAtlas::tumbling_cube};
    ParticleBlend blend{ParticleBlend::alpha};
    ParticleColorMode color_mode{ParticleColorMode::tinted};
    std::uint32_t first{};
    std::uint32_t count{};
};

/**
 * Bounded, allocation-free particle simulation.
 *
 * Deliberately headless: it owns no GPU resource and takes no renderer
 * dependency, so the whole burst geometry is unit-testable. Particles occupy
 * no world-model slot, which is why bursts can be orders of magnitude larger
 * than `TerrainEffectSimulation`'s 40-instance budget.
 *
 * Ordering is decided by build_draw_list(): blended buckets are sorted
 * back-to-front from the eye because every bgfx view in this client is
 * ViewMode::Sequential and performs no sorting of its own.
 */
class ParticleSystem final {
public:
    static constexpr std::size_t maximum_particles{4096U};

    ParticleSystem();

    void emit(const ParticleSpawn& spawn);
    /** Emits `count` particles scattered by `explode_velocity` from `seed`. */
    void emit_burst(const ParticleSpawn& base, std::uint32_t count, std::uint32_t seed);

    /**
     * Sets retail's particle-pool capacity proportion.
     *
     * `create_particles(max_particles_proportion)` changes only the number of
     * reusable native particle slots. It never scales an emitter's authored
     * burst count; a rocket remains 8 glow + 10 debris at every quality tier.
     */
    void set_quality_scale(float scale) noexcept;
    [[nodiscard]] float quality_scale() const noexcept { return quality_scale_; }

    void tick(double dt, const VxlMap& map);
    /** Advances particles without collision, for the offline VFX parity lab. */
    void tick_unbounded(double dt);
    void clear() noexcept;

    [[nodiscard]] std::size_t live_count() const noexcept { return live_; }

    /**
     * Builds the sorted, batched instance stream for one frame.
     *
     * Particles beyond `fog_distance` are dropped: the radial distance is
     * already computed for the sort key, so the cull is free.
     */
    void build_draw_list(std::array<float, 3U> eye, float fog_distance);

    [[nodiscard]] std::span<const ParticleInstance> instances() const noexcept;
    [[nodiscard]] std::span<const ParticleBatch> batches() const noexcept;

private:
    struct Particle final {
        std::array<float, 3U> position{};
        std::array<float, 3U> velocity{};
        std::array<float, 4U> color{};
        float alpha_begin{};
        float alpha_end{};
        float size_begin{};
        float size_end{};
        float rotation_degrees{};
        float rotation_speed{};
        float age{};
        float lifetime{};
        float gravity_scale{};
        float drag{};
        float frame{};
        float frame_timer{};
        ParticleAtlas atlas{ParticleAtlas::tumbling_cube};
        ParticleBlend blend{ParticleBlend::alpha};
        ParticleColorMode color_mode{ParticleColorMode::tinted};
        std::uint8_t frames_x{};
        std::uint8_t frames_y{};
        std::uint8_t framerate{};
        bool forward_animate{};
        bool loop{};
        bool collide{};
        ParticleChildEmitter child_emitter{ParticleChildEmitter::none};
        std::uint32_t child_seed{};
        std::uint32_t child_emissions{};
        bool alive{};
    };

    [[nodiscard]] std::size_t claim_slot() noexcept;
    void tick_impl(double dt, const VxlMap* map);

    std::vector<Particle> pool_;
    /** Ring cursor; the oldest particle is overwritten once the pool fills. */
    std::size_t cursor_{};
    std::size_t live_{};
    float quality_scale_{1.0F};
    std::size_t active_capacity_{maximum_particles};

    std::vector<ParticleInstance> instances_;
    std::vector<ParticleBatch> batches_;
    /** Deferred because claiming a ring slot while iterating can overwrite it. */
    std::vector<ParticleSpawn> child_spawns_;
    /** Scratch reused every frame so build_draw_list() does not allocate. */
    std::vector<std::pair<float, std::uint32_t>> sort_scratch_;
};

} // namespace battlespades::world
