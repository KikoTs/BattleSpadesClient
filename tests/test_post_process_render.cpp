// GPU checks of the world post chain (render::PostSettings) on the real
// WorldRenderer, drawn on a never-shown window and read back.
//
// Every effect is measured against the same frame with the effect off:
//   * a neutral active chain matches the direct-to-backbuffer path;
//   * brightness, gamma and colour-vision correction match their formulas;
//   * bloom spreads light from a bright patch into its dark surround;
//   * motion blur softens a turning camera, leaves a still camera untouched;
//   * ambient occlusion darkens the floor/wall corner more than open floor;
//   * a half-resolution scene upscaled stays close to native but softer, the
//     edge-adaptive upscale is no softer than bilinear, and sharpening adds
//     local contrast;
//   * the first-person view is never touched by motion blur or occlusion.
//
// Usage: aos_post_process_render_tests [direct3d11|direct3d12|vulkan|opengl]
// A backend this machine cannot start is reported as SKIP.

#include "battlespades/render/bgfx_ui_renderer.hpp"
#include "battlespades/render/camera_basis.hpp"
#include "battlespades/render/post_math.hpp"
#include "battlespades/render/render_views.hpp"
#include "battlespades/render/world_renderer.hpp"

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <stb_image_write.h>

#include <cstdlib>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace battlespades;
constexpr std::uint16_t size = 256U;
int failures{};

void check(bool ok, const std::string& message) {
    if (!ok) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void quad(world::ChunkMesh& mesh, std::array<std::array<float, 3U>, 4U> points, std::uint8_t face,
          std::uint32_t colour) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    for (const auto& p : points) {
        mesh.vertices.push_back({p[0], p[1], p[2], colour, face});
    }
    for (const auto index : {0U, 1U, 2U, 0U, 2U, 3U}) {
        mesh.indices.push_back(base + index);
    }
}

using Image = std::vector<std::uint8_t>;

struct Region final {
    int x0, y0, x1, y1;
};

double luma(const Image& image, int x, int y) {
    const auto offset = (static_cast<std::size_t>(y) * size + static_cast<std::size_t>(x)) * 4U;
    return 0.299 * image[offset] + 0.587 * image[offset + 1U] + 0.114 * image[offset + 2U];
}

double mean_luma(const Image& image, Region r) {
    double sum = 0.0;
    int count = 0;
    for (int y = std::max(r.y0, 0); y < std::min(r.y1, int{size}); ++y) {
        for (int x = std::max(r.x0, 0); x < std::min(r.x1, int{size}); ++x) {
            sum += luma(image, x, y);
            ++count;
        }
    }
    return count > 0 ? sum / count : 0.0;
}

/** Mean squared horizontal + vertical luma gradient: how sharp an image is. */
double gradient_energy(const Image& image, Region r) {
    double sum = 0.0;
    int count = 0;
    for (int y = std::max(r.y0, 0); y + 1 < std::min(r.y1, int{size}); ++y) {
        for (int x = std::max(r.x0, 0); x + 1 < std::min(r.x1, int{size}); ++x) {
            const double c = luma(image, x, y);
            const double dx = luma(image, x + 1, y) - c;
            const double dy = luma(image, x, y + 1) - c;
            sum += dx * dx + dy * dy;
            ++count;
        }
    }
    return count > 0 ? sum / count : 0.0;
}

int max_difference(const Image& a, const Image& b, Region r) {
    int worst = 0;
    for (int y = r.y0; y < r.y1; ++y) {
        for (int x = r.x0; x < r.x1; ++x) {
            const auto offset = (static_cast<std::size_t>(y) * size + static_cast<std::size_t>(x)) * 4U;
            for (std::size_t c = 0U; c < 3U; ++c) {
                worst = std::max(worst, std::abs(int{a[offset + c]} - int{b[offset + c]}));
            }
        }
    }
    return worst;
}

double mean_abs_difference(const Image& a, const Image& b, Region r) {
    double sum = 0.0;
    int count = 0;
    for (int y = r.y0; y < r.y1; ++y) {
        for (int x = r.x0; x < r.x1; ++x) {
            sum += std::abs(luma(a, x, y) - luma(b, x, y));
            ++count;
        }
    }
    return sum / count;
}

/** Screen pixel of a world point for a WorldCamera on a square window. */
std::array<int, 2U> project(const render::WorldCamera& camera, std::array<double, 3U> point) {
    const auto basis = render::world_camera_basis(camera.yaw_degrees, camera.pitch_degrees);
    const std::array<double, 3U> v{point[0] - camera.eye[0], point[1] - camera.eye[1],
                                   point[2] - camera.eye[2]};
    const auto dot = [&](const auto& axis) {
        return v[0] * axis[0U] + v[1] * axis[1U] + v[2] * axis[2U];
    };
    const double depth = dot(basis.forward);
    const double tan_half = std::tan(camera.fov_y_degrees * std::numbers::pi / 360.0);
    const double ndc_x = dot(basis.right) / (depth * tan_half);
    const double ndc_y = dot(basis.up) / (depth * tan_half);
    return {static_cast<int>((ndc_x + 1.0) * 0.5 * size), static_cast<int>((1.0 - ndc_y) * 0.5 * size)};
}

struct Harness final {
    render::BgfxUiRenderer& ui;
    render::WorldRenderer& scene;
    bgfx::FrameBufferHandle framebuffer;
    bgfx::TextureHandle colour;
    bgfx::TextureHandle readback;
    std::vector<render::ViewModelDraw> view_model;

    Image capture(const render::WorldCamera& camera, const render::PostSettings& settings) {
        scene.set_post_settings(settings);
        const bool post = settings.active() && scene.post_chain_supported();
        if (!ui.begin_frame()) {
            throw std::runtime_error{std::string{ui.last_error()}};
        }
        if (!scene.submit(camera, {size, size}, view_model)) {
            throw std::runtime_error{std::string{scene.last_error()}};
        }
        std::vector<std::uint16_t> views{render::backdrop_clear_view_id, render::ui_window_view_id,
                                         render::ui_canvas_view_id};
        if (post) {
            views.push_back(render::composite_view_id);
        } else {
            views.push_back(render::world_view_id);
            views.push_back(render::view_model_view_id);
        }
        for (const auto view : views) {
            bgfx::setViewFrameBuffer(view, framebuffer);
        }
        bgfx::blit(render::ui_window_overlay_view_id, readback, 0, 0, colour, 0, 0, size, size);
        Image pixels(static_cast<std::size_t>(size) * size * 4U);
        const auto ready = bgfx::readTexture(readback, pixels.data());
        if (!ui.end_frame()) {
            throw std::runtime_error{std::string{ui.last_error()}};
        }
        bool done = false;
        for (unsigned frame = 0U; frame < 32U && !done; ++frame) {
            done = bgfx::frame() >= ready;
        }
        if (!done) {
            throw std::runtime_error{"readback did not finish"};
        }
        if (bgfx::getCaps()->originBottomLeft) {
            constexpr std::size_t stride = static_cast<std::size_t>(size) * 4U;
            for (std::size_t row = 0U; row < size / 2U; ++row) {
                auto first = pixels.begin() + static_cast<std::ptrdiff_t>(row * stride);
                auto last = pixels.begin() + static_cast<std::ptrdiff_t>((size - row - 1U) * stride);
                std::swap_ranges(first, first + static_cast<std::ptrdiff_t>(stride), last);
            }
        }
        return pixels;
    }
};

render::PostSettings neutral() {
    render::PostSettings settings;
    // Active, but changes no pixel: proves the chain itself is transparent.
    settings.brightness = 1e-6F;
    return settings;
}

void dump(const char* name, const Image& image) {
    // AOS_POST_DUMP=<directory> saves every compared frame for inspection.
    if (const char* directory = SDL_getenv("AOS_POST_DUMP"); directory != nullptr) {
        const auto path = std::string{directory} + "/" + name + ".png";
        stbi_write_png(path.c_str(), size, size, 4, image.data(), size * 4);
    }
}

int run(std::string_view backend_name) {
    render::BgfxUiRendererConfig config;
    auto* window = SDL_CreateWindow("Post process regression", size, size, SDL_WINDOW_HIDDEN);
    if (window == nullptr) {
        std::cout << "SKIP: no window: " << SDL_GetError() << '\n';
        return 0;
    }
    config.native_window.window = SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    config.asset_root = AOS_TEST_ASSET_ROOT;
    config.shader_root = AOS_SHADER_BIN_ROOT;
    config.drawable_extent = {size, size};
    config.vertical_sync = false;
    config.backend = backend_name == "opengl"       ? render::GraphicsBackend::opengl
                     : backend_name == "vulkan"     ? render::GraphicsBackend::vulkan
                     : backend_name == "direct3d12" ? render::GraphicsBackend::direct3d12
                                                    : render::GraphicsBackend::direct3d11;
    const auto expected = backend_name == "opengl"       ? bgfx::RendererType::OpenGL
                          : backend_name == "vulkan"     ? bgfx::RendererType::Vulkan
                          : backend_name == "direct3d12" ? bgfx::RendererType::Direct3D12
                                                         : bgfx::RendererType::Direct3D11;
    render::BgfxUiRenderer ui;
    if (!ui.initialize(config) || bgfx::getRendererType() != expected) {
        std::cout << "SKIP: " << backend_name << " unavailable\n";
        ui.shutdown();
        SDL_DestroyWindow(window);
        return 0;
    }
    render::WorldRenderer scene;
    if (!scene.initialize(config.shader_root, config.asset_root)) {
        throw std::runtime_error{std::string{scene.last_error()}};
    }
    scene.set_quality_profile(
        render::profile_for(settings::ShaderQuality::compatibility, settings::QualityLevel::high));
    render::RetailTerrainLighting lighting;
    lighting.ambient_color = {1.0F, 1.0F, 1.0F};
    lighting.ambient_intensity = 0.55F;
    scene.set_retail_lighting(lighting);
    scene.set_model_culling(false);

    const auto capabilities = scene.post_capabilities();
    std::cout << backend_name << ": chain " << capabilities.chain << " ao "
              << capabilities.ambient_occlusion << " blur " << capabilities.motion_blur
              << " bloom " << capabilities.bloom << " easu "
              << capabilities.edge_adaptive_upscale << '\n';
    check(capabilities.chain, "the post chain must be supported on a desktop backend");

    // A floor at z = 64 (z grows downward), a wall facing +x at x = 250 and a
    // bright white panel and coloured patches on the wall.
    constexpr std::uint32_t grey = 0x00909090U;
    world::ChunkMesh floor;
    floor.key = {15U, 15U};
    floor.minimum = {240.0F, 240.0F, 64.0F};
    floor.maximum = {272.0F, 272.0F, 64.0F};
    for (int y = 240; y < 272; y += 2) {
        for (int x = 250; x < 272; x += 2) {
            const auto shade = ((x + y) / 2) % 2 == 0 ? grey : 0x00707070U;
            const auto fx = static_cast<float>(x);
            const auto fy = static_cast<float>(y);
            quad(floor, {{{fx, fy, 64}, {fx, fy + 2, 64}, {fx + 2, fy + 2, 64}, {fx + 2, fy, 64}}},
                 4, shade);
        }
    }
    // Wall: face 1 (+x), spanning y 240..272, z 48..64.
    quad(floor, {{{250, 240, 48}, {250, 272, 48}, {250, 272, 64}, {250, 240, 64}}}, 1, grey);
    // Coloured patches just in front of the wall.
    quad(floor, {{{250.05F, 249, 58}, {250.05F, 253, 58}, {250.05F, 253, 61}, {250.05F, 249, 61}}},
         1, 0x000000E0U);
    quad(floor, {{{250.05F, 259, 58}, {250.05F, 263, 58}, {250.05F, 263, 61}, {250.05F, 259, 61}}},
         1, 0x0000E000U);
    // A white panel for bloom.
    quad(floor, {{{250.05F, 254, 55.5F}, {250.05F, 258, 55.5F}, {250.05F, 258, 57.5F}, {250.05F, 254, 57.5F}}},
         1, 0x00FFFFFFU);
    floor.minimum = {240.0F, 240.0F, 48.0F};
    if (!scene.upload_chunk(floor)) {
        throw std::runtime_error{std::string{scene.last_error()}};
    }

    // First-person box in the lower right, in GL view space.
    world::ChunkMesh tool;
    quad(tool, {{{0.25F, -0.55F, -1.0F}, {0.55F, -0.55F, -1.0F}, {0.55F, -0.25F, -1.0F},
                 {0.25F, -0.25F, -1.0F}}},
         5, 0x0040A0C0U);
    tool.minimum = {0.25F, -0.55F, -1.0F};
    tool.maximum = {0.55F, -0.25F, -1.0F};
    if (!scene.set_view_model_mesh(0U, tool)) {
        throw std::runtime_error{std::string{scene.last_error()}};
    }

    const auto colour = bgfx::createTexture2D(size, size, false, 1, bgfx::TextureFormat::RGBA8,
                                              BGFX_TEXTURE_RT);
    const auto depth = bgfx::createTexture2D(size, size, false, 1, bgfx::TextureFormat::D24S8,
                                             BGFX_TEXTURE_RT_WRITE_ONLY);
    const std::array attachments{colour, depth};
    const auto framebuffer = bgfx::createFrameBuffer(2, attachments.data(), true);
    const auto readback = bgfx::createTexture2D(size, size, false, 1, bgfx::TextureFormat::RGBA8,
                                                BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    Harness harness{ui, scene, framebuffer, colour, readback, {}};
    render::ViewModelDraw tool_draw{};
    tool_draw.slot = 0U;
    harness.view_model.push_back(tool_draw);

    render::WorldCamera camera;
    camera.eye = {262.0, 256.0, 56.0};
    camera.yaw_degrees = 0.0; // faces -x, at the wall
    camera.pitch_degrees = 25.0;
    camera.fog_distance = 128.0;

    const Region whole{0, 0, size, size};
    const Region tool_region{170, 170, 190, 190};
    const auto legacy = harness.capture(camera, {});
    dump("legacy", legacy);
    const auto chain = harness.capture(camera, neutral());
    dump("neutral", chain);
    const int neutral_diff = max_difference(legacy, chain, whole);
    std::cout << "neutral chain vs direct: max difference " << neutral_diff << '\n';
    check(neutral_diff <= 2, "a neutral active chain must match the direct path");
    check(scene.last_frame_stats().post_passes >= 1U, "the chain must report its passes");
    const double tool_luma = mean_luma(legacy, tool_region);
    std::cout << "first-person box luma " << tool_luma << ", scene mean " << mean_luma(legacy, whole)
              << '\n';

    // Brightness and gamma.
    {
        auto settings = neutral();
        settings.brightness = 0.1F;
        const auto bright = harness.capture(camera, settings);
        const double gain = mean_luma(bright, whole) - mean_luma(legacy, whole);
        std::cout << "brightness +0.1: mean luma +" << gain << '\n';
        check(gain > 18.0 && gain < 26.5, "brightness +0.1 must lift the image by ~25 levels");
        settings = neutral();
        settings.gamma = 1.5F;
        const auto gamma = harness.capture(camera, settings);
        int checked = 0;
        int worst = 0;
        for (int y = 0; y < size; y += 7) {
            for (int x = 0; x < size; x += 7) {
                const auto offset = (static_cast<std::size_t>(y) * size + static_cast<std::size_t>(x)) * 4U;
                const double v = legacy[offset + 1U] / 255.0;
                if (v < 0.15 || v > 0.85) continue;
                const int predicted = static_cast<int>(std::lround(255.0 * std::pow(v, 1.0 / 1.5)));
                worst = std::max(worst, std::abs(predicted - int{gamma[offset + 1U]}));
                ++checked;
            }
        }
        std::cout << "gamma 1.5: " << checked << " midtones, worst error " << worst << '\n';
        check(checked > 20 && worst <= 3, "gamma must follow pow(c, 1 / gamma)");
    }

    // Colour-vision correction follows the CPU matrices.
    for (const auto mode : {render::ColorVisionMode::protanopia, render::ColorVisionMode::deuteranopia,
                            render::ColorVisionMode::tritanopia}) {
        auto settings = neutral();
        settings.color_vision = mode;
        const auto corrected = harness.capture(camera, settings);
        const auto matrix = render::color_vision_correction(mode);
        int worst = 0;
        int changed = 0;
        for (int y = 0; y < size; y += 5) {
            for (int x = 0; x < size; x += 5) {
                const auto offset = (static_cast<std::size_t>(y) * size + static_cast<std::size_t>(x)) * 4U;
                const std::array<float, 3U> in{legacy[offset] / 255.0F, legacy[offset + 1U] / 255.0F,
                                               legacy[offset + 2U] / 255.0F};
                const auto out = render::apply_matrix(matrix, in);
                for (std::size_t c = 0U; c < 3U; ++c) {
                    const int predicted =
                        static_cast<int>(std::lround(255.0F * std::clamp(out[c], 0.0F, 1.0F)));
                    worst = std::max(worst, std::abs(predicted - int{corrected[offset + c]}));
                    changed += std::abs(int{corrected[offset + c]} - int{legacy[offset + c]}) > 6 ? 1 : 0;
                }
            }
        }
        std::cout << "colour vision " << static_cast<int>(mode) << ": worst error " << worst
                  << ", channels changed " << changed << '\n';
        check(worst <= 3, "colour-vision correction must match its matrix");
        check(changed > 0, "colour-vision correction must change the coloured patches");
    }

    // Bloom.
    {
        const auto panel = project(camera, {250.0, 256.0, 56.5});
        const Region around{panel[0] - 30, panel[1] - 16, panel[0] + 30, panel[1] + 16};
        auto settings = neutral();
        settings.bloom = true;
        settings.bloom_intensity = 1.0F;
        const auto bloom = harness.capture(camera, settings);
        dump("bloom", bloom);
        const double lift = mean_luma(bloom, around) - mean_luma(chain, around);
        std::cout << "bloom: panel at " << panel[0] << "," << panel[1] << " surround +" << lift
                  << ", whole +" << mean_luma(bloom, whole) - mean_luma(chain, whole) << '\n';
        check(lift > 1.0, "bloom must spread light around the bright panel");
    }

    // Motion blur: a turning camera softens, a still one does not.
    if (capabilities.motion_blur) {
        auto blurred = neutral();
        blurred.motion_blur = 0.5F;
        auto turned = camera;
        turned.yaw_degrees = 4.0;
        static_cast<void>(harness.capture(camera, neutral()));
        const auto sharp = harness.capture(turned, neutral());
        static_cast<void>(harness.capture(camera, blurred));
        const auto moving = harness.capture(turned, blurred);
        dump("motion_blur", moving);
        const auto still = harness.capture(turned, blurred);
        const Region world_region{0, 0, size, 160};
        const double sharp_energy = gradient_energy(sharp, world_region);
        const double moving_energy = gradient_energy(moving, world_region);
        std::cout << "motion blur: gradient energy " << sharp_energy << " -> " << moving_energy
                  << " turning, still max diff " << max_difference(sharp, still, whole)
                  << ", first-person diff " << max_difference(sharp, moving, tool_region) << '\n';
        check(moving_energy < sharp_energy * 0.8, "a turning camera must be blurred");
        check(max_difference(sharp, still, whole) <= 2, "a still camera must not be blurred");
        check(max_difference(sharp, moving, tool_region) <= 2,
              "motion blur must never touch the first-person view");
    } else {
        std::cout << "motion blur unsupported on " << backend_name << '\n';
    }

    // Ambient occlusion: the floor/wall corner darkens more than open floor.
    if (capabilities.ambient_occlusion) {
        const auto corner = project(camera, {250.2, 256.0, 63.8});
        const auto open = project(camera, {254.5, 256.0, 64.0});
        for (const auto level : {render::AmbientOcclusion::low, render::AmbientOcclusion::high}) {
            auto settings = neutral();
            settings.ambient_occlusion = level;
            const auto occluded = harness.capture(camera, settings);
            dump(level == render::AmbientOcclusion::low ? "ao_low" : "ao_high", occluded);
            const Region corner_region{corner[0] - 20, corner[1] - 3, corner[0] + 20, corner[1] + 3};
            const Region open_region{open[0] - 20, open[1] - 3, open[0] + 20, open[1] + 3};
            const double corner_ratio = mean_luma(occluded, corner_region) / mean_luma(chain, corner_region);
            const double open_ratio = mean_luma(occluded, open_region) / mean_luma(chain, open_region);
            std::cout << "ambient occlusion " << static_cast<int>(level) << ": corner (" << corner[0]
                      << "," << corner[1] << ") x" << corner_ratio << ", open floor x" << open_ratio
                      << ", first-person diff " << max_difference(chain, occluded, tool_region) << '\n';
            check(corner_ratio < open_ratio - 0.05, "occlusion must darken the corner most");
            check(open_ratio > 0.9, "open floor must stay nearly unoccluded");
            check(max_difference(chain, occluded, tool_region) <= 2,
                  "occlusion must never darken the first-person view");
        }
    } else {
        std::cout << "ambient occlusion unsupported on " << backend_name << '\n';
    }

    // Render scale, upscale filter, sharpening.
    {
        const Region world_region{0, 0, size, 160};
        const double native_energy = gradient_energy(chain, world_region);
        auto settings = neutral();
        settings.render_scale = 0.5F;
        settings.upscale = render::UpscaleFilter::bilinear;
        const auto bilinear = harness.capture(camera, settings);
        check(scene.last_frame_stats().post_scene_width == size / 2U,
              "half scale must render a half-size scene");
        settings.upscale = render::UpscaleFilter::edge_adaptive;
        const auto edge = harness.capture(camera, settings);
        dump("easu_half", edge);
        settings.render_scale = 1.5F;
        const auto supersampled = harness.capture(camera, settings);
        settings.render_scale = 1.0F;
        settings.sharpness = 1.0F;
        const auto sharpened = harness.capture(camera, settings);
        dump("sharpened", sharpened);
        const double bilinear_energy = gradient_energy(bilinear, world_region);
        const double edge_energy = gradient_energy(edge, world_region);
        const double sharpened_energy = gradient_energy(sharpened, world_region);
        std::cout << "scale 0.5 bilinear: mean diff " << mean_abs_difference(chain, bilinear, whole)
                  << " energy " << bilinear_energy << "; edge-adaptive: mean diff "
                  << mean_abs_difference(chain, edge, whole) << " energy " << edge_energy
                  << "; native energy " << native_energy << "; scale 1.5 mean diff "
                  << mean_abs_difference(chain, supersampled, whole) << "; sharpened energy "
                  << sharpened_energy << '\n';
        check(mean_abs_difference(chain, bilinear, whole) < 12.0, "a half-scale scene must stay close");
        check(bilinear_energy < native_energy, "a half-scale scene must be softer than native");
        check(edge_energy >= bilinear_energy, "edge-adaptive must be no softer than bilinear");
        check(mean_abs_difference(chain, edge, whole) < 12.0, "edge-adaptive must stay close");
        check(mean_abs_difference(chain, supersampled, whole) < 8.0, "supersampling must stay close");
        check(sharpened_energy > native_energy * 1.05, "sharpening must add local contrast");
    }

    // Turning the chain off restores the direct path.
    const auto restored = harness.capture(camera, {});
    check(max_difference(legacy, restored, whole) == 0, "turning post off must restore the direct path");
    check(scene.last_frame_stats().post_passes == 0U, "an inactive chain submits no passes");

    bgfx::destroy(framebuffer);
    bgfx::destroy(readback);
    scene.shutdown();
    ui.shutdown();
    SDL_DestroyWindow(window);
    return failures;
}

} // namespace

int main(int argc, char** argv) {
    const std::string_view backend = argc > 1 ? argv[1] : "direct3d11";
    try {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            std::cout << "SKIP: SDL video unavailable: " << SDL_GetError() << '\n';
            return 0;
        }
        const int result = run(backend);
        SDL_Quit();
        if (result != 0) {
            std::cerr << "post process render tests failed on " << backend << '\n';
            return 1;
        }
        std::cout << "post process render tests passed on " << backend << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "post process render tests failed: " << error.what() << '\n';
        return 1;
    }
}
