#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace battlespades::render {

struct SunShadowProjection final {
    std::array<float, 16U> view{};
    std::array<float, 16U> projection{};
    std::array<float, 16U> world_to_texture{};
    float depth_span{};
};

/** Conservative light-volume test, independent of camera visibility. */
[[nodiscard]] inline bool sun_shadow_intersects(
    const std::array<float, 16U>& world_to_texture,
    const std::array<float, 3U>& minimum,
    const std::array<float, 3U>& maximum,
    float margin = 0.0F) noexcept {
    for (std::size_t row = 0U; row < 3U; ++row) {
        float low = world_to_texture[12U + row];
        float high = low;
        for (std::size_t axis = 0U; axis < 3U; ++axis) {
            const float a = world_to_texture[axis * 4U + row] * minimum[axis];
            const float b = world_to_texture[axis * 4U + row] * maximum[axis];
            low += std::min(a, b);
            high += std::max(a, b);
        }
        if (high < -margin || low > 1.0F + margin) return false;
    }
    return true;
}

/** D16 quantization plus a small world-space offset; never a draw-distance bias. */
[[nodiscard]] inline float sun_shadow_depth_bias(float depth_span) noexcept {
    return 2.0F / 65535.0F + 0.025F / std::max(depth_span, 1.0F);
}

// A camera-following orthographic volume with a world-stationary sample grid.
// Snap in the LIGHT basis: snapping map x/y/z before rotating still permits
// fractional shadow texels when walking or jumping. Keep the basis independent
// of eye-at subtraction, which otherwise introduces camera-dependent rounding.
[[nodiscard]] inline SunShadowProjection sun_shadow_projection(
    const std::array<double, 3U>& eye,
    const std::array<float, 3U>& sun_direction,
    const std::array<float, 3U>& map_size,
    float extent,
    std::uint16_t resolution,
    bool homogeneous_depth,
    bool origin_bottom_left) noexcept {
    const auto cross = [](const auto& a, const auto& b) {
        return std::array<double, 3U>{a[1] * b[2] - a[2] * b[1],
                                     a[2] * b[0] - a[0] * b[2],
                                     a[0] * b[1] - a[1] * b[0]};
    };
    const auto dot = [](const auto& a, const auto& b) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };
    const auto normalize = [&dot](std::array<double, 3U> value) {
        const double length = std::sqrt(dot(value, value));
        if (!std::isfinite(length) || length < 1e-8) {
            return std::array<double, 3U>{0.0, 0.0, -1.0};
        }
        for (auto& component : value) {
            component /= length;
        }
        return value;
    };
    const auto back = normalize({sun_direction[0], sun_direction[1], sun_direction[2]});
    const std::array<double, 3U> reference = std::abs(back[2]) > 0.995
        ? std::array<double, 3U>{0.0, 1.0, 0.0}
        : std::array<double, 3U>{0.0, 0.0, -1.0};
    const auto right = normalize(cross(reference, back));
    const auto up = cross(back, right);
    const double half_width = std::max(1.0, static_cast<double>(extent));
    const double texel = half_width * 2.0 / std::max<std::uint16_t>(1U, resolution);
    const double center_x = std::round(dot(right, eye) / texel) * texel;
    const double center_y = std::round(dot(up, eye) / texel) * texel;

    // Fix the depth range to map bounds as well. Following the eye along the
    // sun axis changes depth quantization on every jump, even with stable UVs.
    // Padding includes characters and effects just outside the voxel volume.
    double minimum_depth = 0.0;
    double maximum_depth = 0.0;
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        const double end = back[axis] * map_size[axis];
        minimum_depth += std::min(0.0, end);
        maximum_depth += std::max(0.0, end);
    }
    constexpr double padding = 16.0;
    constexpr double near_plane = 0.1;
    const double origin_depth = maximum_depth + padding;
    const double far_plane = maximum_depth - minimum_depth + padding * 2.0;
    const double depth_span = far_plane - near_plane;
    const double texture_y_sign = origin_bottom_left ? 1.0 : -1.0;
    SunShadowProjection result;
    result.depth_span = static_cast<float>(depth_span);
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        result.view[axis * 4U] = static_cast<float>(right[axis]);
        result.view[axis * 4U + 1U] = static_cast<float>(up[axis]);
        result.view[axis * 4U + 2U] = static_cast<float>(back[axis]);
        // Combined world -> [0,1] texture coordinates. Use the backend's
        // render-target origin, not its shader language (SPIR-V is not GLSL).
        result.world_to_texture[axis * 4U] = static_cast<float>(right[axis] / (2.0 * half_width));
        result.world_to_texture[axis * 4U + 1U] = static_cast<float>(up[axis] * texture_y_sign / (2.0 * half_width));
        result.world_to_texture[axis * 4U + 2U] = static_cast<float>(-back[axis] / depth_span);
    }
    result.view[12] = static_cast<float>(-center_x);
    result.view[13] = static_cast<float>(-center_y);
    result.view[14] = static_cast<float>(-origin_depth);
    result.view[15] = 1.0F;
    result.projection[0] = static_cast<float>(1.0 / half_width);
    result.projection[5] = result.projection[0];
    result.projection[10] = static_cast<float>(-(homogeneous_depth ? 2.0 : 1.0) / depth_span);
    result.projection[14] = static_cast<float>(-(homogeneous_depth ? far_plane + near_plane : near_plane) / depth_span);
    result.projection[15] = 1.0F;
    result.world_to_texture[12] = static_cast<float>(0.5 - center_x / (2.0 * half_width));
    result.world_to_texture[13] = static_cast<float>(0.5 - texture_y_sign * center_y / (2.0 * half_width));
    result.world_to_texture[14] = static_cast<float>((origin_depth - near_plane) / depth_span);
    result.world_to_texture[15] = 1.0F;
    return result;
}

} // namespace battlespades::render
