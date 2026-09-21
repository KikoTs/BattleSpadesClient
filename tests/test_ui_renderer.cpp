#include "battlespades/render/bgfx_ui_renderer.hpp"
#include "battlespades/render/render_views.hpp"
#include "battlespades/render/world_renderer.hpp"

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace battlespades;
constexpr std::uint16_t size{64U};
constexpr std::array<std::uint8_t, 4U> white{255U, 255U, 255U, 255U};
constexpr std::array<std::uint8_t, 4U> red{255U, 0U, 0U, 255U};
constexpr std::array<std::uint8_t, 4U> green{0U, 255U, 0U, 255U};

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error{message};
}

struct Window final {
    SDL_Window* value{SDL_CreateWindow("UI backend regression", size, size, SDL_WINDOW_HIDDEN)};
    Window() { expect(value != nullptr, SDL_GetError()); }
    ~Window() { SDL_DestroyWindow(value); }
};

struct FixtureDirectory final {
    std::filesystem::path path{std::filesystem::temp_directory_path() /
        ("battlespades-ui-render-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()))};
    FixtureDirectory() {
        std::filesystem::create_directories(path);
        expect(stbi_write_png((path / "white.png").string().c_str(), 1, 1, 4,
                             white.data(), 4) != 0, "could not write the test PNG");
    }
    ~FixtureDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

struct Capture final {
    bgfx::TextureHandle color{bgfx::createTexture2D(size, size, false, 1U,
        bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT)};
    bgfx::FrameBufferHandle framebuffer{bgfx::createFrameBuffer(1U, &color, false)};
    bgfx::TextureHandle readback{bgfx::createTexture2D(size, size, false, 1U,
        bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK)};

    Capture() {
        expect(bgfx::isValid(color) && bgfx::isValid(framebuffer) && bgfx::isValid(readback),
               "backend could not allocate the pixel capture target");
    }
    ~Capture() {
        for (const auto view : {render::backdrop_clear_view_id, render::ui_window_view_id,
                                render::ui_canvas_view_id})
            bgfx::setViewFrameBuffer(view, BGFX_INVALID_HANDLE);
        if (bgfx::isValid(readback)) bgfx::destroy(readback);
        if (bgfx::isValid(framebuffer)) bgfx::destroy(framebuffer);
        if (bgfx::isValid(color)) bgfx::destroy(color);
    }

    std::vector<std::uint8_t> finish(render::BgfxUiRenderer& renderer) {
        for (const auto view : {render::backdrop_clear_view_id, render::ui_window_view_id,
                                render::ui_canvas_view_id})
            bgfx::setViewFrameBuffer(view, framebuffer);
        bgfx::blit(render::ui_canvas_view_id + 1U, readback, 0U, 0U, color, 0U, 0U, size, size);
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size) * size * 4U);
        const auto ready = bgfx::readTexture(readback, pixels.data());
        expect(renderer.end_frame(), std::string{renderer.last_error()});
        bool completed{};
        for (unsigned frame{}; frame < 32U; ++frame) {
            if (bgfx::frame() >= ready) { completed = true; break; }
        }
        expect(completed, "GPU readback did not finish within 32 frames");
        return pixels;
    }
};

void expect_color(const std::vector<std::uint8_t>& pixels, unsigned x,
                  std::array<std::uint8_t, 4U> expected, std::string_view context) {
    const auto offset = (static_cast<std::size_t>(size / 2U) * size + x) * 4U;
    for (std::size_t channel{}; channel < expected.size(); ++channel) {
        if (std::abs(static_cast<int>(pixels[offset + channel]) - expected[channel]) > 2) {
            throw std::runtime_error{std::string{context} + ": channel " + std::to_string(channel) +
                " expected " + std::to_string(expected[channel]) + " got " +
                std::to_string(pixels[offset + channel])};
        }
    }
}

render::UiGeometry geometry(render::UiTexture texture, std::uint32_t color) {
    auto mesh = std::make_shared<render::UiGeometryData>();
    mesh->vertices = {{0, 0, 0, 0, 0, color}, {48, 0, 0, 1, 0, color},
                      {48, 64, 0, 1, 1, color}, {0, 64, 0, 0, 1, color}};
    mesh->indices = {0U, 1U, 2U, 0U, 2U, 3U};
    render::UiGeometry draw;
    draw.mesh = std::move(mesh);
    draw.texture = texture;
    return draw;
}

void test_backend(render::GraphicsBackend backend, const std::filesystem::path& assets) {
    Window window;
    render::BgfxUiRenderer renderer;
    render::BgfxUiRendererConfig config;
    config.native_window.window = SDL_GetPointerProperty(SDL_GetWindowProperties(window.value),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    config.asset_root = assets;
    config.shader_root = AOS_SHADER_BIN_ROOT;
    config.drawable_extent = {size, size};
    config.design_extent = {size, size};
    config.backend = backend;
    config.vertical_sync = false;
    const auto initialized = renderer.initialize(config);
    expect(initialized, std::string{renderer.last_error()});
    expect(renderer.active_backend() == backend, "bgfx did not select the requested backend");
    expect((bgfx::getCaps()->supported & (BGFX_CAPS_TEXTURE_BLIT | BGFX_CAPS_TEXTURE_READ_BACK)) ==
           (BGFX_CAPS_TEXTURE_BLIT | BGFX_CAPS_TEXTURE_READ_BACK), "backend lacks pixel readback");

    const auto runtime = renderer.create_texture_rgba8(white, {1U, 1U}, render::TextureFilter::nearest);
    expect(runtime.has_value(), std::string{renderer.last_error()});
    // Missing map/particle assets fail after shader/uniform allocation. Retry
    // must release every partial resource while preserving the existing UI
    // texture, especially handle zero: an uninitialized world slot must never
    // claim and destroy a texture owned by this renderer.
    const auto resource_counts = [] {
        const auto* stats = bgfx::getStats();
        return std::array<std::uint16_t, 6U>{stats->numVertexBuffers, stats->numIndexBuffers,
            stats->numPrograms, stats->numShaders, stats->numUniforms, stats->numTextures};
    };
    static_cast<void>(bgfx::frame());
    static_cast<void>(bgfx::frame());
    const auto resources_before_failure = resource_counts();
    {
        render::WorldRenderer failed_world;
        for (unsigned retry{}; retry < 3U; ++retry) {
            expect(!failed_world.initialize(config.shader_root, assets),
                   "World initialization unexpectedly accepted the incomplete fixture assets");
            expect(!failed_world.is_initialized() && !failed_world.last_error().empty(),
                   "Failed initialization lost its state or diagnostic");
            static_cast<void>(bgfx::frame());
            static_cast<void>(bgfx::frame());
            const auto resources_after_failure = resource_counts();
            if (resources_after_failure != resources_before_failure) {
                std::string counts;
                constexpr std::array names{"vertices", "indices", "programs", "shaders", "uniforms", "textures"};
                for (std::size_t index{}; index < names.size(); ++index)
                    counts += std::string{" "} + names[index] + "=" + std::to_string(resources_before_failure[index]) +
                        "->" + std::to_string(resources_after_failure[index]);
                throw std::runtime_error{"Failed world initialization changed GPU resources on retry " +
                    std::to_string(retry) + ":" + counts};
            }
        }
    }
    {
        Capture capture;
        expect(renderer.begin_frame(), std::string{renderer.last_error()});
        expect(renderer.draw(render::UiSprite{runtime->texture, {0, 0, 64, 64}}),
               "World rollback invalidated an existing UI texture");
        expect_color(capture.finish(renderer), 32U, white, "World rollback must preserve unrelated texture pixels");
    }
    const auto stale_after_restart = runtime->texture;
    const auto cached = renderer.load_texture("white.png", render::TextureFilter::nearest);
    const auto retained = renderer.load_texture("white.png", render::TextureFilter::nearest);
    expect(cached && retained && cached->texture == retained->texture, "PNG cache must retain one handle");
    expect(!renderer.update_texture_rgba8(cached->texture, red), "immutable cached PNG accepted an update");
    expect(renderer.release_texture(cached->texture), "first PNG reference could not be released");

    {
        Capture capture;
        expect(renderer.begin_frame(), std::string{renderer.last_error()});
        expect(renderer.draw(render::UiSprite{runtime->texture, {0, 0, 64, 64}, {}, {},
                                              {255U, 0U, 0U, 255U}}), "background sprite failed");
        expect(renderer.draw(geometry(runtime->texture, 0xffff0000U)), "blue markup failed");
        expect(renderer.draw(render::UiSprite{retained->texture, {16, 0, 16, 64}, {}, {},
                                              {0U, 255U, 0U, 255U}}), "foreground sprite failed");
        auto clipped = geometry(runtime->texture, 0xff00ffffU);
        clipped.clip = render::UiRect{32, 0, 8, 64};
        expect(renderer.draw(clipped), "clipped markup failed");
        auto invalid_clip = clipped;
        invalid_clip.clip->x = std::numeric_limits<float>::quiet_NaN();
        expect(!renderer.draw(invalid_clip), "non-finite markup clip reached bgfx");
        auto invalid_mesh = std::make_shared<render::UiGeometryData>(*clipped.mesh);
        invalid_mesh->vertices.front().x = std::numeric_limits<float>::infinity();
        auto invalid_draw = clipped;
        invalid_draw.mesh = invalid_mesh;
        expect(!renderer.draw(invalid_draw), "non-finite markup vertex reached bgfx");
        // Hidden/overflow-clipped RmlUi panels still submit retained geometry.
        // Their empty scissors must neither abort the frame nor paint over
        // the blue, green and yellow regions checked below.
        for (const auto empty : std::array{
                 render::UiRect{0, 0, 0, 64}, render::UiRect{0, 0, 48, 0},
                 render::UiRect{0, 0, 0, 0}, render::UiRect{0, 0, -1, 64},
                 render::UiRect{0, 0, 48, -1}}) {
            auto hidden = geometry(runtime->texture, 0xffff00ffU);
            hidden.clip = empty;
            expect(renderer.draw(hidden), "finite empty markup scissor aborted the frame");
        }
        invalid_clip.clip = render::UiRect{0, 0, 0, std::numeric_limits<float>::quiet_NaN()};
        expect(!renderer.draw(invalid_clip), "empty width concealed a non-finite markup scissor");
        const auto pixels = capture.finish(renderer);
        expect_color(pixels, 8U, {0U, 0U, 255U, 255U}, "markup before sprite");
        expect_color(pixels, 24U, green, "sprite queued after markup must remain on top");
        expect_color(pixels, 36U, {255U, 255U, 0U, 255U}, "markup scissor interior");
        expect_color(pixels, 44U, {0U, 0U, 255U, 255U}, "markup scissor exterior");
        expect_color(pixels, 56U, red, "background outside markup");

        for (const auto& update : {red, green}) {
            expect(renderer.update_texture_rgba8(runtime->texture, update), std::string{renderer.last_error()});
            expect(renderer.begin_frame(), std::string{renderer.last_error()});
            expect(renderer.draw(render::UiSprite{runtime->texture, {0, 0, 64, 64}}), "updated sprite failed");
            expect_color(capture.finish(renderer), 32U, update, "runtime texture upload must change pixels");
        }
        expect(!renderer.resize({65536U, size}), "oversized view extent was accepted");
        expect(renderer.resize({0U, 0U}), "minimize failed");
        expect(!renderer.begin_frame(), "minimized renderer started a frame");
        expect(renderer.set_presentation_options(false, 2U), "minimized presentation update failed");
        expect(renderer.resize({size, size}), "restoring the drawable failed");
        expect(renderer.set_presentation_options(false, 0U), "presentation reset failed");
        expect(renderer.begin_frame(), std::string{renderer.last_error()});
        expect(renderer.draw(render::UiSprite{runtime->texture, {0, 0, 64, 64}}), "restored sprite failed");
        expect_color(capture.finish(renderer), 32U, green, "reset must retain runtime textures");

        bgfx::VertexLayout ui_layout;
        ui_layout.begin(bgfx::getRendererType())
            .add(bgfx::Attrib::Position, 3U, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2U, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4U, bgfx::AttribType::Uint8, true).end();
        expect(renderer.begin_frame(), std::string{renderer.last_error()});
        const auto available = bgfx::getAvailTransientVertexBuffer(UINT32_MAX, ui_layout);
        expect(available > 4U && available < 999'999U, "Unexpected transient test budget");
        auto oversized_mesh = std::make_shared<render::UiGeometryData>();
        oversized_mesh->vertices.resize(static_cast<std::size_t>(available) + 1U);
        oversized_mesh->indices = {0U, 1U, 2U};
        auto overloaded = geometry(runtime->texture, 0xffffffffU);
        overloaded.mesh = oversized_mesh;
        expect(renderer.draw(overloaded), "Valid oversized markup was rejected before submission");
        expect(renderer.draw(render::UiSprite{runtime->texture, {0, 0, 64, 64}}),
               "Sprite after oversized markup was rejected");
        expect_color(capture.finish(renderer), 32U, green,
                     "One exhausted markup draw must not suppress later smaller UI draws");
        expect(renderer.last_frame_dropped_draws() == 1U, "Oversized markup drop was not counted");

        expect(renderer.begin_frame(), std::string{renderer.last_error()});
        bgfx::TransientVertexBuffer occupied{};
        bgfx::allocTransientVertexBuffer(&occupied,
            bgfx::getAvailTransientVertexBuffer(UINT32_MAX, ui_layout), ui_layout);
        render::ViewModelSprite flash;
        flash.texture = runtime->texture;
        flash.position = {0, 0, -1};
        flash.radius = 1;
        expect(renderer.draw(flash), "Transient pressure made a viewmodel effect fatal");
        expect(renderer.draw(render::UiSprite{runtime->texture, {0, 0, 64, 64}}),
               "Transient pressure rejected the queued sprite");
        static_cast<void>(capture.finish(renderer));
        expect(renderer.last_frame_dropped_draws() == 2U,
               "Exhausted sprite and viewmodel draws must each count once");
        expect(renderer.begin_frame(), std::string{renderer.last_error()});
        expect(renderer.draw(render::UiSprite{runtime->texture, {0, 0, 64, 64}}), "Recovery sprite failed");
        expect_color(capture.finish(renderer), 32U, green, "Transient pressure must recover next frame");
        expect(renderer.last_frame_dropped_draws() == 0U, "Drop count leaked into the recovered frame");
    }

    expect(renderer.release_texture(retained->texture), "last PNG reference could not be released");
    expect(!renderer.release_texture(retained->texture), "released PNG handle was accepted twice");
    world::ChunkMesh preview;
    preview.minimum = {-1, -1, 0}; preview.maximum = {1, 1, 0};
    preview.vertices = {{-1, -1, 0, 0x00ffffffU, 0U}, {1, -1, 0, 0x00ffffffU, 0U},
                        {0, 1, 0, 0x00ffffffU, 0U}};
    preview.indices = {0U, 1U, 99U};
    expect(!renderer.set_model_preview(preview), "out-of-range preview index was accepted");
    preview.indices.back() = 2U;
    const auto borrowed = renderer.set_model_preview(preview);
    expect(borrowed.has_value(), "model preview allocation failed");
    expect(!renderer.release_texture(borrowed->texture), "borrowed preview target was released by its caller");
    const std::vector<std::uint8_t> preview_pixels(640U * 320U * 4U);
    expect(!renderer.update_texture_rgba8(borrowed->texture, preview_pixels), "render target accepted a CPU update");
    expect(renderer.begin_frame(), std::string{renderer.last_error()});
    renderer.render_model_preview(0.0F, 0.0F, 1.0F);
    expect(renderer.draw(render::UiSprite{borrowed->texture, {0, 0, 64, 64}}), "borrowed target is no longer usable");
    expect(renderer.end_frame(), std::string{renderer.last_error()});

    renderer.shutdown();
    expect(renderer.initialize(config), "renderer could not reinitialize the same backend");
    auto replacement = renderer.create_texture_rgba8(white, {1U, 1U}, render::TextureFilter::nearest);
    expect(replacement.has_value(), "replacement texture creation failed");
    // Reuse the exact old slot, not merely a different free slot. Otherwise a
    // restart that preserves slots but forgets to advance their generations
    // could pass while the stale slot happens to remain empty.
    for (unsigned attempt{}; replacement->texture.index != stale_after_restart.index && attempt < 16U; ++attempt) {
        replacement = renderer.create_texture_rgba8(white, {1U, 1U}, render::TextureFilter::nearest);
        expect(replacement.has_value(), "replacement slot allocation failed");
    }
    expect(replacement->texture.index == stale_after_restart.index, "test did not reuse the pre-restart slot");
    expect(replacement->texture.generation != stale_after_restart.generation,
           "renderer restart did not invalidate the reused slot generation");
    expect(!renderer.update_texture_rgba8(stale_after_restart, red), "stale pre-restart texture updated a new resource");
    expect(!renderer.release_texture(stale_after_restart), "stale pre-restart texture released a new resource");
    expect(!renderer.release_texture(borrowed->texture), "borrowed pre-restart texture aliased a new resource");
    {
        Capture capture;
        expect(renderer.begin_frame(), std::string{renderer.last_error()});
        expect(!renderer.draw(render::UiSprite{stale_after_restart, {0, 0, 64, 64}}),
               "stale pre-restart handle was rendered");
        expect(renderer.draw(render::UiSprite{replacement->texture, {0, 0, 64, 64}}), "replacement texture could not be drawn");
        expect_color(capture.finish(renderer), 32U, white, "stale handle must not change the replacement texture");
    }
    renderer.shutdown();
    std::cout << render::graphics_backend_name(backend)
              << ": UI ordering, clipping, mutable uploads, reset, preview lifetime and restart passed\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        expect(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        FixtureDirectory assets;
        constexpr std::array candidates{
            std::pair{"d3d11", render::GraphicsBackend::direct3d11},
            std::pair{"d3d12", render::GraphicsBackend::direct3d12},
            std::pair{"vulkan", render::GraphicsBackend::vulkan},
            std::pair{"opengl", render::GraphicsBackend::opengl},
        };
        const std::string_view requested = argc > 1 ? argv[1] : "d3d11";
        unsigned tested{};
        for (const auto& [name, backend] : candidates) {
            if (requested == "all" || requested == name) {
                test_backend(backend, assets.path);
                ++tested;
            }
        }
        expect(tested != 0U, "usage: aos_ui_renderer_tests [d3d11|d3d12|vulkan|opengl|all]");
        SDL_Quit();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
