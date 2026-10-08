#include "battlespades/world/chunk_mesh.hpp"
#include "render/chunk_vertex_layout.hpp"

#include <bgfx/bgfx.h>

#if defined(__HAIKU__)
#include "battlespades/platform/sdl_window_module.hpp"
#include <bgfx/platform.h>
#endif

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using battlespades::world::ChunkVertex;

bool verbose = false;

void expect(bool value, const std::string& message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

/**
 * Routes bgfx's own diagnostics into the test output.
 *
 * A debug-configured bgfx turns a failed internal check into
 * `fatal(Fatal::DebugCheck)` and then breaks, which otherwise surfaces as a
 * silent non-zero exit with nothing on either stream. Printing it is the
 * difference between a diagnosable failure and a mystery. Traces are far too
 * chatty to print unconditionally, so they wait for `--verbose`.
 */
struct DiagnosticCallback final : public bgfx::CallbackI {
    ~DiagnosticCallback() override = default;

    void fatal(const char* path, std::uint16_t line, bgfx::Fatal::Enum code,
               const char* message) override {
        std::cerr << "bgfx fatal (" << path << ':' << line << ", code "
                  << static_cast<int>(code) << "): " << message << std::endl;
    }

    void traceVargs(const char* path, std::uint16_t line, const char* format,
                    std::va_list args) override {
        if (!verbose) {
            return;
        }
        std::array<char, 2048U> buffer{};
        std::vsnprintf(buffer.data(), buffer.size(), format, args);
        std::cerr << "bgfx trace (" << path << ':' << line << "): " << buffer.data();
    }

    void profilerBegin(const char*, std::uint32_t, const char*, std::uint16_t) override {}
    void profilerBeginLiteral(const char*, std::uint32_t, const char*, std::uint16_t) override {}
    void profilerEnd() override {}
    std::uint32_t cacheReadSize(std::uint64_t) override { return 0U; }
    bool cacheRead(std::uint64_t, void*, std::uint32_t) override { return false; }
    void cacheWrite(std::uint64_t, const void*, std::uint32_t) override {}
    void screenShot(const char*, std::uint32_t, std::uint32_t, std::uint32_t, const void*,
                    std::uint32_t, bool) override {}
    void captureBegin(std::uint32_t, std::uint32_t, std::uint32_t, bgfx::TextureFormat::Enum,
                      bool) override {}
    void captureEnd() override {}
    void captureFrame(const void*, std::uint32_t) override {}
};

/**
 * The Classic tables, duplicated from vs_world.sc on purpose.
 *
 * In Classic mode fs_world resolves to `albedo * v_shade`, so with pure white
 * albedo and fog disabled a readback pixel IS `face_shade * occlusion_shade`
 * scaled to a byte. Kept verbatim rather than shared with ChunkMesherConfig, so
 * that changing the shader without updating this test surfaces as a failure
 * instead of silently agreeing.
 */
constexpr std::array<float, 6U> kFaceShade{0.85F, 0.85F, 0.75F, 0.75F, 1.00F, 0.60F};
constexpr std::array<float, 4U> kOcclusionShade{1.00F, 0.80F, 0.65F, 0.50F};

/** One rendered band: a face index and a corner occlusion level to decode. */
struct Sample final {
    std::uint8_t face{};
    std::uint8_t occlusion{};
};

[[nodiscard]] std::uint8_t expected_shade(const Sample& sample) {
    const float shade = kFaceShade[sample.face] * kOcclusionShade[sample.occlusion];
    return static_cast<std::uint8_t>(shade * 255.0F + 0.5F);
}

[[nodiscard]] const bgfx::Memory* load_shader(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        throw std::runtime_error{"could not open shader " + path.string()};
    }
    const std::string bytes{std::istreambuf_iterator<char>{input}, {}};
    return bgfx::copy(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
}

constexpr std::array<float, 16U> kIdentity{1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
                                           0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};

/**
 * The shipped layout, but with Color1's normalization forced.
 *
 * Passing the production value must reproduce `chunk_vertex_layout()` exactly,
 * which main() asserts. That is what keeps this a guard on the real encoding
 * rather than a restatement of the test's own assumptions.
 */
[[nodiscard]] bgfx::VertexLayout layout_with_color1(bool normalized) {
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3U, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4U, bgfx::AttribType::Uint8, true)
        .add(bgfx::Attrib::Color1, 4U, bgfx::AttribType::Uint8, normalized)
        .add(bgfx::Attrib::Color2, 4U, bgfx::AttribType::Uint8, true)
        .add(bgfx::Attrib::TexCoord0, 4U, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 1U, bgfx::AttribType::Float)
        .end();
    return layout;
}

/**
 * Render one band per sample through the real vs_world/fs_world pair and read
 * the pixels back off the GPU.
 *
 * `normalized` selects how Color1 -- the attribute carrying the face index and
 * the corner occlusion level -- is declared. That single flag is the subject of
 * this test: the shader decodes both bytes with `* 255.0`, which is meaningful
 * only if the attribute arrives normalized.
 */
[[nodiscard]] std::vector<std::uint8_t> render_samples(bool normalized,
                                                       const std::vector<Sample>& samples,
                                                       const std::filesystem::path& shader_root) {
    const auto width = static_cast<std::uint16_t>(samples.size());
    constexpr std::uint16_t height = 1U;

    const auto layout = layout_with_color1(normalized);
    expect(battlespades::render::chunk_vertex_layout_matches_struct(layout),
           "layout stride " + std::to_string(layout.getStride()) +
               " must match sizeof(ChunkVertex) " + std::to_string(sizeof(ChunkVertex)));

    std::vector<ChunkVertex> vertices;
    std::vector<std::uint16_t> indices;
    for (std::size_t index{}; index < samples.size(); ++index) {
        const auto& sample = samples[index];
        const float x0 = -1.0F + 2.0F * static_cast<float>(index) / static_cast<float>(width);
        const float x1 = -1.0F + 2.0F * static_cast<float>(index + 1U) / static_cast<float>(width);
        // White albedo, and static_light 0 so the placed-block term is inert.
        constexpr std::uint32_t white = 0xFFFFFFFFU;
        const auto base = static_cast<std::uint16_t>(vertices.size());
        vertices.push_back({x0, -1.0F, 0.0F, white, sample.face, sample.occlusion, 0U, 0U, 0U});
        vertices.push_back({x1, -1.0F, 0.0F, white, sample.face, sample.occlusion, 0U, 0U, 0U});
        vertices.push_back({x1, 1.0F, 0.0F, white, sample.face, sample.occlusion, 0U, 0U, 0U});
        vertices.push_back({x0, 1.0F, 0.0F, white, sample.face, sample.occlusion, 0U, 0U, 0U});
        constexpr std::array<std::uint16_t, 6U> quad{0U, 1U, 2U, 0U, 2U, 3U};
        for (const std::uint16_t offset : quad) {
            indices.push_back(static_cast<std::uint16_t>(base + offset));
        }
    }

    const auto vertex_buffer = bgfx::createVertexBuffer(
        bgfx::copy(vertices.data(),
                   static_cast<std::uint32_t>(vertices.size() * sizeof(ChunkVertex))),
        layout);
    const auto index_buffer = bgfx::createIndexBuffer(bgfx::copy(
        indices.data(), static_cast<std::uint32_t>(indices.size() * sizeof(std::uint16_t))));

    const auto vertex_shader = bgfx::createShader(load_shader(shader_root / "vs_world.bin"));
    const auto fragment_shader = bgfx::createShader(load_shader(shader_root / "fs_world.bin"));
    const auto program = bgfx::createProgram(vertex_shader, fragment_shader, true);
    expect(bgfx::isValid(program), "world shader program must link");

    const auto colour = bgfx::createTexture2D(width, height, false, 1U,
                                              bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT);
    const auto readback =
        bgfx::createTexture2D(width, height, false, 1U, bgfx::TextureFormat::RGBA8,
                              BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    const auto framebuffer = bgfx::createFrameBuffer(1U, &colour, false);

    // fs_world declares the retail AO atlas in addition to the shadow and
    // skylight samplers. These vertices deliberately carry Color2 alpha zero,
    // so they exercise the Legacy KV6 fallback rather than terrain; binding a
    // real one-pixel atlas still keeps debug bgfx from rejecting slot zero.
    constexpr std::array<std::uint8_t, 4U> white_texel{255U, 255U, 255U, 255U};
    const auto retail_ao_texture = bgfx::createTexture2D(
        1U, 1U, false, 1U, bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
        bgfx::copy(white_texel.data(), static_cast<std::uint32_t>(white_texel.size())));
    const auto shadow_texture = bgfx::createTexture2D(
        1U, 1U, false, 1U, bgfx::TextureFormat::D16,
        BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
            BGFX_SAMPLER_COMPARE_LEQUAL);
    const auto skylight_texture =
        bgfx::createTexture2D(1U, 1U, false, 1U, bgfx::TextureFormat::R8,
                              BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    // OpenGL requires active samplers of different types to use distinct units,
    // even when this test's Classic branch does not sample the 3D volume.
    const auto emissive_texture = bgfx::createTexture3D(
        1U, 1U, 1U, false, bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP,
        bgfx::copy(white_texel.data(), static_cast<std::uint32_t>(white_texel.size())));
    const auto shadow_sampler = bgfx::createUniform("s_shadowMap", bgfx::UniformType::Sampler);
    const auto skylight_sampler = bgfx::createUniform("s_skylight", bgfx::UniformType::Sampler);
    const auto emissive_sampler = bgfx::createUniform("s_emissiveVolume", bgfx::UniformType::Sampler);
    const auto retail_ao_sampler =
        bgfx::createUniform("s_retailAo", bgfx::UniformType::Sampler);
    const auto retail_noise_sampler =
        bgfx::createUniform("s_retailNoise", bgfx::UniformType::Sampler);

    const auto light_params = bgfx::createUniform("u_lightParams", bgfx::UniformType::Vec4);
    const auto fog_params = bgfx::createUniform("u_fogParams", bgfx::UniformType::Vec4);
    const auto fog_curve = bgfx::createUniform("u_fogCurve", bgfx::UniformType::Vec4);
    const auto fog_horizon = bgfx::createUniform("u_fogHorizon", bgfx::UniformType::Vec4);
    const auto camera = bgfx::createUniform("u_cameraPosition", bgfx::UniformType::Vec4);
    const auto shadow_matrix = bgfx::createUniform("u_shadowMatrix", bgfx::UniformType::Mat4);
    const auto shadow_params = bgfx::createUniform("u_shadowParams", bgfx::UniformType::Vec4);

    // Classic shading, and a fog distance far enough away that the fog term is
    // exactly zero over this unit-cube geometry.
    constexpr std::array<float, 4U> kClassic{1.0F, 0.0F, 0.0F, 0.0F};
    constexpr std::array<float, 4U> kFogDisabled{0.0F, 0.0F, 0.0F, 1.0e6F};
    constexpr std::array<float, 4U> kZero{0.0F, 0.0F, 0.0F, 0.0F};

    bgfx::setViewFrameBuffer(0U, framebuffer);
    bgfx::setViewRect(0U, 0U, 0U, width, height);
    bgfx::setViewClear(0U, BGFX_CLEAR_COLOR, 0x000000FFU);
    bgfx::setViewTransform(0U, kIdentity.data(), kIdentity.data());
    bgfx::touch(0U);

    bgfx::setUniform(light_params, kClassic.data());
    bgfx::setUniform(fog_params, kFogDisabled.data());
    bgfx::setUniform(fog_curve, kZero.data());
    bgfx::setUniform(fog_horizon, kZero.data());
    bgfx::setUniform(camera, kZero.data());
    bgfx::setUniform(shadow_matrix, kIdentity.data());
    bgfx::setUniform(shadow_params, kZero.data());
    bgfx::setTexture(0U, retail_ao_sampler, retail_ao_texture);
    bgfx::setTexture(4U, retail_noise_sampler, retail_ao_texture);
    bgfx::setTexture(1U, shadow_sampler, shadow_texture);
    bgfx::setTexture(2U, skylight_sampler, skylight_texture);
    bgfx::setTexture(3U, emissive_sampler, emissive_texture);
    bgfx::setTransform(kIdentity.data());
    bgfx::setVertexBuffer(0U, vertex_buffer);
    bgfx::setIndexBuffer(index_buffer);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(0U, program);

    // The blit view needs the same framebuffer: a view with none targets the
    // backbuffer, and headless mode has no backbuffer.
    bgfx::setViewFrameBuffer(1U, framebuffer);
    bgfx::setViewRect(1U, 0U, 0U, width, height);
    bgfx::blit(1U, readback, 0U, 0U, colour, 0U, 0U, width, height);

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4U);
    const auto ready = bgfx::readTexture(readback, pixels.data());
    while (bgfx::frame() < ready) {
    }

    std::vector<std::uint8_t> shades;
    shades.reserve(samples.size());
    for (std::size_t index{}; index < samples.size(); ++index) {
        shades.push_back(pixels[index * 4U]);
    }

    bgfx::destroy(framebuffer);
    bgfx::destroy(readback);
    bgfx::destroy(colour);
    bgfx::destroy(program);
    bgfx::destroy(vertex_buffer);
    bgfx::destroy(index_buffer);
    bgfx::destroy(shadow_sampler);
    bgfx::destroy(skylight_sampler);
    bgfx::destroy(emissive_sampler);
    bgfx::destroy(retail_ao_sampler);
    bgfx::destroy(retail_noise_sampler);
    bgfx::destroy(retail_ao_texture);
    bgfx::destroy(shadow_texture);
    bgfx::destroy(skylight_texture);
    bgfx::destroy(emissive_texture);
    bgfx::destroy(light_params);
    bgfx::destroy(fog_params);
    bgfx::destroy(fog_curve);
    bgfx::destroy(fog_horizon);
    bgfx::destroy(camera);
    bgfx::destroy(shadow_matrix);
    bgfx::destroy(shadow_params);
    return shades;
}

void report(const std::string& label, const std::vector<std::uint8_t>& shades) {
    std::cout << label << ": ";
    for (const auto shade : shades) {
        std::cout << static_cast<int>(shade) << ' ';
    }
    std::cout << '\n';
}

void check(const std::string& label, const std::vector<Sample>& samples,
           const std::vector<std::uint8_t>& shades) {
    report(label, shades);
    for (std::size_t index{}; index < samples.size(); ++index) {
        const auto expected = static_cast<int>(expected_shade(samples[index]));
        const auto actual = static_cast<int>(shades[index]);
        expect(actual >= expected - 2 && actual <= expected + 2,
               label + " sample " + std::to_string(index) + " shaded " + std::to_string(actual) +
                   ", expected " + std::to_string(expected));
    }
}

/** The six exposed-face directions, all unoccluded. */
[[nodiscard]] std::vector<Sample> face_samples() {
    return {{0U, 0U}, {1U, 0U}, {2U, 0U}, {3U, 0U}, {4U, 0U}, {5U, 0U}};
}

/** One face at each corner occlusion level. */
[[nodiscard]] std::vector<Sample> occlusion_samples() {
    return {{4U, 0U}, {4U, 1U}, {4U, 2U}, {4U, 3U}};
}

/**
 * Everything that can be checked without a GPU.
 *
 * Deliberately ahead of `bgfx::init`, so the encoding is still guarded on a
 * machine with no Direct3D 11 device where the render probe can only skip.
 */
void check_layout_encoding() {
    const auto shipped = battlespades::render::chunk_vertex_layout();
    expect(battlespades::render::chunk_vertex_layout_matches_struct(shipped),
           "shipped layout stride " + std::to_string(shipped.getStride()) +
               " must match sizeof(ChunkVertex) " + std::to_string(sizeof(ChunkVertex)));
    // Pin Color1 to normalized. vs_world.sc decodes both of its meaningful bytes
    // with `* 255.0`, which is only correct for a normalized attribute; the
    // render probe below measures what the alternative actually produces.
    expect(shipped.m_hash == layout_with_color1(true).m_hash,
           "the shipped chunk vertex layout must declare Color1 normalized");
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::string mode = argc > 1 ? argv[1] : "";
        verbose = mode == "--verbose";

        check_layout_encoding();

        const std::filesystem::path shader_root{AOS_SHADER_BIN_ROOT};
        static DiagnosticCallback callback;
        bgfx::Init init;
        init.callback = &callback;
#if defined(__HAIKU__)
        // Haiku's OpenGL Kit needs a real BGLView, including offscreen draws.
        battlespades::platform::SdlWindowModule window{
            battlespades::platform::SdlWindowConfig{
                .title = "BattleSpades Haiku world shader test",
                .initial_extent = {640U, 480U},
            }};
        const bool started = window.start();
        expect(started, "SDL: " + std::string{window.last_error()});
        init.platformData.context = window.native_handle().graphics_context;
        init.type = bgfx::RendererType::OpenGL;
        init.resolution.width = 640U;
        init.resolution.height = 480U;
        const auto expected_backend = bgfx::RendererType::OpenGL;
        const auto backend = shader_root / "glsl";
        static_cast<void>(bgfx::renderFrame());
#else
        // Headless: bgfx treats a null window handle as such, and then requires
        // a 0x0 backbuffer. Rendering goes to an offscreen framebuffer instead,
        // so this needs a GPU but never a desktop.
        init.type = bgfx::RendererType::Direct3D11;
        init.resolution.width = 0U;
        init.resolution.height = 0U;
        const auto expected_backend = bgfx::RendererType::Direct3D11;
        const auto backend = shader_root / "dx11";
#endif
        if (!bgfx::init(init)) {
#if defined(__HAIKU__)
            throw std::runtime_error{"Haiku OpenGL initialization failed"};
#else
            std::cout << "SKIP: no Direct3D11 device for the render probe "
                         "(layout encoding still checked)\n";
            return 0;
#endif
        }
        // bgfx can fall back instead of failing. The probe's shader binaries
        // must match the selected backend or the readback is meaningless.
        if (bgfx::getRendererType() != expected_backend) {
#if defined(__HAIKU__)
            bgfx::shutdown();
            throw std::runtime_error{"Haiku must use the OpenGL shader variant"};
#else
            std::cout << "SKIP: bgfx selected " << bgfx::getRendererName(bgfx::getRendererType())
                      << ", not " << bgfx::getRendererName(expected_backend)
                      << " (layout encoding still checked)\n";
            bgfx::shutdown();
            return 0;
#endif
        }

        const auto* caps = bgfx::getCaps();
        if ((caps->supported & BGFX_CAPS_TEXTURE_READ_BACK) == 0U ||
            (caps->supported & BGFX_CAPS_TEXTURE_BLIT) == 0U) {
#if defined(__HAIKU__)
            bgfx::shutdown();
            throw std::runtime_error{"Haiku OpenGL cannot blit or read textures back"};
#else
            std::cout << "SKIP: device cannot blit or read textures back\n";
            bgfx::shutdown();
            return 0;
#endif
        }

        if (mode == "--probe") {
            // Informational: render both encodings so the difference between
            // them is a measurement rather than an argument.
            report("faces     normalized=true ", render_samples(true, face_samples(), backend));
            report("faces     normalized=false", render_samples(false, face_samples(), backend));
            report("occlusion normalized=true ", render_samples(true, occlusion_samples(), backend));
            report("occlusion normalized=false",
                   render_samples(false, occlusion_samples(), backend));
            bgfx::shutdown();
            return 0;
        }

        const auto faces = face_samples();
        const auto face_shades = render_samples(true, faces, backend);
        check("faces", faces, face_shades);
        // The whole point of the attribute: the six directions must not collapse
        // onto one shade, which is exactly what a non-normalized Color1 gives.
        expect(face_shades[4U] != face_shades[5U] && face_shades[0U] != face_shades[2U],
               "face shading must vary per face");

        const auto occlusions = occlusion_samples();
        const auto occlusion_shades = render_samples(true, occlusions, backend);
        check("occlusion", occlusions, occlusion_shades);
        expect(occlusion_shades.front() != occlusion_shades.back(),
               "corner occlusion must vary per level");

        bgfx::shutdown();
        std::cout << "world vertex layout tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "world vertex layout tests failed: " << error.what() << '\n';
        return 1;
    }
}
