#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string_view>

namespace battlespades::world {

inline constexpr std::uint8_t retail_parachute_equipment{72U};
inline constexpr std::string_view retail_parachute_model{"parachute"};
inline constexpr std::string_view retail_first_person_parachute_model{"parachute_firstperson"};

/**
 * Character.set_parachute_model @ 0x10014B20: DisplayList z_offset=0 and
 * size=0.12 for both original KV6 meshes. draw_fps_parachute @ 0x10062A20
 * translates to GL (x, -z + A1911, y), A1911=1.5, then rotates by yaw only.
 * In canonical +Z-down coordinates the first-person anchor is eye.z - 1.5.
 * Third person inherits the ordinary body transform with no extra offset.
 */
inline constexpr float retail_parachute_scale{0.12F};
inline constexpr float retail_parachute_pivot_offset{0.0F};
inline constexpr double retail_first_person_parachute_height{1.5};

/**
 * Raw KV6 mesh vertices use retail render axes (x,-z,y). Convert those to
 * canonical world axes, apply the DisplayList scale, then the character yaw
 * and world anchor. Pitch is deliberately absent: looking down does not tip
 * the canopy. Row-vector layout matches WorldModelDraw/bgfx transforms.
 */
[[nodiscard]] inline std::array<float, 16U> retail_parachute_world_transform(
    const std::array<double, 3U>& position, double yaw_degrees,
    bool first_person) noexcept {
    const auto yaw = yaw_degrees * std::numbers::pi / 180.0;
    const float c = static_cast<float>(std::cos(yaw)) * retail_parachute_scale;
    const float s = static_cast<float>(std::sin(yaw)) * retail_parachute_scale;
    return {c, s, 0.0F, 0.0F,
            0.0F, 0.0F, -retail_parachute_scale, 0.0F,
            -s, c, 0.0F, 0.0F,
            static_cast<float>(position[0U]), static_cast<float>(position[1U]),
            static_cast<float>(position[2U] -
                (first_person ? retail_first_person_parachute_height : 0.0)), 1.0F};
}

} // namespace battlespades::world
