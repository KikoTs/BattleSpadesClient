#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

bgfx::ShaderHandle shader(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary | std::ios::ate};
    check(input.good(), "cannot open Vulkan UI shader");
    const auto length = input.tellg();
    check(length > 0 && length < 1024 * 1024, "invalid shader length");
    const auto* memory = bgfx::alloc(static_cast<std::uint32_t>(length));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(memory->data), static_cast<std::streamsize>(length));
    check(input.good(), "cannot read Vulkan UI shader");
    const auto handle = bgfx::createShader(memory);
    check(bgfx::isValid(handle), "cannot create Vulkan UI shader");
    return handle;
}
} // namespace

// Headless real-driver coverage: can run under Linux with Mesa lavapipe as well
// as a hardware Vulkan driver. No window system or BattleSpades assets needed.
int main(int argc, char** argv) {
    bool initialized{};
    try {
        check(argc == 2, "usage: aos_vulkan_descriptor_pressure_tests <shader-bin-root>");
        bgfx::Init init;
        init.type = bgfx::RendererType::Vulkan;
        // bgfx requires a zero-sized backbuffer for a headless device. The
        // actual render target below is an independent 64x64 framebuffer.
        init.resolution.width = 0;
        init.resolution.height = 0;
        initialized = bgfx::init(init);
        check(initialized, "cannot initialize headless Vulkan");
        const auto root = std::filesystem::path{argv[1]} / "spirv";
        const auto program = bgfx::createProgram(shader(root / "vs_ui.bin"),
            shader(root / "fs_ui.bin"), true);
        check(bgfx::isValid(program), "cannot create Vulkan UI program");
        const auto sampler = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
        check(bgfx::isValid(sampler), "cannot create texture sampler");
        struct Vertex { float x, y, z, u, v; std::uint32_t color; };
        constexpr std::array vertices{
            Vertex{-1, -1, 0, 0, 0, UINT32_MAX}, Vertex{1, -1, 0, 1, 0, UINT32_MAX},
            Vertex{1, 1, 0, 1, 1, UINT32_MAX}, Vertex{-1, 1, 0, 0, 1, UINT32_MAX}};
        constexpr std::array<std::uint16_t, 6> indices{0, 1, 2, 0, 2, 3};
        bgfx::VertexLayout layout;
        layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true).end();
        const auto vertex_buffer = bgfx::createVertexBuffer(bgfx::copy(vertices.data(), sizeof(vertices)), layout);
        const auto index_buffer = bgfx::createIndexBuffer(bgfx::copy(indices.data(), sizeof(indices)));
        check(bgfx::isValid(vertex_buffer) && bgfx::isValid(index_buffer), "cannot create quad");
        const auto color = bgfx::createTexture2D(64, 64, false, 1,
            bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT);
        const auto framebuffer = bgfx::createFrameBuffer(1, &color, true);
        const auto readback = bgfx::createTexture2D(64, 64, false, 1,
            bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
        check(bgfx::isValid(framebuffer) && bgfx::isValid(readback), "cannot create readback target");
        bgfx::setViewRect(0, 0, 0, 64, 64);
        bgfx::setViewMode(0, bgfx::ViewMode::Sequential);
        bgfx::setViewFrameBuffer(0, framebuffer);
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR, 0x000000ff);
        std::vector<bgfx::TextureHandle> textures;
        constexpr unsigned unique_count{3'200};
        for (unsigned index{}; index < unique_count; ++index) {
            const std::array<std::uint8_t, 4> pixel{index + 1 == unique_count ? std::uint8_t{255} :
                std::uint8_t{0}, index % 2 ? std::uint8_t{255} : std::uint8_t{0}, 0, 255};
            const auto texture = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                BGFX_SAMPLER_POINT, bgfx::copy(pixel.data(), 4));
            check(bgfx::isValid(texture), "cannot create pressure texture");
            textures.push_back(texture);
        }
        const auto draw = [&](bgfx::TextureHandle texture) {
            bgfx::setVertexBuffer(0, vertex_buffer);
            bgfx::setIndexBuffer(index_buffer);
            bgfx::setTexture(0, sampler, texture);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
            bgfx::submit(0, program);
        };
        const auto finish = [&](std::uint8_t expected_red) {
            bgfx::blit(1, readback, 0, 0, color);
            std::array<std::uint8_t, 64 * 64 * 4> pixels{};
            const auto ready = bgfx::readTexture(readback, pixels.data());
            bool complete{};
            for (unsigned frame{}; frame < 32; ++frame) {
                if (bgfx::frame() >= ready) { complete = true; break; }
            }
            check(complete, "readback timed out");
            const auto offset = (32 * 64 + 32) * 4;
            check(pixels[offset] == expected_red && pixels[offset + 1] == 255,
                  "descriptor pressure lost the last draw");
        };
        for (unsigned cycle{}; cycle < 8; ++cycle) {
            for (unsigned index{}; index < 4'096; ++index) draw(textures[index % 2]);
            finish(0);
            for (const auto texture : textures) draw(texture);
            finish(255);
        }
        for (const auto texture : textures) bgfx::destroy(texture);
        bgfx::destroy(readback);
        bgfx::destroy(framebuffer);
        bgfx::destroy(vertex_buffer);
        bgfx::destroy(index_buffer);
        bgfx::destroy(sampler);
        bgfx::destroy(program);
        bgfx::shutdown();
        initialized = false;
        std::cout << "Headless Vulkan descriptor reuse, overflow and deferred release passed\n";
        return 0;
    } catch (const std::exception& error) {
        if (initialized) bgfx::shutdown();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
