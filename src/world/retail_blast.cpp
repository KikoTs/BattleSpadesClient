#include "battlespades/world/retail_blast.hpp"

#include "battlespades/world/voxel_raycast.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <array>
#include <cmath>

namespace battlespades::world {
namespace {

constexpr double body_offset_standing{0.75};
constexpr double body_offset_crouching{1.25};
constexpr std::array<double, 3U> sight_weights{0.5, 0.3, 0.2};
constexpr std::array<double, 3U> sight_offsets_standing{0.0, 0.9, 1.8};
constexpr std::array<double, 3U> sight_offsets_crouching{0.0, 0.45, 0.9};
constexpr double sight_ray_start_fraction{0.1};

[[nodiscard]] bool sight_ray_blocked(const VxlMap& map, Vec3 explosion, Vec3 point) noexcept {
    const Vec3 v{point.x - explosion.x, point.y - explosion.y, point.z - explosion.z};
    const double length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (length <= 1.0e-9) return false;
    const std::array<float, 3U> start{
        static_cast<float>(explosion.x + v.x * sight_ray_start_fraction),
        static_cast<float>(explosion.y + v.y * sight_ray_start_fraction),
        static_cast<float>(explosion.z + v.z * sight_ray_start_fraction)};
    const std::array<float, 3U> direction{static_cast<float>(v.x / length),
                                          static_cast<float>(v.y / length),
                                          static_cast<float>(v.z / length)};
    return trace_first_solid(map, start, direction, static_cast<float>(length)).has_value();
}

} // namespace

std::optional<RetailBlastSpec> retail_blast_for_damage_type(std::uint8_t damage_type) noexcept {
    switch (damage_type) {
    case 7U: return RetailBlastSpec{4.0, 0.5, 1.0};     // GRENADE
    case 8U: return RetailBlastSpec{6.0, 0.0, 0.25};    // ROCKET
    case 9U: return RetailBlastSpec{6.0, 0.0, 0.25};    // ROCKET2 (stock, no self override)
    case 10U: return RetailBlastSpec{3.0, 0.01, 0.1};   // DRILL
    case 11U: return RetailBlastSpec{3.5, 0.1, 0.2};    // DRILL_DESTROYED
    case 12U: return RetailBlastSpec{3.0, 0.2, 1.0};    // ROCKET_TURRET
    case 13U: return RetailBlastSpec{3.0, 0.05, 0.1};   // CORPSE
    case 15U: return RetailBlastSpec{6.0, 0.75, 0.75};  // LANDMINE
    case 16U: return RetailBlastSpec{8.0, 0.1, 0.15};   // DYNAMITE
    case 18U: return RetailBlastSpec{6.0, 1.0, 2.0};    // AIRSTRIKE
    case 19U: return RetailBlastSpec{7.0, 2.0, 3.0};    // BOMB
    case 20U: return RetailBlastSpec{5.0, 0.3, 0.3};    // SNOWBALL
    case 21U: return RetailBlastSpec{3.0, 0.1, 0.3};    // ROCKET_TURRET_ROCKET
    case 22U: return RetailBlastSpec{9.0, 0.1, 0.1};    // CLASSIC_GRENADE
    case 23U: return RetailBlastSpec{6.0, 0.25, 0.5};   // ANTIPERSONNEL_GRENADE
    case 24U: return RetailBlastSpec{4.0, 0.0, 0.1};    // MOLOTOV
    case 30U: return RetailBlastSpec{4.0, 0.0, 0.25};   // UGC_ROCKET2
    case 33U: return RetailBlastSpec{3.0, 0.01, 0.1};   // UGC_DRILL
    case 37U: return RetailBlastSpec{4.0, 0.0, 0.25};   // GRENADE_LAUNCHER
    case 38U: return RetailBlastSpec{3.0, 0.0, 0.1};    // radar station (inferred slot)
    case 39U: return RetailBlastSpec{5.0, 0.75, 0.1};   // STICKY_GRENADE (stock min/max order)
    case 40U: return RetailBlastSpec{6.0, 0.75, 0.75};  // MINE_LAUNCHER
    case 41U: return RetailBlastSpec{8.0, 0.1, 0.15};   // C4
    default: break;
    }
    return std::nullopt;
}

std::optional<Vec3> retail_blast_impulse(const VxlMap* map, Vec3 explosion, Vec3 eye,
                                         bool crouched, const RetailBlastSpec& spec) noexcept {
    if (spec.radius <= 0.0) return std::nullopt;
    const double body_offset = crouched ? body_offset_crouching : body_offset_standing;
    const double bx = eye.x - explosion.x;
    const double by = eye.y - explosion.y;
    const double bz = eye.z + body_offset - explosion.z;
    const double distance_sq = bx * bx + by * by + bz * bz;
    const double radius_sq = spec.radius * spec.radius;
    if (distance_sq >= radius_sq) return std::nullopt;
    const double falloff = (radius_sq - distance_sq) / radius_sq;

    double sight = 1.0;
    if (map != nullptr) {
        sight = 0.0;
        const auto& offsets = crouched ? sight_offsets_crouching : sight_offsets_standing;
        for (std::size_t index{}; index < sight_weights.size(); ++index) {
            const Vec3 point{eye.x, eye.y, eye.z + offsets[index]};
            if (!sight_ray_blocked(*map, explosion, point)) sight += sight_weights[index];
        }
    }
    if (sight <= 0.0) return std::nullopt;

    const double dx = eye.x - explosion.x;
    const double dy = eye.y - explosion.y;
    const double dz = eye.z - explosion.z;
    const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (length <= 1.0e-9) return std::nullopt;
    const double kmin = spec.knockback_min * sight;
    const double kmax = spec.knockback_max * sight;
    const double magnitude = kmin + falloff * (kmax - kmin);
    if (magnitude == 0.0) return std::nullopt;
    const double scale = magnitude / length;
    return Vec3{dx * scale, dy * scale, dz * scale};
}

} // namespace battlespades::world
