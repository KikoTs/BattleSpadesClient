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
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace battlespades;
constexpr std::uint16_t size = 256U;
void expect(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
void quad(world::ChunkMesh& mesh, float z, std::uint32_t color, float half = 12.0F,
          bool grain = false) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    constexpr std::array<std::array<float, 2U>, 4U> corners{{{-1,-1}, {-1,1}, {1,1}, {1,-1}}};
    constexpr std::array<std::uint8_t, 4U> noise{0U, 1U, 3U, 2U};
    for (std::size_t i{}; i < 4U; ++i) {
        world::ChunkVertex vertex{256.0F + corners[i][0] * half,
                                  256.0F + corners[i][1] * half, z, color, 4U};
        if (grain) {
            vertex.noise_corner = noise[i];
            vertex.static_light = 0xFF000000U;
            vertex.ao_u = vertex.edge_u = 0.375F;
            vertex.ao_v = vertex.edge_v = 0.625F;
        }
        mesh.vertices.push_back(vertex);
    }
    for (const auto i : {0U, 1U, 2U, 0U, 2U, 3U}) mesh.indices.push_back(base + i);
    mesh.minimum = {256.0F - half, 256.0F - half, 0.0F};
    mesh.maximum = {256.0F + half, 256.0F + half, 100.0F};
}
double deviation(const std::vector<std::uint8_t>& pixels, int radius) {
    double sum{}, square{};
    std::size_t count{};
    for (int y = size / 2 - radius; y < size / 2 + radius; ++y) {
        for (int x = size / 2 - radius; x < size / 2 + radius; ++x) {
            const double value = pixels[(static_cast<std::size_t>(y) * size + static_cast<std::size_t>(x)) * 4U];
            sum += value;
            square += value * value;
            ++count;
        }
    }
    const auto samples = static_cast<double>(count);
    return std::sqrt(std::max(0.0, square / samples - (sum / samples) * (sum / samples)));
}
}

int main(int argc, char** argv) {
    using namespace battlespades;
    try {
        expect(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        auto* window = SDL_CreateWindow("Placement rendering regression", size, size, SDL_WINDOW_HIDDEN);
        expect(window != nullptr, SDL_GetError());
        render::BgfxUiRenderer ui;
        render::BgfxUiRendererConfig config;
        config.native_window.window = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
            SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        config.asset_root = AOS_TEST_ASSET_ROOT;
        config.shader_root = AOS_SHADER_BIN_ROOT;
        config.drawable_extent = {size, size};
        config.backend = render::GraphicsBackend::direct3d11;
        config.vertical_sync = false;
        expect(ui.initialize(config), std::string{ui.last_error()});
        render::WorldRenderer scene;
        expect(scene.initialize(config.shader_root, config.asset_root), std::string{scene.last_error()});
        scene.set_model_culling(false); // views are overridden after submit
        auto profile = render::profile_for(settings::ShaderQuality::compatibility, settings::QualityLevel::high);
        scene.set_quality_profile(profile);
        world::MapAtmosphere atmosphere;
        atmosphere.fog_density = 0.0F;
        scene.set_atmosphere(atmosphere);
        world::ChunkMesh background, nearest, layered, reversed, foreground, grain;
        quad(background, 70, 0x00404040, 64);
        quad(nearest, 50, 0x000000FF);
        for (const float z : {60.0F, 58.0F, 56.0F, 50.0F}) quad(layered, z, 0x000000FF);
        for (const float z : {50.0F, 56.0F, 58.0F, 60.0F}) quad(reversed, z, 0x000000FF);
        quad(foreground, 40, 0x0000FF00, 6);
        quad(grain, 50, 0x00404040, 8, true);
        const std::array meshes{background, nearest, layered, reversed, foreground, grain};
        for (std::uint32_t slot{}; slot < meshes.size(); ++slot)
            expect(scene.set_world_model_mesh(slot, meshes[slot]), std::string{scene.last_error()});

        const auto color = bgfx::createTexture2D(size, size, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT);
        const auto depth = bgfx::createTexture2D(size, size, false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT_WRITE_ONLY);
        const std::array attachments{color, depth};
        const auto framebuffer = bgfx::createFrameBuffer(2, attachments.data(), false);
        const auto readback = bgfx::createTexture2D(size, size, false, 1, bgfx::TextureFormat::RGBA8,
                                                  BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
        expect(bgfx::isValid(framebuffer) && bgfx::isValid(readback), "Capture target failed");
        std::array<float, 16U> view{}, projection{};
        bx::mtxLookAt(view.data(), {256,256,0}, {256,256,64}, {0,-1,0}, bx::Handedness::Right);
        const auto capture = [&](std::span<const render::WorldModelDraw> draws, float half = 16.0F) {
            render::WorldCamera camera;
            camera.eye = {256,256,0};
            camera.fog_distance = 10000;
            bx::mtxOrtho(projection.data(), -half, half, -half, half, .1F, 100, 0,
                         bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
            expect(ui.begin_frame(), std::string{ui.last_error()});
            expect(scene.submit(camera, config.drawable_extent, {}, draws), std::string{scene.last_error()});
            for (const auto id : {render::backdrop_clear_view_id, render::world_view_id,
                                  render::view_model_view_id, render::ui_window_view_id, render::ui_canvas_view_id})
                bgfx::setViewFrameBuffer(id, framebuffer);
            bgfx::setViewTransform(render::world_view_id, view.data(), projection.data());
            bgfx::blit(render::ui_canvas_view_id + 1, readback, 0, 0, color, 0, 0, size, size);
            std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size) * size * 4U);
            const auto ready = bgfx::readTexture(readback, pixels.data());
            expect(ui.end_frame(), std::string{ui.last_error()});
            while (bgfx::frame() < ready) {}
            return pixels;
        };
        const auto ghost = [](std::uint32_t slot) { render::WorldModelDraw draw{slot}; draw.opacity = .4F; return draw; };
        const std::array single{render::WorldModelDraw{0}, ghost(1)};
        const std::array many{render::WorldModelDraw{0}, ghost(2)};
        const std::array reverse{render::WorldModelDraw{0}, ghost(3)};
        const auto reference = capture(single);
        const auto layers = capture(many);
        const auto reverse_layers = capture(reverse);
        const auto middle = (static_cast<std::size_t>(size / 2) * size + size / 2) * 4U;
        expect(reference[middle] > reference[middle + 1] + 40, "Ghost color is missing");
        expect(layers == reference && reverse_layers == reference,
               "Ghost rear faces accumulate alpha or depend on mesh order");
        const std::array occluded{render::WorldModelDraw{0}, ghost(2), render::WorldModelDraw{4}};
        const auto with_front = capture(occluded);
        expect(with_front[middle + 1] > 200 && with_front[middle] < 10,
               "Ghost ignores opaque geometry in front");
        auto invisible = ghost(2); invisible.opacity = 0;
        const std::array hidden{render::WorldModelDraw{0}, invisible};
        const std::array background_only{render::WorldModelDraw{0}};
        expect(capture(hidden) == capture(background_only), "Zero-opacity ghost changes the frame");
        const std::array texture{render::WorldModelDraw{0}, render::WorldModelDraw{5}};
        const auto near_grain = capture(texture);
        const auto far_grain = capture(texture, 128);
        const auto near_deviation = deviation(near_grain, 56);
        const auto far_deviation = deviation(far_grain, 6);
        std::cout << "grain standard deviation near=" << near_deviation << " far=" << far_deviation << '\n';
        // Legacy uses the original un-mipped blue channel, verified against
        // GL_LINEAR in vxl.pyd. Its distant grain must not be averaged away.
        expect(near_deviation > 0.5 && far_deviation > 0.5,
               "Original Legacy terrain grain disappeared");
        const std::filesystem::path output = argc > 1 ? argv[1] : "tmp/placement-render";
        std::filesystem::create_directories(output);
        for (const auto& [name, pixels] : std::array{
                 std::pair{"ghost-layered", &layers}, std::pair{"ghost-occluded", &with_front},
                 std::pair{"terrain-near", &near_grain}, std::pair{"terrain-far", &far_grain}})
            expect(stbi_write_png((output / (std::string{name} + ".png")).string().c_str(),
                                  size, size, 4, pixels->data(), size * 4) != 0, "Capture write failed");
        scene.shutdown();
        bgfx::destroy(readback); bgfx::destroy(framebuffer); bgfx::destroy(depth); bgfx::destroy(color);
        ui.shutdown(); SDL_DestroyWindow(window); SDL_Quit();
        std::cout << "Placement and terrain filtering render checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
