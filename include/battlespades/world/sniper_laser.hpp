#pragma once

#include "battlespades/world/vxl_map.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <optional>

namespace battlespades::world {

enum class SniperLaserColor : std::uint8_t {
    neutral,
    blue,
    green,
};

/** One living character body that can terminate the observer-side beam. */
struct SniperLaserTarget final {
    std::uint8_t player_id{};
    std::array<float, 3U> position{};
    bool crouching{};
    bool dead{};
};

/** Presentation-only body clipping; direction must be normalized. */
[[nodiscard]] std::optional<float> trace_player_body(
    std::array<float,3> origin, std::array<float,3> direction,
    const SniperLaserTarget& target, float maximum_distance) noexcept;

/** Replicated state required by retail LaserAttachment.update/draw. */
struct SniperLaserInput final {
    bool server_enabled{};
    std::uint8_t owner_id{};
    std::uint8_t tool_id{};
    std::uint8_t team{};
    bool zoomed{};
    std::array<float, 3U> position{};
    std::array<float, 3U> orientation{1.0F, 0.0F, 0.0F};
    std::array<float, 3U> observer_position{};
    float maximum_range{10'000.0F};
};

/** Fully clipped beam submitted to the renderer for one remote sniper. */
struct SniperLaserPose final {
    static constexpr float start_distance{2.5F};
    static constexpr float fade_in_distance{1.5F};
    static constexpr float fade_out_distance{3.0F};
    static constexpr float thickness{0.08F};

    bool visible{};
    SniperLaserColor color{SniperLaserColor::neutral};
    std::array<float, 3U> origin{};
    std::array<float, 3U> direction{1.0F, 0.0F, 0.0F};
    float distance{};
    float alpha{};
    bool player_hit{};
};

/**
 * Reconstruct retail's threat-visible sniper beam from replicated state.
 *
 * Terrain/player tests are presentation-only and never mutate authority.
 * Invalid state, non-sniper tools and disabled/idle zoom fail closed. The
 * observer fade is the recovered 0.45-to-12 block line-distance curve.
 */
[[nodiscard]] SniperLaserPose evaluate_sniper_laser(
    const VxlMap& map, const SniperLaserInput& input,
    std::span<const SniperLaserTarget> targets = {}) noexcept;

} // namespace battlespades::world
