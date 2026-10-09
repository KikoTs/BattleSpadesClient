/* Hit boxes and spread adapted from OpenSpades/ZeroSpades Player.cpp.
 * Copyright (c) 2013 yvt. GPL-3.0-or-later. See docs/CLASSIC_PROTOCOL.md. */
#include "battlespades/world/classic_combat.hpp"
#include "battlespades/world/classic_weapons.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <random>

namespace battlespades::world {
namespace {
Vec3 sub(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
double dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
double chebyshev(Vec3 a) {
    return std::max({std::abs(a.x), std::abs(a.y), std::abs(a.z)});
}
Vec3 rotate_z(Vec3 v, double a) {
    return {v.x * std::cos(a) - v.y * std::sin(a), v.x * std::sin(a) + v.y * std::cos(a), v.z};
}
Vec3 rotate_x(Vec3 v, double a) {
    return {v.x, v.y * std::cos(a) - v.z * std::sin(a), v.y * std::sin(a) + v.z * std::cos(a)};
}
std::array<float, 3> floats(Vec3 v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}
double box(Vec3 start, Vec3 dir, Vec3 origin, double yaw, double pitch, Vec3 minimum, Vec3 size) {
    const auto p = rotate_x(rotate_z(sub(start, origin), -yaw), -pitch);
    const auto d = rotate_x(rotate_z(dir, -yaw), -pitch);
    const std::array<double, 3> pos{p.x, p.y, p.z}, direction{d.x, d.y, d.z},
        lo{minimum.x, minimum.y, minimum.z}, extent{size.x, size.y, size.z};
    double near = 0, far = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < 3; ++i) {
        if (std::abs(direction[i]) < 1e-10) {
            if (pos[i] < lo[i] || pos[i] > lo[i] + extent[i])
                return far = -1;
            continue;
        }
        double a = (lo[i] - pos[i]) / direction[i], b = (lo[i] + extent[i] - pos[i]) / direction[i];
        if (a > b)
            std::swap(a, b);
        near = std::max(near, a);
        far = std::min(far, b);
        if (near > far)
            return -1;
    }
    return near;
}
bool overlap(VoxelCell cell, Vec3 eye, bool crouch) {
    return eye.x + 0.45 > cell.x && eye.x - 0.45 < cell.x + 1.0 && eye.y + 0.45 > cell.y &&
           eye.y - 0.45 < cell.y + 1.0 && eye.z + (crouch ? 1.35 : 2.25) > cell.z &&
           eye.z - 0.45 < cell.z + 1.0;
}

std::array<std::uint64_t, 2> fresh_spread_seed() {
    // Observed shots can construct short-lived combat queries. Seed the source
    // once per thread, rather than asking the OS for entropy on every shot.
    static thread_local auto source = [] {
        std::random_device entropy;
        std::seed_seq seed{entropy(), entropy(), entropy(), entropy()};
        return std::mt19937_64{seed};
    }();
    return {source(), source()};
}
} // namespace

ClassicCombat::ClassicCombat() : ClassicCombat{fresh_spread_seed()} {}

ClassicCombat::ClassicCombat(std::array<std::uint64_t, 2> seed) noexcept : random_{seed} {
    if (random_.state[0] == 0) random_.state[0] = 0x9E3779B97F4A7C15ULL;
    if (random_.state[1] == 0) random_.state[1] = 0xBF58476D1CE4E5B9ULL;
}

ClassicCombat::SpreadRandom::result_type ClassicCombat::SpreadRandom::operator()() noexcept {
    auto x = state[0];
    const auto y = state[1];
    state[0] = y;
    x ^= x << 23U;
    state[1] = x ^ y ^ (x >> 17U) ^ (y >> 26U);
    return state[1] + y;
}

ClassicAttackResult ClassicCombat::attack(const VxlMap& map,
                                          const PlayerMovementState& player,
                                          std::span<const ClassicHitTarget> targets,
                                          const WeaponAction& action,
                                          std::uint8_t protocol,
                                          bool aiming,
                                          double seconds,
                                          std::optional<Vec3> view_eye) {
    ClassicAttackResult result;
    const bool melee = action.kind == WeaponActionKind::melee;
    const auto rules = classic_weapon_rules(protocol, action.tool_id);
    if (!melee && !rules)
        return result;
    result.damage = expire(seconds);
    const auto eye = view_eye.value_or(player.position);
    Vec3 start = eye;
    if (!melee) {
        start.x += player.orientation.x * 0.01;
        start.y += player.orientation.y * 0.01;
        start.z += player.orientation.z * 0.01;
    }
    auto pellet = floats(player.orientation);
    std::uniform_int_distribution<int> sample{0, 32767};
    for (int n = 0; n < (melee ? 1 : rules->pellets); ++n) {
        if (!melee) {
            float spread = static_cast<float>(rules->spread);
            if (aiming) spread *= 0.5F;
            if (player.crouch && action.tool_id != 37) spread *= 0.5F;
            for (auto& component : pellet) {
                const int first = sample(random_), second = sample(random_);
                component += static_cast<float>(first - second) / 16383.0F * spread;
            }
        }
        const float length = std::sqrt(pellet[0] * pellet[0] + pellet[1] * pellet[1] + pellet[2] * pellet[2]);
        if (length < 1e-9)
            break;
        const Vec3 ray{pellet[0] / length, pellet[1] / length, pellet[2] / length};
        auto terrain = trace_first_solid(map, floats(start), floats(ray), melee ? 32.0F : 256.0F);
        // CastRay2 traces farther than the playable fog radius. Both original
        // clients gate block damage by horizontal distance, not ray length.
        if (!melee && terrain &&
            std::hypot(terrain->position[0] - start.x, terrain->position[1] - start.y) > 128.0)
            terrain.reset();
        std::optional<ClassicHit> target;
        unsigned hit_parts{};
        double closest = std::numeric_limits<double>::infinity();
        for (const auto& t : targets) {
            const auto diff = sub(t.position, start);
            const double horizontal = diff.x * diff.x + diff.y * diff.y;
            const double projection = dot(diff, ray);
            if (horizontal > 128.0 * 128.0 || projection <= 0 ||
                dot(diff, diff) - projection * projection >= 9.0)
                continue;
            if (melee) {
                if (!action.secondary && dot(diff, diff) <= 9.0) {
                    target = ClassicHit{t.id, 4};
                    closest = std::sqrt(dot(diff, diff));
                    break;
                }
                continue;
            }
            const double yaw = std::atan2(t.orientation.y, t.orientation.x) + std::numbers::pi / 2;
            const double pitch =
                -std::atan2(t.orientation.z, std::hypot(t.orientation.x, t.orientation.y));
            double arm_pitch = pitch - (t.sprinting ? 0.9 : 0);
            if (arm_pitch < 0)
                arm_pitch = std::max(arm_pitch, -std::numbers::pi / 2) * 0.9;
            const Vec3 lower{t.position.x, t.position.y, t.position.z + (t.crouched ? 0.75 : 1.2)};
            const Vec3 torso{lower.x, lower.y, lower.z - (t.crouched ? 0.5 : 1.0)};
            const Vec3 head{torso.x, torso.y, torso.z - (t.crouched ? 0.05 : 0)};
            const Vec3 arms{torso.x, torso.y, torso.z + (t.crouched ? 0 : 0.1)};
            const auto consider = [&](double distance, std::uint8_t part) {
                if (distance >= 0 && (distance < closest ||
                                      ((part == 0 || part == 1) && target && target->part == 2))) {
                    if (!target || target->player != t.id) hit_parts = 0;
                    hit_parts |= 1U << part;
                    closest = distance;
                    target = ClassicHit{t.id, part};
                }
            };
            consider(box(start, ray, head, yaw, pitch, {-0.3, -0.3, -0.6}, {0.6, 0.6, 0.6}), 1);
            consider(box(start,
                         ray,
                         torso,
                         yaw,
                         0,
                         t.crouched ? Vec3{-0.4, -0.1, -0.1} : Vec3{-0.4, -0.2, 0},
                         t.crouched ? Vec3{0.8, 0.8, 0.7} : Vec3{0.8, 0.4, 0.9}),
                     0);
            for (int leg = 0; leg < 2; ++leg)
                consider(
                    box(start,
                        ray,
                        lower,
                        yaw,
                        0,
                        {leg ? 0.1 : -0.4, t.crouched ? -0.1 : -0.2, t.crouched ? -0.2 : -0.15},
                        {0.3, 0.4, t.crouched ? 0.8 : 1.2}),
                    3);
            if (!target || (target->part != 0 && target->part != 1))
                consider(box(start, ray, arms, yaw, arm_pitch, {-0.6, -1, -0.1}, {1.2, 1, 0.6}), 2);
        }
        if (!melee) {
            const auto distance = std::min(
                {128.0, closest, terrain ? static_cast<double>(terrain->distance) : 128.0});
            result.tracers.push_back({ray,
                                      {start.x + ray.x * distance,
                                       start.y + ray.y * distance,
                                       start.z + ray.z * distance}});
        }
        if (target && (!terrain || closest < terrain->distance)) {
            // ZeroSpades keeps the accepted body flags for this player. A ray
            // crossing a nearer leg after the torso must still report torso
            // damage; `closest` remains the actual first surface contact.
            if (!melee)
                target->part = (hit_parts & 1U) ? 0 : (hit_parts & 2U) ? 1 :
                               (hit_parts & 4U) ? 2 : 3;
            target->position = {start.x + ray.x * closest, start.y + ray.y * closest,
                                start.z + ray.z * closest};
            result.hits.push_back(*target);
            continue;
        }
        if (!terrain || terrain->cell.z >= 238)
            continue;
        if (melee) {
            Vec3 center{terrain->cell.x + 0.5, terrain->cell.y + 0.5, terrain->cell.z + 0.5};
            if (!action.secondary) {
                center.x += terrain->normal[0] * 0.6;
                center.y += terrain->normal[1] * 0.6;
                center.z += terrain->normal[2] * 0.6;
            }
            if (chebyshev(sub(center, start)) >= 3)
                continue;
            if (action.secondary) {
                result.impacts.push_back(
                    {TerrainImpactKind::melee,
                     terrain->cell,
                     map.color(terrain->cell.x, terrain->cell.y, terrain->cell.z)
                         .value_or(VxlColor{}),
                     terrain->normal,
                     false,
                     1,
                     action.tool_id});
                result.destroy = terrain->cell;
                result.block_action = 2;
                continue;
            }
        }
        auto& damage =
            damage_[terrain->cell.x | (terrain->cell.y << 9U) | (terrain->cell.z << 18U)];
        if (damage.expires == 0) {
            const auto prior = map.damaged_block(terrain->cell.x, terrain->cell.y, terrain->cell.z);
            damage.original = prior ? prior->original_color
                                    : map.color(terrain->cell.x, terrain->cell.y, terrain->cell.z)
                                          .value_or(VxlColor{});
        }
        damage.expires = seconds + 10.0;
        damage.remaining = std::max(0, damage.remaining - (melee ? 50 : rules->block_damage));
        result.damage.push_back({terrain->cell, damage.remaining, damage.original});
        result.impacts.push_back({melee ? TerrainImpactKind::melee : TerrainImpactKind::bullet,
                                  terrain->cell,
                                  damage.original,
                                  terrain->normal,
                                  false,
                                  1,
                                  action.tool_id});
        if (damage.remaining <= 0 && !result.destroy) {
            result.destroy = terrain->cell;
        }
    }
    return result;
}

std::vector<ClassicBlockDamage> ClassicCombat::expire(double seconds) {
    std::vector<ClassicBlockDamage> expired;
    const bool full = damage_.size() > 4096;
    std::erase_if(damage_, [&](const auto& entry) {
        if (!full && entry.second.expires > seconds)
            return false;
        const auto key = entry.first;
        expired.push_back(
            {{key & 511U, (key >> 9U) & 511U, key >> 18U}, 100, entry.second.original});
        return true;
    });
    return expired;
}

std::optional<VoxelCell> classic_build_target(const VxlMap& map,
                                              const PlayerMovementState& p,
                                              std::span<const ClassicHitTarget> targets,
                                              bool allow_local_overlap,
                                              std::optional<Vec3> view_eye) {
    const auto eye = view_eye.value_or(p.position);
    const auto hit = trace_first_solid(map, floats(eye), floats(p.orientation), 12);
    if (!hit)
        return {};
    const auto x = static_cast<int>(hit->cell.x) + hit->normal[0],
               y = static_cast<int>(hit->cell.y) + hit->normal[1],
               z = static_cast<int>(hit->cell.z) + hit->normal[2];
    if (x < 0 || x >= 512 || y < 0 || y >= 512 || z < 176 || z >= 238)
        return {};
    VoxelCell cell{static_cast<std::uint32_t>(x),
                   static_cast<std::uint32_t>(y),
                   static_cast<std::uint32_t>(z)};
    if (chebyshev(sub({x + 0.5, y + 0.5, z + 0.5}, eye)) >= 3 ||
        map.solid(cell.x, cell.y, cell.z) ||
        (!allow_local_overlap && overlap(cell, p.position, p.crouch)))
        return {};
    for (const auto& target : targets)
        if (overlap(cell, target.position, target.crouched))
            return {};
    return cell;
}

std::optional<VoxelCell> ClassicBlockPlacement::request(
    const VxlMap& map, const PlayerMovementState& player, std::span<const ClassicHitTarget> others,
    std::optional<Vec3> eye) {
    if (pending_) return {};
    const auto cell = classic_build_target(map, player, others, true, eye);
    if (!cell) return {};
    if (!overlap(*cell, player.position, player.crouch)) return cell;
    if (player.airborne) pending_ = cell;
    return {};
}

std::optional<VoxelCell> ClassicBlockPlacement::update(
    const VxlMap& map, const PlayerMovementState& player, std::span<const ClassicHitTarget> others,
    bool enabled, std::optional<Vec3> eye) {
    if (!enabled || !player.airborne) pending_.reset();
    if (!pending_) return {};
    const auto cell = *pending_;
    // Do not retarget when the player looks away, extend build range or place
    // inside another body. Revalidate support after any intervening map edit.
    const bool supported = map.solid(cell.x - 1U, cell.y, cell.z) ||
        map.solid(cell.x + 1U, cell.y, cell.z) || map.solid(cell.x, cell.y - 1U, cell.z) ||
        map.solid(cell.x, cell.y + 1U, cell.z) || map.solid(cell.x, cell.y, cell.z - 1U) ||
        map.solid(cell.x, cell.y, cell.z + 1U);
    if (!supported || map.solid(cell.x, cell.y, cell.z) ||
        chebyshev(sub({cell.x + 0.5, cell.y + 0.5, cell.z + 0.5}, eye.value_or(player.position))) >= 3) {
        pending_.reset();
        return {};
    }
    if (overlap(cell, player.position, player.crouch)) return {};
    for (const auto& other : others)
        if (overlap(cell, other.position, other.crouched)) return {};
    pending_.reset();
    return cell;
}

} // namespace battlespades::world
