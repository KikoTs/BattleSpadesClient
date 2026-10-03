// CPU-side math of the world post chain: colour-vision matrices, scene
// target sizing, ambient-occlusion levels and camera-cut detection.

#include "battlespades/render/post_math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>
#include <stdexcept>
#include <string>

namespace {

using namespace battlespades::render;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

double distance(std::array<float, 3U> a, std::array<float, 3U> b) {
    return std::sqrt(static_cast<double>((a[0U] - b[0U]) * (a[0U] - b[0U]) +
                                         (a[1U] - b[1U]) * (a[1U] - b[1U]) +
                                         (a[2U] - b[2U]) * (a[2U] - b[2U])));
}

void colour_vision() {
    const std::array<float, 3U> grey{0.5F, 0.5F, 0.5F};
    for (const auto mode : {ColorVisionMode::protanopia, ColorVisionMode::deuteranopia,
                            ColorVisionMode::tritanopia}) {
        const auto simulated = apply_matrix(color_vision_simulation(mode), grey);
        expect(distance(simulated, grey) < 0.02,
               "a simulated deficiency must leave neutral grey (nearly) unchanged");
        const auto corrected = apply_matrix(color_vision_correction(mode), grey);
        expect(distance(corrected, grey) < 0.02, "correction must leave neutral grey alone");
    }
    // Over every pair of in-gamut colours that a deficient viewer confuses
    // (clearly different, but nearly identical as simulated), correction must
    // make them easier to tell apart as that viewer sees them, on average.
    std::vector<std::array<float, 3U>> palette;
    for (int r = 0; r <= 8; ++r) {
        for (int g = 0; g <= 8; ++g) {
            for (int b = 0; b <= 8; ++b) {
                palette.push_back({0.1F * static_cast<float>(r + 1),
                                   0.1F * static_cast<float>(g + 1),
                                   0.1F * static_cast<float>(b + 1)});
            }
        }
    }
    const auto clamp01 = [](std::array<float, 3U> c) {
        for (auto& channel : c) channel = std::clamp(channel, 0.0F, 1.0F);
        return c;
    };
    for (const auto mode : {ColorVisionMode::protanopia, ColorVisionMode::deuteranopia,
                            ColorVisionMode::tritanopia}) {
        const auto seen = color_vision_simulation(mode);
        const auto fix = color_vision_correction(mode);
        double before = 0.0;
        double after = 0.0;
        int pairs = 0;
        for (std::size_t i = 0; i < palette.size(); ++i) {
            for (std::size_t j = i + 1U; j < palette.size(); ++j) {
                const double seen_apart =
                    distance(apply_matrix(seen, palette[i]), apply_matrix(seen, palette[j]));
                if (distance(palette[i], palette[j]) < 0.3 || seen_apart > 0.08) {
                    continue;
                }
                before += seen_apart;
                after += distance(apply_matrix(seen, clamp01(apply_matrix(fix, palette[i]))),
                                  apply_matrix(seen, clamp01(apply_matrix(fix, palette[j]))));
                ++pairs;
            }
        }
        std::cout << "cvd mode " << static_cast<int>(mode) << ": " << pairs
                  << " confusable pairs, mean seen distance " << before / pairs << " -> "
                  << after / pairs << '\n';
        expect(pairs > 10, "the palette must contain confusable pairs");
        expect(after > before * 1.5, "daltonization must separate confusable colours");
    }
    const auto identity = color_vision_correction(ColorVisionMode::off);
    expect(identity[0U][0U] == 1.0F && identity[0U][1U] == 0.0F && identity[2U][2U] == 1.0F,
           "off must be the identity");
}

void sizing() {
    expect(post_scene_extent({1920U, 1080U}, 1.0F, 16384U) == PostExtent{1920U, 1080U},
           "scale 1 keeps the window size");
    expect(post_scene_extent({1920U, 1080U}, 0.5F, 16384U) == PostExtent{960U, 540U},
           "scale 0.5 halves each edge");
    expect(post_scene_extent({1920U, 1080U}, 0.1F, 16384U) == PostExtent{960U, 540U},
           "scale clamps at 0.5");
    expect(post_scene_extent({1920U, 1080U}, 4.0F, 16384U) == PostExtent{3840U, 2160U},
           "scale clamps at 2");
    expect(post_scene_extent({3840U, 2160U}, 2.0F, 4096U) == PostExtent{4096U, 2304U},
           "the device limit wins and keeps the aspect");
    expect(post_scene_extent({100U, 40U}, 0.5F, 16384U) == PostExtent{64U, 64U},
           "tiny windows keep a 64-texel minimum");
    expect(post_scene_extent({0U, 40U}, 1.0F, 16384U) == PostExtent{},
           "an empty window has no scene");
    expect(post_scene_extent({800U, 600U}, std::nanf(""), 16384U) == PostExtent{800U, 600U},
           "a non-finite scale falls back to 1");
}

void ambient_occlusion_levels() {
    const auto off = ambient_occlusion_parameters(AmbientOcclusion::off);
    const auto low = ambient_occlusion_parameters(AmbientOcclusion::low);
    const auto medium = ambient_occlusion_parameters(AmbientOcclusion::medium);
    const auto high = ambient_occlusion_parameters(AmbientOcclusion::high);
    expect(off.samples == 0U, "off samples nothing");
    expect(low.samples < medium.samples && medium.samples < high.samples && high.samples <= 16U,
           "levels add samples up to the shader's 16");
    expect(low.half_resolution && medium.half_resolution && !high.half_resolution,
           "only High runs at full resolution");
}

void camera_cuts() {
    const std::array<double, 3U> eye{256.0, 256.0, 40.0};
    const std::array<double, 3U> forward{1.0, 0.0, 0.0};
    expect(!camera_cut(eye, {256.5, 256.0, 40.0}, forward, {0.996, 0.087, 0.0}),
           "walking and a 5 degree turn is motion");
    expect(camera_cut(eye, {270.0, 256.0, 40.0}, forward, forward), "a 14 block jump is a cut");
    expect(camera_cut(eye, eye, forward, {0.0, 1.0, 0.0}), "a 90 degree snap is a cut");
    expect(camera_cut(eye, {std::nan(""), 0.0, 0.0}, forward, forward), "NaN is a cut");
}

} // namespace

int main() {
    try {
        colour_vision();
        sizing();
        ambient_occlusion_levels();
        camera_cuts();
        std::cout << "post math tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "post math tests failed: " << error.what() << '\n';
        return 1;
    }
}
