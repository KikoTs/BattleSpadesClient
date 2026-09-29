#include "battlespades/world/terrain_effects.hpp"

#include "battlespades/world/particle_effects.hpp"
#include "battlespades/world/retail_character_pose.hpp"
#include "battlespades/world/retail_view_model.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_set>
#include <utility>

namespace battlespades::world {
namespace {

struct CellHash final {
    [[nodiscard]] std::size_t operator()(const VoxelCell& cell) const noexcept {
        return (static_cast<std::size_t>(cell.x) * 73'856'093U) ^
               (static_cast<std::size_t>(cell.y) * 19'349'663U) ^
               (static_cast<std::size_t>(cell.z) * 83'492'791U);
    }
};

struct Face final {
    std::array<std::int32_t, 3U> normal;
    std::array<std::array<float, 3U>, 4U> corners;
    float shade;
};

constexpr std::array<Face, 6U> faces{{
    {{{-1, 0, 0}}, {{{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}}, 0.85F},
    {{{1, 0, 0}}, {{{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}}, 0.85F},
    {{{0, -1, 0}}, {{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}}, 0.75F},
    {{{0, 1, 0}}, {{{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}}, 0.75F},
    {{{0, 0, -1}}, {{{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}}}, 1.00F},
    {{{0, 0, 1}}, {{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}}, 0.60F},
}};

[[nodiscard]] std::uint32_t pack_abgr(VxlColor color, float shade) noexcept {
    const auto channel = [shade](std::uint8_t value) {
        return static_cast<std::uint32_t>(
            std::clamp(std::lround(static_cast<float>(value) * shade), 0L, 255L));
    };
    // Alpha is SELF-ILLUMINATION, not opacity: fs_world applies it as
    // `lit += albedo * v_color0.a * u_emissiveParams.x` after every occlusion
    // term, deliberately, because a light source is not dimmed by shadow or
    // ambient occlusion. Terrain writes 0 there.
    //
    // Leaving this at 255 gave every falling structure and every debris chip a
    // full unshaded copy of its own albedo on top of its lit result, so a
    // collapsing wall glowed and visibly failed to match the world it had just
    // been part of -- brightest of all on the night maps, where the surrounding
    // terrain is dim and the added term is not.
    //
    // Rubble is not a light source.
    return (channel(color.blue) << 16U) | (channel(color.green) << 8U) |
           channel(color.red);
}

void add_cube(ChunkMesh& mesh, std::array<float, 3U> origin, float size,
              VxlColor color,
              const std::array<bool, 6U>& visible = {true, true, true, true, true, true}) {
    for (std::uint8_t face_index{}; face_index < faces.size(); ++face_index) {
        if (!visible[face_index]) {
            continue;
        }
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (const auto& corner : faces[face_index].corners) {
            const ChunkVertex vertex{
                origin[0U] + corner[0U] * size,
                origin[1U] + corner[1U] * size,
                origin[2U] + corner[2U] * size,
                // Pure albedo: fs_world lights these per pixel from face_index,
                // so baking the face shade here would darken them twice.
                pack_abgr(color, 1.0F),
                face_index,
                0U,
                0U,
                0U,
            };
            mesh.vertices.push_back(vertex);
            for (std::size_t axis{}; axis < 3U; ++axis) {
                const float value = axis == 0U ? vertex.x : axis == 1U ? vertex.y : vertex.z;
                mesh.minimum[axis] = std::min(mesh.minimum[axis], value);
                mesh.maximum[axis] = std::max(mesh.maximum[axis], value);
            }
        }
        constexpr std::array<std::uint32_t, 6U> indices{0U, 1U, 2U, 0U, 2U, 3U};
        for (const auto index : indices) {
            mesh.indices.push_back(base + index);
        }
    }
}

[[nodiscard]] ChunkMesh component_mesh(const FallingComponent& component,
                                       std::array<float, 3U> pivot) {
    ChunkMesh mesh;
    constexpr float maximum = std::numeric_limits<float>::max();
    mesh.minimum = {maximum, maximum, maximum};
    mesh.maximum = {-maximum, -maximum, -maximum};
    std::unordered_set<VoxelCell, CellHash> cells;
    cells.reserve(component.size());
    for (const auto& voxel : component) {
        cells.insert(voxel.cell);
    }
    for (const auto& voxel : component) {
        std::array<bool, 6U> visible{};
        for (std::size_t index{}; index < faces.size(); ++index) {
            const auto& normal = faces[index].normal;
            const auto x = static_cast<std::int64_t>(voxel.cell.x) + normal[0U];
            const auto y = static_cast<std::int64_t>(voxel.cell.y) + normal[1U];
            const auto z = static_cast<std::int64_t>(voxel.cell.z) + normal[2U];
            visible[index] = x < 0 || y < 0 || z < 0 ||
                             x >= static_cast<std::int64_t>(VxlMap::width) ||
                             y >= static_cast<std::int64_t>(VxlMap::depth) ||
                             z >= static_cast<std::int64_t>(VxlMap::height) ||
                             !cells.contains({static_cast<std::uint32_t>(x),
                                              static_cast<std::uint32_t>(y),
                                              static_cast<std::uint32_t>(z)});
        }
        add_cube(mesh,
                 {static_cast<float>(voxel.cell.x) - pivot[0U],
                  static_cast<float>(voxel.cell.y) - pivot[1U],
                  static_cast<float>(voxel.cell.z) - pivot[2U]},
                 1.0F, voxel.color, visible);
    }
    return mesh;
}

[[nodiscard]] ChunkMesh chip_mesh(VxlColor color, float size) {
    ChunkMesh mesh;
    constexpr float maximum = std::numeric_limits<float>::max();
    mesh.minimum = {maximum, maximum, maximum};
    mesh.maximum = {-maximum, -maximum, -maximum};
    add_cube(mesh, {-size * 0.5F, -size * 0.5F, -size * 0.5F}, size, color);
    return mesh;
}

[[nodiscard]] std::uint32_t mix(std::uint32_t value) noexcept {
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    return value ^ (value >> 16U);
}

[[nodiscard]] float random_signed(std::uint32_t seed) noexcept {
    return static_cast<float>(mix(seed) & 0xFFFFU) / 32767.5F - 1.0F;
}

[[nodiscard]] constexpr float degrees_to_radians(float degrees) noexcept {
    return degrees * 0.01745329251994329577F;
}

/**
 * Applies the exact row-vector Rx -> Ry -> Rz chain used by
 * NativeFrontendModule::terrain_effect_draws.
 *
 * Keeping debris emission on the same transform is important: the particle
 * burst must leave the tilted wall's visible position rather than its old,
 * upright coordinates.
 */
[[nodiscard]] std::array<float, 3U> rotate_point(
    std::array<float, 3U> point,
    const std::array<float, 3U>& rotation_degrees) noexcept {
    {
        const float angle = degrees_to_radians(rotation_degrees[0U]);
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        const float y = point[1U] * cosine - point[2U] * sine;
        const float z = point[1U] * sine + point[2U] * cosine;
        point[1U] = y;
        point[2U] = z;
    }
    {
        const float angle = degrees_to_radians(rotation_degrees[1U]);
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        const float x = point[0U] * cosine + point[2U] * sine;
        const float z = -point[0U] * sine + point[2U] * cosine;
        point[0U] = x;
        point[2U] = z;
    }
    {
        const float angle = degrees_to_radians(rotation_degrees[2U]);
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        const float x = point[0U] * cosine - point[1U] * sine;
        const float y = point[0U] * sine + point[1U] * cosine;
        point[0U] = x;
        point[1U] = y;
    }
    return point;
}

[[nodiscard]] std::array<float, 3U> transformed_voxel_center(
    const FallingVoxel& voxel, const std::array<float, 3U>& source_pivot,
    const TerrainEffectInstance& presented) noexcept {
    auto relative = std::array<float, 3U>{
        static_cast<float>(voxel.cell.x) + 0.5F - source_pivot[0U],
        static_cast<float>(voxel.cell.y) + 0.5F - source_pivot[1U],
        static_cast<float>(voxel.cell.z) + 0.5F - source_pivot[2U],
    };
    relative = rotate_point(relative, presented.rotation_degrees);
    for (std::size_t axis{}; axis < relative.size(); ++axis) {
        relative[axis] += presented.position[axis];
    }
    return relative;
}

} // namespace

bool retail_falling_blocks_step(const VxlMap& map, std::array<float, 3U>& position,
                                std::array<float, 3U>& velocity, float dt,
                                float gravity) noexcept {
    const auto previous = position;
    velocity[2U] += gravity * dt;
    const float scale = dt * 32.0F;
    for (std::size_t axis{}; axis < 3U; ++axis) position[axis] += velocity[axis] * scale;
    const auto cell_x = static_cast<std::int64_t>(std::floor(position[0U]));
    const auto cell_y = static_cast<std::int64_t>(std::floor(position[1U]));
    const auto cell_z = static_cast<std::int64_t>(std::floor(position[2U]));
    if (cell_z < 0) return false;
    // The water-aware solid test: z=239 probes 238 (the bed is water), and
    // anything at or below z=240 always counts as a hit.
    if (cell_z < static_cast<std::int64_t>(VxlMap::height)) {
        const auto probe_z =
            cell_z == static_cast<std::int64_t>(VxlMap::height) - 1 ? cell_z - 1 : cell_z;
        if (cell_x < 0 || cell_y < 0 || cell_x >= static_cast<std::int64_t>(VxlMap::width) ||
            cell_y >= static_cast<std::int64_t>(VxlMap::depth) ||
            !map.solid(static_cast<std::uint32_t>(cell_x), static_cast<std::uint32_t>(cell_y),
                       static_cast<std::uint32_t>(probe_z))) {
            return false;
        }
    }
    // Reflect the axis whose cell changed (z first, then x, else y), restore
    // the previous position and halve the velocity.
    if (cell_z != static_cast<std::int64_t>(std::floor(previous[2U]))) {
        velocity[2U] = -velocity[2U];
    } else if (cell_x != static_cast<std::int64_t>(std::floor(previous[0U]))) {
        velocity[0U] = -velocity[0U];
    } else if (cell_y != static_cast<std::int64_t>(std::floor(previous[1U]))) {
        velocity[1U] = -velocity[1U];
    }
    position = previous;
    for (auto& component : velocity) component *= 0.5F;
    return true;
}

std::size_t falling_blocks_particle_mod(std::size_t block_count) noexcept {
    // int(FALLING_BLOCKS_PARTICLE_MOD_MIN + size / FALLING_BLOCKS_MAX_SIZE *
    //     (FALLING_BLOCKS_PARTICLE_MOD_MAX - FALLING_BLOCKS_PARTICLE_MOD_MIN))
    return static_cast<std::size_t>(
        static_cast<double>(falling_blocks_particle_mod_min) +
        static_cast<double>(block_count) / static_cast<double>(falling_blocks_max_size) *
            static_cast<double>(falling_blocks_particle_mod_max -
                                falling_blocks_particle_mod_min));
}

std::array<float, 3U> retail_random_unit_axis(std::uint32_t& state) noexcept {
    const auto next = [&state] {
        state = state * 214'013U + 2'531'011U;
        return static_cast<double>((state >> 16U) & 0x7FFFU);
    };
    // world.pyd sub_100027F0: z = rand()/16383 - 1, phi = rand() * 2pi/32767.
    const double z = next() / 16383.0 - 1.0;
    const double phi = next() * 0.00019175345369149;
    const double radius = std::sqrt(std::max(0.0, 1.0 - z * z));
    return {static_cast<float>(std::cos(phi) * radius),
            static_cast<float>(std::sin(phi) * radius), static_cast<float>(z)};
}

std::string_view falling_sound_group(TerrainSoundKind kind,
                                     std::size_t block_count,
                                     bool submerged) noexcept {
    const auto tier = falling_sound_tier(block_count);
    if (kind == TerrainSoundKind::structure_split) {
        switch (tier) {
        case FallingSoundTier::small:
            return "des_split_small_001-003";
        case FallingSoundTier::medium:
            return "des_split_med_001-003";
        case FallingSoundTier::large:
            return "des_split_large_001-003";
        }
    }
    if (kind == TerrainSoundKind::structure_break) {
        if (submerged) {
            switch (tier) {
            case FallingSoundTier::small:
                return "des_imp_small_water_001-004";
            case FallingSoundTier::medium:
                return "des_imp_med_water_001-004";
            case FallingSoundTier::large:
                return "des_imp_large_water_001-004";
            }
        }
        switch (tier) {
        case FallingSoundTier::small:
            return "des_imp_small_001-004";
        case FallingSoundTier::medium:
            return "des_imp_med_001-004";
        case FallingSoundTier::large:
            return "des_imp_large_001-004";
        }
    }
    return {};
}

ChunkMesh placement_preview_cube(VxlColor color) {
    ChunkMesh mesh;
    constexpr float maximum = std::numeric_limits<float>::max();
    mesh.minimum = {maximum, maximum, maximum};
    mesh.maximum = {-maximum, -maximum, -maximum};
    add_cube(mesh, {0.0F, 0.0F, 0.0F}, 1.0F, color);
    return mesh;
}

ChunkMesh placement_preview_wire_cube(VxlColor color) {
    ChunkMesh mesh;
    constexpr float maximum = std::numeric_limits<float>::max();
    mesh.minimum = {maximum, maximum, maximum};
    mesh.maximum = {-maximum, -maximum, -maximum};
    // draw_cube(..., textured_wireframe=True): the twelve cube edges as thin
    // bars, so the classic ghost reads as an outline rather than a solid.
    constexpr float thickness{0.04F};
    const auto add_bar = [&mesh, color](std::array<float, 3U> low, std::array<float, 3U> high) {
        for (std::uint8_t face_index{}; face_index < faces.size(); ++face_index) {
            const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
            for (const auto& corner : faces[face_index].corners) {
                const ChunkVertex vertex{
                    low[0U] + corner[0U] * (high[0U] - low[0U]),
                    low[1U] + corner[1U] * (high[1U] - low[1U]),
                    low[2U] + corner[2U] * (high[2U] - low[2U]),
                    pack_abgr(color, 1.0F),
                    face_index,
                    0U,
                    0U,
                    0U,
                };
                mesh.vertices.push_back(vertex);
                for (std::size_t axis{}; axis < 3U; ++axis) {
                    const float value = axis == 0U ? vertex.x : axis == 1U ? vertex.y : vertex.z;
                    mesh.minimum[axis] = std::min(mesh.minimum[axis], value);
                    mesh.maximum[axis] = std::max(mesh.maximum[axis], value);
                }
            }
            constexpr std::array<std::uint32_t, 6U> indices{0U, 1U, 2U, 0U, 2U, 3U};
            for (const auto index : indices) {
                mesh.indices.push_back(base + index);
            }
        }
    };
    constexpr float low_edge{-thickness * 0.5F};
    constexpr float high_edge{1.0F + thickness * 0.5F};
    for (const float a : {low_edge, 1.0F - thickness * 0.5F}) {
        for (const float b : {low_edge, 1.0F - thickness * 0.5F}) {
            add_bar({low_edge, a, b}, {high_edge, a + thickness, b + thickness});
            add_bar({a, low_edge, b}, {a + thickness, high_edge, b + thickness});
            add_bar({a, b, low_edge}, {a + thickness, b + thickness, high_edge});
        }
    }
    return mesh;
}

void TerrainEffectSimulation::set_particle_sink(ParticleSystem* particles) noexcept {
    particles_ = particles;
}

void TerrainEffectSimulation::set_grave_model(Kv6Model model) {
    grave_model_ = std::move(model);
}

void TerrainEffectSimulation::spawn_falling(FallingComponent component) {
    if (component.empty()) {
        return;
    }
    // gameScene FallingBlocks.initialize (0x100C7290): the body's origin is
    // the bounding-box centre, x1 + (x2 - x1) * 0.5 on every axis; world.pyd
    // FallingBlocks.initialize (0x1000B7C0) zeroes the velocity and picks a
    // random unit tumble axis. There is no size cap: every voxel is drawn.
    std::array<std::uint32_t, 3U> minimum{
        std::numeric_limits<std::uint32_t>::max(),
        std::numeric_limits<std::uint32_t>::max(),
        std::numeric_limits<std::uint32_t>::max()};
    std::array<std::uint32_t, 3U> maximum{};
    for (const auto& voxel : component) {
        for (std::size_t axis{}; axis < minimum.size(); ++axis) {
            const std::uint32_t coordinate =
                axis == 0U ? voxel.cell.x : axis == 1U ? voxel.cell.y : voxel.cell.z;
            minimum[axis] = std::min(minimum[axis], coordinate);
            maximum[axis] = std::max(maximum[axis], coordinate);
        }
    }
    std::array<float, 3U> pivot{};
    for (std::size_t axis{}; axis < pivot.size(); ++axis) {
        pivot[axis] = static_cast<float>(minimum[axis]) +
                      static_cast<float>(maximum[axis] - minimum[axis]) * 0.5F;
    }
    const auto seed = mix(component.front().cell.x ^ (component.front().cell.y << 10U) ^
                          (component.front().cell.z << 20U));
    ActiveEffect effect;
    effect.presented.id = next_id_++;
    effect.presented.kind = TerrainEffectKind::falling_structure;
    effect.presented.position = pivot;
    effect.source_pivot = pivot;
    effect.presented.mesh = component_mesh(component, pivot);
    // No timer: the body lives until its first map contact.
    effect.lifetime = std::numeric_limits<float>::infinity();
    std::uint32_t axis_state = seed;
    effect.angular_velocity = retail_random_unit_axis(axis_state);
    const auto block_count = static_cast<std::uint16_t>(
        std::min<std::size_t>(component.size(), std::numeric_limits<std::uint16_t>::max()));
    // Retail plays a des_split_ group the moment a component detaches. It uses
    // one 0.75 volume for every size; perceived weight comes from the authored
    // small/medium/large bank, not from scaling the same generic sample.
    sound_events_.push_back({TerrainSoundKind::structure_split, pivot,
                             0.75F,
                             static_cast<std::uint8_t>(seed % 3U), 0U,
                             block_count});
    effect.source = std::move(component);
    active_.push_back(std::move(effect));
    enforce_limit();
}

void TerrainEffectSimulation::spawn_impact(const TerrainImpactEvent& impact,
                                           TerrainImpactSoundPolicy sound_policy) {
    if (impact.kind == TerrainImpactKind::burn || impact.kind == TerrainImpactKind::dissolve) {
        // Single-block fire/goo ticks have no sound, light or blast. Goo
        // throws chemical debris on every tick (on_single_block_damaged);
        // fire only chips the block it finally burns through. The chemical
        // debris tint is VERIFY (A2433's goo green is used).
        static_cast<void>(sound_policy);
        if (particles_ != nullptr &&
            (impact.kind == TerrainImpactKind::dissolve || impact.destroyed)) {
            auto chips = impact;
            if (impact.kind == TerrainImpactKind::dissolve) {
                chips.color = VxlColor{20U, 255U, 50U, 255U};
            }
            emit_block_break(*particles_, chips);
        }
        return;
    }
    const auto origin = terrain_impact_position(impact);
    const bool explosive = impact.kind == TerrainImpactKind::explosion ||
                           impact.kind == TerrainImpactKind::fire ||
                           impact.kind == TerrainImpactKind::chemical;
    const auto sound_kind = impact.kind == TerrainImpactKind::bullet
                                ? TerrainSoundKind::bullet_impact
                            : impact.kind == TerrainImpactKind::melee
                                ? TerrainSoundKind::melee_impact
                            : impact.kind == TerrainImpactKind::block_cannon
                                ? TerrainSoundKind::block_cannon_impact
                            : impact.kind == TerrainImpactKind::corpse_explosion
                                ? TerrainSoundKind::death_explosion
                            : impact.kind == TerrainImpactKind::grave_explosion
                                ? TerrainSoundKind::explosion
                            : impact.kind == TerrainImpactKind::fire
                                ? TerrainSoundKind::fire_explosion
                            : impact.kind == TerrainImpactKind::drill
                                ? TerrainSoundKind::drill_bore
                                : TerrainSoundKind::explosion;
    if (sound_policy == TerrainImpactSoundPolicy::full) {
        sound_events_.push_back({
            sound_kind,
            origin,
            1.0F,
            static_cast<std::uint8_t>(mix(impact.cell.x ^ (impact.cell.y << 9U) ^
                                          (impact.cell.z << 18U))),
            impact.source_tool,
        });
    }
    if (sound_policy != TerrainImpactSoundPolicy::silent && impact.destroyed &&
        impact.kind == TerrainImpactKind::bullet) {
        sound_events_.push_back({TerrainSoundKind::block_break,
                                 origin,
                                 1.0F,
                                 static_cast<std::uint8_t>(
                                     mix(impact.cell.x ^ (impact.cell.y << 11U) ^
                                         (impact.cell.z << 21U)) % 4U)});
    }
    // The sprite bursts carry the fidelity; the cube instances below remain
    // the recovered, slot-bounded fallback and keep their exact retail counts.
    if (particles_ != nullptr) {
        if (impact.kind == TerrainImpactKind::corpse_explosion) {
            emit_corpse_explosion(*particles_, impact);
        } else if (impact.kind == TerrainImpactKind::grave_explosion) {
            emit_grave_explosion(*particles_, impact,
                                 grave_model_ ? &*grave_model_ : nullptr);
        } else if (impact.kind == TerrainImpactKind::block_cannon) {
            // Retail GameScene.create_snowke_ring: the SMOKE_RING layout in
            // the SnowkeTrail atlas, instead of the RPG effect compositor.
            emit_snowke_ring(*particles_, origin, impact.color,
                             mix(impact.cell.x ^ (impact.cell.y << 9U) ^ (impact.cell.z << 18U)));
        } else if (explosive) {
            emit_explosion(*particles_, impact);
        } else {
            emit_block_break(*particles_, impact);
        }
    }
    if (explosive) {
        ActiveLight light;
        light.presented.position = origin;
        const float authored_radius = std::clamp(impact.radius, 1.0F, 8.0F);
        light.peak_radius = 5.0F + authored_radius * 2.4F;
        light.peak_intensity = 3.8F + authored_radius * 0.48F;
        if (impact.kind == TerrainImpactKind::chemical) {
            light.hot_color = {0.78F, 1.0F, 0.50F};
            light.cool_color = {0.10F, 0.82F, 0.16F};
            light.lifetime = 1.10F;
            light.peak_intensity *= 0.72F;
        } else if (impact.kind == TerrainImpactKind::fire) {
            light.hot_color = {1.0F, 0.88F, 0.52F};
            light.cool_color = {1.0F, 0.12F, 0.01F};
            light.lifetime = 1.35F;
        }
        light.presented.radius = light.peak_radius * 0.15F;
        light.presented.intensity = light.peak_intensity;
        active_lights_.push_back(light);

        // The RPG compositor sends several bright trails away from the impact.
        // One stationary light illuminated only the centre while those visible
        // hot particles cast nothing. Three lower-energy lights travel with the
        // burst; the renderer still selects only the active quality tier's
        // bounded light budget.
        if (impact.kind == TerrainImpactKind::explosion) {
            const auto seed = mix(impact.cell.x ^ (impact.cell.y << 9U) ^
                                  (impact.cell.z << 18U) ^
                                  (static_cast<std::uint32_t>(impact.source_tool) << 24U));
            for (std::uint32_t index{}; index < 3U; ++index) {
                ActiveLight trail = light;
                const auto trail_seed = mix(seed ^ ((index + 1U) * 0x9E3779B9U));
                trail.peak_radius *= 0.43F;
                trail.peak_intensity *= 0.38F;
                trail.lifetime *= 0.50F;
                trail.presented.radius = trail.peak_radius * 0.18F;
                trail.presented.intensity = trail.peak_intensity;
                trail.velocity = {
                    random_signed(trail_seed) * 8.0F,
                    random_signed(trail_seed + 1U) * 8.0F,
                    random_signed(trail_seed + 2U) * 4.0F - 2.5F};
                active_lights_.push_back(trail);
            }
        }
        if (active_lights_.size() > maximum_dynamic_lights) {
            active_lights_.erase(
                active_lights_.begin(),
                active_lights_.begin() +
                    static_cast<std::ptrdiff_t>(active_lights_.size() -
                                                maximum_dynamic_lights));
        }
    }
    // ExplodeOnImpactEntity creates four glowing cubes plus ten ordinary
    // map-coloured particles. Keeping these as depth-tested cubes avoids the
    // old skybox-blended billboard artifacts while preserving the burst.
    // The depth-tested cube path is a renderer fallback only. Running it
    // alongside ParticleSystem duplicated every explosion (10 debris + four
    // glows twice) and was the main cause of the noisy, oversized blasts.
    const std::uint32_t particle_count = particles_ != nullptr ? 0U
        : impact.kind == TerrainImpactKind::block_cannon ? 0U
        : impact.kind == TerrainImpactKind::corpse_explosion ? 40U
        : impact.kind == TerrainImpactKind::grave_explosion ? 28U
        : explosive ? 10U
                    : 4U;
    const float radius = std::clamp(impact.radius, 1.0F, 8.0F);
    for (std::uint32_t index{}; index < particle_count; ++index) {
        const auto seed = mix(impact.cell.x ^ (impact.cell.y << 9U) ^
                              (impact.cell.z << 18U) ^ (index * 0x9E3779B9U));
        ActiveEffect chip;
        chip.presented.id = next_id_++;
        chip.presented.kind = TerrainEffectKind::impact_chip;
        const VxlColor particle_color =
            impact.kind == TerrainImpactKind::chemical
                ? VxlColor{static_cast<std::uint8_t>(65U + index * 3U),
                           210U, 72U, 255U}
            : impact.kind == TerrainImpactKind::fire && index % 3U != 0U
                ? VxlColor{238U, static_cast<std::uint8_t>(80U + index * 5U),
                           24U, 255U}
                : impact.color;
        chip.presented.mesh = chip_mesh(particle_color,
                                        explosive ? 0.20F : 0.125F);
        chip.presented.position = {origin[0U] + random_signed(seed) * 0.12F,
                                   origin[1U] + random_signed(seed + 1U) * 0.12F,
                                   origin[2U] + random_signed(seed + 2U) * 0.12F};
        const float scatter = explosive ? 0.07F + radius * 0.018F : 0.025F;
        const float normal_push = explosive ? 0.03F : 0.04F;
        chip.velocity = {
            static_cast<float>(impact.normal[0U]) * normal_push +
                random_signed(seed + 3U) * scatter,
            static_cast<float>(impact.normal[1U]) * normal_push +
                random_signed(seed + 4U) * scatter,
            static_cast<float>(impact.normal[2U]) * normal_push +
                random_signed(seed + 5U) * scatter - (explosive ? 0.12F : 0.0F),
        };
        chip.angular_velocity = {4.0F + random_signed(seed + 6U) * 2.0F,
                                 4.0F + random_signed(seed + 7U) * 2.0F,
                                 4.0F + random_signed(seed + 8U) * 2.0F};
        chip.lifetime = explosive ? 1.0F : impact.destroyed ? 0.8F : 0.45F;
        active_.push_back(std::move(chip));
    }
    if (particles_ == nullptr && explosive) {
        for (std::uint32_t index{}; index < 4U; ++index) {
            const auto seed = mix(impact.cell.x ^ (impact.cell.y << 9U) ^
                                  (impact.cell.z << 18U) ^
                                  ((index + 19U) * 0x9E3779B9U));
            ActiveEffect glow;
            glow.presented.id = next_id_++;
            glow.presented.kind = TerrainEffectKind::explosion_glow;
            const VxlColor color = impact.kind == TerrainImpactKind::chemical
                                       ? VxlColor{120U, 255U, 82U, 255U}
                                       : VxlColor{255U, 216U, 92U, 255U};
            glow.presented.mesh = chip_mesh(color, 0.34F + 0.03F * radius);
            glow.presented.position = origin;
            glow.velocity = {random_signed(seed) * 0.12F,
                             random_signed(seed + 1U) * 0.12F,
                             random_signed(seed + 2U) * 0.12F - 0.06F};
            glow.angular_velocity = {7.0F, 5.0F, 9.0F};
            glow.lifetime = 0.32F;
            active_.push_back(std::move(glow));
        }
    }
    const std::uint32_t aftermath_count =
        impact.kind == TerrainImpactKind::fire ? 5U
        : impact.kind == TerrainImpactKind::chemical ? 8U
                                                     : 0U;
    for (std::uint32_t index{}; index < aftermath_count; ++index) {
        const auto seed = mix(impact.cell.x ^ (impact.cell.y << 9U) ^
                              (impact.cell.z << 18U) ^
                              ((index + 47U) * 0x9E3779B9U));
        ActiveEffect aftermath;
        aftermath.presented.id = next_id_++;
        aftermath.presented.kind = impact.kind == TerrainImpactKind::fire
                                       ? TerrainEffectKind::fire_flame
                                       : TerrainEffectKind::chemical_cloud;
        const VxlColor color = impact.kind == TerrainImpactKind::fire
                                   ? VxlColor{255U,
                                              static_cast<std::uint8_t>(72U + index * 20U),
                                              12U, 255U}
                                   : VxlColor{72U, 196U, 80U, 255U};
        aftermath.presented.mesh = chip_mesh(
            color, impact.kind == TerrainImpactKind::fire ? 0.28F : 0.38F);
        aftermath.presented.position = {
            origin[0U] + random_signed(seed) * 0.8F,
            origin[1U] + random_signed(seed + 1U) * 0.8F,
            origin[2U] - 0.15F};
        aftermath.velocity = {random_signed(seed + 2U) * 0.012F,
                              random_signed(seed + 3U) * 0.012F,
                              -0.018F - std::abs(random_signed(seed + 4U)) * 0.015F};
        aftermath.angular_velocity = {1.0F, 1.5F, 2.0F};
        aftermath.lifetime = impact.kind == TerrainImpactKind::fire ? 4.0F : 2.0F;
        active_.push_back(std::move(aftermath));
    }
    enforce_limit();
}

bool weapon_flash_casts_light(std::uint8_t tool_id) noexcept {
    // Every gun with a propellant flash (a first- or third-person retail
    // muzzleflash_default draw) plus the launchers. The old hand list keyed
    // SMG/shotgun lights to the wrong ids (20, 48), so most guns cast none.
    return retail_view_muzzle_flash(tool_id).has_value() ||
           retail_third_person_muzzle_attachment(tool_id).has_value() ||
           tool_id == 12U || tool_id == 13U || tool_id == 46U || tool_id == 55U;
}

bool muzzle_light_enabled(bool retail_look, std::uint8_t tool_id) noexcept {
    // Retail has no shot light: character.pyd only asks light_manager for a
    // dynamic light in Grenade.initialize.
    return !retail_look && weapon_flash_casts_light(tool_id);
}

void TerrainEffectSimulation::spawn_weapon_flash(
    std::array<float, 3U> position,
    std::uint8_t tool_id) {
    if (!weapon_flash_casts_light(tool_id)) {
        return;
    }
    for (const float value : position) {
        if (!std::isfinite(value)) {
            return;
        }
    }

    ActiveLight flash;
    flash.presented.position = position;
    const bool heavy = tool_id == 12U || tool_id == 13U || tool_id == 46U || tool_id == 55U;
    const bool shotgun = tool_id == 9U || tool_id == 10U || tool_id == 37U || tool_id == 62U;
    // A warm propellant flash that reaches a few blocks: enough to light the
    // hands, the gun and the wall in front of it for a frame or four, then
    // cool and die within ~70 ms so it never reads as a lamp.
    flash.hot_color = heavy ? std::array<float, 3U>{1.0F, 0.76F, 0.32F}
                            : std::array<float, 3U>{1.0F, 0.80F, 0.50F};
    flash.cool_color = {1.0F, 0.42F, 0.12F};
    flash.peak_radius = heavy ? 4.2F : shotgun ? 3.8F : 3.2F;
    flash.peak_intensity = heavy ? 1.3F : shotgun ? 1.1F : 0.9F;
    flash.presented.radius = flash.peak_radius;
    flash.presented.intensity = flash.peak_intensity;
    flash.lifetime = heavy ? 0.080F : 0.065F;
    active_lights_.push_back(flash);
    if (active_lights_.size() > maximum_dynamic_lights) {
        active_lights_.erase(active_lights_.begin());
    }
}

void TerrainEffectSimulation::tick(double dt, const VxlMap& map) {
    const float seconds = static_cast<float>(std::clamp(dt, 0.0, 0.1));
    const float retail_steps = seconds * 60.0F;

    public_lights_.clear();
    for (auto& light : active_lights_) {
        light.age += seconds;
        for (std::size_t axis{}; axis < 3U; ++axis) {
            light.presented.position[axis] += light.velocity[axis] * seconds;
        }
        const float life01 =
            std::clamp(light.age / std::max(light.lifetime, 0.001F), 0.0F, 1.0F);
        // The first 70 ms are a white-hot expansion. Afterwards the radius
        // holds briefly while energy falls quadratically and the colour cools
        // toward orange/red. This makes the world react in the same frame as
        // the flash without leaving a fake lamp behind after the smoke.
        constexpr float expansion_fraction{0.11F};
        const float expanded = std::clamp(life01 / expansion_fraction, 0.0F, 1.0F);
        const float decay01 = std::clamp(
            (life01 - expansion_fraction) / (1.0F - expansion_fraction), 0.0F,
            1.0F);
        light.presented.radius =
            light.peak_radius * (0.18F + 0.82F * expanded);
        const float energy = 1.0F - decay01;
        light.presented.intensity =
            light.peak_intensity * energy * energy *
            (1.0F + (1.0F - expanded) * 0.65F);
        for (std::size_t channel{}; channel < 3U; ++channel) {
            light.presented.color[channel] =
                light.hot_color[channel] +
                (light.cool_color[channel] - light.hot_color[channel]) *
                    decay01;
        }
        if (light.age < light.lifetime &&
            light.presented.intensity > 0.005F) {
            public_lights_.push_back(light.presented);
        }
    }
    std::erase_if(active_lights_, [](const ActiveLight& light) {
        return light.age >= light.lifetime;
    });

    std::vector<std::size_t> breaking;
    for (std::size_t index{}; index < active_.size(); ++index) {
        auto& effect = active_[index];
        effect.age += seconds;
        if (effect.presented.kind == TerrainEffectKind::falling_structure) {
            // gameScene FallingBlocks.update (0x100C8720): world_object.update
            // runs world.pyd sub_10007DC0 (gravity, pos += v * dt * 32, a
            // solid-grid probe at the bbox-centre origin, bounce). No hit:
            // rotate_x/y/z += axis * dt * 50. First hit: break up.
            if (retail_falling_blocks_step(map, effect.presented.position, effect.velocity,
                                           seconds, gravity_) ||
                effect.age > falling_structure_safety_seconds) {
                breaking.push_back(index);
                continue;
            }
            for (std::size_t axis{}; axis < 3U; ++axis) {
                effect.presented.rotation_degrees[axis] +=
                    effect.angular_velocity[axis] * seconds * 50.0F;
            }
            continue;
        }
        if (effect.presented.kind != TerrainEffectKind::fire_flame &&
            effect.presented.kind != TerrainEffectKind::chemical_cloud) {
            effect.velocity[2U] += seconds; // recovered world gravity is 1.0
        }
        // Cosmetic chips, glows, flames and clouds keep their hand-authored
        // 60-scale velocities (a falling structure uses the retail * 32 step).
        const float position_scale = seconds * 60.0F;
        for (std::size_t axis{}; axis < 3U; ++axis) {
            effect.presented.position[axis] +=
                effect.velocity[axis] * position_scale;
            // Cosmetic tumble is authored in degrees per retail step.
            effect.presented.rotation_degrees[axis] +=
                effect.angular_velocity[axis] * retail_steps;
        }
    }

    for (auto iterator = breaking.rbegin(); iterator != breaking.rend(); ++iterator) {
        ActiveEffect source = std::move(active_[*iterator]);
        active_.erase(active_.begin() + static_cast<std::ptrdiff_t>(*iterator));
        const auto size = source.source.size();
        // The impact bank plays at world_object.position; the water tier is
        // chosen from its z (>= MAP_Z - 2) by the audio consumer.
        sound_events_.push_back({TerrainSoundKind::structure_break,
                                 source.presented.position,
                                 0.75F,
                                 static_cast<std::uint8_t>(
                                     mix(static_cast<std::uint32_t>(
                                             source.presented.id)) %
                                     4U),
                                 0U,
                                 static_cast<std::uint16_t>(std::min<std::size_t>(
                                     size, std::numeric_limits<std::uint16_t>::max()))});
        // Every int(5 + size / 8000 * 10)-th voxel breaks into five particles
        // carrying the bounced body velocity; the body is then deleted.
        if (particles_ != nullptr) {
            emit_falling_blocks_breakup(
                *particles_, source.source, source.presented.position,
                source.source_pivot, source.presented.rotation_degrees,
                source.velocity, static_cast<std::uint32_t>(source.presented.id));
        } else {
            // Headless/test and renderer-loss fallback. The live renderer uses
            // the one-particle-per-block burst below rather than duplicating it
            // with these slot-bounded cubes.
            make_debris(source);
        }
    }
    enforce_limit();
    public_instances_.clear();
    public_instances_.reserve(active_.size());
    for (const auto& effect : active_) {
        if (effect.age < effect.lifetime) {
            public_instances_.push_back(effect.presented);
        }
    }
    std::erase_if(active_, [](const ActiveEffect& effect) {
        return effect.presented.kind != TerrainEffectKind::falling_structure &&
               effect.age >= effect.lifetime;
    });
}

void TerrainEffectSimulation::make_debris(const ActiveEffect& source) {
    if (source.source.empty()) {
        return;
    }
    // Same retail sampling as the particle breakup (every mod-th voxel), kept
    // within the instance slot budget for the headless fallback.
    const std::size_t stride = falling_blocks_particle_mod(source.source.size());
    const std::size_t available = maximum_instances > active_.size()
                                      ? maximum_instances - active_.size()
                                      : 0U;
    const std::size_t count = std::min(
        available, std::max<std::size_t>(1U, (source.source.size() + stride - 1U) / stride));
    for (std::size_t index{}; index < count; ++index) {
        const auto& voxel = source.source[std::min(index * stride, source.source.size() - 1U)];
        const auto seed = mix(voxel.cell.x ^ (voxel.cell.y << 9U) ^
                              (voxel.cell.z << 18U) ^ static_cast<std::uint32_t>(index));
        ActiveEffect debris;
        debris.presented.id = next_id_++;
        debris.presented.kind = TerrainEffectKind::block_debris;
        debris.presented.mesh = chip_mesh(voxel.color, 0.35F);
        debris.presented.position = transformed_voxel_center(
            voxel, source.source_pivot, source.presented);
        // Preserve some of the structure's sideways fall in the rubble, then
        // add a larger impact kick. The old burst forgot x/y motion entirely
        // and appeared at the source coordinates underneath the tilted mesh.
        debris.velocity = {
            source.velocity[0U] * 0.45F + random_signed(seed) * 0.075F,
            source.velocity[1U] * 0.45F + random_signed(seed + 1U) * 0.075F,
            -0.11F - std::abs(random_signed(seed + 2U)) * 0.09F};
        debris.angular_velocity = {random_signed(seed + 3U) * 6.0F,
                                   random_signed(seed + 4U) * 6.0F,
                                   random_signed(seed + 5U) * 6.0F};
        debris.lifetime = 0.75F;
        active_.push_back(std::move(debris));
    }
}

void TerrainEffectSimulation::enforce_limit() {
    while (active_.size() > maximum_instances) {
        const auto cosmetic = std::find_if(active_.begin(), active_.end(), [](const auto& effect) {
            return effect.presented.kind != TerrainEffectKind::falling_structure;
        });
        active_.erase(cosmetic == active_.end() ? active_.begin() : cosmetic);
    }
}

void TerrainEffectSimulation::clear() noexcept {
    active_.clear();
    public_instances_.clear();
    active_lights_.clear();
    public_lights_.clear();
    sound_events_.clear();
}

std::span<const TerrainEffectInstance> TerrainEffectSimulation::instances() const noexcept {
    return public_instances_;
}

std::span<const DynamicLight> TerrainEffectSimulation::lights() const noexcept {
    return public_lights_;
}

std::vector<TerrainSoundEvent> TerrainEffectSimulation::take_sound_events() {
    return std::exchange(sound_events_, {});
}

} // namespace battlespades::world
