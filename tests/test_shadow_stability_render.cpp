#include "battlespades/render/bgfx_ui_renderer.hpp"
#include "battlespades/render/render_views.hpp"
#include "battlespades/render/world_renderer.hpp"

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace battlespades;
constexpr std::uint16_t size = 512U;
void expect(bool ok, const std::string& message) {
    if (!ok) {
        throw std::runtime_error(message);
    }
}

void quad(world::ChunkMesh& mesh, std::array<std::array<float, 3U>, 4U> points,
          std::uint8_t face, std::uint32_t color) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    for (const auto& p : points) {
        mesh.vertices.push_back({p[0], p[1], p[2], color, face});
    }
    for (const auto index : {0U, 1U, 2U, 0U, 2U, 3U}) {
        mesh.indices.push_back(base + index);
    }
}
} // namespace

int main(int argc, char** argv) {
    using namespace battlespades;
    try {
        expect(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        auto* window = SDL_CreateWindow("Shadow stability regression", size, size, SDL_WINDOW_HIDDEN);
        expect(window != nullptr, SDL_GetError());
        render::BgfxUiRenderer ui;
        render::BgfxUiRendererConfig config;
        config.native_window.window = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
            SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        config.asset_root = AOS_TEST_ASSET_ROOT;
        config.shader_root = AOS_SHADER_BIN_ROOT;
        config.drawable_extent = {size, size};
        const std::string_view backend = argc > 1 ? argv[1] : "direct3d11";
        expect(backend == "direct3d11" || backend == "direct3d12" || backend == "vulkan" || backend == "opengl",
               "Unknown shadow test backend");
        config.backend = backend == "vulkan" ? render::GraphicsBackend::vulkan
            : backend == "opengl" ? render::GraphicsBackend::opengl
            : backend == "direct3d12" ? render::GraphicsBackend::direct3d12
                                      : render::GraphicsBackend::direct3d11;
        config.vertical_sync = false;
        expect(ui.initialize(config), std::string{ui.last_error()});
        const auto expected_backend = backend == "vulkan" ? bgfx::RendererType::Vulkan
            : backend == "opengl" ? bgfx::RendererType::OpenGL
            : backend == "direct3d12" ? bgfx::RendererType::Direct3D12 : bgfx::RendererType::Direct3D11;
        expect(bgfx::getRendererType() == expected_backend,
               "Shadow test did not initialize the requested backend");
        render::WorldRenderer scene;
        expect(scene.initialize(config.shader_root, config.asset_root), std::string{scene.last_error()});
        world::MapAtmosphere atmosphere;
        atmosphere.key_intensity = 1.0F;
        atmosphere.ambient_intensity = 0.25F;
        atmosphere.fog_density = 0.0F;
        atmosphere.specular_strength = 0.0F;
        scene.set_atmosphere(atmosphere);

        // Full resident terrain grid proves the shadow pass rejects work by
        // its own light volume, while retaining off-camera shadow casters.
        for (std::uint32_t y = 0; y < 32U; ++y) {
            for (std::uint32_t x = 0; x < 32U; ++x) {
                world::ChunkMesh tile;
                tile.key = {x, y};
                const float left = static_cast<float>(x * 16U);
                const float top = static_cast<float>(y * 16U);
                tile.minimum = {left, top, 64.0F};
                tile.maximum = {left + 16.0F, top + 16.0F, 64.0F};
                quad(tile, {{{left, top, 64}, {left + 16, top, 64},
                             {left + 16, top + 16, 64}, {left, top + 16, 64}}}, 4, 0x00808080);
                expect(scene.upload_chunk(tile), "Terrain grid upload failed");
            }
        }

        world::ChunkMesh floor;
        floor.minimum = {220, 220, 64};
        floor.maximum = {290, 290, 64};
        quad(floor, {{{220, 220, 64}, {290, 220, 64}, {290, 290, 64}, {220, 290, 64}}}, 4, 0x00808080);
        expect(scene.set_world_model_mesh(0, floor), std::string{scene.last_error()});
        world::ChunkMesh box;
        box.minimum = {250, 250, 56};
        box.maximum = {254, 254, 64};
        quad(box, {{{250, 250, 56}, {250, 254, 56}, {250, 254, 64}, {250, 250, 64}}}, 0, 0x00707070);
        quad(box, {{{254, 250, 56}, {254, 254, 56}, {254, 254, 64}, {254, 250, 64}}}, 1, 0x00707070);
        quad(box, {{{250, 250, 56}, {254, 250, 56}, {254, 250, 64}, {250, 250, 64}}}, 2, 0x00707070);
        quad(box, {{{250, 254, 56}, {254, 254, 56}, {254, 254, 64}, {250, 254, 64}}}, 3, 0x00707070);
        quad(box, {{{250, 250, 56}, {254, 250, 56}, {254, 254, 56}, {250, 254, 56}}}, 4, 0x00707070);
        expect(scene.set_world_model_mesh(1, box), std::string{scene.last_error()});
        expect(scene.set_view_model_mesh(0, box), std::string{scene.last_error()});
        // Rejected replacements must retain the terrain grid and box. The
        // shadow readback below proves that the resident caster still draws.
        auto invalid = box;
        invalid.indices.back() = static_cast<std::uint32_t>(invalid.vertices.size());
        expect(!scene.upload_chunk(invalid) && scene.resident_chunks() == 1024U,
               "Invalid chunk indices replaced resident terrain");
        expect(!scene.set_world_model_mesh(1, invalid) && !scene.set_view_model_mesh(0, invalid),
               "Invalid model indices reached the GPU");
        invalid = box;
        invalid.vertices.front().ao_u = std::numeric_limits<float>::infinity();
        expect(!scene.upload_chunk(invalid) && !scene.set_world_model_mesh(1, invalid),
               "Non-finite vertex attributes reached the GPU");
        invalid = box;
        invalid.minimum[0] = invalid.maximum[0] + 1.0F;
        expect(!scene.upload_chunk(invalid) && !scene.set_world_model_mesh(1, invalid),
               "Inverted model bounds were accepted");
        invalid = box;
        invalid.indices.clear();
        expect(!scene.upload_chunk(invalid) && !scene.set_world_model_mesh(1, invalid),
               "A partial mesh erased its resident replacement target");
        scene.clear_view_model();
        // The observer below replaces the world view after submit, so the
        // moving camera must not cull the models the observer looks at.
        scene.set_model_culling(false);
        const std::array draws{render::WorldModelDraw{0}, render::WorldModelDraw{1}};
        const auto colour = bgfx::createTexture2D(size, size, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT);
        const auto depth = bgfx::createTexture2D(size, size, false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT_WRITE_ONLY);
        const std::array attachments{colour, depth};
        const auto framebuffer = bgfx::createFrameBuffer(2, attachments.data(), false);
        const auto readback = bgfx::createTexture2D(size, size, false, 1, bgfx::TextureFormat::RGBA8,
                                                  BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
        expect(bgfx::isValid(framebuffer) && bgfx::isValid(readback), "Shadow capture target failed");

        // Keep the observer fixed while the REAL renderer's camera (and shadow
        // volume) walks/jumps. Disabling fog/specular above isolates the shadow
        // so each pixel is the same world point throughout the motion sequence.
        std::array<float, 16U> observer_view{}, observer_projection{};
        bx::mtxLookAt(observer_view.data(), {256, 256, 0}, {256, 256, 64}, {0, -1, 0}, bx::Handedness::Right);
        bx::mtxOrtho(observer_projection.data(), -16, 16, -16, 16, 0.1F, 100, 0,
                     bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
        const auto capture = [&](const render::WorldCamera& camera, const render::QualityProfile& profile) {
            scene.set_quality_profile(profile);
            expect(ui.begin_frame(), std::string{ui.last_error()});
            expect(scene.submit(camera, config.drawable_extent, {}, draws), std::string{scene.last_error()});
            for (const auto view : {render::backdrop_clear_view_id, render::world_view_id,
                                   render::view_model_view_id, render::ui_window_view_id, render::ui_canvas_view_id}) {
                bgfx::setViewFrameBuffer(view, framebuffer);
            }
            bgfx::setViewTransform(render::world_view_id, observer_view.data(), observer_projection.data());
            bgfx::blit(render::ui_canvas_view_id + 1, readback, 0, 0, colour, 0, 0, size, size);
            std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size) * size * 4U);
            const auto ready = bgfx::readTexture(readback, pixels.data());
            expect(ui.end_frame(), std::string{ui.last_error()});
            bool completed = false;
            for (unsigned frame = 0U; frame < 32U; ++frame) {
                if (bgfx::frame() >= ready) { completed = true; break; }
            }
            expect(completed, "GPU shadow readback did not finish within 32 frames");
            // Normalize framebuffer readback to the top-left image convention
            // used by the world-point assertions and saved evidence images.
            if (bgfx::getCaps()->originBottomLeft) {
                constexpr std::size_t stride = static_cast<std::size_t>(size) * 4U;
                for (std::size_t row = 0U; row < size / 2U; ++row) {
                    auto first = pixels.begin() + static_cast<std::ptrdiff_t>(row * stride);
                    auto last = pixels.begin() + static_cast<std::ptrdiff_t>((size - row - 1U) * stride);
                    std::swap_ranges(first, first + static_cast<std::ptrdiff_t>(stride), last);
                }
            }
            return pixels;
        };
        const std::filesystem::path output = argc > 2 ? argv[2] : "tmp/shadow-stability";
        std::filesystem::create_directories(output);
        for (const auto quality : {settings::ShaderQuality::medium, settings::ShaderQuality::ultra}) {
            auto profile = render::profile_for(quality, settings::QualityLevel::high);
            profile.hdr_target = false;
            for (const double distance : {64.0, 256.0}) {
                render::WorldCamera camera;
                camera.eye = {255.91, 253.17, 58.31};
                camera.fog_distance = distance;
                auto unshadowed = profile;
                unshadowed.shadow_cascades = 0U;
                const auto lit = capture(camera, unshadowed);
                const auto reference = capture(camera, profile);
                const auto shadow_stats = scene.last_frame_stats();
                expect(shadow_stats.shadow_chunks_submitted > 0U && shadow_stats.shadow_chunks_culled > 0U &&
                           shadow_stats.shadow_chunks_submitted + shadow_stats.shadow_chunks_culled == 1024U,
                       "Shadow pass did not cull the resident terrain grid");
                std::size_t shadow_pixels = 0U;
                for (std::size_t p = 0U; p < reference.size(); p += 4U) {
                    if (static_cast<int>(lit[p]) - reference[p] > 20) {
                        ++shadow_pixels;
                    }
                }
                expect(shadow_pixels > 1000U, "Sun shadow is missing/inverted; stability alone cannot prove correct rendering");
                // Sun points toward +x/+y: the box must shadow this floor point
                // to its -x side, with the opposite side still exposed. These
                // are separate from the box, so a flipped lookup cannot pass
                // merely by producing a stable dark patch somewhere else.
                constexpr std::size_t shaded_point = (160U * size + 128U) * 4U; // (248,250,64)
                constexpr std::size_t exposed_point = (192U * size + 280U) * 4U; // (257.5,252,64)
                expect(static_cast<int>(lit[shaded_point]) - reference[shaded_point] > 20 &&
                           std::abs(static_cast<int>(lit[exposed_point]) - reference[exposed_point]) < 3,
                       "Sun shadow is projected on the wrong side of its caster");
                constexpr std::size_t contact_point = (192U * size + 156U) * 4U; // (249.75,252,64)
                expect(static_cast<int>(lit[contact_point]) - reference[contact_point] > 15,
                       "Shadow detached from the caster at floor contact");
                std::size_t worst_changed = 0U;
                double worst_mean_error = 0.0;
                for (int frame = 1; frame <= 48; ++frame) {
                    const double t = static_cast<double>(frame) / 24.0;
                    camera.eye = {255.91 + t * 4.0, 253.17 - t * 3.0, 58.31 - std::abs(std::sin(t * 3.0)) * 3.5};
                    camera.yaw_degrees = t * 67.0;
                    camera.pitch_degrees = std::sin(t) * 35.0;
                    camera.fov_y_degrees = 50.0 + std::sin(t) * 25.0;
                    const auto moved = capture(camera, profile);
                    std::size_t changed = 0U;
                    double total_error = 0.0;
                    for (std::size_t p = 0U; p < reference.size(); p += 4U) {
                        const int error = std::abs(static_cast<int>(moved[p]) - reference[p]);
                        total_error += error;
                        if (error > 3) {
                            ++changed;
                        }
                    }
                    worst_changed = std::max(worst_changed, changed);
                    worst_mean_error = std::max(worst_mean_error, total_error / (size * size));
                }
                const auto stem = std::to_string(profile.shadow_resolution) + "-" + std::to_string(static_cast<int>(distance));
                expect(stbi_write_png((output / (stem + ".png")).string().c_str(), size, size, 4, reference.data(), size * 4) != 0,
                       "Shadow capture write failed");
                std::cout << stem << ": shadow pixels=" << shadow_pixels << ", worst changed pixels=" << worst_changed
                          << ", mean error=" << worst_mean_error << ", shadow chunks="
                          << shadow_stats.shadow_chunks_submitted << "/1024\n";
                expect(worst_changed <= 20U && worst_mean_error < 0.02,
                       "Walking/jumping made stationary world shadows wobble");
            }
        }
        scene.shutdown();
        bgfx::destroy(readback);
        bgfx::destroy(framebuffer);
        bgfx::destroy(depth);
        bgfx::destroy(colour);
        ui.shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        std::cout << "192 GPU shadow movement frames passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
