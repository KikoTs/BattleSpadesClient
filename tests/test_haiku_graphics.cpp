// Uses only project-owned shaders and generated pixels; no retail assets.
#include "battlespades/platform/sdl_window_module.hpp"
#include "battlespades/render/bgfx_ui_renderer.hpp"

#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error{message};
}
}

int main() {
    namespace platform = battlespades::platform;
    namespace render = battlespades::render;
    try {
        // Recreate both SDL's context and bgfx to catch stale borrowed handles.
        for (int iteration{}; iteration < 2; ++iteration) {
            platform::SdlWindowModule window{platform::SdlWindowConfig{
                .title = "BattleSpades Haiku graphics test",
                .initial_extent = {640U, 480U},
                .minimum_extent = {320U, 240U},
            }};
            const auto check_window = [&window](bool success) {
                require(success, "SDL: " + std::string{window.last_error()});
            };
            check_window(window.start());
            const auto native = window.native_handle();
            require(native.system == platform::NativeWindowSystem::haiku && native.valid(),
                    "SDL must provide a native Haiku BGLView context");
            render::BgfxUiRenderer renderer;
            const auto check_renderer = [&renderer](bool success) {
                require(success, "bgfx: " + std::string{renderer.last_error()});
            };
            const auto extent = window.drawable_extent();
            render::BgfxUiRendererConfig config{
                .native_window = {.graphics_context = native.graphics_context},
                .drawable_extent = {extent.width, extent.height},
                .design_extent = {640U, 480U},
                .asset_root = std::filesystem::current_path(),
                .shader_root = AOS_SHADER_BIN_ROOT,
                .backend = render::GraphicsBackend::opengl,
            };
            check_renderer(renderer.initialize(config));
            const std::array<std::uint8_t, 4> green{0U, 255U, 0U, 255U};
            const auto texture = renderer.create_texture_rgba8(green, {1U, 1U}, render::TextureFilter::nearest);
            require(texture.has_value(), std::string{renderer.last_error()});
            for (const auto size : {platform::WindowExtent{640U, 480U}, platform::WindowExtent{800U, 600U}}) {
                check_window(window.apply_display_mode(size, false));
                static_cast<void>(window.tick({}));
                const auto drawable = window.drawable_extent();
                check_renderer(renderer.resize({drawable.width, drawable.height}));
                bool captured{};
                for (int frame{}; frame < 16 && !captured; ++frame) {
                    static_cast<void>(window.tick({}));
                    check_renderer(renderer.begin_frame());
                    check_renderer(renderer.draw(render::UiSprite{texture->texture, {0, 0, 640, 480}}));
                    if (frame == 2) require(renderer.request_backbuffer_capture(), "capture request");
                    check_renderer(renderer.end_frame());
                    if (const auto pixels = renderer.take_backbuffer_capture()) {
                        require(pixels->width == drawable.width && pixels->height == drawable.height,
                                "capture must follow the resized drawable");
                        const auto offset = (static_cast<std::size_t>(pixels->height / 2U) * pixels->width +
                                             pixels->width / 2U) * 4U;
                        require(pixels->rgba.at(offset) < 8U && pixels->rgba.at(offset + 1U) > 247U &&
                                    pixels->rgba.at(offset + 2U) < 8U,
                                "the Haiku OpenGL backbuffer must contain the generated green sprite");
                        captured = true;
                    }
                }
                require(captured, "Haiku OpenGL did not return a screenshot within 16 frames");
            }
            renderer.shutdown();
            window.stop();
        }
        std::cout << "Haiku OpenGL pixels, resize and context lifecycle passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
