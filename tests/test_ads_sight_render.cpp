// Where the aimed sight actually lands on screen, measured off a real GPU.
//
// docs/ADS_SIGHT_PLACEMENT.md recovers Character.draw_sight's transform, and
// tests/test_ads_sight_pin.cpp checks that transform arithmetically. Neither
// answers the only question a player asks -- "is it centred?" -- because that
// depends on the KV6 mesher's voxel convention as much as on the constants.
// This renders the aimed viewmodel exactly as world_renderer.cpp does, through
// the shipped shaders into an offscreen target, and reads the pixels back.
//
// Headless: bgfx with a null window handle needs a GPU but never a desktop.
// See tests/test_world_vertex_layout.cpp for the same setup.

#include "battlespades/world/chunk_mesh.hpp"
#include "battlespades/world/retail_view_model.hpp"
#include "battlespades/world/weapon_catalog.hpp"
#include "battlespades/world/weapon_models.hpp"
#include "render/chunk_vertex_layout.hpp"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using battlespades::world::ChunkMesh;
using battlespades::world::ChunkVertex;
using battlespades::world::RetailSightPose;
using battlespades::world::evaluate_weapon_sight;
using battlespades::world::find_weapon_definition;
using battlespades::world::load_weapon_models;

bool verbose = false;

void expect(bool value, const std::string& message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

// ---------------------------------------------------------------------------
// The transform, duplicated from native_frontend_module.cpp on purpose.
//
// Those helpers are private to the frontend translation unit. Restating them
// here means a change to the real chain that this copy does not follow shows
// up as a measurement that stops matching the game, which is the point.
// ---------------------------------------------------------------------------

using Mat4 = std::array<float, 16U>;

[[nodiscard]] Mat4 mat_identity() {
    return {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
}

[[nodiscard]] Mat4 mat_mul(const Mat4& a, const Mat4& b) {
    Mat4 result{};
    for (std::size_t row{}; row < 4U; ++row) {
        for (std::size_t column{}; column < 4U; ++column) {
            float sum{};
            for (std::size_t k{}; k < 4U; ++k) {
                sum += a[row * 4U + k] * b[k * 4U + column];
            }
            result[row * 4U + column] = sum;
        }
    }
    return result;
}

[[nodiscard]] Mat4 mat_translate(float x, float y, float z) {
    auto result = mat_identity();
    result[12U] = x;
    result[13U] = y;
    result[14U] = z;
    return result;
}

[[nodiscard]] Mat4 mat_scale(float scale) {
    auto result = mat_identity();
    result[0U] = scale;
    result[5U] = scale;
    result[10U] = scale;
    return result;
}

[[nodiscard]] Mat4 mat_rotate_y(float degrees) {
    const auto radians = static_cast<float>(degrees * std::numbers::pi / 180.0);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    auto result = mat_identity();
    result[0U] = c;
    result[2U] = -s;
    result[8U] = s;
    result[10U] = c;
    return result;
}

/** native_frontend_module.cpp:6675-6679, the aimed sight branch. */
[[nodiscard]] Mat4 sight_matrix(const RetailSightPose& sight) {
    Mat4 model = mat_scale(static_cast<float>(sight.model_scale));
    model = mat_mul(model, mat_translate(static_cast<float>(sight.position.x),
                                         static_cast<float>(sight.position.y),
                                         static_cast<float>(sight.position.z)));
    return mat_mul(model, mat_rotate_y(static_cast<float>(sight.yaw_degrees)));
}

/** native_frontend_module.cpp:5481, Character.draw_sight's second model. */
[[nodiscard]] Mat4 pin_matrix(const RetailSightPose& sight) {
    Mat4 pin = mat_scale(static_cast<float>(sight.pin_scale));
    pin = mat_mul(pin, mat_translate(static_cast<float>(sight.pin_position.x),
                                     static_cast<float>(sight.pin_position.y),
                                     static_cast<float>(sight.pin_position.z)));
    return mat_mul(pin, mat_rotate_y(static_cast<float>(sight.yaw_degrees)));
}

// ---------------------------------------------------------------------------
// Offscreen render
// ---------------------------------------------------------------------------

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

[[nodiscard]] const bgfx::Memory* load_shader(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        throw std::runtime_error{"could not open shader " + path.string()};
    }
    const std::string bytes{std::istreambuf_iterator<char>{input}, {}};
    return bgfx::copy(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
}

struct DrawItem final {
    const ChunkMesh* mesh{};
    Mat4 transform{};
};

/** One rendered frame, RGBA8, row 0 at the top. Alpha 0 means background. */
struct Frame final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> pixels;

    [[nodiscard]] const std::uint8_t* at(std::uint32_t x, std::uint32_t y) const {
        return pixels.data() + (static_cast<std::size_t>(y) * width + x) * 4U;
    }
};

/**
 * Render `draws` through the shipped vs_world/fs_world pair with the exact
 * viewmodel view set up by world_renderer.cpp:1874-1883: identity view matrix,
 * symmetric projection, near 0.01 and far 8.0, right handed.
 */
[[nodiscard]] Frame render(const std::vector<DrawItem>& draws, std::uint32_t width,
                           std::uint32_t height, double fov_y_degrees,
                           const std::filesystem::path& shader_root) {
    const auto w16 = static_cast<std::uint16_t>(width);
    const auto h16 = static_cast<std::uint16_t>(height);
    const auto layout = battlespades::render::chunk_vertex_layout();

    const auto vertex_shader = bgfx::createShader(load_shader(shader_root / "vs_world.bin"));
    const auto fragment_shader = bgfx::createShader(load_shader(shader_root / "fs_world.bin"));
    const auto program = bgfx::createProgram(vertex_shader, fragment_shader, true);
    expect(bgfx::isValid(program), "world shader program must link");

    const auto colour = bgfx::createTexture2D(w16, h16, false, 1U,
                                              bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT);
    const auto depth = bgfx::createTexture2D(w16, h16, false, 1U, bgfx::TextureFormat::D24S8,
                                             BGFX_TEXTURE_RT);
    const auto readback =
        bgfx::createTexture2D(w16, h16, false, 1U, bgfx::TextureFormat::RGBA8,
                              BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    const std::array<bgfx::TextureHandle, 2U> attachments{colour, depth};
    const auto framebuffer =
        bgfx::createFrameBuffer(static_cast<std::uint8_t>(attachments.size()),
                                attachments.data(), false);

    // Every sampler fs_world declares needs a real texture bound even when the
    // Classic branch reads none: a debug-configured bgfx aborts on the render
    // thread otherwise, with nothing on either stream.
    const auto shadow_texture = bgfx::createTexture2D(
        1U, 1U, false, 1U, bgfx::TextureFormat::D16,
        BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
            BGFX_SAMPLER_COMPARE_LEQUAL);
    const auto skylight_texture =
        bgfx::createTexture2D(1U, 1U, false, 1U, bgfx::TextureFormat::R8,
                              BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    const auto emissive_texture =
        bgfx::createTexture3D(1U, 1U, 1U, false, bgfx::TextureFormat::RGBA8,
                              BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
                                  BGFX_SAMPLER_W_CLAMP);
    const auto shadow_sampler = bgfx::createUniform("s_shadowMap", bgfx::UniformType::Sampler);
    const auto skylight_sampler = bgfx::createUniform("s_skylight", bgfx::UniformType::Sampler);
    const auto emissive_sampler =
        bgfx::createUniform("s_emissiveVolume", bgfx::UniformType::Sampler);

    const auto light_params = bgfx::createUniform("u_lightParams", bgfx::UniformType::Vec4);
    const auto fog_params = bgfx::createUniform("u_fogParams", bgfx::UniformType::Vec4);
    const auto fog_curve = bgfx::createUniform("u_fogCurve", bgfx::UniformType::Vec4);
    const auto fog_horizon = bgfx::createUniform("u_fogHorizon", bgfx::UniformType::Vec4);
    const auto camera = bgfx::createUniform("u_cameraPosition", bgfx::UniformType::Vec4);
    const auto shadow_matrix = bgfx::createUniform("u_shadowMatrix", bgfx::UniformType::Mat4);
    const auto shadow_params = bgfx::createUniform("u_shadowParams", bgfx::UniformType::Vec4);
    const auto emissive_params =
        bgfx::createUniform("u_emissiveParams", bgfx::UniformType::Vec4);
    const auto indirect_params =
        bgfx::createUniform("u_indirectParams", bgfx::UniformType::Vec4);
    const auto model_opacity =
        bgfx::createUniform("u_modelOpacity", bgfx::UniformType::Vec4);

    // Classic shading: fs_world collapses to `albedo * v_shade`, the retail
    // face table. Fog is pushed past the far plane so it contributes zero.
    constexpr std::array<float, 4U> kClassic{1.0F, 1.0F, 0.0F, 1.0F};
    constexpr std::array<float, 4U> kFogDisabled{0.0F, 0.0F, 0.0F, 1.0e6F};
    constexpr std::array<float, 4U> kZero{0.0F, 0.0F, 0.0F, 0.0F};
    constexpr std::array<float, 4U> kOpaque{1.0F, 0.0F, 0.0F, 0.0F};
    const auto identity = mat_identity();

    std::array<float, 16U> projection{};
    bx::mtxProj(projection.data(), static_cast<float>(fov_y_degrees),
                static_cast<float>(width) / static_cast<float>(height), 0.01F, 8.0F,
                bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);

    bgfx::setViewFrameBuffer(0U, framebuffer);
    bgfx::setViewRect(0U, 0U, 0U, w16, h16);
    // Alpha 0 marks background: fs_world writes a literal 1.0 there for every
    // fragment it produces, so coverage is exact and needs no colour key.
    bgfx::setViewClear(0U, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x00000000U, 1.0F, 0U);
    bgfx::setViewMode(0U, bgfx::ViewMode::Sequential);
    bgfx::setViewTransform(0U, identity.data(), projection.data());
    bgfx::touch(0U);

    std::vector<bgfx::VertexBufferHandle> vertex_buffers;
    std::vector<bgfx::IndexBufferHandle> index_buffers;
    for (const auto& draw : draws) {
        const auto& mesh = *draw.mesh;
        if (mesh.vertices.empty() || mesh.indices.empty()) {
            continue;
        }
        const auto vertex_buffer = bgfx::createVertexBuffer(
            bgfx::copy(mesh.vertices.data(),
                       static_cast<std::uint32_t>(mesh.vertices.size() * sizeof(ChunkVertex))),
            layout);
        const auto index_buffer = bgfx::createIndexBuffer(
            bgfx::copy(mesh.indices.data(),
                       static_cast<std::uint32_t>(mesh.indices.size() * sizeof(std::uint32_t))),
            BGFX_BUFFER_INDEX32);
        vertex_buffers.push_back(vertex_buffer);
        index_buffers.push_back(index_buffer);

        bgfx::setTransform(draw.transform.data());
        bgfx::setVertexBuffer(0U, vertex_buffer);
        bgfx::setIndexBuffer(index_buffer);
        bgfx::setUniform(light_params, kClassic.data());
        bgfx::setUniform(fog_params, kFogDisabled.data());
        bgfx::setUniform(fog_curve, kZero.data());
        bgfx::setUniform(fog_horizon, kZero.data());
        bgfx::setUniform(camera, kZero.data());
        bgfx::setUniform(shadow_matrix, identity.data());
        bgfx::setUniform(shadow_params, kZero.data());
        bgfx::setUniform(emissive_params, kZero.data());
        bgfx::setUniform(indirect_params, kZero.data());
        bgfx::setUniform(model_opacity, kOpaque.data());
        bgfx::setTexture(1U, shadow_sampler, shadow_texture);
        bgfx::setTexture(2U, skylight_sampler, skylight_texture);
        bgfx::setTexture(3U, emissive_sampler, emissive_texture);
        // world_renderer.cpp:1911-1913, minus MSAA so a pixel is either covered
        // or not and the centroid is not smeared by resolve.
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                       BGFX_STATE_DEPTH_TEST_LESS);
        bgfx::submit(0U, program);
    }

    bgfx::setViewFrameBuffer(1U, framebuffer);
    bgfx::setViewRect(1U, 0U, 0U, w16, h16);
    bgfx::blit(1U, readback, 0U, 0U, colour, 0U, 0U, w16, h16);

    Frame frame;
    frame.width = width;
    frame.height = height;
    frame.pixels.resize(static_cast<std::size_t>(width) * height * 4U);
    const auto ready = bgfx::readTexture(readback, frame.pixels.data());
    while (bgfx::frame() < ready) {
    }

    for (const auto handle : vertex_buffers) {
        bgfx::destroy(handle);
    }
    for (const auto handle : index_buffers) {
        bgfx::destroy(handle);
    }
    bgfx::destroy(framebuffer);
    bgfx::destroy(readback);
    bgfx::destroy(colour);
    bgfx::destroy(depth);
    bgfx::destroy(program);
    bgfx::destroy(shadow_sampler);
    bgfx::destroy(skylight_sampler);
    bgfx::destroy(emissive_sampler);
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
    bgfx::destroy(emissive_params);
    bgfx::destroy(indirect_params);
    bgfx::destroy(model_opacity);
    return frame;
}

// ---------------------------------------------------------------------------
// Measurement
// ---------------------------------------------------------------------------

/**
 * Signed screen statistics, in percent of the frame's own width and height,
 * measured from the exact centre. Positive x is right, positive y is up.
 */
struct Coverage final {
    std::size_t pixels{};
    double centroid_x{};
    double centroid_y{};
    double min_x{};
    double max_x{};
    double min_y{};
    double max_y{};
};

/** Pixel centres sit at index + 0.5; the optical axis sits at extent / 2. */
[[nodiscard]] double horizontal_percent(std::uint32_t x, std::uint32_t width) {
    const double centre = static_cast<double>(width) * 0.5;
    return (static_cast<double>(x) + 0.5 - centre) / static_cast<double>(width) * 100.0;
}

[[nodiscard]] double vertical_percent(std::uint32_t y, std::uint32_t height) {
    const double centre = static_cast<double>(height) * 0.5;
    return (centre - (static_cast<double>(y) + 0.5)) / static_cast<double>(height) * 100.0;
}

using PixelFilter = bool (*)(const std::uint8_t*);

[[nodiscard]] bool covered(const std::uint8_t* pixel) { return pixel[3U] != 0U; }

/**
 * The hole you actually aim through.
 *
 * The `sight_pos.z = -1.85` family cancels character.pyx:2081 exactly, which
 * centres a 1.45-unit hollow tube on the eye point; it fills the frame and the
 * only thing that reads as an aim mark is the gap in the middle. For those,
 * background IS the measurement.
 */
[[nodiscard]] bool uncovered(const std::uint8_t* pixel) { return pixel[3U] == 0U; }

/**
 * The pin's red tip, #78181C, after Classic shading has scaled it.
 *
 * Every other voxel in semi_sight_pin.kv6 is neutral grey, so "red dominates
 * both other channels by better than 2x" separates the bead from the post
 * regardless of which face shade the pixel came from.
 */
[[nodiscard]] bool red_bead(const std::uint8_t* pixel) {
    if (pixel[3U] == 0U) {
        return false;
    }
    const auto red = static_cast<int>(pixel[0U]);
    const auto green = static_cast<int>(pixel[1U]);
    const auto blue = static_cast<int>(pixel[2U]);
    return red > 10 && red > green * 2 && red > blue * 2;
}

[[nodiscard]] Coverage measure(const Frame& frame, PixelFilter filter) {
    Coverage result;
    double sum_x{};
    double sum_y{};
    bool first = true;
    for (std::uint32_t y{}; y < frame.height; ++y) {
        for (std::uint32_t x{}; x < frame.width; ++x) {
            if (!filter(frame.at(x, y))) {
                continue;
            }
            const double px = horizontal_percent(x, frame.width);
            const double py = vertical_percent(y, frame.height);
            sum_x += px;
            sum_y += py;
            ++result.pixels;
            if (first) {
                result.min_x = result.max_x = px;
                result.min_y = result.max_y = py;
                first = false;
                continue;
            }
            result.min_x = std::min(result.min_x, px);
            result.max_x = std::max(result.max_x, px);
            result.min_y = std::min(result.min_y, py);
            result.max_y = std::max(result.max_y, py);
        }
    }
    if (result.pixels != 0U) {
        result.centroid_x = sum_x / static_cast<double>(result.pixels);
        result.centroid_y = sum_y / static_cast<double>(result.pixels);
    }
    return result;
}

/** Half-width of the frame in degrees, for reporting an angular error. */
[[nodiscard]] double degrees_off_axis(double percent_of_width, double fov_y_degrees,
                                      double aspect) {
    const double half_y = fov_y_degrees * 0.5 * std::numbers::pi / 180.0;
    const double tan_half_x = std::tan(half_y) * aspect;
    // percent of full width -> normalised device coordinate.
    const double ndc = percent_of_width / 100.0 * 2.0;
    return std::atan(ndc * tan_half_x) * 180.0 / std::numbers::pi;
}

void report(const std::string& label, const Coverage& coverage, double fov_y_degrees,
            double aspect) {
    std::printf("  %-22s pixels %8zu | x centroid %+8.4f%%  bbox [%+8.4f%%, %+8.4f%%]"
                "  (%+.4f deg)\n",
                label.c_str(), coverage.pixels, coverage.centroid_x, coverage.min_x,
                coverage.max_x, degrees_off_axis(coverage.centroid_x, fov_y_degrees, aspect));
    std::printf("  %-22s              | y centroid %+8.4f%%  bbox [%+8.4f%%, %+8.4f%%]\n", "",
                coverage.centroid_y, coverage.min_y, coverage.max_y);
}

// ---------------------------------------------------------------------------
// PNG (stored-deflate, so nothing outside the standard library is needed)
// ---------------------------------------------------------------------------

[[nodiscard]] std::uint32_t crc32_bytes(const std::vector<std::uint8_t>& bytes,
                                        std::size_t offset) {
    static std::array<std::uint32_t, 256U> table{};
    static bool built = false;
    if (!built) {
        for (std::uint32_t index{}; index < 256U; ++index) {
            std::uint32_t value = index;
            for (std::uint32_t bit{}; bit < 8U; ++bit) {
                value = ((value & 1U) != 0U) ? (0xEDB88320U ^ (value >> 1U)) : (value >> 1U);
            }
            table[index] = value;
        }
        built = true;
    }
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t index = offset; index < bytes.size(); ++index) {
        crc = table[(crc ^ bytes[index]) & 0xFFU] ^ (crc >> 8U);
    }
    return crc ^ 0xFFFFFFFFU;
}

[[nodiscard]] std::uint32_t adler32_bytes(const std::vector<std::uint8_t>& bytes) {
    std::uint32_t a = 1U;
    std::uint32_t b = 0U;
    for (const auto byte : bytes) {
        a = (a + byte) % 65521U;
        b = (b + a) % 65521U;
    }
    return (b << 16U) | a;
}

void push_be32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24U));
    out.push_back(static_cast<std::uint8_t>(value >> 16U));
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
    out.push_back(static_cast<std::uint8_t>(value));
}

void push_chunk(std::vector<std::uint8_t>& out, const char (&tag)[5U],
                const std::vector<std::uint8_t>& payload) {
    push_be32(out, static_cast<std::uint32_t>(payload.size()));
    const std::size_t crc_start = out.size();
    for (std::size_t index{}; index < 4U; ++index) {
        out.push_back(static_cast<std::uint8_t>(tag[index]));
    }
    out.insert(out.end(), payload.begin(), payload.end());
    std::vector<std::uint8_t> crc_input{out.begin() + static_cast<std::ptrdiff_t>(crc_start),
                                        out.end()};
    push_be32(out, crc32_bytes(crc_input, 0U));
}

/**
 * Write `rgb` (width * height * 3, row 0 at the top) as a PNG.
 *
 * Stored deflate blocks make the file larger than a compressed one and keep
 * the encoder to forty lines with no third-party dependency, which is the
 * right trade for a diagnostic image.
 */
void write_png(const std::filesystem::path& path, std::uint32_t width, std::uint32_t height,
               const std::vector<std::uint8_t>& rgb) {
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(height) * (1U + static_cast<std::size_t>(width) * 3U));
    for (std::uint32_t y{}; y < height; ++y) {
        raw.push_back(0U); // filter: none
        const auto row = static_cast<std::size_t>(y) * width * 3U;
        raw.insert(raw.end(), rgb.begin() + static_cast<std::ptrdiff_t>(row),
                   rgb.begin() + static_cast<std::ptrdiff_t>(row + width * 3U));
    }

    std::vector<std::uint8_t> stream{0x78U, 0x01U};
    std::size_t offset{};
    do {
        const auto chunk = static_cast<std::uint16_t>(
            std::min<std::size_t>(65535U, raw.size() - offset));
        const bool last = offset + chunk >= raw.size();
        stream.push_back(last ? 1U : 0U);
        stream.push_back(static_cast<std::uint8_t>(chunk));
        stream.push_back(static_cast<std::uint8_t>(chunk >> 8U));
        stream.push_back(static_cast<std::uint8_t>(~chunk));
        stream.push_back(static_cast<std::uint8_t>((~chunk) >> 8U));
        stream.insert(stream.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                      raw.begin() + static_cast<std::ptrdiff_t>(offset + chunk));
        offset += chunk;
    } while (offset < raw.size());
    push_be32(stream, adler32_bytes(raw));

    std::vector<std::uint8_t> header;
    push_be32(header, width);
    push_be32(header, height);
    header.push_back(8U); // bit depth
    header.push_back(2U); // colour type: truecolour
    header.push_back(0U);
    header.push_back(0U);
    header.push_back(0U);

    std::vector<std::uint8_t> file{0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU};
    push_chunk(file, "IHDR", header);
    push_chunk(file, "IDAT", stream);
    push_chunk(file, "IEND", {});

    std::ofstream output{path, std::ios::binary};
    if (!output) {
        throw std::runtime_error{"could not write " + path.string()};
    }
    output.write(reinterpret_cast<const char*>(file.data()),
                 static_cast<std::streamsize>(file.size()));
}

/**
 * Composite the frame over a neutral background and stamp the exact optical
 * axis, so the saved image answers "is it centred" by eye as well as by number.
 */
void save_frame(const std::filesystem::path& path, const Frame& frame) {
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(frame.width) * frame.height * 3U);
    for (std::uint32_t y{}; y < frame.height; ++y) {
        for (std::uint32_t x{}; x < frame.width; ++x) {
            const auto* pixel = frame.at(x, y);
            const auto index = (static_cast<std::size_t>(y) * frame.width + x) * 3U;
            if (pixel[3U] != 0U) {
                rgb[index] = pixel[0U];
                rgb[index + 1U] = pixel[1U];
                rgb[index + 2U] = pixel[2U];
                continue;
            }
            // A coarse checker so the transparent background cannot be mistaken
            // for a black part of the model.
            const bool light = (((x / 32U) + (y / 32U)) % 2U) == 0U;
            const std::uint8_t tone = light ? 42U : 32U;
            rgb[index] = tone;
            rgb[index + 1U] = tone;
            rgb[index + 2U] = static_cast<std::uint8_t>(tone + 6U);
        }
    }
    // The optical axis. Width is even for every resolution used here, so the
    // axis falls on the boundary between the two central columns; both get a
    // mark and the true centre is the seam between them.
    const std::uint32_t cx = frame.width / 2U;
    const std::uint32_t cy = frame.height / 2U;
    const auto stamp = [&](std::uint32_t x, std::uint32_t y) {
        if (x >= frame.width || y >= frame.height) {
            return;
        }
        const auto index = (static_cast<std::size_t>(y) * frame.width + x) * 3U;
        rgb[index] = 0U;
        rgb[index + 1U] = 255U;
        rgb[index + 2U] = 255U;
    };
    for (std::uint32_t y{}; y < frame.height; ++y) {
        if ((y / 8U) % 2U == 0U) {
            stamp(cx - 1U, y);
            stamp(cx, y);
        }
    }
    for (std::uint32_t x{}; x < frame.width; ++x) {
        if ((x / 8U) % 2U == 0U) {
            stamp(x, cy - 1U);
            stamp(x, cy);
        }
    }
    write_png(path, frame.width, frame.height, rgb);
}

// ---------------------------------------------------------------------------
// Cases
// ---------------------------------------------------------------------------

struct Case final {
    std::uint8_t tool_id{};
    const char* name{};
};

constexpr std::array<Case, 4U> kCases{{
    {6U, "RIFLE"},
    {7U, "SMG"},
    {18U, "SNIPER"},
    {12U, "RPG"},
}};

/** weapon_zoom.cpp:48 -- one unit of zoom is 37.5 degrees of vertical view. */
constexpr double kAdsFovYDegrees{37.5};
constexpr std::uint32_t kWidth{1920U};
constexpr std::uint32_t kHeight{1080U};

/**
 * Percent of the frame a centred measurement may drift.
 *
 * Every figure below measures 0.0000% exactly once the mesher matches retail,
 * because the assets are symmetric and an even frame width samples symmetrically
 * about the axis. One pixel is 0.052% of the width, so this is well under half a
 * pixel and still seven times tighter than the 0.21% the red bead moves if the
 * KV6 cube convention regresses to spanning [coord, coord + 1].
 */
constexpr double kTolerance{0.03};

/**
 * The ADS branch must be reachable for these weapons at all.
 *
 * tutorial_session.cpp:679 only toggles `zoomed_` inside
 * `if (pressed && aims_down_sights(behavior))`, and the sandbox upload at
 * native_frontend_module.cpp:6528-6534 only fills `sandbox_sight_slot` when the
 * catalog names a sight mesh. If either fails, the aimed branch is dead and the
 * weapon renders in the hip pose -- 0.4 units off-axis, not half a voxel.
 */
void the_aimed_branch_is_reachable() {
    for (const auto& item : kCases) {
        const auto* weapon = find_weapon_definition(item.tool_id);
        expect(weapon != nullptr, std::string{item.name} + " must be a catalog row");
        expect(battlespades::world::aims_down_sights(
                   battlespades::world::weapon_secondary_behavior(*weapon)),
               std::string{item.name} + " must enter ADS on the secondary press edge");
        expect(!weapon->sight_model_asset.empty(),
               std::string{item.name} + " must name a sight mesh");
        // native_frontend_module.cpp:6520-6550 spends one slot per first-person
        // part, then the sight, then the pin, then two arms.
        const std::size_t needed = weapon->first_person_models.size() + 1U +
                                   (weapon->pin_model.asset.empty() ? 0U : 1U) + 2U;
        expect(needed <= 10U, std::string{item.name} + " must fit the viewmodel slot budget");
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::filesystem::path png_directory;
        bool measure_only = false;
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--verbose") {
                verbose = true;
            } else if (argument == "--measure") {
                measure_only = true;
            } else if (argument == "--png" && index + 1 < argc) {
                png_directory = argv[++index];
            }
        }

        the_aimed_branch_is_reachable();

        const std::filesystem::path asset_root{AOS_TEST_ASSET_ROOT};
        const std::filesystem::path shader_root{AOS_SHADER_BIN_ROOT};

        static DiagnosticCallback callback;
        bgfx::Init init;
        init.callback = &callback;
        init.type = bgfx::RendererType::Direct3D11;
        init.resolution.width = 0U;
        init.resolution.height = 0U;
        if (!bgfx::init(init)) {
            std::cout << "SKIP: no Direct3D11 device for the ADS sight render probe\n";
            return 0;
        }
        const auto* caps = bgfx::getCaps();
        if ((caps->supported & BGFX_CAPS_TEXTURE_READ_BACK) == 0U ||
            (caps->supported & BGFX_CAPS_TEXTURE_BLIT) == 0U) {
            std::cout << "SKIP: device cannot blit or read textures back\n";
            bgfx::shutdown();
            return 0;
        }

        const auto backend = shader_root / "dx11";
        constexpr double aspect =
            static_cast<double>(kWidth) / static_cast<double>(kHeight);

        std::string failures;
        for (const auto& item : kCases) {
            const auto loaded = load_weapon_models(asset_root, item.tool_id);
            expect(static_cast<bool>(loaded), loaded.error);
            expect(loaded.models->sight.has_value(),
                   std::string{item.name} + " sight mesh must load");
            const auto sight = evaluate_weapon_sight(item.tool_id);

            std::vector<DrawItem> draws;
            draws.push_back({&*loaded.models->sight, sight_matrix(sight)});
            if (sight.has_pin && loaded.models->pin.has_value()) {
                draws.push_back({&*loaded.models->pin, pin_matrix(sight)});
            }

            const auto frame = render(draws, kWidth, kHeight, kAdsFovYDegrees, backend);
            const auto all = measure(frame, covered);
            std::printf("%s (tool %u), %ux%u, ADS fov_y %.1f deg\n", item.name,
                        static_cast<unsigned>(item.tool_id), kWidth, kHeight,
                        kAdsFovYDegrees);
            report("sight+pin", all, kAdsFovYDegrees, aspect);
            expect(all.pixels > 0U, std::string{item.name} + " must draw something");

            // A sight that fills the frame is a tube seen from inside it, so
            // the hole is the aim mark rather than the silhouette.
            const auto frame_pixels = static_cast<std::size_t>(kWidth) * kHeight;
            const bool fills_frame = all.pixels * 2U > frame_pixels;
            Coverage hole;
            if (fills_frame) {
                hole = measure(frame, uncovered);
                report("aim hole", hole, kAdsFovYDegrees, aspect);
            }

            const auto check = [&](const char* what, const char* axis, double value) {
                if (std::fabs(value) <= kTolerance) {
                    return;
                }
                failures += "\n  " + std::string{item.name} + ' ' + what + ' ' + axis +
                            " is " + std::to_string(value) +
                            "% of the frame, must be within " +
                            std::to_string(kTolerance) + '%';
            };

            if (sight.has_pin) {
                const auto bead = measure(frame, red_bead);
                report("red pin bead", bead, kAdsFovYDegrees, aspect);
                if (!measure_only) {
                    expect(bead.pixels > 0U, "the rifle pin's red tip must be visible");
                    // classicRifleWeapon.py:27 hides the HUD crosshair while
                    // aimed, so this bead IS the aim mark. It must straddle the
                    // axis, not merely sit near it.
                    check("red bead", "centroid x", bead.centroid_x);
                    check("red bead", "bbox skew x", bead.min_x + bead.max_x);
                    expect(bead.min_x < 0.0 && bead.max_x > 0.0,
                           "the rifle's red bead must straddle the view axis");
                }
            }

            if (!measure_only) {
                // Every sight asset is mirror-symmetric across its own X centre
                // and carries its pivot there, so a correctly composed chain
                // projects a silhouette that is symmetric about the optical
                // axis at every depth it spans. Half a voxel of pivot error
                // breaks this and nothing else in the chain can.
                check("sight silhouette", "centroid x", all.centroid_x);
                check("sight silhouette", "bbox skew x", all.min_x + all.max_x);
                if (fills_frame) {
                    // The scope family cancels character.pyx:2080 as well:
                    // sight_pos.y 0.325 against the constant -0.35 leaves
                    // -0.025, half a voxel at scale 0.05, so the tube centres
                    // on the eye vertically too. This is the only case where a
                    // sight is supposed to be vertically centred.
                    check("aim hole", "centroid x", hole.centroid_x);
                    check("aim hole", "centroid y", hole.centroid_y);
                    check("aim hole", "bbox skew x", hole.min_x + hole.max_x);
                    check("aim hole", "bbox skew y", hole.min_y + hole.max_y);
                }
            }

            if (!png_directory.empty()) {
                std::filesystem::create_directories(png_directory);
                const auto path =
                    png_directory / (std::string{"ads_"} + item.name + ".png");
                save_frame(path, frame);
                std::cout << "  wrote " << path.string() << '\n';
            }
        }

        bgfx::shutdown();
        if (!failures.empty()) {
            throw std::runtime_error{"aimed sight is not centred:" + failures};
        }
        std::cout << "ADS sight render probe passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ADS sight render probe failed: " << error.what() << '\n';
        return 1;
    }
}
