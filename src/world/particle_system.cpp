#include "battlespades/world/particle_system.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace battlespades::world {
namespace {

/** Same bit mixer the terrain effect simulation uses, for matching scatter. */
[[nodiscard]] std::uint32_t mix(std::uint32_t value) noexcept {
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    return value ^ (value >> 16U);
}

/** Deterministic copy of the MSVCRT rand() step used by the 32-bit client. */
class RetailRand final {
public:
    explicit RetailRand(std::uint32_t seed) noexcept : state_{seed} {}

    [[nodiscard]] std::uint32_t next() noexcept {
        state_ = state_ * 214013U + 2531011U;
        return (state_ >> 16U) & 0x7FFFU;
    }

private:
    std::uint32_t state_{};
};

[[nodiscard]] constexpr float channel(std::uint8_t value) noexcept {
    return static_cast<float>(value) / 255.0F;
}

[[nodiscard]] bool cell_is_solid(const VxlMap& map, float x, float y, float z) noexcept {
    if (x < 0.0F || y < 0.0F || z < 0.0F) {
        return false;
    }
    const auto cell_x = static_cast<std::int64_t>(std::floor(x));
    const auto cell_y = static_cast<std::int64_t>(std::floor(y));
    const auto cell_z = static_cast<std::int64_t>(std::floor(z));
    if (cell_x >= static_cast<std::int64_t>(VxlMap::width) ||
        cell_y >= static_cast<std::int64_t>(VxlMap::depth)) {
        return false;
    }
    // The forced z=239 bed makes anything at or below the floor solid.
    if (cell_z >= static_cast<std::int64_t>(VxlMap::height)) {
        return true;
    }
    return map.solid(static_cast<std::uint32_t>(cell_x),
                     static_cast<std::uint32_t>(cell_y),
                     static_cast<std::uint32_t>(cell_z));
}

} // namespace

ParticleSystem::ParticleSystem() {
    pool_.resize(maximum_particles);
    instances_.reserve(maximum_particles);
    sort_scratch_.reserve(maximum_particles);
    child_spawns_.reserve(maximum_particles);
    batches_.reserve(particle_atlas_count * particle_blend_count *
                     particle_color_mode_count);
}

std::size_t ParticleSystem::claim_slot() noexcept {
    // A plain ring: once the pool is full the oldest particle is overwritten.
    // Cosmetic loss under a flood is preferable to dropping the newest burst,
    // which is the one the player is looking at.
    const std::size_t slot = cursor_;
    cursor_ = (cursor_ + 1U) % active_capacity_;
    if (!pool_[slot].alive) {
        ++live_;
    }
    return slot;
}

void ParticleSystem::emit(const ParticleSpawn& spawn) {
    if (spawn.lifetime <= 0.0F) {
        return;
    }
    const std::size_t slot = claim_slot();
    Particle& particle = pool_[slot];
    particle.position = spawn.position;
    particle.velocity = spawn.velocity;
    particle.color = {channel(spawn.color.red), channel(spawn.color.green),
                      channel(spawn.color.blue), 1.0F};
    particle.alpha_begin = spawn.alpha_begin * channel(spawn.color.alpha);
    particle.alpha_end = spawn.alpha_end * channel(spawn.color.alpha);
    particle.size_begin = spawn.size_begin;
    particle.size_end = spawn.size_end;
    particle.rotation_degrees = spawn.rotation_degrees;
    particle.rotation_speed = spawn.rotation_speed;
    particle.age = 0.0F;
    particle.lifetime = spawn.lifetime;
    particle.gravity_scale = spawn.gravity_scale;
    particle.drag = spawn.drag;
    particle.atlas = spawn.atlas;
    particle.blend = spawn.blend;
    particle.color_mode = spawn.color_mode;
    particle.frames_x = std::max<std::uint8_t>(1U, spawn.frames_x);
    particle.frames_y = std::max<std::uint8_t>(1U, spawn.frames_y);
    particle.framerate = spawn.framerate;
    particle.forward_animate = spawn.forward_animate;
    particle.loop = spawn.loop;
    particle.collide = spawn.collide;
    particle.child_emitter = spawn.child_emitter;
    particle.child_seed = mix(static_cast<std::uint32_t>(slot * 0x9E3779B9U) ^
                              static_cast<std::uint32_t>(
                                  std::lround(spawn.position[0U] * 17.0F +
                                              spawn.position[1U] * 257.0F +
                                              spawn.position[2U] * 4099.0F)));
    particle.child_emissions = 0U;
    particle.alive = true;
    // A particle is born where it is: nothing to blend from before that.
    particle.previous_position = particle.position;
    particle.previous_rotation_degrees = particle.rotation_degrees;
    particle.previous_age = particle.age;

    const auto cells =
        static_cast<std::uint32_t>(particle.frames_x) * particle.frames_y;
    particle.frame_timer = 0.0F;
    if (spawn.start_frame == 0U) {
        // draw.pyd stores frames one-based and calls rand()%cells+1 for every
        // particle. The renderer-facing value here is zero-based; include the
        // pool slot so colocated particles do not all start on the same cell.
        const auto seed = mix(static_cast<std::uint32_t>(
                                  std::lround(spawn.position[0U] * 8.0F +
                                              spawn.position[1U] * 131.0F +
                                              spawn.position[2U] * 977.0F)) ^
                              static_cast<std::uint32_t>(slot * 0x9E3779B9U));
        particle.frame = static_cast<float>(seed % cells);
    } else {
        particle.frame = static_cast<float>((spawn.start_frame - 1U) % cells);
    }
}

void ParticleSystem::emit_burst(const ParticleSpawn& base, std::uint32_t count,
                                std::uint32_t seed) {
    // Retail quality changes create_particles() pool capacity, not the count
    // passed to use_particles_for_effect(). Preserve every authored burst;
    // the ring naturally replaces its oldest members if capacity is exceeded.
    RetailRand retail_random{seed};
    for (std::uint32_t index{}; index < count; ++index) {
        const std::uint32_t particle_seed = mix(seed ^ (index * 0x9E3779B9U));
        ParticleSpawn spawn = base;
        if (base.explode_velocity != 0.0F) {
            // draw.pyd sub_10003080 samples a normalized uniform sphere:
            // z=rand()/16383-1, theta=rand()*0.00019175345369149. Using
            // sequential MSVCRT values matters visually: hashing seed+1 made
            // some small RPG bursts cluster into one hemisphere.
            const float z = std::clamp(
                static_cast<float>(retail_random.next()) / 16383.0F - 1.0F,
                -1.0F, 1.0F);
            const float theta = static_cast<float>(retail_random.next()) *
                                0.00019175345369149F;
            const float radius = std::sqrt(std::max(0.0F, 1.0F - z * z));
            spawn.velocity = {
                base.velocity[0U] +
                    std::cos(theta) * radius * base.explode_velocity,
                base.velocity[1U] +
                    std::sin(theta) * radius * base.explode_velocity,
                base.velocity[2U] +
                    z * base.explode_velocity,
            };
        }
        // The constructor copies authored rotation, angular speed and lifetime
        // verbatim into every member of the burst. Randomizing them was a port
        // invention and is especially visible when a retail explosion ends.
        if (base.start_frame == 0U) {
            // Native rand() is evaluated once per claimed particle. Supplying
            // the recovered one-based cell here preserves that variation while
            // keeping deterministic replay for tests and captures.
            const auto cells = static_cast<std::uint32_t>(
                std::max<std::uint8_t>(1U, base.frames_x)) *
                               std::max<std::uint8_t>(1U, base.frames_y);
            spawn.start_frame = static_cast<std::uint8_t>(particle_seed % cells + 1U);
        } else {
            spawn.start_frame = base.start_frame;
        }
        emit(spawn);
    }
}

void ParticleSystem::set_quality_scale(float scale) noexcept {
    quality_scale_ = std::clamp(scale, 0.0F, 1.0F);
    active_capacity_ = std::max<std::size_t>(
        1U,
        static_cast<std::size_t>(
            std::lround(static_cast<float>(maximum_particles) * quality_scale_)));
    for (std::size_t index = active_capacity_; index < pool_.size(); ++index) {
        if (pool_[index].alive) {
            pool_[index].alive = false;
            --live_;
        }
    }
    cursor_ %= active_capacity_;
}

void ParticleSystem::tick(double dt, const VxlMap& map) {
    tick_impl(dt, &map);
}

void ParticleSystem::tick_unbounded(double dt) {
    tick_impl(dt, nullptr);
}

void ParticleSystem::tick_impl(double dt, const VxlMap* map) {
    // Native draw.pyd sub_1000BED0 uses velocity * (32 * dt). This constant is
    // independent of the game's 60 Hz simulation rate.
    const float seconds = static_cast<float>(std::clamp(dt, 0.0, 0.1));
    const float retail_motion = seconds * 32.0F;
    const float drag_steps = seconds * 60.0F;
    child_spawns_.clear();
    for (Particle& particle : pool_) {
        if (!particle.alive) {
            continue;
        }
        // Presentation only: the state a render-rate frame blends from.
        particle.previous_position = particle.position;
        particle.previous_rotation_degrees = particle.rotation_degrees;
        particle.previous_age = particle.age;
        particle.age += seconds;
        if (particle.age >= particle.lifetime) {
            particle.alive = false;
            --live_;
            continue;
        }
        // draw.pyd sub_1000BED0: v.z += dt * particle_gravity (the StateData
        // world gravity) for particles created with gravity=True.
        particle.velocity[2U] += seconds * particle.gravity_scale * gravity_;
        if (particle.drag > 0.0F) {
            const float retained = std::max(0.0F, 1.0F - particle.drag * drag_steps);
            for (float& axis : particle.velocity) {
                axis *= retained;
            }
        }

        // sub_10033D70 first copies the current position into the particle's
        // "previous" slot and only then moves it; that slot is the position
        // sub_10033C00 hands to the spawn-point child.
        const std::array<float, 3U> previous_position = particle.position;
        std::array<float, 3U> next{
            particle.position[0U] + particle.velocity[0U] * retail_motion,
            particle.position[1U] + particle.velocity[1U] * retail_motion,
            particle.position[2U] + particle.velocity[2U] * retail_motion,
        };
        if (particle.collide && map != nullptr &&
            cell_is_solid(*map, next[0U], next[1U], next[2U])) {
            // Native collision resolves only the crossed cell axis (z, then x,
            // then y), restores the previous position, damps angular velocity
            // by .7 and every linear axis by .5. It bounces; it never enters a
            // synthetic "rest" state.
            const auto old_cell = std::array<std::int64_t, 3U>{
                static_cast<std::int64_t>(std::floor(particle.position[0U])),
                static_cast<std::int64_t>(std::floor(particle.position[1U])),
                static_cast<std::int64_t>(std::floor(particle.position[2U]))};
            const auto next_cell = std::array<std::int64_t, 3U>{
                static_cast<std::int64_t>(std::floor(next[0U])),
                static_cast<std::int64_t>(std::floor(next[1U])),
                static_cast<std::int64_t>(std::floor(next[2U]))};
            if (next_cell[2U] != old_cell[2U]) {
                particle.velocity[2U] = -particle.velocity[2U];
            } else if (next_cell[0U] != old_cell[0U]) {
                particle.velocity[0U] = -particle.velocity[0U];
            } else if (next_cell[1U] != old_cell[1U]) {
                particle.velocity[1U] = -particle.velocity[1U];
            }
            next = particle.position;
            particle.rotation_speed *= 0.7F;
            for (float& axis : particle.velocity) {
                axis *= 0.5F;
            }
        }
        particle.position = next;
        particle.rotation_degrees += particle.rotation_speed * seconds;

        if (particle.child_emitter == ParticleChildEmitter::glow_smoke_trail &&
            child_spawns_.size() < maximum_particles) {
            // draw.pyd sub_10033D70 calls the configured spawn point exactly
            // once per parent update. sub_10033C00 then creates one
            // stationary child from four MSVCRT rand() calls, in this order:
            //   size     = rand()/5458.1665 + 3     (authored 3..9)
            //   rotation = rand()/3.8350067 + 160
            //   frame    = rand() % 64 + 1          (overrides the record's 1)
            //   forward  = rand() != 0              (practically always true)
            // Everything else comes from GLOW_SMOKE_TRAIL_SPAWN_POINT, built
            // at gameScene module init (0x101BA447..0x101BA9E4):
            // create_particle_spawn_point(1, 0.0, uniform(3,6), randint(160,
            // 200), 0, -1, BLOCK_SMOKE_TRAIL_LIFETIME=1.0, False, False,
            // particle_smoke_trail, particle_smoke_lut, 1, 8, 8, 1, 0, 60,
            // ALPHA_BLEND_MODE_BLEND). decay_rate -1 means the puff GROWS to
            // twice its size over its life (sub_10033D70: size *
            // (1 - elapsed01 * decay) * 0.1), at 60 fps without looping.
            RetailRand child_random{
                mix(particle.child_seed ^
                    (particle.child_emissions++ * 0x9E3779B9U))};
            const float authored_size =
                static_cast<float>(child_random.next()) / 5458.16650390625F + 3.0F;

            ParticleSpawn child;
            child.position = previous_position;
            child.color = VxlColor{
                static_cast<std::uint8_t>(std::clamp(
                    std::lround(particle.color[0U] * 255.0F), 0L, 255L)),
                static_cast<std::uint8_t>(std::clamp(
                    std::lround(particle.color[1U] * 255.0F), 0L, 255L)),
                static_cast<std::uint8_t>(std::clamp(
                    std::lround(particle.color[2U] * 255.0F), 0L, 255L)),
                255U};
            child.size_begin = authored_size * 0.1F;
            child.size_end = child.size_begin * 2.0F;
            child.alpha_begin = 1.0F;
            child.alpha_end = 1.0F;
            child.rotation_degrees =
                static_cast<float>(child_random.next()) / 3.835006713867188F +
                160.0F;
            child.rotation_speed = 0.0F;
            child.lifetime = 1.0F;
            child.gravity_scale = 0.0F;
            // IDA: glowBlockParticles.py builds GLOW_SMOKE_TRAIL_SPAWN_POINT
            // from aosimages.particle_smoke_trail and particle_smoke_lut.
            // The parent is the glow cube; its history is the authored cloud
            // atlas, not another chain of cubes.
            child.atlas = ParticleAtlas::smoke_trail;
            child.blend = ParticleBlend::alpha;
            child.color_mode = ParticleColorMode::smoke_lut;
            child.frames_x = 8U;
            child.frames_y = 8U;
            child.start_frame = static_cast<std::uint8_t>(
                child_random.next() % 64U + 1U);
            child.framerate = 60U;
            child.forward_animate = child_random.next() != 0U;
            child.loop = false;
            child.collide = false;
            child_spawns_.push_back(child);
        }

        if (particle.framerate > 0U) {
            particle.frame_timer += seconds;
            const float interval = 1.0F / static_cast<float>(particle.framerate);
            if (particle.frame_timer >= interval) {
                // draw.pyd sub_10008DA0 advances at most one cell per update
                // and discards the remainder. Its one-based loop comparison
                // also skips the terminal cell when moving forward (and the
                // first when moving backward); preserve that odd retail rule.
                const auto cells = static_cast<std::uint32_t>(particle.frames_x) *
                                   particle.frames_y;
                const auto frame = static_cast<std::uint32_t>(particle.frame);
                if (particle.forward_animate) {
                    if (frame + 2U < cells) {
                        particle.frame = static_cast<float>(frame + 1U);
                    } else {
                        particle.frame = particle.loop
                                             ? 0.0F
                                             : static_cast<float>(cells - 1U);
                    }
                } else if (frame > 1U) {
                    particle.frame = static_cast<float>(frame - 1U);
                } else {
                    particle.frame = particle.loop
                                         ? static_cast<float>(cells - 1U)
                                         : 0.0F;
                }
                particle.frame_timer = 0.0F;
            }
        }
    }
    for (const ParticleSpawn& child : child_spawns_) {
        emit(child);
    }
}

void ParticleSystem::clear() noexcept {
    for (Particle& particle : pool_) {
        particle.alive = false;
    }
    cursor_ = 0U;
    live_ = 0U;
    instances_.clear();
    batches_.clear();
    child_spawns_.clear();
}

void ParticleSystem::build_draw_list(std::array<float, 3U> eye, float fog_distance,
                                     double alpha) {
    instances_.clear();
    batches_.clear();
    // Below 1 the instance is drawn between its last two simulated states;
    // at 1 (every frame of a 60 Hz presentation) it is the simulated state
    // itself, with no arithmetic applied to it.
    const bool blended = alpha < 1.0;
    const float blend_weight =
        blended && std::isfinite(alpha) ? static_cast<float>(std::max(alpha, 0.0)) : 1.0F;
    const float cull = fog_distance > 0.0F ? fog_distance : 0.0F;
    const float cull_squared = cull * cull;

    // Rocket.delete creates the LUT glow first and the map-colour chunks
    // second. Preserve that painter order across renderer batches: debris
    // must sit over the central glow, as in retail and the packet capture.
    constexpr std::array color_mode_order{
        ParticleColorMode::smoke_lut,
        ParticleColorMode::glow_lut,
        ParticleColorMode::tinted};
    constexpr std::size_t combo_count =
        particle_blend_count * color_mode_order.size() * particle_atlas_count;
    const auto color_mode_rank = [&](ParticleColorMode mode) -> std::size_t {
        for (std::size_t rank{}; rank < color_mode_order.size(); ++rank) {
            if (color_mode_order[rank] == mode) return rank;
        }
        return color_mode_order.size();
    };
    // One pass over the pool buckets every live, unculled particle by its
    // (blend, colour mode, atlas) batch. The previous form rescanned all 4096
    // slots once per combination -- 54 full scans a frame even when idle.
    thread_local std::array<std::vector<std::pair<float, std::uint32_t>>, combo_count> buckets;
    for (auto& bucket : buckets) bucket.clear();
    for (std::uint32_t index{}; index < pool_.size(); ++index) {
        const Particle& particle = pool_[index];
        if (!particle.alive) {
            continue;
        }
        const auto rank = color_mode_rank(particle.color_mode);
        const auto blend_index = static_cast<std::size_t>(particle.blend);
        const auto atlas_index = static_cast<std::size_t>(particle.atlas);
        if (rank >= color_mode_order.size() || blend_index >= particle_blend_count ||
            atlas_index >= particle_atlas_count) {
            continue;
        }
        const float dx = particle.position[0U] - eye[0U];
        const float dy = particle.position[1U] - eye[1U];
        const float dz = particle.position[2U] - eye[2U];
        const float distance_squared = dx * dx + dy * dy + dz * dz;
        if (cull_squared > 0.0F && distance_squared > cull_squared) {
            continue;
        }
        buckets[(blend_index * color_mode_order.size() + rank) * particle_atlas_count +
                atlas_index]
            .emplace_back(distance_squared, index);
    }

    for (std::size_t blend_index{}; blend_index < particle_blend_count; ++blend_index) {
        const auto blend = static_cast<ParticleBlend>(blend_index);
        for (std::size_t rank{}; rank < color_mode_order.size(); ++rank) {
            const auto color_mode = color_mode_order[rank];
            for (std::size_t atlas_index{}; atlas_index < particle_atlas_count; ++atlas_index) {
                const auto atlas = static_cast<ParticleAtlas>(atlas_index);
                auto& bucket =
                    buckets[(blend_index * color_mode_order.size() + rank) *
                                particle_atlas_count +
                            atlas_index];
                if (bucket.empty()) {
                    continue;
                }
                if (blend != ParticleBlend::additive) {
                    // Additive is order-independent; the others must resolve
                    // back-to-front because nothing writes depth.
                    std::ranges::sort(bucket, std::greater{},
                                      &std::pair<float, std::uint32_t>::first);
                }

                const auto first = static_cast<std::uint32_t>(instances_.size());
                const auto append_instance = [this, blended,
                                              blend_weight](const Particle& particle) {
                    const float age =
                        blended ? particle.previous_age +
                                      (particle.age - particle.previous_age) * blend_weight
                                : particle.age;
                    const float life01 = std::clamp(age / particle.lifetime, 0.0F, 1.0F);
                    const float size =
                        particle.size_begin +
                        (particle.size_end - particle.size_begin) * life01;
                    const float opacity =
                        (particle.alpha_begin +
                         (particle.alpha_end - particle.alpha_begin) * life01);
                    ParticleInstance instance;
                    instance.position = particle.position;
                    float rotation_degrees = particle.rotation_degrees;
                    if (blended) {
                        for (std::size_t axis{}; axis < 3U; ++axis) {
                            instance.position[axis] =
                                particle.previous_position[axis] +
                                (particle.position[axis] - particle.previous_position[axis]) *
                                    blend_weight;
                        }
                        rotation_degrees =
                            particle.previous_rotation_degrees +
                            (particle.rotation_degrees - particle.previous_rotation_degrees) *
                                blend_weight;
                    }
                    instance.size = std::max(0.0F, size);
                    instance.rgba = {particle.color[0U], particle.color[1U],
                                     particle.color[2U],
                                     std::clamp(opacity, 0.0F, 1.0F)};
                    if (particle.blend == ParticleBlend::premultiplied) {
                        for (std::size_t axis{}; axis < 3U; ++axis) {
                            instance.rgba[axis] *= instance.rgba[3U];
                        }
                    }
                    instance.rotation_radians =
                        rotation_degrees * static_cast<float>(std::numbers::pi / 180.0);
                    instance.life01 = life01;
                    instance.frame = std::floor(particle.frame);
                    instance.atlas_grid_x = static_cast<float>(particle.frames_x);
                    instances_.push_back(instance);
                };
                for (const auto& [distance_squared, index] : bucket) {
                    static_cast<void>(distance_squared);
                    const Particle& particle = pool_[index];
                    append_instance(particle);
                }
                batches_.push_back(ParticleBatch{
                    atlas, blend, color_mode, first,
                    static_cast<std::uint32_t>(instances_.size()) - first});
            }
        }
    }
}

std::span<const ParticleInstance> ParticleSystem::instances() const noexcept {
    return instances_;
}

std::span<const ParticleBatch> ParticleSystem::batches() const noexcept {
    return batches_;
}

} // namespace battlespades::world
