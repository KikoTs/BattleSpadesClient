#pragma once

#include "battlespades/render/post_settings.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace battlespades::render {

using PostMatrix3 = std::array<std::array<float, 3U>, 3U>;

namespace post_detail {

[[nodiscard]] constexpr PostMatrix3 multiply(const PostMatrix3& a, const PostMatrix3& b) noexcept {
    PostMatrix3 result{};
    for (std::size_t row{}; row < 3U; ++row) {
        for (std::size_t column{}; column < 3U; ++column) {
            float sum{};
            for (std::size_t k{}; k < 3U; ++k) {
                sum += a[row][k] * b[k][column];
            }
            result[row][column] = sum;
        }
    }
    return result;
}

// Linear RGB <-> LMS cone response (Vienot, Brettel and Mollon 1999, as used
// by the Fidaner, Lin and Ozguven daltonization).
inline constexpr PostMatrix3 rgb_to_lms{{{17.8824F, 43.5161F, 4.11935F},
                                         {3.45565F, 27.1554F, 3.86714F},
                                         {0.0299566F, 0.184309F, 1.46709F}}};
inline constexpr PostMatrix3 lms_to_rgb{{{0.0809444479F, -0.130504409F, 0.116721066F},
                                         {-0.0102485335F, 0.0540193266F, -0.113614708F},
                                         {-0.000365296938F, -0.00412161469F, 0.693511405F}}};

[[nodiscard]] constexpr PostMatrix3 lms_deficiency(ColorVisionMode mode) noexcept {
    switch (mode) {
    case ColorVisionMode::protanopia:
        return {{{0.0F, 2.02344F, -2.52581F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}}};
    case ColorVisionMode::deuteranopia:
        return {{{1.0F, 0.0F, 0.0F}, {0.494207F, 0.0F, 1.24827F}, {0.0F, 0.0F, 1.0F}}};
    case ColorVisionMode::tritanopia:
        return {{{1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {-0.395913F, 0.801109F, 0.0F}}};
    case ColorVisionMode::off:
        break;
    }
    return {{{1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}}};
}

} // namespace post_detail

/** What a viewer with `mode` sees of an RGB colour (identity for `off`). */
[[nodiscard]] constexpr PostMatrix3 color_vision_simulation(ColorVisionMode mode) noexcept {
    return post_detail::multiply(
        post_detail::lms_to_rgb,
        post_detail::multiply(post_detail::lms_deficiency(mode), post_detail::rgb_to_lms));
}

/**
 * Daltonization: the colour information a viewer with `mode` loses is moved
 * into the channels they can still tell apart, D = I + E (I - S) with the
 * Fidaner error-shift matrix E. Identity for `off`.
 */
[[nodiscard]] constexpr PostMatrix3 color_vision_correction(ColorVisionMode mode) noexcept {
    if (mode == ColorVisionMode::off) {
        return {{{1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}}};
    }
    constexpr PostMatrix3 shift{{{0.0F, 0.0F, 0.0F}, {0.7F, 1.0F, 0.0F}, {0.7F, 0.0F, 1.0F}}};
    const auto simulated = color_vision_simulation(mode);
    PostMatrix3 lost{};
    for (std::size_t row{}; row < 3U; ++row) {
        for (std::size_t column{}; column < 3U; ++column) {
            lost[row][column] = (row == column ? 1.0F : 0.0F) - simulated[row][column];
        }
    }
    auto result = post_detail::multiply(shift, lost);
    for (std::size_t index{}; index < 3U; ++index) {
        result[index][index] += 1.0F;
    }
    return result;
}

[[nodiscard]] constexpr std::array<float, 3U> apply_matrix(const PostMatrix3& matrix,
                                                    std::array<float, 3U> colour) noexcept {
    return {matrix[0U][0U] * colour[0U] + matrix[0U][1U] * colour[1U] + matrix[0U][2U] * colour[2U],
            matrix[1U][0U] * colour[0U] + matrix[1U][1U] * colour[1U] + matrix[1U][2U] * colour[2U],
            matrix[2U][0U] * colour[0U] + matrix[2U][1U] * colour[1U] + matrix[2U][2U] * colour[2U]};
}

struct PostExtent final {
    std::uint32_t width{};
    std::uint32_t height{};

    [[nodiscard]] friend constexpr bool operator==(const PostExtent&, const PostExtent&) = default;
};

inline constexpr float minimum_render_scale{0.5F};
inline constexpr float maximum_render_scale{2.0F};
inline constexpr std::uint32_t minimum_scene_edge{64U};

/**
 * Size of the offscreen scene for a window: `scale` (clamped to 0.5 .. 2.0)
 * of each edge, at least 64 texels and at most the device's texture limit,
 * keeping the window's aspect when the limit bites.
 */
[[nodiscard]] inline PostExtent post_scene_extent(PostExtent drawable, float scale,
                                                  std::uint32_t maximum_texture) noexcept {
    if (drawable.width == 0U || drawable.height == 0U) {
        return {};
    }
    const float clamped = std::isfinite(scale)
                              ? std::clamp(scale, minimum_render_scale, maximum_render_scale)
                              : 1.0F;
    double width = std::round(static_cast<double>(drawable.width) * clamped);
    double height = std::round(static_cast<double>(drawable.height) * clamped);
    const double limit = static_cast<double>(std::max<std::uint32_t>(maximum_texture, minimum_scene_edge));
    const double over = std::max(width / limit, height / limit);
    if (over > 1.0) {
        width = std::floor(width / over);
        height = std::floor(height / over);
    }
    const auto edge = static_cast<double>(minimum_scene_edge);
    return {static_cast<std::uint32_t>(std::max(width, edge)),
            static_cast<std::uint32_t>(std::max(height, edge))};
}

/** Per-level screen-space ambient occlusion parameters. */
struct AmbientOcclusionParameters final {
    /** World radius in blocks. */
    float radius{};
    std::uint32_t samples{};
    /** Computed at half the scene resolution and upsampled by the world pass. */
    bool half_resolution{};
    float strength{};
};

[[nodiscard]] constexpr AmbientOcclusionParameters ambient_occlusion_parameters(
    AmbientOcclusion level) noexcept {
    switch (level) {
    case AmbientOcclusion::low:
        return {0.9F, 6U, true, 1.2F};
    case AmbientOcclusion::medium:
        return {1.1F, 10U, true, 1.3F};
    case AmbientOcclusion::high:
        return {1.25F, 16U, false, 1.4F};
    case AmbientOcclusion::off:
        break;
    }
    return {};
}

/**
 * Whether two consecutive cameras are a cut (respawn, teleport, camera-mode
 * switch) rather than motion; a cut is never motion-blurred.
 */
[[nodiscard]] inline bool camera_cut(const std::array<double, 3U>& previous_eye,
                                     const std::array<double, 3U>& eye,
                                     const std::array<double, 3U>& previous_forward,
                                     const std::array<double, 3U>& forward) noexcept {
    const double dx = eye[0U] - previous_eye[0U];
    const double dy = eye[1U] - previous_eye[1U];
    const double dz = eye[2U] - previous_eye[2U];
    const double turn = previous_forward[0U] * forward[0U] + previous_forward[1U] * forward[1U] +
                        previous_forward[2U] * forward[2U];
    // More than 3 blocks or 45 degrees between two presented frames.
    return !(dx * dx + dy * dy + dz * dz <= 9.0) || !(turn >= 0.70710678);
}

} // namespace battlespades::render
