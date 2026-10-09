#include "battlespades/world/blood_marks.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace battlespades::world {
namespace {
float random(std::uint32_t& seed) {
    seed = seed * 1664525U + 1013904223U;
    return static_cast<float>(seed >> 8U) / 16777216.F;
}
constexpr std::array<std::array<std::array<float, 3>, 4>, 6> faces{
    {{{{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}},
     {{{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}},
     {{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}},
     {{{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}},
     {{{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}}},
     {{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}}}};
void box(ChunkMesh& mesh,
         const std::array<float, 3>& minimum,
         const std::array<float, 3>& maximum,
         std::uint32_t color) {
    for (std::uint8_t face = 0; face < 6; ++face) {
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (const auto& corner : faces[face]) {
            std::array<float, 3> p{};
            for (std::size_t axis = 0; axis < 3; ++axis) {
                p[axis] = minimum[axis] + corner[axis] * (maximum[axis] - minimum[axis]);
                mesh.minimum[axis] = std::min(mesh.minimum[axis], p[axis]);
                mesh.maximum[axis] = std::max(mesh.maximum[axis], p[axis]);
            }
            // Alpha remains zero: it is the world shader's emissive channel.
            mesh.vertices.push_back({p[0], p[1], p[2], color, face, 0, 0, 0});
        }
        for (const auto index : {0U, 1U, 2U, 0U, 2U, 3U})
            mesh.indices.push_back(base + index);
    }
}
bool exposed(const VxlMap& map, VoxelCell cell, const std::array<std::int32_t, 3>& n) {
    return map.solid(cell.x, cell.y, cell.z) &&
           !map.solid(static_cast<std::uint32_t>(static_cast<int>(cell.x) + n[0]),
                      static_cast<std::uint32_t>(static_cast<int>(cell.y) + n[1]),
                      static_cast<std::uint32_t>(static_cast<int>(cell.z) + n[2]));
}
} // namespace
BloodMarks::BloodMarks() {
    drops_.reserve(maximum_drops);
    marks_.reserve(maximum_marks);
}
void BloodMarks::emit(std::array<float, 3> position, std::uint32_t seed) {
    if (!std::ranges::all_of(position, [](float v) { return std::isfinite(v); }))
        return;
    seed ^= ++emission_ * 0x9E3779B9U;
    for (int i = 0; i < 8; ++i) {
        Drop drop{
            position,
            {(random(seed) - .5F) * 8.F, (random(seed) - .5F) * 8.F, (random(seed) - .65F) * 6.F},
            0,
            .045F + random(seed) * .055F,
            seed};
        if (drops_.size() < maximum_drops)
            drops_.push_back(drop);
        else
            drops_[next_drop_++ % maximum_drops] = drop;
    }
    ++revision_;
}
void BloodMarks::splatter(const VoxelRayHit& hit, const VxlMap& map, std::uint32_t seed) {
    std::size_t axis = 0;
    while (axis < 3 && hit.normal[axis] == 0)
        ++axis;
    if (axis == 3)
        return;
    const auto u = (axis + 1) % 3, v = (axis + 2) % 3;
    // Broken pixel clusters, clipped independently to the exposed block face.
    for (int i = 0; i < 7; ++i) {
        auto center = hit.position;
        center[u] += (random(seed) - .5F) * .5F;
        center[v] += (random(seed) - .5F) * .5F;
        auto cell = std::array<std::int32_t, 3>{static_cast<int>(hit.cell.x),
                                                static_cast<int>(hit.cell.y),
                                                static_cast<int>(hit.cell.z)};
        cell[u] = static_cast<int>(std::floor(center[u]));
        cell[v] = static_cast<int>(std::floor(center[v]));
        const VoxelCell support{static_cast<std::uint32_t>(cell[0]),
                                static_cast<std::uint32_t>(cell[1]),
                                static_cast<std::uint32_t>(cell[2])};
        if (!exposed(map, support, hit.normal))
            continue;
        Mark mark{support,
                  hit.normal,
                  center,
                  center,
                  0,
                  20.F + random(seed) * 12.F,
                  static_cast<std::uint32_t>(80.F + random(seed) * 55.F) | 0x00030600U};
        for (const auto tangent : {u, v}) {
            const float half = .025F + random(seed) * .065F;
            mark.minimum[tangent] =
                std::max(center[tangent] - half, static_cast<float>(cell[tangent]) + .002F);
            mark.maximum[tangent] =
                std::min(center[tangent] + half, static_cast<float>(cell[tangent] + 1) - .002F);
        }
        const float surface = static_cast<float>(cell[axis]) + (hit.normal[axis] > 0 ? 1.F : 0.F);
        const float offset = surface + static_cast<float>(hit.normal[axis]) * .014F;
        mark.minimum[axis] = offset - .006F;
        mark.maximum[axis] = offset + .006F;
        if (marks_.size() < maximum_marks)
            marks_.push_back(mark);
        else
            marks_[next_mark_++ % maximum_marks] = mark;
    }
    ++revision_;
}
void BloodMarks::tick(double dt, const VxlMap& map, double gravity) {
    if (!std::isfinite(dt) || dt <= 0)
        return;
    const float seconds = static_cast<float>(std::min(dt, .1));
    gravity = std::isfinite(gravity) ? std::clamp(gravity, 0., 4.) : 1.;
    bool changed = !drops_.empty();
    for (auto& mark : marks_) {
        mark.age += seconds;
        changed |= mark.age > mark.lifetime - 5;
    }
    const auto old_count = marks_.size();
    std::erase_if(marks_, [&](const Mark& mark) {
        return mark.age >= mark.lifetime || !exposed(map, mark.cell, mark.normal);
    });
    changed |= old_count != marks_.size();
    for (auto& drop : drops_) {
        drop.age += seconds;
        float remaining = seconds;
        while (remaining > 1e-6F && drop.age < 2.5F) {
            const float step = std::min(remaining, 1.F / 120.F);
            remaining -= step;
            drop.velocity[2] += 32.F * static_cast<float>(gravity) * step;
            std::array<float, 3> delta{};
            float distance{};
            for (std::size_t a = 0; a < 3; ++a) {
                delta[a] = drop.velocity[a] * step;
                distance += delta[a] * delta[a];
            }
            distance = std::sqrt(distance);
            if (distance < 1e-7F)
                continue;
            auto direction = delta;
            for (auto& value : direction)
                value /= distance;
            if (const auto hit = trace_first_solid(map, drop.position, direction, distance)) {
                splatter(*hit, map, drop.seed);
                drop.age = 3;
                break;
            }
            for (std::size_t a = 0; a < 3; ++a)
                drop.position[a] += delta[a];
        }
    }
    std::erase_if(drops_, [](const Drop& d) { return d.age >= 2.5F; });
    if (changed)
        ++revision_;
}
void BloodMarks::clear() {
    if (drops_.empty() && marks_.empty())
        return;
    drops_.clear();
    marks_.clear();
    next_drop_ = next_mark_ = 0;
    ++revision_;
}
ChunkMesh BloodMarks::mesh() const {
    ChunkMesh result;
    result.minimum.fill(std::numeric_limits<float>::max());
    result.maximum.fill(std::numeric_limits<float>::lowest());
    result.vertices.reserve((drops_.size() + marks_.size()) * 24);
    result.indices.reserve((drops_.size() + marks_.size()) * 36);
    for (const auto& drop : drops_) {
        auto minimum = drop.position, maximum = drop.position;
        for (std::size_t a = 0; a < 3; ++a) {
            minimum[a] -= drop.size * .5F;
            maximum[a] += drop.size * .5F;
        }
        box(result, minimum, maximum, 0x00000475U);
    }
    for (const auto& mark : marks_) {
        auto minimum = mark.minimum, maximum = mark.maximum;
        const float coverage = std::clamp((mark.lifetime - mark.age) / 5.F, 0.F, 1.F);
        for (std::size_t a = 0; a < 3; ++a)
            if (mark.normal[a] == 0) {
                const float center = (minimum[a] + maximum[a]) * .5F,
                            half = (maximum[a] - minimum[a]) * .5F * coverage;
                minimum[a] = center - half;
                maximum[a] = center + half;
            }
        box(result, minimum, maximum, mark.color);
    }
    if (result.empty()) {
        result.minimum = {};
        result.maximum = {};
    }
    return result;
}
} // namespace battlespades::world
