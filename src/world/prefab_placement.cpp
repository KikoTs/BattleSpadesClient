#include "battlespades/world/prefab_placement.hpp"

#include "battlespades/shared/retail_constants.hpp"
#include "battlespades/world/particle_system.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace battlespades::world {

BlockLinePlacementEvaluation evaluate_block_line_placement(
    const VxlMap& map, std::span<const VoxelCell> line) noexcept {
    BlockLinePlacementEvaluation result;
    // The server accepts at most 64 cells, including already occupied ones.
    result.all_in_bounds = line.size() <= 64U;
    constexpr std::array<std::array<std::int32_t, 3U>, 6U> neighbours{{
        {-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
        {0, 1, 0}, {0, 0, -1}, {0, 0, 1},
    }};
    for (std::size_t index{}; index < line.size(); ++index) {
        const auto& cell = line[index];
        if (cell.x >= VxlMap::width || cell.y >= VxlMap::depth ||
            cell.z == 0U || cell.z > VxlMap::height - 2U) {
            result.all_in_bounds = false;
            continue;
        }
        // BlockToolCommon.get_block_line(False, False) omits existing solids.
        if (map.solid(cell.x, cell.y, cell.z)) continue;
        ++result.required_blocks;
        bool supported{};
        for (const auto& offset : neighbours) {
            const auto x = static_cast<std::int32_t>(cell.x) + offset[0U];
            const auto y = static_cast<std::int32_t>(cell.y) + offset[1U];
            const auto z = static_cast<std::int32_t>(cell.z) + offset[2U];
            if (x < 0 || y < 0 || z < 0 || x >= static_cast<std::int32_t>(VxlMap::width) ||
                y >= static_cast<std::int32_t>(VxlMap::depth) ||
                z >= static_cast<std::int32_t>(VxlMap::height)) continue;
            const VoxelCell neighbour{static_cast<std::uint32_t>(x),
                                      static_cast<std::uint32_t>(y),
                                      static_cast<std::uint32_t>(z)};
            const auto earlier = line.first(index);
            if (map.solid(neighbour.x, neighbour.y, neighbour.z) ||
                std::ranges::find(earlier, neighbour) != earlier.end()) {
                supported = true;
                break;
            }
        }
        result.all_supported = result.all_supported && supported;
    }
    return result;
}

PrefabPlacementCell rotate_prefab_cell(
    PrefabPlacementCell cell, std::uint8_t yaw, std::uint8_t pitch, std::uint8_t roll) noexcept {
    for (std::uint8_t step{}; step < (roll & 3U); ++step) cell = {-cell.z, cell.y, cell.x};
    for (std::uint8_t step{}; step < (pitch & 3U); ++step) cell = {cell.x, cell.z, -cell.y};
    for (std::uint8_t step{}; step < (yaw & 3U); ++step) cell = {cell.y, -cell.x, cell.z};
    return cell;
}

std::array<float, 16U> prefab_preview_transform(
    const std::array<float, 3U>& pivot, PrefabPlacementCell anchor,
    std::uint8_t yaw, std::uint8_t pitch, std::uint8_t roll) noexcept {
    // KV6 writes (x-px, -(z-pz), y-py), with each cube centred on its voxel.
    // Build the signed-permutation matrix directly, avoiding Euler-order and
    // pivot errors between the visible ghost and the server's voxel expansion.
    const auto x = rotate_prefab_cell({1, 0, 0}, yaw, pitch, roll);
    const auto y = rotate_prefab_cell({0, 1, 0}, yaw, pitch, roll);
    const auto z = rotate_prefab_cell({0, 0, 1}, yaw, pitch, roll);
    // Cell coordinates and axis components are small integers, exact in float.
    const auto translate = [&pivot](std::int32_t origin, int ax, int ay, int az) noexcept {
        return static_cast<float>(origin) + 0.5F + pivot[0U] * static_cast<float>(ax) +
               pivot[1U] * static_cast<float>(ay) + pivot[2U] * static_cast<float>(az);
    };
    return {static_cast<float>(x.x), static_cast<float>(x.y), static_cast<float>(x.z), 0.0F,
            static_cast<float>(-z.x), static_cast<float>(-z.y), static_cast<float>(-z.z), 0.0F,
            static_cast<float>(y.x), static_cast<float>(y.y), static_cast<float>(y.z), 0.0F,
            translate(anchor.x, x.x, y.x, z.x),
            translate(anchor.y, x.y, y.y, z.y),
            translate(anchor.z, x.z, y.z, z.z), 1.0F};
}

namespace {

/** PrefabManager.intersects_with_world: only prefab_yaw is forwarded. */
[[nodiscard]] bool prefab_intersects_world(const VxlMap& map,
                                           std::span<const PrefabPlacementCell> authored,
                                           PrefabPlacementCell anchor,
                                           std::uint8_t yaw) noexcept {
    for (const auto cell : authored) {
        const auto rotated = rotate_prefab_cell(cell, yaw, std::uint8_t{0}, std::uint8_t{0});
        const auto x = anchor.x + rotated.x;
        const auto y = anchor.y + rotated.y;
        const auto z = anchor.z + rotated.z;
        if (x < 0 || y < 0 || z < 0) continue;
        if (map.solid(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                      static_cast<std::uint32_t>(z))) {
            return true;
        }
    }
    return false;
}

} // namespace

PrefabGhostPosition prefab_ghost_position(const VxlMap& map,
                                          std::span<const PrefabPlacementCell> authored,
                                          const PrefabGhostRequest& request) noexcept {
    // shared/prefabManager.py get_prefab_ghost_position, line for line. The
    // sizes are the KV6 header dimensions rotated as SIGNED values, so every
    // `int(world_size / 2.0)` truncates toward zero on its own sign.
    const auto& size = request.size;
    const auto world_size = rotate_prefab_cell(
        {size[0U], size[1U], size[2U]}, request.yaw, request.pitch, request.roll);
    const double radius = std::sqrt(
        (static_cast<double>(world_size.x) * world_size.x +
         static_cast<double>(world_size.y) * world_size.y +
         static_cast<double>(world_size.z) * world_size.z) /
        4.0);
    // PREFAB_DISTANCES: {radius key: constant, scaled, min, max}.
    struct DistanceBand final {
        double key;
        double constant;
        double scaled;
        double minimum;
        double maximum;
    };
    static constexpr std::array<DistanceBand, 6U> bands{{
        {0.0, 1.0, 1.20, 7.0, 12.0},
        {10.0, 1.0, 1.20, 7.0, 999999.0},
        {20.0, 1.0, 1.15, 7.0, 999999.0},
        {70.0, 1.0, 1.10, 7.0, 999999.0},
        {120.0, 1.0, 1.05, 7.0, 999999.0},
        {1000.0, 1.0, 1.00, 7.0, 999999.0},
    }};
    const DistanceBand* band = &bands.front();
    for (const auto& candidate : bands) {
        if (std::abs(candidate.key - radius) < std::abs(band->key - radius)) band = &candidate;
    }
    double distance = radius * band->scaled + band->constant;
    if (distance < band->minimum) distance = band->minimum;
    if (distance > band->maximum) distance = band->maximum;

    const double target_x = request.position[0U] + distance * request.orientation[0U];
    const double target_y = request.position[1U] + distance * request.orientation[1U];
    double target_z = request.position[2U] + distance * request.orientation[2U];
    constexpr double map_z_top = static_cast<double>(VxlMap::height) - 1.0;
    if (target_z >= map_z_top) target_z = map_z_top;
    PrefabPlacementCell scan{static_cast<std::int32_t>(std::floor(target_x)),
                             static_cast<std::int32_t>(std::floor(target_y)),
                             static_cast<std::int32_t>(std::floor(target_z))};
    scan.x -= world_size.x / 2;
    scan.y -= world_size.y / 2;
    scan.z -= world_size.z / 2;

    if (request.use_player_orientation) {
        // NORTH, EAST, SOUTH, WEST = 0..3; the offset is authored relative to
        // the player's facing and rotated into the world by that facing.
        const auto direction = static_cast<std::uint8_t>(request.player_direction & 3U);
        const auto relative = (static_cast<std::int32_t>(request.yaw & 3U) + 4 -
                               static_cast<std::int32_t>(direction)) % 4;
        PrefabPlacementCell offset{};
        if ((size[0U] & 1) == 0) {
            if (relative == 3) ++offset.y;
            if (relative == 2) --offset.x;
        }
        if ((size[1U] & 1) == 0) {
            if (relative == 0) ++offset.y;
            if (relative == 3) --offset.x;
        }
        const auto world_offset =
            rotate_prefab_cell(offset, direction, std::uint8_t{0}, std::uint8_t{0});
        scan.x += world_offset.x;
        scan.y += world_offset.y;
        scan.z += world_offset.z;
    }
    // PREFAB_INITIAL_VERTICAL_OFFSET * size_z / 2.0 on the UNROTATED size.
    scan.z += static_cast<std::int32_t>(-0.8 * static_cast<double>(size[2U]) / 2.0);

    if (request.check_world_intersect && !authored.empty()) {
        // KV6.get_bounds on the unrotated, pivot-reset model.
        std::int32_t z_lo = authored.front().z;
        std::int32_t z_hi = authored.front().z;
        for (const auto cell : authored) {
            z_lo = std::min(z_lo, cell.z);
            z_hi = std::max(z_hi, cell.z);
        }
        const double feet = request.crouching ? 1.35 : 2.25;
        const auto player_floor_z =
            static_cast<std::int32_t>(std::floor(request.position[2U] + feet));
        const bool looking_down = std::floor(request.orientation[2U]) >= 0.0;
        const auto map_z_last = static_cast<std::int32_t>(VxlMap::height) - 1;
        // move_point_towards_face(FACE_BOTTOM, 1.0) is z -= 1 (common.pyd).
        // The bound only guards a pathological column; retail has none.
        for (std::int32_t guard{}; guard < 1024; ++guard) {
            const bool may_lift = (looking_down && scan.z + z_hi > player_floor_z) ||
                                  scan.z - z_lo >= map_z_last;
            if (!may_lift || !prefab_intersects_world(map, authored, scan, request.yaw)) break;
            --scan.z;
        }
    }

    return {scan,
            {scan.x + world_size.x / 2, scan.y + world_size.y / 2,
             scan.z + world_size.z / 2}};
}

void PrefabPlacementPreview::reset(std::span<const PrefabPlacementCell> authored) {
    authored_.assign(authored.begin(), authored.end());
    rotated_.clear();
    rotation_.reset();
    evaluated_map_ = nullptr;
    evaluations_ = 0U;
}

const PrefabPlacementBounds& PrefabPlacementPreview::bounds(
    std::uint8_t yaw, std::uint8_t pitch, std::uint8_t roll) {
    const std::array rotation{static_cast<std::uint8_t>(yaw & 3U),
                              static_cast<std::uint8_t>(pitch & 3U),
                              static_cast<std::uint8_t>(roll & 3U)};
    if (rotation_ == rotation) return bounds_;
    rotation_ = rotation;
    evaluated_map_ = nullptr;
    rotated_.clear();
    rotated_.reserve(authored_.size());
    bounds_ = {};
    if (!authored_.empty()) {
        bounds_.minimum.fill(std::numeric_limits<std::int32_t>::max());
        bounds_.maximum.fill(std::numeric_limits<std::int32_t>::lowest());
    }
    for (const auto cell : authored_) {
        const auto rotated = rotate_prefab_cell(cell, yaw, pitch, roll);
        rotated_.push_back(rotated);
        const std::array values{rotated.x, rotated.y, rotated.z};
        for (std::size_t axis{}; axis < 3U; ++axis) {
            bounds_.minimum[axis] = std::min(bounds_.minimum[axis], values[axis]);
            bounds_.maximum[axis] = std::max(bounds_.maximum[axis], values[axis]);
        }
    }
    return bounds_;
}

PrefabPlacementEvaluation PrefabPlacementPreview::evaluate(
    const VxlMap& map, PrefabPlacementCell anchor,
    std::uint8_t yaw, std::uint8_t pitch, std::uint8_t roll) {
    static_cast<void>(bounds(yaw, pitch, roll));
    if (evaluated_map_ != &map || evaluated_revision_ != map.revision() ||
        evaluated_anchor_ != anchor) {
        evaluation_ = evaluate_prefab_placement(map, rotated_, anchor);
        evaluated_map_ = &map;
        evaluated_revision_ = map.revision();
        evaluated_anchor_ = anchor;
        ++evaluations_;
    }
    return evaluation_;
}

std::string_view placement_sound_stem(PlacementFeedbackKind kind) noexcept {
    switch (kind) {
    case PlacementFeedbackKind::block:
        return "build";
    case PlacementFeedbackKind::prefab:
        return "prefabbuild";
    case PlacementFeedbackKind::ugc_prefab:
        return "ugc_place";
    }
    return {};
}

PrefabPlacementEvaluation evaluate_prefab_placement(
    const VxlMap& map, std::span<const PrefabPlacementCell> footprint,
    PrefabPlacementCell anchor) noexcept {
    PrefabPlacementEvaluation result;
    result.model_blocks = footprint.size();
    static constexpr std::array<std::array<std::int32_t, 3U>, 6U> neighbours{{
        {-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
        {0, 1, 0},  {0, 0, -1}, {0, 0, 1},
    }};

    for (const auto& offset_cell : footprint) {
        const PrefabPlacementCell cell{offset_cell.x + anchor.x,
                                        offset_cell.y + anchor.y,
                                        offset_cell.z + anchor.z};
        // Z_ABOVE_WATERPLANE is 238 (inclusive); 239 is the immutable bed.
        // The local server also reserves z=0 so live builds cannot enter sky.
        if (cell.x < 0 || cell.y < 0 || cell.z <= 0 ||
            cell.x >= static_cast<std::int32_t>(VxlMap::width) ||
            cell.y >= static_cast<std::int32_t>(VxlMap::depth) ||
            cell.z > static_cast<std::int32_t>(VxlMap::height - 2U)) {
            result.all_in_bounds = false;
            continue;
        }

        const auto x = static_cast<std::uint32_t>(cell.x);
        const auto y = static_cast<std::uint32_t>(cell.y);
        const auto z = static_cast<std::uint32_t>(cell.z);
        if (map.solid(x, y, z)) {
            // Overlap is intentional retail behaviour: that authored voxel is
            // already satisfied and also attaches the remaining new cells.
            result.touches_world = true;
            continue;
        }
        ++result.required_blocks;

        // One world contact attaches the whole prefab; the remaining cells
        // still need bounds/overlap checks, but no more six-neighbour scans.
        if (result.touches_world) continue;

        for (const auto& offset : neighbours) {
            const auto nx = cell.x + offset[0U];
            const auto ny = cell.y + offset[1U];
            const auto nz = cell.z + offset[2U];
            if (nx >= 0 && ny >= 0 && nz >= 0 &&
                nx < static_cast<std::int32_t>(VxlMap::width) &&
                ny < static_cast<std::int32_t>(VxlMap::depth) &&
                nz < static_cast<std::int32_t>(VxlMap::height) &&
                map.solid(static_cast<std::uint32_t>(nx),
                          static_cast<std::uint32_t>(ny),
                          static_cast<std::uint32_t>(nz))) {
                result.touches_world = true;
                break;
            }
        }
    }
    return result;
}

bool PrefabPlacementTransaction::stage(
    std::span<const PrefabPlacementVoxel> voxels) {
    if (voxels.empty() || voxels.size() > maximum_voxels - staged_.size()) {
        return false;
    }
    for (const auto& voxel : voxels) {
        if (const auto found = staged_indices_.find(voxel.cell);
            found != staged_indices_.end()) {
            // Slices may overlap at a defensive replay boundary.  The newest
            // authoritative colour wins without charging another visual cell.
            staged_[found->second].color = voxel.color;
        } else {
            staged_indices_.emplace(voxel.cell, staged_.size());
            staged_.push_back(voxel);
        }
    }
    return true;
}

PrefabCommitResult PrefabPlacementTransaction::commit(VxlMap& map) noexcept {
    PrefabCommitResult result;
    if (staged_.empty()) {
        return result;
    }

    for (const auto& voxel : staged_) {
        if (voxel.cell.x < 0 || voxel.cell.y < 0 || voxel.cell.z < 0 ||
            voxel.cell.x >= static_cast<std::int32_t>(VxlMap::width) ||
            voxel.cell.y >= static_cast<std::int32_t>(VxlMap::depth) ||
            voxel.cell.z >= static_cast<std::int32_t>(VxlMap::height - 1U)) {
            clear();
            return result;
        }
    }

    for (const auto& voxel : staged_) {
        const auto x = static_cast<std::uint32_t>(voxel.cell.x);
        const auto y = static_cast<std::uint32_t>(voxel.cell.y);
        const auto z = static_cast<std::uint32_t>(voxel.cell.z);
        if (map.solid(x, y, z)) {
            // Existing world geometry satisfies the prefab footprint and is
            // not recoloured. Retail charges and builds only missing voxels.
            ++result.overlapped;
            continue;
        }
        if (map.set_voxel(x, y, z, voxel.color)) {
            ++result.placed;
        }
    }
    result.committed = true;
    clear();
    return result;
}

void PrefabPlacementTransaction::clear() noexcept {
    staged_.clear();
    staged_indices_.clear();
}

VxlColor retail_prefab_blend(VxlColor base, VxlColor voxel) noexcept {
    const auto channel = [](std::uint8_t a, std::uint8_t b) {
        const double value =
            static_cast<double>(b) + (static_cast<double>(a) - static_cast<double>(b)) * 0.5;
        // Python int() truncates toward zero; the result is never negative.
        return static_cast<std::uint8_t>(std::clamp(std::trunc(value), 0.0, 255.0));
    };
    return {channel(base.red, voxel.red), channel(base.green, voxel.green),
            channel(base.blue, voxel.blue), 255U};
}

std::vector<PrefabSmokeRing> prefab_smoke_rings(
    std::span<const PrefabModelVoxel> model, std::uint32_t model_size_x,
    PrefabPlacementCell anchor, std::uint8_t yaw, std::uint8_t pitch,
    std::uint8_t roll) {
    std::vector<PrefabSmokeRing> rings;
    if (model.empty()) return rings;
    std::int32_t max_z = model.front().z;
    for (const auto& voxel : model) max_z = std::max(max_z, voxel.z);
    const float radius = (static_cast<float>(model_size_x) - 1.0F) / 2.0F;
    for (const auto& voxel : model) {
        if (voxel.z != max_z) continue;
        const auto rotated = rotate_prefab_cell({voxel.x, voxel.y, voxel.z}, yaw, pitch, roll);
        rings.push_back({{static_cast<float>(rotated.x + anchor.x),
                          static_cast<float>(rotated.y + anchor.y),
                          static_cast<float>(rotated.z + anchor.z) + 1.0F},
                         radius});
    }
    return rings;
}

void emit_prefab_smoke_ring(ParticleSystem& particles, const VxlMap& map,
                            const PrefabSmokeRing& ring, std::uint32_t seed) {
    constexpr auto count = static_cast<std::uint32_t>(retail::SMOKE_RING_NOOF);
    std::uint32_t state = seed * 747'796'405U + 2'891'336'453U;
    const auto next_unit = [&state] {
        state = state * 1'664'525U + 1'013'904'223U;
        return static_cast<float>(state >> 8U) / 16'777'216.0F;
    };
    for (std::uint32_t index{}; index < count; ++index) {
        const auto angle = 2.0 * std::numbers::pi * static_cast<double>(index) /
                           static_cast<double>(count);
        const std::array<float, 3U> position{
            ring.position[0U] + static_cast<float>(std::cos(angle)) * ring.radius + 0.5F,
            ring.position[1U] + static_cast<float>(std::sin(angle)) * ring.radius + 0.5F,
            ring.position[2U]};
        if (!std::isfinite(position[0U]) || !std::isfinite(position[1U]) ||
            position[0U] < 0.0F || position[1U] < 0.0F || position[2U] < 0.0F) {
            continue;
        }
        const auto x = static_cast<std::uint32_t>(position[0U]);
        const auto y = static_cast<std::uint32_t>(position[1U]);
        const auto z = static_cast<std::uint32_t>(position[2U]);
        // Retail skips a ring particle whose colour lookup finds no voxel.
        const auto color = map.color(x, y, z);
        if (!color.has_value()) continue;
        ParticleSpawn puff;
        puff.position = position;
        puff.color = VxlColor{color->red, color->green, color->blue, 255U};
        puff.explode_velocity = static_cast<float>(retail::SMOKE_RING_VELOCITY) * 0.1F;
        const auto size_min = static_cast<float>(retail::SMOKE_RING_PARTICLE_SIZE_MIN);
        const auto size_max = static_cast<float>(retail::SMOKE_RING_PARTICLE_SIZE_MAX);
        // draw.pyd's common 0.1 particle-size multiplier.
        puff.size_begin = (size_min + (size_max - size_min) * next_unit()) * 0.1F;
        const auto decay = static_cast<float>(retail::SMOKE_RING_DECAY_RATE_MIN) +
                           (static_cast<float>(retail::SMOKE_RING_DECAY_RATE_MAX) -
                            static_cast<float>(retail::SMOKE_RING_DECAY_RATE_MIN)) *
                               next_unit();
        puff.size_end = puff.size_begin * (1.0F + decay);
        puff.alpha_begin = 0.8F;
        puff.alpha_end = 0.0F;
        puff.lifetime = static_cast<float>(retail::SMOKE_RING_LIFETIME);
        puff.gravity_scale = 0.0F;
        puff.atlas = ParticleAtlas::smoke_trail;
        puff.blend = ParticleBlend::premultiplied;
        puff.frames_x = 8U;
        puff.frames_y = 8U;
        puff.start_frame = 1U;
        puff.framerate = 30U;
        puff.loop = false;
        puff.collide = false;
        particles.emit_burst(puff, 1U, seed ^ (index * 0x9E3779B9U));
    }
}

} // namespace battlespades::world
