#include "battlespades/render/skydome_animation.hpp"

#include <bimg/decode.h>
#include <bx/allocator.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

// Pins the direction the skydome layers scroll. Retail flips V at .aos load
// (mesh.pyd 0x100042F0: v = 1 - v), uploads the TGA rows unflipped
// (mesh.pyd 0x100015B0), and adds time * uv_speed in skydome_vert. We keep
// the raw V and bimg decodes top row first, so the V speed must be negated.

namespace {

void expect(bool value, const std::string& message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    expect(static_cast<bool>(input), "cannot read " + path.string());
    return {std::istreambuf_iterator<char>{input}, {}};
}

[[nodiscard]] std::uint32_t u32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    expect(offset + 4U <= bytes.size(), "truncated .aos");
    std::uint32_t value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

/// Where in the picture (0 = top row, 1 = bottom row) retail samples.
/// GL t=0 is the first TGA file row, which is the bottom of a bottom-origin
/// picture, and t is the loader-flipped V plus the shader's scroll.
[[nodiscard]] float retail_sample_from_top(float raw_v, float authored_speed_v, float time) {
    const float gl_t = (1.0F - raw_v) + (time * authored_speed_v);
    return 1.0F - gl_t;
}

/// Where in the picture our vs_skydome.sc samples: raw V plus the converted
/// speed, against a texture bimg decoded top row first.
[[nodiscard]] float native_sample_from_top(float raw_v, float authored_speed_v, float time) {
    const auto speed = battlespades::render::retail_skydome_uv_speed({0.0F, authored_speed_v});
    return raw_v + (time * speed[1U]);
}

void vertical_scroll_matches_retail() {
    // Classic's SKYCloudsA authors uv_speeds [0.00025, 0.0003].
    constexpr std::array<float, 2U> classic_clouds_a{0.00025F, 0.0003F};
    const float time = battlespades::render::retail_skydome_time(10.0F);
    for (const float raw_v : {0.0F, 0.25F, 0.512F, 1.0F}) {
        const float retail = retail_sample_from_top(raw_v, classic_clouds_a[1U], time);
        const float native = native_sample_from_top(raw_v, classic_clouds_a[1U], time);
        expect(std::fabs(retail - native) < 1.0e-5F,
               "skydome V scroll diverges from retail at v=" + std::to_string(raw_v));
        // A positive authored V speed walks retail's sample toward the top of
        // the picture, i.e. the clouds on screen travel toward the bottom row.
        expect(native < raw_v, "positive authored V speed must sample toward the picture top");
    }
    const auto converted = battlespades::render::retail_skydome_uv_speed(classic_clouds_a);
    expect(converted[0U] == classic_clouds_a[0U],
           "U is sampled identically to retail and must keep its authored sign");
    expect(converted[1U] == -classic_clouds_a[1U], "V speed must be negated for our raw-V mesh");
}

using Vec3 = std::array<float, 3U>;
using Mat3 = std::array<Vec3, 3U>;

[[nodiscard]] Mat3 axis_rotation(int axis, float degrees) {
    const double radians = static_cast<double>(degrees) * 3.14159265358979323846 / 180.0;
    const auto c = static_cast<float>(std::cos(radians));
    const auto s = static_cast<float>(std::sin(radians));
    switch (axis) {
    case 0:
        return {{{1.0F, 0.0F, 0.0F}, {0.0F, c, -s}, {0.0F, s, c}}};
    case 1:
        return {{{c, 0.0F, s}, {0.0F, 1.0F, 0.0F}, {-s, 0.0F, c}}};
    default:
        return {{{c, -s, 0.0F}, {s, c, 0.0F}, {0.0F, 0.0F, 1.0F}}};
    }
}

[[nodiscard]] Vec3 rotate_by(const Mat3& m, const Vec3& v) {
    return {m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2],
            m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
            m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2]};
}

[[nodiscard]] bool close_to(const Vec3& a, const Vec3& b) {
    return std::fabs(a[0] - b[0]) < 1.0e-4F && std::fabs(a[1] - b[1]) < 1.0e-4F &&
           std::fabs(a[2] - b[2]) < 1.0e-4F;
}

/// SkyDome.do_mesh_rotation (gameScene.pyd 0x1010FF40) calls glRotatef for
/// X, then Y, then Z, so a vertex sees Rx * Ry * Rz.
void layer_rotation_matches_retail_gl_order() {
    using battlespades::render::retail_skydome_rotate;
    const std::array<Vec3, 4U> samples{{{1.0F, 0.0F, 0.0F},
                                        {0.0F, 1.0F, 0.0F},
                                        {0.0F, 0.0F, 1.0F},
                                        {0.3F, -0.7F, 0.648F}}};
    for (const Vec3 degrees : {Vec3{180.0F, 180.0F, 0.0F}, Vec3{180.0F, 45.0F, 0.0F},
                               Vec3{180.0F, 90.0F, 0.0F}, Vec3{0.0F, 0.0F, 0.0F},
                               Vec3{30.0F, 60.0F, 20.0F}}) {
        const auto rx = axis_rotation(0, degrees[0]);
        const auto ry = axis_rotation(1, degrees[1]);
        const auto rz = axis_rotation(2, degrees[2]);
        for (const auto& sample : samples) {
            const auto retail = rotate_by(rx, rotate_by(ry, rotate_by(rz, sample)));
            expect(close_to(retail_skydome_rotate(sample, degrees), retail),
                   "skydome layer rotation is not retail's Rx*Ry*Rz");
            // The old X-first bake, kept here only to show which domes moved.
            const auto x_first = rotate_by(rz, rotate_by(ry, rotate_by(rx, sample)));
            if (degrees[1] == 180.0F || degrees[1] == 0.0F) {
                expect(close_to(x_first, retail),
                       "the common (180, 180, 0) domes must be unchanged by the order fix");
            }
        }
    }
    // Colosseum and Invasion are the domes the order actually moves.
    const Vec3 east{1.0F, 0.0F, 0.0F};
    const auto invasion = retail_skydome_rotate(east, {180.0F, 90.0F, 0.0F});
    expect(close_to(invasion, rotate_by(axis_rotation(0, 180.0F), rotate_by(axis_rotation(1, 90.0F), east))),
           "Invasion dome orientation diverges from retail");
    expect(close_to(invasion, {0.0F, 0.0F, 1.0F}),
           "Invasion's +x must land on retail's +z (X-first put it on -z)");
}

/// The negation assumes bimg hands rows to bgfx top row first for the
/// bottom-origin files retail ships; a decoder that kept file order would
/// turn every sky upside down and the scroll back round.
void bimg_decodes_bottom_origin_sky_top_row_first(const std::filesystem::path& root) {
    const auto path = root / "tga" / "t_WW2_DL_SkyGrad.tga";
    const auto bytes = read_file(path);
    expect(bytes.size() > 18U, "truncated sky gradient");
    const std::uint32_t width = bytes[12] | (static_cast<std::uint32_t>(bytes[13]) << 8U);
    const std::uint32_t height = bytes[14] | (static_cast<std::uint32_t>(bytes[15]) << 8U);
    const std::uint32_t channels = bytes[16] / 8U;
    expect(bytes[2] == 2U && (bytes[17] & 0x20U) == 0U && channels >= 3U,
           "the probe gradient must be an uncompressed bottom-origin TGA");
    const std::size_t pixels = 18U + bytes[0];
    expect(bytes.size() >= pixels + static_cast<std::size_t>(width) * height * channels,
           "truncated sky gradient pixels");

    bx::DefaultAllocator allocator;
    auto* image = bimg::imageParse(&allocator,
                                   bytes.data(),
                                   static_cast<std::uint32_t>(bytes.size()),
                                   bimg::TextureFormat::RGBA8);
    expect(image != nullptr && image->m_width == width && image->m_height == height,
           "bimg could not decode the sky gradient");
    const auto* decoded = static_cast<const std::uint8_t*>(image->m_data);
    bool first_is_last_file_row = true;
    bool first_is_first_file_row = true;
    for (std::uint32_t column{}; column < width; ++column) {
        const std::size_t last = pixels + ((static_cast<std::size_t>(height - 1U) * width) + column) * channels;
        const std::size_t first = pixels + static_cast<std::size_t>(column) * channels;
        for (std::uint32_t c{}; c < 3U; ++c) {
            // TGA stores BGR; RGBA8 is RGB.
            const auto value = decoded[(static_cast<std::size_t>(column) * 4U) + c];
            first_is_last_file_row = first_is_last_file_row && value == bytes[last + 2U - c];
            first_is_first_file_row = first_is_first_file_row && value == bytes[first + 2U - c];
        }
    }
    bimg::imageFree(image);
    expect(first_is_last_file_row && !first_is_first_file_row,
           "bimg must decode a bottom-origin sky TGA top row first");
}

/// Retail's loader ignores the TGA origin bit, so the derivation holds only
/// while every texture a dome binds is bottom-origin and left-to-right.
void every_dome_texture_is_bottom_origin(const std::filesystem::path& root) {
    std::set<std::string> textures;
    std::size_t domes{};
    for (const auto& entry : std::filesystem::directory_iterator{root / "mesh"}) {
        if (!entry.is_directory()) {
            continue;
        }
        const auto definition = entry.path() / (entry.path().filename().string() + ".txt");
        if (!std::filesystem::is_regular_file(definition)) {
            continue;
        }
        ++domes;
        std::ifstream input{definition};
        nlohmann::json json;
        input >> json;
        for (const auto& layer : json.at("render_list")) {
            auto mesh = entry.path() / (layer.get<std::string>() + ".aos");
            if (!std::filesystem::is_regular_file(mesh)) {
                mesh = root / "mesh" / (layer.get<std::string>() + ".aos");
            }
            const auto bytes = read_file(mesh);
            std::size_t offset = 8U;
            const auto count = u32(bytes, 4U);
            for (std::uint32_t index{}; index < count; ++index) {
                offset += 4U + u32(bytes, offset);
                offset += 4U + (static_cast<std::size_t>(u32(bytes, offset)) * 36U);
                const auto length = u32(bytes, offset);
                offset += 4U;
                expect(offset + length <= bytes.size(), "truncated .aos texture name");
                textures.emplace(reinterpret_cast<const char*>(bytes.data() + offset), length);
                offset += length;
            }
        }
    }
    expect(domes >= 27U, "expected the full shipped skydome set");
    expect(!textures.empty(), "no skydome textures found");
    for (const auto& texture : textures) {
        const auto bytes = read_file(root / "tga" / texture);
        expect(bytes.size() > 18U, "truncated " + texture);
        expect((bytes[17] & 0x30U) == 0U,
               texture + " is not bottom-left origin; retail would show it flipped");
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        expect(argc == 2, "usage: aos_skydome_scroll_tests <asset-root>");
        const std::filesystem::path root{argv[1]};
        vertical_scroll_matches_retail();
        layer_rotation_matches_retail_gl_order();
        bimg_decodes_bottom_origin_sky_top_row_first(root);
        every_dome_texture_is_bottom_origin(root);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "skydome scroll tests passed\n";
    return 0;
}
