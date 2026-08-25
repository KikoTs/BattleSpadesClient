#include "battlespades/world/sniper_laser.hpp"

#include "battlespades/world/voxel_raycast.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <ranges>

namespace battlespades::world {
namespace {

constexpr std::uint8_t sniper_tool_id{18U};
constexpr std::uint8_t sniper2_tool_id{19U};
constexpr float player_radius{0.45F};
constexpr float warning_fade_distance{12.0F};
// The world projection ends beyond the complete 512x512 map diagonal. A
// retail 10,000-block miss cannot contribute additional visible pixels.
constexpr float maximum_presented_distance{1024.0F};

[[nodiscard]] bool finite(std::array<float, 3U> value) noexcept {
    return std::ranges::all_of(value, [](float component) {
        return std::isfinite(component);
    });
}

[[nodiscard]] std::array<float, 3U> add_scaled(
    std::array<float, 3U> origin, std::array<float, 3U> direction,
    float distance) noexcept {
    return {origin[0U] + direction[0U] * distance,
            origin[1U] + direction[1U] * distance,
            origin[2U] + direction[2U] * distance};
}

[[nodiscard]] float segment_distance_squared(
    std::array<float, 3U> point, std::array<float, 3U> start,
    std::array<float, 3U> end) noexcept {
    std::array<float, 3U> segment{
        end[0U] - start[0U], end[1U] - start[1U], end[2U] - start[2U]};
    std::array<float, 3U> relative{
        point[0U] - start[0U], point[1U] - start[1U], point[2U] - start[2U]};
    const float length_squared = segment[0U] * segment[0U] +
                                 segment[1U] * segment[1U] +
                                 segment[2U] * segment[2U];
    const float projection =
        length_squared > 1.0e-8F
            ? std::clamp((relative[0U] * segment[0U] +
                          relative[1U] * segment[1U] +
                          relative[2U] * segment[2U]) /
                             length_squared,
                         0.0F, 1.0F)
            : 0.0F;
    const float dx = relative[0U] - segment[0U] * projection;
    const float dy = relative[1U] - segment[1U] * projection;
    const float dz = relative[2U] - segment[2U] * projection;
    return dx * dx + dy * dy + dz * dz;
}

/** Bounded slab intersection with retail's authoritative collision volume. */
[[nodiscard]] std::optional<float> player_intersection(
    std::array<float, 3U> origin, std::array<float, 3U> direction,
    const SniperLaserTarget& target, float maximum_distance) noexcept {
    const float contact_offset = target.crouching ? 1.35F : 2.25F;
    const std::array<float, 3U> minimum{
        target.position[0U] - player_radius,
        target.position[1U] - player_radius,
        target.position[2U] - player_radius};
    const std::array<float, 3U> maximum{
        target.position[0U] + player_radius,
        target.position[1U] + player_radius,
        target.position[2U] + contact_offset};
    float enter{};
    float leave{maximum_distance};
    for (std::size_t axis{}; axis < 3U; ++axis) {
        if (std::fabs(direction[axis]) <= 1.0e-7F) {
            if (origin[axis] < minimum[axis] || origin[axis] > maximum[axis]) {
                return std::nullopt;
            }
            continue;
        }
        float first = (minimum[axis] - origin[axis]) / direction[axis];
        float second = (maximum[axis] - origin[axis]) / direction[axis];
        if (first > second) std::swap(first, second);
        enter = std::max(enter, first);
        leave = std::min(leave, second);
        if (enter > leave) return std::nullopt;
    }
    return enter >= 0.0F && enter <= maximum_distance
               ? std::optional<float>{enter}
               : std::nullopt;
}

} // namespace

SniperLaserPose evaluate_sniper_laser(
    const VxlMap& map, const SniperLaserInput& input,
    std::span<const SniperLaserTarget> targets) noexcept {
    SniperLaserPose result;
    if (!input.server_enabled || !input.zoomed ||
        (input.tool_id != sniper_tool_id && input.tool_id != sniper2_tool_id) ||
        !finite(input.position) || !finite(input.orientation) ||
        !finite(input.observer_position) || !std::isfinite(input.maximum_range) ||
        input.maximum_range <= SniperLaserPose::start_distance) {
        return result;
    }

    const float magnitude = std::sqrt(
        input.orientation[0U] * input.orientation[0U] +
        input.orientation[1U] * input.orientation[1U] +
        input.orientation[2U] * input.orientation[2U]);
    if (!std::isfinite(magnitude) || magnitude <= 1.0e-6F) return result;
    result.direction = {input.orientation[0U] / magnitude,
                        input.orientation[1U] / magnitude,
                        input.orientation[2U] / magnitude};
    result.origin = input.position;
    result.color = input.team == 2U   ? SniperLaserColor::blue
                   : input.team == 3U ? SniperLaserColor::green
                                      : SniperLaserColor::neutral;

    result.distance = std::min(input.maximum_range, maximum_presented_distance);
    if (const auto terrain = trace_first_solid(
            map, result.origin, result.direction, result.distance);
        terrain.has_value()) {
        result.distance = terrain->distance;
    }
    for (const auto& target : targets) {
        if (target.player_id == input.owner_id || target.dead ||
            !finite(target.position)) {
            continue;
        }
        if (const auto hit = player_intersection(
                result.origin, result.direction, target, result.distance);
            hit.has_value() && *hit < result.distance) {
            result.distance = *hit;
            result.player_hit = true;
        }
    }
    if (result.distance <= SniperLaserPose::start_distance) return result;

    const auto end = add_scaled(result.origin, result.direction, result.distance);
    const float distance_squared = segment_distance_squared(
        input.observer_position, result.origin, end);
    constexpr float minimum_squared{player_radius * player_radius};
    constexpr float maximum_squared{warning_fade_distance * warning_fade_distance};
    const float clamped = std::clamp(distance_squared, minimum_squared, maximum_squared);
    result.alpha = 1.0F -
                   (clamped - minimum_squared) /
                       (maximum_squared - minimum_squared);
    result.visible = result.alpha > 0.0F;
    return result;
}

} // namespace battlespades::world
