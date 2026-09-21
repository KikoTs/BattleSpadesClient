#include "battlespades/render/shadow_projection.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool ok, const char* message) {
    if (!ok) {
        throw std::runtime_error(message);
    }
}

std::array<double, 4U> transform(const std::array<float, 16U>& matrix,
                                const std::array<double, 4U>& point) {
    std::array<double, 4U> result{};
    for (std::size_t row = 0U; row < 4U; ++row) {
        for (std::size_t column = 0U; column < 4U; ++column) {
            result[row] += matrix[column * 4U + row] * point[column];
        }
    }
    return result;
}
} // namespace

int main() {
    using battlespades::render::sun_shadow_projection;
    try {
        constexpr std::array<float, 3U> map_size{512.0F, 512.0F, 240.0F};
        constexpr std::array<double, 3U> start{255.91, 253.17, 58.31};
        constexpr std::array<double, 4U> receiver{257.3, 250.7, 64.0, 1.0};
        const std::array suns{
            std::array<float, 3U>{0.36F, 0.26F, -0.90F},
            std::array<float, 3U>{-0.73F, 0.67F, -0.13F},
            std::array<float, 3U>{0.01F, 0.001F, -0.9999F},
            std::array<float, 3U>{0.0F, 0.0F, -1.0F},
            std::array<float, 3U>{0.0F, 0.0F, 0.0F}};
        std::size_t cases = 0U;
        std::array<float, 16U> unit{};
        unit[0] = unit[5] = unit[10] = unit[15] = 1.0F;
        using battlespades::render::sun_shadow_intersects;
        expect(sun_shadow_intersects(unit, {0.2F, 0.2F, 0.2F}, {0.8F, 0.8F, 0.8F}), "Interior caster culled");
        expect(sun_shadow_intersects(unit, {-4.0F, -4.0F, -4.0F}, {4.0F, 4.0F, 4.0F}), "Enclosing caster culled");
        expect(sun_shadow_intersects(unit, {-0.2F, 0.2F, 0.2F}, {0.0F, 0.8F, 0.8F}), "Boundary caster culled");
        expect(!sun_shadow_intersects(unit, {1.1F, 0.2F, 0.2F}, {1.2F, 0.8F, 0.8F}), "Outside caster submitted");
        expect(sun_shadow_intersects(unit, {1.001F, 0.2F, 0.2F}, {1.002F, 0.8F, 0.8F}, 0.003F), "Filter guard ignored");
        for (const auto& sun : suns) {
            for (const float extent : {32.0F, 105.6F, 160.0F}) {
                for (const auto resolution : std::array<std::uint16_t, 2U>{1536U, 2048U}) {
                    for (const bool homogeneous : {false, true}) {
                        for (const bool bottom_left : {false, true}) {
                            const auto base = sun_shadow_projection(start, sun, map_size, extent,
                                                                    resolution, homogeneous, bottom_left);
                            const auto anchor = transform(base.world_to_texture, receiver);
                            expect(sun_shadow_intersects(base.world_to_texture, {0, 0, 0}, map_size),
                                   "Light-basis culling lost the enclosing map");
                            const auto bias_world = battlespades::render::sun_shadow_depth_bias(base.depth_span) * base.depth_span;
                            expect(bias_world > 0.025F && bias_world < 0.06F,
                                   "Shadow bias detaches contact from voxel geometry");
                            for (int frame = 0; frame < 240; ++frame) {
                                const double t = static_cast<double>(frame) / 60.0;
                                // Walk + strafe while repeatedly jumping/falling across texel boundaries.
                                const std::array<double, 3U> eye{
                                    start[0] + t * 4.0, start[1] - t * 3.0,
                                    start[2] - std::abs(std::sin(t * 3.0)) * 3.5};
                                const auto moved = sun_shadow_projection(eye, sun, map_size, extent,
                                                                         resolution, homogeneous, bottom_left);
                                const auto coordinate = transform(moved.world_to_texture, receiver);
                                for (std::size_t axis = 0U; axis < 2U; ++axis) {
                                    const double delta = (coordinate[axis] - anchor[axis]) * resolution;
                                    expect(std::abs(delta - std::round(delta)) < 0.0005,
                                           "Movement changed the fractional shadow texel position");
                                }
                                expect(coordinate[2] == anchor[2], "Jump changed shadow depth quantization");
                                for (std::size_t element = 0U; element < 12U; ++element) {
                                    expect(moved.view[element] == base.view[element], "Movement rotated the sun basis");
                                }
                                const auto clip = transform(moved.projection, transform(moved.view, receiver));
                                const std::array<double, 3U> sampled{
                                    clip[0] * 0.5 + 0.5,
                                    clip[1] * (bottom_left ? 0.5 : -0.5) + 0.5,
                                    homogeneous ? clip[2] * 0.5 + 0.5 : clip[2]};
                                for (std::size_t axis = 0U; axis < 3U; ++axis) {
                                    expect(std::isfinite(coordinate[axis]) &&
                                               std::abs(coordinate[axis] - sampled[axis]) < 0.000002,
                                           "Shadow sampling disagrees with backend projection/origin");
                                }
                                ++cases;
                            }
                            for (unsigned corner = 0U; corner < 8U; ++corner) {
                                const auto depth = transform(base.world_to_texture,
                                    {(corner & 1U) != 0U ? 512.0 : 0.0,
                                     (corner & 2U) != 0U ? 512.0 : 0.0,
                                     (corner & 4U) != 0U ? 240.0 : 0.0, 1.0})[2];
                                expect(depth > 0.0 && depth < 1.0, "Map caster clipped by shadow depth range");
                            }
                        }
                    }
                }
            }
        }
        std::cout << cases << " stable shadow movement/backend projection cases passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
