#include "battlespades/render/world_renderer.hpp"
#include "battlespades/render/skydome_animation.hpp"

#include "battlespades/render/bgfx_ui_renderer.hpp"
#include "battlespades/render/camera_basis.hpp"
#include "battlespades/render/render_views.hpp"
#include "battlespades/render/shadow_projection.hpp"
#include "chunk_vertex_layout.hpp"
#include "post_process.hpp"

#include <bgfx/bgfx.h>
#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/math.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::render {
namespace {

constexpr std::array<std::uint8_t, 3U> default_fog_color{111U, 215U, 223U};

struct SkydomeVertex final {
    float x{};
    float y{};
    float z{};
    float red{1.0F};
    float green{1.0F};
    float blue{1.0F};
    float alpha{1.0F};
    float u{};
    float v{};
};

static_assert(sizeof(SkydomeVertex) == 36U);

[[nodiscard]] bool valid_mesh_upload(const world::ChunkMesh& mesh) noexcept {
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        return mesh.vertices.empty() && mesh.indices.empty();
    }
    if (mesh.vertices.size() > UINT32_MAX / sizeof(world::ChunkVertex) ||
        mesh.indices.size() > UINT32_MAX / sizeof(std::uint32_t) ||
        mesh.indices.size() % 3U != 0U) return false;
    for (std::size_t axis{}; axis < 3U; ++axis) {
        if (!std::isfinite(mesh.minimum[axis]) || !std::isfinite(mesh.maximum[axis]) ||
            mesh.minimum[axis] > mesh.maximum[axis]) return false;
    }
    return std::ranges::all_of(mesh.indices, [&](auto index) { return index < mesh.vertices.size(); }) &&
           std::ranges::all_of(mesh.vertices, [](const auto& vertex) {
               return std::isfinite(vertex.x) && std::isfinite(vertex.y) && std::isfinite(vertex.z) &&
                      std::isfinite(vertex.ao_u) && std::isfinite(vertex.ao_v) &&
                      std::isfinite(vertex.edge_u) && std::isfinite(vertex.edge_v) &&
                      std::isfinite(vertex.retail_baked_light) &&
                      vertex.retail_baked_light >= 0.0F && vertex.retail_baked_light <= 1.0F &&
                      vertex.face < 6U && vertex.occlusion < 4U && vertex.noise_corner < 4U;
           });
}

[[nodiscard]] std::vector<std::uint8_t> read_binary(const std::filesystem::path& path);

struct ParsedSkydomeMesh final {
    std::string name;
    std::vector<SkydomeVertex> vertices;
    std::string texture_name;
};

class BinaryCursor final {
public:
    explicit BinaryCursor(std::span<const std::uint8_t> bytes) : bytes_{bytes} {}

    [[nodiscard]] std::optional<std::uint32_t> u32() noexcept {
        if (remaining() < 4U)
            return std::nullopt;
        const auto value = static_cast<std::uint32_t>(bytes_[offset_]) |
                           (static_cast<std::uint32_t>(bytes_[offset_ + 1U]) << 8U) |
                           (static_cast<std::uint32_t>(bytes_[offset_ + 2U]) << 16U) |
                           (static_cast<std::uint32_t>(bytes_[offset_ + 3U]) << 24U);
        offset_ += 4U;
        return value;
    }

    [[nodiscard]] std::optional<float> f32() noexcept {
        const auto bits = u32();
        if (!bits.has_value())
            return std::nullopt;
        float value{};
        const auto raw = *bits;
        std::memcpy(&value, &raw, sizeof(value));
        return value;
    }

    [[nodiscard]] std::optional<std::string> text(std::size_t count, std::size_t maximum) {
        if (count == 0U || count > maximum || count > remaining()) {
            return std::nullopt;
        }
        const auto* begin = reinterpret_cast<const char*>(bytes_.data() + offset_);
        std::string result{begin, count};
        offset_ += count;
        return result;
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - offset_;
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_{};
};

[[nodiscard]] bool safe_asset_basename(std::string_view name, std::string_view extension) noexcept {
    if (name.empty() || name.size() > 127U || !name.ends_with(extension)) {
        return false;
    }
    return std::ranges::all_of(name, [](unsigned char character) {
        return std::isalnum(character) != 0 || character == ' ' || character == '_' ||
               character == '-' || character == '.';
    });
}

[[nodiscard]] std::optional<std::vector<ParsedSkydomeMesh>>
read_aos_meshes(const std::filesystem::path& path, std::string& error) {
    const auto bytes = read_binary(path);
    if (bytes.size() < 12U) {
        error = "missing or truncated retail .aos mesh: " + path.string();
        return std::nullopt;
    }
    BinaryCursor reader{bytes};
    const auto declared_size = reader.u32();
    const auto mesh_count = reader.u32();
    // One shipped WW1 compatibility mesh has a stale size word. The retail
    // loader trusts the bounded records, not that advisory exporter field.
    static_cast<void>(declared_size);
    if (!mesh_count.has_value() || *mesh_count == 0U || *mesh_count > 64U) {
        error = "invalid retail .aos mesh count: " + path.string();
        return std::nullopt;
    }

    std::vector<ParsedSkydomeMesh> result;
    result.reserve(*mesh_count);
    for (std::uint32_t mesh_index{}; mesh_index < *mesh_count; ++mesh_index) {
        const auto name_length = reader.u32();
        if (!name_length.has_value()) {
            error = "truncated retail .aos mesh name";
            return std::nullopt;
        }
        auto name = reader.text(*name_length, 127U);
        const auto vertex_count = reader.u32();
        if (!name.has_value() || !vertex_count.has_value() || *vertex_count == 0U ||
            *vertex_count > 1'000'000U ||
            static_cast<std::uint64_t>(*vertex_count) * sizeof(SkydomeVertex) >
                reader.remaining()) {
            error = "invalid retail .aos vertex stream: " + path.string();
            return std::nullopt;
        }

        ParsedSkydomeMesh mesh;
        mesh.name = std::move(*name);
        mesh.vertices.reserve(*vertex_count);
        for (std::uint32_t vertex_index{}; vertex_index < *vertex_count; ++vertex_index) {
            SkydomeVertex vertex;
            auto values = std::array<std::optional<float>, 9U>{reader.f32(),
                                                               reader.f32(),
                                                               reader.f32(),
                                                               reader.f32(),
                                                               reader.f32(),
                                                               reader.f32(),
                                                               reader.f32(),
                                                               reader.f32(),
                                                               reader.f32()};
            if (std::ranges::any_of(values, [](const auto& value) {
                    return !value.has_value() || !std::isfinite(*value);
                })) {
                error = "non-finite or truncated retail .aos vertex";
                return std::nullopt;
            }
            vertex = {*values[0U],
                      *values[1U],
                      *values[2U],
                      *values[3U],
                      *values[4U],
                      *values[5U],
                      *values[6U],
                      *values[7U],
                      *values[8U]};
            mesh.vertices.push_back(vertex);
        }
        const auto texture_length = reader.u32();
        if (!texture_length.has_value()) {
            error = "truncated retail .aos texture name";
            return std::nullopt;
        }
        auto texture = reader.text(*texture_length, 127U);
        if (!texture.has_value() || !safe_asset_basename(*texture, ".tga")) {
            error = "unsafe retail .aos texture name";
            return std::nullopt;
        }
        mesh.texture_name = std::move(*texture);
        result.push_back(std::move(mesh));
    }
    if (reader.remaining() != 0U) {
        error = "retail .aos mesh has trailing bytes: " + path.string();
        return std::nullopt;
    }
    return result;
}

struct Plane final {
    float x{};
    float y{};
    float z{};
    float w{};
};

[[nodiscard]] std::vector<std::uint8_t> read_binary(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        return {};
    }
    return {std::istreambuf_iterator<char>{input}, {}};
}

[[nodiscard]] const char* shader_directory() noexcept {
    switch (bgfx::getRendererType()) {
    case bgfx::RendererType::Direct3D11:
    case bgfx::RendererType::Direct3D12:
        return "dx11";
    case bgfx::RendererType::OpenGL:
        return "glsl";
    case bgfx::RendererType::OpenGLES:
        return "essl";
    case bgfx::RendererType::Vulkan:
        return "spirv";
    case bgfx::RendererType::Metal:
        return "metal";
    default:
        return "";
    }
}

/** Row-major world-space frustum planes from the combined view-projection. */
[[nodiscard]] std::array<Plane, 6U> frustum_planes(const std::array<float, 16U>& m) noexcept {
    const auto row = [&](std::size_t index) {
        return std::array<float, 4U>{m[index], m[index + 4U], m[index + 8U], m[index + 12U]};
    };
    const auto r0 = row(0U);
    const auto r1 = row(1U);
    const auto r2 = row(2U);
    const auto r3 = row(3U);
    std::array<Plane, 6U> planes{};
    for (std::size_t axis{}; axis < 3U; ++axis) {
        const auto& r = axis == 0U ? r0 : axis == 1U ? r1 : r2;
        planes[axis * 2U] = {r3[0U] + r[0U], r3[1U] + r[1U], r3[2U] + r[2U], r3[3U] + r[3U]};
        planes[axis * 2U + 1U] = {r3[0U] - r[0U], r3[1U] - r[1U], r3[2U] - r[2U], r3[3U] - r[3U]};
    }
    return planes;
}

[[nodiscard]] bool aabb_outside(const Plane& plane,
                                const std::array<float, 3U>& minimum,
                                const std::array<float, 3U>& maximum) noexcept {
    const float x = plane.x >= 0.0F ? maximum[0U] : minimum[0U];
    const float y = plane.y >= 0.0F ? maximum[1U] : minimum[1U];
    const float z = plane.z >= 0.0F ? maximum[2U] : minimum[2U];
    return plane.x * x + plane.y * y + plane.z * z + plane.w < 0.0F;
}

struct ChunkSlot final {
    bgfx::VertexBufferHandle vertices{bgfx::kInvalidHandle};
    bgfx::IndexBufferHandle indices{bgfx::kInvalidHandle};
    std::array<float, 3U> minimum{};
    std::array<float, 3U> maximum{};
    bool resident{};
    /** Uploaded in the half-float position layout (RenderTuning::packed_terrain). */
    bool packed{};
    std::uint32_t vertex_bytes{};
    std::uint32_t index_bytes{};
};

/**
 * Sends each world uniform only when its value changes within one submit().
 *
 * bgfx keeps uniform values as renderer-global state and applies a draw's
 * recorded updates in render order, so inside a ViewMode::Sequential view a
 * value set for one draw is still in force for the next. Re-sending ~25
 * unchanged uniforms per chunk and model part made every draw re-commit and
 * re-upload both constant buffers. The writer is rebuilt for each submit(), so
 * the first draw of a frame always sends everything: nothing a previous frame,
 * the view-model pass or another renderer left behind is relied upon.
 */
class UniformWriter final {
public:
    explicit UniformWriter(bool cached) noexcept : cached_{cached} {
        index_.fill(unset);
    }

    /** `count` vec4s (or mat4s when `floats_each` is 16). */
    void set(bgfx::UniformHandle handle, const void* data, std::uint16_t count = 1U,
             std::size_t floats_each = 4U) {
        const std::size_t floats = static_cast<std::size_t>(count) * floats_each;
        if (cached_ && handle.idx < index_.size() && floats <= Entry::capacity) {
            auto& slot = index_[handle.idx];
            if (slot == unset) {
                if (used_ == entries_.size()) {
                    bgfx::setUniform(handle, data, count);
                    return;
                }
                slot = static_cast<std::uint8_t>(used_++);
            } else if (entries_[slot].floats == floats &&
                       std::memcmp(entries_[slot].values.data(), data,
                                   floats * sizeof(float)) == 0) {
                return;
            }
            entries_[slot].floats = floats;
            std::memcpy(entries_[slot].values.data(), data, floats * sizeof(float));
        }
        bgfx::setUniform(handle, data, count);
    }

private:
    struct Entry final {
        /** Eight vec4 point lights is the largest world uniform. */
        static constexpr std::size_t capacity{32U};
        std::array<float, capacity> values{};
        std::size_t floats{};
    };
    static constexpr std::uint8_t unset{0xFFU};

    bool cached_{};
    std::size_t used_{};
    std::array<Entry, 48U> entries_{};
    /** Uniform handle index -> entry; bgfx allows at most 512 uniforms. */
    std::array<std::uint8_t, 512U> index_{};
};

/** `BATTLESPADES_RENDER_TUNING=legacy` selects RenderTuning::legacy(). */
[[nodiscard]] bool legacy_render_tuning_requested() {
#if defined(_WIN32)
    char* buffer{};
    std::size_t size{};
    if (_dupenv_s(&buffer, &size, "BATTLESPADES_RENDER_TUNING") != 0 || buffer == nullptr) {
        return false;
    }
    const std::string value{buffer};
    std::free(buffer);
    return value == "legacy";
#else
    const auto* value = std::getenv("BATTLESPADES_RENDER_TUNING");
    return value != nullptr && std::string_view{value} == "legacy";
#endif
}

/** One corner of the shared billboard quad, expanded per instance in vs_particle. */
struct ParticleQuadVertex final {
    float x{};
    float y{};
    float u{};
    float v{};
};

static_assert(sizeof(ParticleQuadVertex) == 16U);

struct ParticleAtlasDefinition final {
    const char* file;
    float frames_x;
    float frames_y;
};

/**
 * Authored retail sprite sheets, indexed by world::ParticleAtlas.
 *
 * `soft_round` has no retail counterpart: retail's round sprites are low
 * resolution and were the specific complaint that motivated this work, so it
 * is generated analytically at load time instead. NONRETAIL.
 */
constexpr std::array<ParticleAtlasDefinition, world::particle_atlas_count> particle_atlases{{
    {"Tumbling_cube_anim.png", 8.0F, 8.0F},
    {"Tumbling_Glowcube_anim_8x8.png", 8.0F, 8.0F},
    {"SmokeTrail_anim_8x8.png", 8.0F, 8.0F},
    {"PickUp_Twinkle_anim_4x4.png", 4.0F, 4.0F},
    {nullptr, 1.0F, 1.0F},
    {"SnowkeTrail_anim_8x8.png", 8.0F, 8.0F},
}};

/**
 * The renderer's own fallback profile.
 *
 * Written out rather than calling profile_for() so the bgfx target does not
 * have to link the settings library for a single default value.
 */
[[nodiscard]] QualityProfile default_quality_profile() noexcept {
    QualityProfile profile;
    profile.enhanced_lighting = true;
    return profile;
}

/**
 * Builds the NONRETAIL soft radial sprite used for sparks, flash and
 * shockwaves. A smooth analytic falloff has no resolution ceiling, which is
 * the whole point of not reusing the retail bitmap.
 */
[[nodiscard]] bgfx::TextureHandle create_soft_round_texture() {
    constexpr std::uint16_t edge{128U};
    const auto* memory = bgfx::alloc(static_cast<std::uint32_t>(edge) * edge * 4U);
    auto* pixels = memory->data;
    for (std::uint16_t y{}; y < edge; ++y) {
        for (std::uint16_t x{}; x < edge; ++x) {
            const float dx = (static_cast<float>(x) + 0.5F) / edge * 2.0F - 1.0F;
            const float dy = (static_cast<float>(y) + 0.5F) / edge * 2.0F - 1.0F;
            const float distance = std::sqrt(dx * dx + dy * dy);
            // Smoothstep falloff squared: a tight hot core with a soft skirt.
            const float linear = std::clamp(1.0F - distance, 0.0F, 1.0F);
            const float falloff = linear * linear * (3.0F - 2.0F * linear);
            const auto value = static_cast<std::uint8_t>(
                std::clamp(std::lround(falloff * falloff * 255.0F), 0L, 255L));
            const std::size_t offset = (static_cast<std::size_t>(y) * edge + x) * 4U;
            pixels[offset] = 255U;
            pixels[offset + 1U] = 255U;
            pixels[offset + 2U] = 255U;
            pixels[offset + 3U] = value;
        }
    }
    return bgfx::createTexture2D(edge,
                                 edge,
                                 false,
                                 1U,
                                 bgfx::TextureFormat::RGBA8,
                                 BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
                                 memory);
}

} // namespace

struct WorldRenderer::Impl final {
    bool initialized{};
    std::string last_error;
    std::filesystem::path asset_root;
    bgfx::VertexLayout layout{};
    /** Terrain-only 40-byte layout; models and effects keep `layout`. */
    bgfx::VertexLayout packed_layout{};
    bool packed_supported{};
    RenderTuning tuning{};
    /** Reused by upload_chunk so a live remesh does not allocate per chunk. */
    std::vector<PackedTerrainVertex> pack_scratch;
    /** Visible chunk slots of the current submit(), nearest first. */
    std::vector<std::pair<float, std::uint16_t>> visible_chunks;
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle camera_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle fog_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sun_direction_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sun_color_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sky_ambient_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle ground_ambient_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle fog_horizon_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle fog_curve_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle model_opacity_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle point_light_position_radius_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle point_light_color_intensity_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle retail_light0_direction_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle retail_light1_direction_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle retail_light0_color_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle retail_light1_color_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle retail_ambient_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle retail_view_direction_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle retail_ao_sampler = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle retail_ao_texture = BGFX_INVALID_HANDLE;
    RetailTerrainLighting retail_lighting{};
    world::MapAtmosphere atmosphere;

    bgfx::ProgramHandle shadow_program = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle shadow_target = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle shadow_texture = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shadow_matrix_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shadow_params_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shadow_sampler = BGFX_INVALID_HANDLE;
    std::uint16_t shadow_resolution{};
    bool shadow_supported{};

    bgfx::TextureHandle skylight_texture = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle skylight_sampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle skylight_params_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle emissive_params_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle indirect_params_uniform = BGFX_INVALID_HANDLE;
    /** Which way the hemispheric ambient calls "up", in the normal's own space. */
    bgfx::UniformHandle up_axis_uniform = BGFX_INVALID_HANDLE;
    /** rgb: additive map light for the current model draw (enhanced tiers). */
    bgfx::UniformHandle model_light_uniform = BGFX_INVALID_HANDLE;
    /** Non-owning; sampled per model draw on enhanced tiers. */
    const world::StaticLightField* model_placed_lights{};
    const world::EmissiveVolume* model_cast_lights{};
    bgfx::UniformHandle emissive_volume_sampler = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle emissive_volume_texture = BGFX_INVALID_HANDLE;
    bool emissive_volume_resident{};
    /** Per-map amplification after the coarse volume's distance falloff. */
    float emissive_cast_gain{2.2F};
    bool skylight_resident{};
    /** Skylight reaching the camera; scales the viewmodel's ambient. */
    float viewmodel_skylight{1.0F};

    void release_skylight() noexcept {
        if (bgfx::isValid(skylight_texture)) {
            bgfx::destroy(skylight_texture);
            skylight_texture = BGFX_INVALID_HANDLE;
        }
        skylight_resident = false;
    }

    void release_shadow_target() noexcept {
        if (bgfx::isValid(shadow_target)) {
            bgfx::destroy(shadow_target);
            shadow_target = BGFX_INVALID_HANDLE;
        }
        // The framebuffer owns its attachment, so the texture handle is stale
        // once it is destroyed; never destroy it separately.
        shadow_texture = BGFX_INVALID_HANDLE;
        shadow_resolution = 0U;
    }

    /**
     * Creates or resizes the sun's depth-only shadow map.
     *
     * Returns false when the backend cannot compare-sample a depth texture, in
     * which case the world renders unshadowed rather than failing the frame.
     */
    [[nodiscard]] bool ensure_shadow_target(std::uint16_t resolution) {
        if (!shadow_supported || resolution == 0U) {
            release_shadow_target();
            return false;
        }
        if (bgfx::isValid(shadow_target) && shadow_resolution == resolution) {
            return true;
        }
        release_shadow_target();
        const auto texture = bgfx::createTexture2D(
            resolution,
            resolution,
            false,
            1U,
            bgfx::TextureFormat::D16,
            BGFX_TEXTURE_RT |
                BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_COMPARE_LEQUAL);
        if (!bgfx::isValid(texture)) {
            return false;
        }
        std::array<bgfx::TextureHandle, 1U> attachments{texture};
        shadow_target = bgfx::createFrameBuffer(
            static_cast<std::uint8_t>(attachments.size()), attachments.data(), true);
        if (!bgfx::isValid(shadow_target)) {
            bgfx::destroy(texture);
            return false;
        }
        shadow_texture = texture;
        shadow_resolution = resolution;
        return true;
    }
    // Must default to LIT. The TerrainLighting member this replaces defaulted
    // to `enhanced`, and the gameplay-lab path never calls the setter, so a
    // default-constructed all-off profile would silently downgrade it to Legacy.
    QualityProfile profile{default_quality_profile()};
    // Optional world post chain and world-texture sampling (post_settings.hpp).
    PostProcessor post;
    PostSettings post_settings{};
    TextureFiltering texture_filtering{};

    /**
     * Sampler override for a world-space texture created with `base` address
     * flags: UINT32_MAX (the texture's own flags) at the default smooth,
     * non-anisotropic setting, so that path stays byte-identical.
     */
    [[nodiscard]] std::uint32_t world_sampler(std::uint32_t base) const noexcept {
        if (texture_filtering == TextureFiltering{}) {
            return UINT32_MAX;
        }
        if (!texture_filtering.smooth) {
            return base | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
        }
        return base | BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC;
    }
    bgfx::VertexLayout skydome_layout{};
    bgfx::ProgramHandle skydome_program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle skydome_sampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle skydome_uv_time = BGFX_INVALID_HANDLE;
    std::array<std::uint8_t, 3U> fog_bytes{default_fog_color};
    std::array<std::uint8_t, 3U> retail_fog_bytes{default_fog_color};
    std::optional<std::array<std::uint8_t, 3U>> retail_sea_color{};
    std::array<float, 4U> fog_color{default_fog_color[0U] / 255.0F,
                                    default_fog_color[1U] / 255.0F,
                                    default_fog_color[2U] / 255.0F,
                                    1.0F};
    std::string skydome_name;
    std::chrono::steady_clock::time_point skydome_clock{std::chrono::steady_clock::now()};
    struct SkydomeSlot final {
        bgfx::VertexBufferHandle vertices{bgfx::kInvalidHandle};
        bgfx::TextureHandle texture{bgfx::kInvalidHandle};
        std::array<float, 2U> uv_speed{};
    };
    std::vector<SkydomeSlot> skydome_slots;
    std::array<ChunkSlot, 32U * 32U> chunks{};
    std::size_t resident_count{};
    WorldFrameStats stats{};
    struct ModelSlot final {
        bgfx::VertexBufferHandle vertices{bgfx::kInvalidHandle};
        bgfx::IndexBufferHandle indices{bgfx::kInvalidHandle};
        bool resident{};
        /** Model-space bounding sphere; a negative radius disables culling. */
        std::array<float, 3U> bound_centre{};
        float bound_radius{-1.0F};
    };
    std::array<ModelSlot, WorldRenderer::view_model_slot_count> view_model_slots{};
    std::array<ModelSlot, WorldRenderer::world_model_slot_count> world_model_slots{};
    bool model_culling{true};

    bgfx::ProgramHandle particle_program = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout particle_layout{};
    bgfx::VertexBufferHandle particle_quad = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle particle_quad_indices = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle particle_sampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle particle_lut_sampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle particle_grid = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle particle_mode = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle particle_glow_lut_texture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle particle_smoke_lut_texture = BGFX_INVALID_HANDLE;
    std::array<bgfx::TextureHandle, world::particle_atlas_count> particle_textures = [] {
        std::array<bgfx::TextureHandle, world::particle_atlas_count> handles;
        // Handle zero is valid and may belong to another renderer. Initialize
        // every slot, including atlases added after this renderer was written.
        handles.fill(BGFX_INVALID_HANDLE);
        return handles;
    }();
    /** Neutral, Blue and Green retail LaserAttachment textures, in enum order. */
    std::array<bgfx::TextureHandle, 3U> laser_beam_textures{
        {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    std::array<bgfx::TextureHandle, 3U> laser_spot_textures{
        {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    bgfx::TextureHandle spot_shadow_texture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle zone_texture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle solid_zone_texture = BGFX_INVALID_HANDLE;

    void release_particles() noexcept {
        for (auto& texture : particle_textures) {
            if (bgfx::isValid(texture)) {
                bgfx::destroy(texture);
                texture = BGFX_INVALID_HANDLE;
            }
        }
        if (bgfx::isValid(particle_glow_lut_texture)) {
            bgfx::destroy(particle_glow_lut_texture);
            particle_glow_lut_texture = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(particle_smoke_lut_texture)) {
            bgfx::destroy(particle_smoke_lut_texture);
            particle_smoke_lut_texture = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(particle_quad)) {
            bgfx::destroy(particle_quad);
            particle_quad = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(particle_quad_indices)) {
            bgfx::destroy(particle_quad_indices);
            particle_quad_indices = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(particle_program)) {
            bgfx::destroy(particle_program);
            particle_program = BGFX_INVALID_HANDLE;
        }
        for (auto* uniform : {&particle_sampler, &particle_lut_sampler,
                              &particle_grid, &particle_mode}) {
            if (bgfx::isValid(*uniform)) {
                bgfx::destroy(*uniform);
                *uniform = BGFX_INVALID_HANDLE;
            }
        }
    }

    void release_lasers() noexcept {
        for (auto* collection : {&laser_beam_textures, &laser_spot_textures}) {
            for (auto& texture : *collection) {
                if (bgfx::isValid(texture)) {
                    bgfx::destroy(texture);
                    texture = BGFX_INVALID_HANDLE;
                }
            }
        }
        if (bgfx::isValid(spot_shadow_texture)) {
            bgfx::destroy(spot_shadow_texture);
            spot_shadow_texture = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(zone_texture)) {
            bgfx::destroy(zone_texture);
            zone_texture = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(solid_zone_texture)) {
            bgfx::destroy(solid_zone_texture);
            solid_zone_texture = BGFX_INVALID_HANDLE;
        }
    }

    static void release_skydome_slot(SkydomeSlot& slot) noexcept {
        if (bgfx::isValid(slot.vertices)) {
            bgfx::destroy(slot.vertices);
            slot.vertices = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(slot.texture)) {
            bgfx::destroy(slot.texture);
            slot.texture = BGFX_INVALID_HANDLE;
        }
    }

    void release_skydome() noexcept {
        for (auto& slot : skydome_slots) {
            release_skydome_slot(slot);
        }
        skydome_slots.clear();
        skydome_name.clear();
    }

    void release_model_slot(ModelSlot& slot) noexcept {
        if (bgfx::isValid(slot.vertices)) {
            bgfx::destroy(slot.vertices);
            slot.vertices = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(slot.indices)) {
            bgfx::destroy(slot.indices);
            slot.indices = BGFX_INVALID_HANDLE;
        }
        slot.resident = false;
    }

    [[nodiscard]] bool create_model_slot(const world::ChunkMesh& mesh, ModelSlot& replacement) {
        if (mesh.empty()) return true;
        replacement.vertices = bgfx::createVertexBuffer(
            bgfx::copy(mesh.vertices.data(), static_cast<std::uint32_t>(
                mesh.vertices.size() * sizeof(world::ChunkVertex))), layout);
        replacement.indices = bgfx::createIndexBuffer(
            bgfx::copy(mesh.indices.data(), static_cast<std::uint32_t>(
                mesh.indices.size() * sizeof(std::uint32_t))), BGFX_BUFFER_INDEX32);
        if (!bgfx::isValid(replacement.vertices) || !bgfx::isValid(replacement.indices)) {
            release_model_slot(replacement);
            return false;
        }
        replacement.resident = true;
        return true;
    }

    [[nodiscard]] bool fail(std::string message) {
        last_error = std::move(message);
        return false;
    }

    [[nodiscard]] bgfx::ShaderHandle load_shader(const std::filesystem::path& path) {
        const auto bytes = read_binary(path);
        if (bytes.empty()) {
            static_cast<void>(fail("unable to read compiled world shader: " + path.string()));
            return BGFX_INVALID_HANDLE;
        }
        const auto* memory = bgfx::copy(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
        const auto shader = bgfx::createShader(memory);
        if (!bgfx::isValid(shader)) {
            static_cast<void>(fail("bgfx rejected compiled world shader: " + path.string()));
        }
        return shader;
    }

    [[nodiscard]] bgfx::TextureHandle load_skydome_texture(const std::filesystem::path& path,
                                                           std::string& error) {
        const auto bytes = read_binary(path);
        if (bytes.empty() || bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
            error = "unable to read skydome TGA: " + path.string();
            return BGFX_INVALID_HANDLE;
        }
        bx::DefaultAllocator allocator;
        auto* image = bimg::imageParse(&allocator,
                                       bytes.data(),
                                       static_cast<std::uint32_t>(bytes.size()),
                                       bimg::TextureFormat::RGBA8);
        if (image == nullptr || image->m_width == 0U || image->m_height == 0U ||
            image->m_width > UINT16_MAX || image->m_height > UINT16_MAX || image->m_depth != 1U ||
            image->m_numLayers != 1U || image->m_cubeMap) {
            if (image != nullptr)
                bimg::imageFree(image);
            error = "unsupported skydome TGA: " + path.string();
            return BGFX_INVALID_HANDLE;
        }
        const auto* memory = bgfx::copy(image->m_data, image->m_size);
        const auto texture = bgfx::createTexture2D(static_cast<std::uint16_t>(image->m_width),
                                                   static_cast<std::uint16_t>(image->m_height),
                                                   image->m_numMips > 1U,
                                                   1U,
                                                   bgfx::TextureFormat::RGBA8,
                                                   BGFX_TEXTURE_NONE,
                                                   memory);
        bimg::imageFree(image);
        if (!bgfx::isValid(texture)) {
            error = "bgfx rejected skydome TGA: " + path.string();
        }
        return texture;
    }

    /**
     * Loads one particle sprite sheet.
     *
     * Clamping is mandatory: with the default wrap mode the bilinear filter
     * samples across the sheet edge and neighbouring 8x8 cells bleed into each
     * other, which reads as a flickering seam on every animated sprite.
     */
    [[nodiscard]] bgfx::TextureHandle load_particle_texture(
        const std::filesystem::path& path,
        std::string& error,
        std::uint64_t sampler_flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
        bool bottom_row_first = false) {
        const auto bytes = read_binary(path);
        if (bytes.empty() || bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
            error = "unable to read particle atlas: " + path.string();
            return BGFX_INVALID_HANDLE;
        }
        bx::DefaultAllocator allocator;
        auto* image = bimg::imageParse(&allocator,
                                       bytes.data(),
                                       static_cast<std::uint32_t>(bytes.size()),
                                       bimg::TextureFormat::RGBA8);
        if (image == nullptr || image->m_width == 0U || image->m_height == 0U ||
            image->m_width > UINT16_MAX || image->m_height > UINT16_MAX || image->m_depth != 1U ||
            image->m_numLayers != 1U || image->m_cubeMap) {
            if (image != nullptr)
                bimg::imageFree(image);
            error = "unsupported particle atlas: " + path.string();
            return BGFX_INVALID_HANDLE;
        }
        if (bottom_row_first) {
            // Upload the image's bottom row as texture row 0 (v = 0), the
            // memory layout the retail GL client gave its TGA atlases.
            const auto stride = static_cast<std::size_t>(image->m_width) * 4U;
            auto* rows = static_cast<std::uint8_t*>(image->m_data);
            for (std::uint32_t row{}; row < image->m_height / 2U; ++row) {
                std::swap_ranges(rows + row * stride, rows + (row + 1U) * stride,
                                 rows + (image->m_height - row - 1U) * stride);
            }
        }
        const auto* memory = bgfx::copy(image->m_data, image->m_size);
        const auto texture = bgfx::createTexture2D(static_cast<std::uint16_t>(image->m_width),
                                                   static_cast<std::uint16_t>(image->m_height),
                                                   false,
                                                   1U,
                                                   bgfx::TextureFormat::RGBA8,
                                                   sampler_flags,
                                                   memory);
        bimg::imageFree(image);
        if (!bgfx::isValid(texture)) {
            error = "bgfx rejected particle atlas: " + path.string();
        }
        return texture;
    }

    void release_chunk(ChunkSlot& slot) noexcept {
        if (bgfx::isValid(slot.vertices)) {
            bgfx::destroy(slot.vertices);
            slot.vertices = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(slot.indices)) {
            bgfx::destroy(slot.indices);
            slot.indices = BGFX_INVALID_HANDLE;
        }
        if (slot.resident) {
            slot.resident = false;
            --resident_count;
        }
        slot.packed = false;
        slot.vertex_bytes = 0U;
        slot.index_bytes = 0U;
    }

    /**
     * Creates one terrain chunk's buffers, packed when the tuning, the backend
     * and the chunk's own values all allow it. `replacement` keeps invalid
     * handles on failure.
     */
    [[nodiscard]] bool create_chunk_buffers(const world::ChunkMesh& mesh, ChunkSlot& replacement) {
        if (mesh.empty()) {
            return true;
        }
        const bool pack = tuning.packed_terrain && packed_supported;
        bool packed = false;
        if (pack) {
            pack_scratch.resize(mesh.vertices.size());
            packed = pack_terrain_vertices(mesh.vertices, pack_scratch);
        }
        if (packed) {
            replacement.vertex_bytes = static_cast<std::uint32_t>(
                pack_scratch.size() * sizeof(PackedTerrainVertex));
            replacement.vertices = bgfx::createVertexBuffer(
                bgfx::copy(pack_scratch.data(), replacement.vertex_bytes), packed_layout);
        } else {
            replacement.vertex_bytes = static_cast<std::uint32_t>(
                mesh.vertices.size() * sizeof(world::ChunkVertex));
            replacement.vertices = bgfx::createVertexBuffer(
                bgfx::copy(mesh.vertices.data(), replacement.vertex_bytes), layout);
        }
        // Sixteen-bit indices address every vertex of all but the densest
        // chunks and halve the index buffer; the triangles are the same.
        if (pack && mesh.vertices.size() <= 0x10000U) {
            replacement.index_bytes = static_cast<std::uint32_t>(
                mesh.indices.size() * sizeof(std::uint16_t));
            const auto* memory = bgfx::alloc(replacement.index_bytes);
            auto* narrow = reinterpret_cast<std::uint16_t*>(memory->data);
            for (std::size_t index{}; index < mesh.indices.size(); ++index) {
                narrow[index] = static_cast<std::uint16_t>(mesh.indices[index]);
            }
            replacement.indices = bgfx::createIndexBuffer(memory);
        } else {
            replacement.index_bytes = static_cast<std::uint32_t>(
                mesh.indices.size() * sizeof(std::uint32_t));
            replacement.indices = bgfx::createIndexBuffer(
                bgfx::copy(mesh.indices.data(), replacement.index_bytes), BGFX_BUFFER_INDEX32);
        }
        if (!bgfx::isValid(replacement.vertices) || !bgfx::isValid(replacement.indices)) {
            if (bgfx::isValid(replacement.vertices)) {
                bgfx::destroy(replacement.vertices);
                replacement.vertices = BGFX_INVALID_HANDLE;
            }
            if (bgfx::isValid(replacement.indices)) {
                bgfx::destroy(replacement.indices);
                replacement.indices = BGFX_INVALID_HANDLE;
            }
            return false;
        }
        replacement.packed = packed;
        replacement.resident = true;
        return true;
    }
};

WorldRenderer::WorldRenderer() : impl_{std::make_unique<Impl>()} {}

WorldRenderer::~WorldRenderer() {
    shutdown();
}

bool WorldRenderer::initialize(const std::filesystem::path& shader_root,
                               const std::filesystem::path& asset_root,
                               TextureQualityTier texture_quality) {
    if (impl_->initialized) {
        return impl_->fail("world renderer is already initialized");
    }
    struct InitializationRollback final {
        WorldRenderer* renderer;
        ~InitializationRollback() { if (renderer != nullptr) renderer->shutdown(); }
    } rollback{this};
    const auto* directory = shader_directory();
    if (directory[0U] == '\0') {
        return impl_->fail("the selected bgfx backend has no world shader variant");
    }
    const auto backend_root = shader_root / directory;
    impl_->post.set_shader_root(backend_root);
    const auto vertex = impl_->load_shader(backend_root / "vs_world.bin");
    if (!bgfx::isValid(vertex)) {
        return false;
    }
    const auto fragment = impl_->load_shader(backend_root / "fs_world.bin");
    if (!bgfx::isValid(fragment)) {
        bgfx::destroy(vertex);
        return false;
    }
    impl_->program = bgfx::createProgram(vertex, fragment, true);
    if (!bgfx::isValid(impl_->program)) {
        return impl_->fail("bgfx could not link the world shader program");
    }

    impl_->layout = chunk_vertex_layout();
    if (!chunk_vertex_layout_matches_struct(impl_->layout)) {
        return impl_->fail("world vertex layout stride does not match ChunkVertex");
    }
    impl_->packed_layout = packed_terrain_vertex_layout();
    impl_->packed_supported =
        impl_->packed_layout.getStride() == sizeof(PackedTerrainVertex) &&
        (bgfx::getCaps()->supported & BGFX_CAPS_VERTEX_ATTRIB_HALF) != 0U;
    // Field fallback: the pre-tuning submission path, without a rebuild.
    if (legacy_render_tuning_requested()) {
        impl_->tuning = RenderTuning::legacy();
    }

    const auto skydome_vertex = impl_->load_shader(backend_root / "vs_skydome.bin");
    if (!bgfx::isValid(skydome_vertex)) {
        return false;
    }
    const auto skydome_fragment = impl_->load_shader(backend_root / "fs_skydome.bin");
    if (!bgfx::isValid(skydome_fragment)) {
        bgfx::destroy(skydome_vertex);
        return false;
    }
    impl_->skydome_program = bgfx::createProgram(skydome_vertex, skydome_fragment, true);
    if (!bgfx::isValid(impl_->skydome_program)) {
        return impl_->fail("bgfx could not link the retail skydome program");
    }
    impl_->skydome_layout.begin()
        .add(bgfx::Attrib::Position, 3U, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4U, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2U, bgfx::AttribType::Float)
        .end();

    impl_->camera_uniform = bgfx::createUniform("u_cameraPosition", bgfx::UniformType::Vec4);
    impl_->fog_uniform = bgfx::createUniform("u_fogParams", bgfx::UniformType::Vec4);
    impl_->light_uniform = bgfx::createUniform("u_lightParams", bgfx::UniformType::Vec4);
    impl_->sun_direction_uniform = bgfx::createUniform("u_sunDirection", bgfx::UniformType::Vec4);
    impl_->sun_color_uniform = bgfx::createUniform("u_sunColor", bgfx::UniformType::Vec4);
    impl_->sky_ambient_uniform = bgfx::createUniform("u_skyAmbient", bgfx::UniformType::Vec4);
    impl_->ground_ambient_uniform = bgfx::createUniform("u_groundAmbient", bgfx::UniformType::Vec4);
    impl_->fog_horizon_uniform = bgfx::createUniform("u_fogHorizon", bgfx::UniformType::Vec4);
    impl_->fog_curve_uniform = bgfx::createUniform("u_fogCurve", bgfx::UniformType::Vec4);
    impl_->model_opacity_uniform = bgfx::createUniform("u_modelOpacity", bgfx::UniformType::Vec4);
    impl_->point_light_position_radius_uniform =
        bgfx::createUniform("u_pointLightPositionRadius",
                            bgfx::UniformType::Vec4,
                            static_cast<std::uint16_t>(maximum_dynamic_lights));
    impl_->point_light_color_intensity_uniform =
        bgfx::createUniform("u_pointLightColorIntensity",
                            bgfx::UniformType::Vec4,
                            static_cast<std::uint16_t>(maximum_dynamic_lights));
    impl_->retail_light0_direction_uniform =
        bgfx::createUniform("u_retailLight0Direction", bgfx::UniformType::Vec4);
    impl_->retail_light1_direction_uniform =
        bgfx::createUniform("u_retailLight1Direction", bgfx::UniformType::Vec4);
    impl_->retail_light0_color_uniform =
        bgfx::createUniform("u_retailLight0Color", bgfx::UniformType::Vec4);
    impl_->retail_light1_color_uniform =
        bgfx::createUniform("u_retailLight1Color", bgfx::UniformType::Vec4);
    impl_->retail_ambient_uniform =
        bgfx::createUniform("u_retailAmbient", bgfx::UniformType::Vec4);
    impl_->retail_view_direction_uniform =
        bgfx::createUniform("u_retailViewDirection", bgfx::UniformType::Vec4);
    if (!bgfx::isValid(impl_->light_uniform) || !bgfx::isValid(impl_->sun_direction_uniform) ||
        !bgfx::isValid(impl_->sun_color_uniform) || !bgfx::isValid(impl_->sky_ambient_uniform) ||
        !bgfx::isValid(impl_->ground_ambient_uniform) ||
        !bgfx::isValid(impl_->fog_horizon_uniform) || !bgfx::isValid(impl_->fog_curve_uniform) ||
        !bgfx::isValid(impl_->model_opacity_uniform) ||
        !bgfx::isValid(impl_->point_light_position_radius_uniform) ||
        !bgfx::isValid(impl_->point_light_color_intensity_uniform) ||
        !bgfx::isValid(impl_->retail_light0_direction_uniform) ||
        !bgfx::isValid(impl_->retail_light1_direction_uniform) ||
        !bgfx::isValid(impl_->retail_light0_color_uniform) ||
        !bgfx::isValid(impl_->retail_light1_color_uniform) ||
        !bgfx::isValid(impl_->retail_ambient_uniform) ||
        !bgfx::isValid(impl_->retail_view_direction_uniform)) {
        return impl_->fail("bgfx could not create world lighting uniforms");
    }
    impl_->skydome_sampler = bgfx::createUniform("s_skyTexture", bgfx::UniformType::Sampler);
    impl_->skydome_uv_time = bgfx::createUniform("u_skyUvTime", bgfx::UniformType::Vec4);
    if (!bgfx::isValid(impl_->camera_uniform) || !bgfx::isValid(impl_->fog_uniform) ||
        !bgfx::isValid(impl_->skydome_sampler) || !bgfx::isValid(impl_->skydome_uv_time)) {
        return impl_->fail("bgfx could not create world uniforms");
    }

    impl_->retail_ao_sampler = bgfx::createUniform("s_retailAo", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(impl_->retail_ao_sampler)) {
        return impl_->fail("bgfx could not create the retail terrain atlas sampler");
    }
    {
        std::filesystem::path ao_path = asset_root / "tga";
        if (texture_quality == TextureQualityTier::low) {
            ao_path /= "low";
        } else if (texture_quality == TextureQualityTier::medium) {
            ao_path /= "med";
        }
        ao_path /= "ao_cube512.tga";
        std::string error;
        // Retail sub_10014A30 uses GL_LINEAR and CLAMP_TO_EDGE for both axes.
        // Its TGA rows reach GL bottom row first, so atlas v = 0 is the
        // image's bottom edge. Uploading top-down mirrored every AO cell
        // vertically: measured against the retail client's own AO channel,
        // the mirrored atlas turned smooth corner darkening into a per-block
        // checkerboard of half-dark faces.
        impl_->retail_ao_texture = impl_->load_particle_texture(
            ao_path, error, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, true);
        if (!bgfx::isValid(impl_->retail_ao_texture)) {
            return impl_->fail(std::move(error));
        }
    }

    const auto shadow_vertex = impl_->load_shader(backend_root / "vs_shadow.bin");
    if (!bgfx::isValid(shadow_vertex)) {
        return false;
    }
    const auto shadow_fragment = impl_->load_shader(backend_root / "fs_shadow.bin");
    if (!bgfx::isValid(shadow_fragment)) {
        bgfx::destroy(shadow_vertex);
        return false;
    }
    impl_->shadow_program = bgfx::createProgram(shadow_vertex, shadow_fragment, true);
    if (!bgfx::isValid(impl_->shadow_program)) {
        return impl_->fail("bgfx could not link the shadow program");
    }
    impl_->shadow_matrix_uniform = bgfx::createUniform("u_shadowMatrix", bgfx::UniformType::Mat4);
    impl_->shadow_params_uniform = bgfx::createUniform("u_shadowParams", bgfx::UniformType::Vec4);
    impl_->shadow_sampler = bgfx::createUniform("s_shadowMap", bgfx::UniformType::Sampler);
    impl_->skylight_sampler = bgfx::createUniform("s_skylight", bgfx::UniformType::Sampler);
    impl_->skylight_params_uniform =
        bgfx::createUniform("u_skylightParams", bgfx::UniformType::Vec4);
    impl_->emissive_params_uniform =
        bgfx::createUniform("u_emissiveParams", bgfx::UniformType::Vec4);
    impl_->indirect_params_uniform =
        bgfx::createUniform("u_indirectParams", bgfx::UniformType::Vec4);
    impl_->emissive_volume_sampler =
        bgfx::createUniform("s_emissiveVolume", bgfx::UniformType::Sampler);
    impl_->up_axis_uniform = bgfx::createUniform("u_upAxis", bgfx::UniformType::Vec4);
    impl_->model_light_uniform = bgfx::createUniform("u_modelLight", bgfx::UniformType::Vec4);
    if (!bgfx::isValid(impl_->model_light_uniform) || !bgfx::isValid(impl_->shadow_matrix_uniform) ||
        !bgfx::isValid(impl_->shadow_params_uniform) || !bgfx::isValid(impl_->shadow_sampler) ||
        !bgfx::isValid(impl_->skylight_sampler) || !bgfx::isValid(impl_->skylight_params_uniform) ||
        !bgfx::isValid(impl_->up_axis_uniform) || !bgfx::isValid(impl_->emissive_params_uniform)) {
        return impl_->fail("bgfx could not create shadow uniforms");
    }
    // Comparison sampling of a depth texture is what makes a shadow map cheap;
    // without it the world simply renders unshadowed.
    const auto* caps = bgfx::getCaps();
    impl_->shadow_supported =
        (caps->supported & BGFX_CAPS_TEXTURE_COMPARE_LEQUAL) != 0U &&
        (caps->formats[bgfx::TextureFormat::D16] & BGFX_CAPS_FORMAT_TEXTURE_2D) != 0U;

    const auto particle_vertex = impl_->load_shader(backend_root / "vs_particle.bin");
    if (!bgfx::isValid(particle_vertex)) {
        return false;
    }
    const auto particle_fragment = impl_->load_shader(backend_root / "fs_particle.bin");
    if (!bgfx::isValid(particle_fragment)) {
        bgfx::destroy(particle_vertex);
        return false;
    }
    impl_->particle_program = bgfx::createProgram(particle_vertex, particle_fragment, true);
    if (!bgfx::isValid(impl_->particle_program)) {
        return impl_->fail("bgfx could not link the particle program");
    }
    impl_->particle_layout.begin()
        .add(bgfx::Attrib::Position, 2U, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2U, bgfx::AttribType::Float)
        .end();

    // One shared quad; every particle is an instance of it. Retail draw.pyd
    // sub_10001730 builds each corner as (corner - 0.5) * 2.0 * size, i.e.
    // `size` is a HALF-extent and a particle is 2*size wide. The previous
    // [-0.5, 0.5] corners drew every retail-sized particle at half width
    // (quarter area): small explosion cubes and thin smoke trails.
    static constexpr std::array<ParticleQuadVertex, 4U> quad{{
        {-1.0F, -1.0F, 0.0F, 1.0F},
        {1.0F, -1.0F, 1.0F, 1.0F},
        {1.0F, 1.0F, 1.0F, 0.0F},
        {-1.0F, 1.0F, 0.0F, 0.0F},
    }};
    static constexpr std::array<std::uint16_t, 6U> quad_indices{0U, 1U, 2U, 0U, 2U, 3U};
    impl_->particle_quad =
        bgfx::createVertexBuffer(bgfx::makeRef(quad.data(), sizeof(quad)), impl_->particle_layout);
    impl_->particle_quad_indices =
        bgfx::createIndexBuffer(bgfx::makeRef(quad_indices.data(), sizeof(quad_indices)));
    if (!bgfx::isValid(impl_->particle_quad) || !bgfx::isValid(impl_->particle_quad_indices)) {
        return impl_->fail("bgfx could not create the particle quad");
    }

    impl_->particle_sampler = bgfx::createUniform("s_particleAtlas", bgfx::UniformType::Sampler);
    impl_->particle_lut_sampler =
        bgfx::createUniform("s_particleLut", bgfx::UniformType::Sampler);
    impl_->particle_grid = bgfx::createUniform("u_atlasGrid", bgfx::UniformType::Vec4);
    impl_->particle_mode = bgfx::createUniform("u_particleMode", bgfx::UniformType::Vec4);
    if (!bgfx::isValid(impl_->particle_sampler) ||
        !bgfx::isValid(impl_->particle_lut_sampler) ||
        !bgfx::isValid(impl_->particle_grid) || !bgfx::isValid(impl_->particle_mode)) {
        return impl_->fail("bgfx could not create particle uniforms");
    }

    for (std::size_t index{}; index < particle_atlases.size(); ++index) {
        const auto& definition = particle_atlases[index];
        if (definition.file == nullptr) {
            impl_->particle_textures[index] = create_soft_round_texture();
        } else {
            std::string error;
            impl_->particle_textures[index] = impl_->load_particle_texture(
                asset_root /
                    texture_quality_asset(
                        std::filesystem::path{"png"} / "high" / definition.file,
                        texture_quality),
                error);
            if (!bgfx::isValid(impl_->particle_textures[index])) {
                return impl_->fail(std::move(error));
            }
        }
    }
    {
        std::string error;
        // aoslib/images.py binds particle_lut_image to this exact retail
        // asset.  particle_lut.png is a separate lookup table despite its
        // tempting name; swapping them changes the RPG palette.
        impl_->particle_glow_lut_texture = impl_->load_particle_texture(
            asset_root /
                texture_quality_asset("png/high/tumbling_cube_lut.png", texture_quality),
            error);
        if (!bgfx::isValid(impl_->particle_glow_lut_texture)) {
            return impl_->fail(std::move(error));
        }
    }
    {
        std::string error;
        // glowBlockParticles.py's child spawn point uses the smoke-specific
        // LUT. This is the yellow/orange cloud ramp visible in the original
        // RPG fingers, distinct from the parent glow-cube lookup above.
        impl_->particle_smoke_lut_texture = impl_->load_particle_texture(
            asset_root / texture_quality_asset("png/high/particle_lut.png", texture_quality),
            error);
        if (!bgfx::isValid(impl_->particle_smoke_lut_texture)) {
            return impl_->fail(std::move(error));
        }
    }
    {
        static constexpr std::array<const char*, 3U> beam_files{
            "laser_sight_beam_small.png",
            "laser_sight_beam_blue.png",
            "laser_sight_beam_green.png"};
        static constexpr std::array<const char*, 3U> spot_files{
            "laser_spot.png", "laser_spot_blue.png", "laser_spot_green.png"};
        for (std::size_t index{}; index < beam_files.size(); ++index) {
            std::string error;
            // The retail quad's U coordinate spans each complete beam segment.
            // V must clamp at the transparent edge; U stays repeat-capable for
            // the moving LaserAttachment texture.
            impl_->laser_beam_textures[index] = impl_->load_particle_texture(
                asset_root /
                    texture_quality_asset(
                        std::filesystem::path{"png"} / "high" / beam_files[index],
                        texture_quality),
                error,
                BGFX_SAMPLER_V_CLAMP);
            if (!bgfx::isValid(impl_->laser_beam_textures[index])) {
                return impl_->fail(std::move(error));
            }
            impl_->laser_spot_textures[index] = impl_->load_particle_texture(
                asset_root /
                    texture_quality_asset(
                        std::filesystem::path{"png"} / "high" / spot_files[index],
                        texture_quality),
                error);
            if (!bgfx::isValid(impl_->laser_spot_textures[index])) {
                return impl_->fail(std::move(error));
            }
        }
    }
    {
        std::string error;
        impl_->spot_shadow_texture = impl_->load_particle_texture(
            asset_root / "tga" / "spot_shadow.tga", error,
            BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        if (!bgfx::isValid(impl_->spot_shadow_texture)) {
            return impl_->fail(std::move(error));
        }
        impl_->zone_texture = impl_->load_particle_texture(
            asset_root /
                texture_quality_asset("png/high/alpha_block.png",
                                      texture_quality),
            error,
            BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        if (!bgfx::isValid(impl_->zone_texture)) {
            return impl_->fail(std::move(error));
        }
        impl_->solid_zone_texture = impl_->load_particle_texture(
            asset_root /
                texture_quality_asset("png/high/alpha_block_solid.png",
                                      texture_quality),
            error,
            BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        if (!bgfx::isValid(impl_->solid_zone_texture)) {
            return impl_->fail(std::move(error));
        }
    }

    bgfx::setViewName(world_view_id, "BattleSpades world");
    impl_->asset_root = asset_root;
    impl_->initialized = true;
    impl_->last_error.clear();
    rollback.renderer = nullptr;
    return true;
}

void WorldRenderer::shutdown() noexcept {
    if (impl_ == nullptr) {
        return;
    }
    clear_chunks();
    clear_view_model();
    clear_world_models();
    impl_->release_skydome();
    impl_->release_particles();
    impl_->release_lasers();
    impl_->post.shutdown();
    impl_->release_shadow_target();
    impl_->release_skylight();
    if (bgfx::isValid(impl_->retail_ao_texture)) {
        bgfx::destroy(impl_->retail_ao_texture);
        impl_->retail_ao_texture = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(impl_->retail_ao_sampler)) {
        bgfx::destroy(impl_->retail_ao_sampler);
        impl_->retail_ao_sampler = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(impl_->emissive_volume_texture)) {
        bgfx::destroy(impl_->emissive_volume_texture);
        impl_->emissive_volume_texture = BGFX_INVALID_HANDLE;
    }
    for (auto* uniform : {&impl_->skylight_sampler,
                          &impl_->skylight_params_uniform,
                          &impl_->emissive_params_uniform,
                          &impl_->indirect_params_uniform,
                          &impl_->up_axis_uniform,
                          &impl_->model_light_uniform,
                          &impl_->emissive_volume_sampler}) {
        if (bgfx::isValid(*uniform)) {
            bgfx::destroy(*uniform);
            *uniform = BGFX_INVALID_HANDLE;
        }
    }
    if (bgfx::isValid(impl_->shadow_program)) {
        bgfx::destroy(impl_->shadow_program);
        impl_->shadow_program = BGFX_INVALID_HANDLE;
    }
    for (auto* uniform :
         {&impl_->shadow_matrix_uniform, &impl_->shadow_params_uniform, &impl_->shadow_sampler}) {
        if (bgfx::isValid(*uniform)) {
            bgfx::destroy(*uniform);
            *uniform = BGFX_INVALID_HANDLE;
        }
    }
    if (bgfx::isValid(impl_->program)) {
        bgfx::destroy(impl_->program);
        impl_->program = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(impl_->skydome_program)) {
        bgfx::destroy(impl_->skydome_program);
        impl_->skydome_program = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(impl_->camera_uniform)) {
        bgfx::destroy(impl_->camera_uniform);
        impl_->camera_uniform = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(impl_->fog_uniform)) {
        bgfx::destroy(impl_->fog_uniform);
        impl_->fog_uniform = BGFX_INVALID_HANDLE;
    }
    for (auto* uniform : {&impl_->light_uniform,
                          &impl_->sun_direction_uniform,
                          &impl_->sun_color_uniform,
                          &impl_->sky_ambient_uniform,
                          &impl_->ground_ambient_uniform,
                          &impl_->fog_horizon_uniform,
                          &impl_->fog_curve_uniform,
                          &impl_->model_opacity_uniform,
                          &impl_->point_light_position_radius_uniform,
                          &impl_->point_light_color_intensity_uniform,
                          &impl_->retail_light0_direction_uniform,
                          &impl_->retail_light1_direction_uniform,
                          &impl_->retail_light0_color_uniform,
                          &impl_->retail_light1_color_uniform,
                          &impl_->retail_ambient_uniform,
                          &impl_->retail_view_direction_uniform}) {
        if (bgfx::isValid(*uniform)) {
            bgfx::destroy(*uniform);
            *uniform = BGFX_INVALID_HANDLE;
        }
    }
    if (bgfx::isValid(impl_->skydome_sampler)) {
        bgfx::destroy(impl_->skydome_sampler);
        impl_->skydome_sampler = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(impl_->skydome_uv_time)) {
        bgfx::destroy(impl_->skydome_uv_time);
        impl_->skydome_uv_time = BGFX_INVALID_HANDLE;
    }
    impl_->initialized = false;
}

bool WorldRenderer::is_initialized() const noexcept {
    return impl_->initialized;
}

std::string_view WorldRenderer::last_error() const noexcept {
    return impl_->last_error;
}

bool WorldRenderer::set_skydome(std::string_view definition_name) {
    if (!impl_->initialized) {
        return impl_->fail("skydome selection before renderer initialization");
    }
    if (!safe_asset_basename(definition_name, ".txt")) {
        return impl_->fail("unsafe skydome definition name");
    }
    if (impl_->skydome_name == definition_name && !impl_->skydome_slots.empty()) {
        impl_->last_error.clear();
        return true;
    }

    try {
        const auto stem = std::filesystem::path{definition_name}.stem();
        const auto directory = impl_->asset_root / "mesh" / stem;
        const auto definition_path = directory / definition_name;
        std::ifstream input{definition_path};
        if (!input) {
            return impl_->fail("missing retail skydome definition: " + definition_path.string());
        }
        nlohmann::json definition;
        input >> definition;
        const auto render_list = definition.find("render_list");
        if (render_list == definition.end() || !render_list->is_array() || render_list->empty() ||
            render_list->size() > 64U) {
            return impl_->fail("invalid retail skydome render_list: " + definition_path.string());
        }

        const auto vector3 =
            [&](std::string_view table, std::string_view layer, std::array<float, 3U> fallback) {
                const auto table_it = definition.find(table);
                if (table_it == definition.end() || !table_it->is_object()) {
                    return fallback;
                }
                const auto value = table_it->find(layer);
                if (value == table_it->end() || !value->is_array() || value->size() != 3U) {
                    return fallback;
                }
                for (std::size_t index{}; index < fallback.size(); ++index) {
                    if (!(*value)[index].is_number())
                        return fallback;
                    fallback[index] = (*value)[index].get<float>();
                }
                return fallback;
            };
        const auto vector2 =
            [&](std::string_view table, std::string_view layer, std::array<float, 2U> fallback) {
                const auto table_it = definition.find(table);
                if (table_it == definition.end() || !table_it->is_object()) {
                    return fallback;
                }
                const auto value = table_it->find(layer);
                if (value == table_it->end() || !value->is_array() || value->size() != 2U) {
                    return fallback;
                }
                for (std::size_t index{}; index < fallback.size(); ++index) {
                    if (!(*value)[index].is_number())
                        return fallback;
                    fallback[index] = (*value)[index].get<float>();
                }
                return fallback;
            };
        const auto scalar = [&](std::string_view table, std::string_view layer, float fallback) {
            const auto table_it = definition.find(table);
            if (table_it == definition.end() || !table_it->is_object()) {
                return fallback;
            }
            const auto value = table_it->find(layer);
            return value != table_it->end() && value->is_number() ? value->get<float>() : fallback;
        };

        std::vector<Impl::SkydomeSlot> replacement;
        const auto release_replacement = [&replacement]() noexcept {
            for (auto& slot : replacement) {
                Impl::release_skydome_slot(slot);
            }
        };
        for (const auto& entry : *render_list) {
            if (!entry.is_string()) {
                release_replacement();
                return impl_->fail("non-string retail skydome layer");
            }
            const auto layer = entry.get<std::string>();
            if (!safe_asset_basename(layer + ".aos", ".aos")) {
                release_replacement();
                return impl_->fail("unsafe retail skydome layer name");
            }
            auto mesh_path = directory / (layer + ".aos");
            if (!std::filesystem::is_regular_file(mesh_path)) {
                mesh_path = impl_->asset_root / "mesh" / (layer + ".aos");
            }
            std::string error;
            auto meshes = read_aos_meshes(mesh_path, error);
            if (!meshes.has_value()) {
                release_replacement();
                return impl_->fail(std::move(error));
            }

            const auto rotation = vector3("rotation", layer, {});
            const auto translation = vector3("translation", layer, {});
            const auto scale = scalar("scale", layer, 1.0F);
            const auto uv_speed = vector2("uv_speeds", layer, {});
            if (!std::isfinite(scale) ||
                std::ranges::any_of(rotation, [](float value) { return !std::isfinite(value); }) ||
                std::ranges::any_of(translation,
                                    [](float value) { return !std::isfinite(value); }) ||
                std::ranges::any_of(uv_speed, [](float value) { return !std::isfinite(value); })) {
                release_replacement();
                return impl_->fail("non-finite retail skydome transform");
            }

            for (auto& mesh : *meshes) {
                // The retail .aos exporter is Y-up. Bake the authored SRT and
                // convert to our canonical z-down world once at load time;
                // each frame then needs only a translation to the camera.
                // Rotation order follows SkyDome.do_mesh_rotation
                // (gameScene.pyd 0x1010FF40): Rx * Ry * Rz, Z applied first.
                for (auto& vertex : mesh.vertices) {
                    auto position = retail_skydome_rotate(
                        {vertex.x * scale, vertex.y * scale, vertex.z * scale}, rotation);
                    position[0U] += translation[0U];
                    position[1U] += translation[1U];
                    position[2U] += translation[2U];
                    vertex.x = position[0U];
                    vertex.y = position[2U];
                    vertex.z = -position[1U];
                }

                Impl::SkydomeSlot slot;
                // Retail flips V at load (mesh.pyd 0x100042F0: v = 1 - v)
                // and scrolls the flipped coordinate; we keep the raw V, so
                // the V speed must be negated or the clouds drift backwards.
                slot.uv_speed = retail_skydome_uv_speed(uv_speed);
                const auto* vertex_memory = bgfx::copy(
                    mesh.vertices.data(),
                    static_cast<std::uint32_t>(mesh.vertices.size() * sizeof(SkydomeVertex)));
                slot.vertices = bgfx::createVertexBuffer(vertex_memory, impl_->skydome_layout);
                slot.texture = impl_->load_skydome_texture(
                    impl_->asset_root / "tga" / mesh.texture_name, error);
                if (!bgfx::isValid(slot.vertices) || !bgfx::isValid(slot.texture)) {
                    Impl::release_skydome_slot(slot);
                    release_replacement();
                    return impl_->fail(error.empty() ? "bgfx rejected retail skydome mesh"
                                                     : std::move(error));
                }
                replacement.push_back(slot);
            }
        }

        impl_->release_skydome();
        impl_->skydome_slots = std::move(replacement);
        impl_->skydome_name = std::string{definition_name};
        impl_->skydome_clock = std::chrono::steady_clock::now();
        // Derive the map's lighting from the sky the player will stand under.
        // Doing it here inherits this function's transactional guarantee: a
        // malformed dome leaves the previous atmosphere resident exactly as it
        // leaves the previous meshes resident. A server-supplied fog colour
        // that already arrived stays authoritative over the derived one.
        const auto previous_fog = impl_->fog_bytes;
        impl_->atmosphere = world::resolve_map_atmosphere(impl_->asset_root, definition_name);
        impl_->atmosphere.fog_color = previous_fog;
        impl_->last_error.clear();
        return true;
    } catch (const std::exception& exception) {
        return impl_->fail(std::string{"could not load retail skydome: "} + exception.what());
    }
}

std::string_view WorldRenderer::skydome_name() const noexcept {
    return impl_->skydome_name;
}

void WorldRenderer::set_fog_color(std::array<std::uint8_t, 3U> color) noexcept {
    impl_->fog_bytes = color;
    impl_->retail_fog_bytes = color;
    impl_->fog_color = {color[0U] / 255.0F, color[1U] / 255.0F, color[2U] / 255.0F, 1.0F};
    // The scene owner chooses authority: live official maps install their
    // locally measured horizon, while UGC/unknown maps pass through StateData.
    impl_->atmosphere.fog_color = color;
}

void WorldRenderer::set_retail_fog_color(std::array<std::uint8_t, 3U> color) noexcept {
    impl_->retail_fog_bytes = color;
}

std::array<std::uint8_t, 3U> retail_sea_color_for(const world::VxlMap& map,
                                                   world::VxlColor bed_water_color) noexcept {
    auto bed = map.color(0U, 0U, world::VxlMap::height - 1U).value_or(bed_water_color);
    if (bed.red == 0U && bed.green == 0U && bed.blue == 0U && bed.alpha == 0U) {
        bed = bed_water_color;
    }
    return {bed.red, bed.green, bed.blue};
}

void WorldRenderer::set_retail_sea_color(
    std::optional<std::array<std::uint8_t, 3U>> color) noexcept {
    impl_->retail_sea_color = color;
}

void WorldRenderer::set_retail_lighting(const RetailTerrainLighting& lighting) noexcept {
    impl_->retail_lighting = lighting;
}

const RetailTerrainLighting& WorldRenderer::retail_lighting() const noexcept {
    return impl_->retail_lighting;
}

void WorldRenderer::set_model_culling(bool enabled) noexcept {
    if (impl_ != nullptr) {
        impl_->model_culling = enabled;
    }
}

void WorldRenderer::set_quality_profile(const QualityProfile& profile) noexcept {
    impl_->profile = profile;
}

void WorldRenderer::set_post_settings(const PostSettings& settings) noexcept {
    impl_->post_settings = settings;
}

const PostSettings& WorldRenderer::post_settings() const noexcept {
    return impl_->post_settings;
}

PostCapabilities WorldRenderer::post_capabilities() const noexcept {
    if (impl_ == nullptr || !impl_->initialized) {
        return {};
    }
    try {
        return impl_->post.capabilities();
    } catch (...) {
        return {};
    }
}

bool WorldRenderer::post_chain_supported() const noexcept {
    return post_capabilities().chain;
}

void WorldRenderer::set_texture_filtering(const TextureFiltering& filtering) noexcept {
    impl_->texture_filtering = filtering;
}

void WorldRenderer::set_atmosphere(const world::MapAtmosphere& atmosphere) noexcept {
    impl_->atmosphere = atmosphere;
}

void WorldRenderer::set_viewmodel_skylight(float skylight) noexcept {
    impl_->viewmodel_skylight = std::clamp(skylight, 0.0F, 1.0F);
}

void WorldRenderer::set_model_light_sources(const world::StaticLightField* placed,
                                            const world::EmissiveVolume* cast) noexcept {
    impl_->model_placed_lights = placed;
    impl_->model_cast_lights = cast;
}

void WorldRenderer::set_emissive_volume(std::span<const std::uint8_t> cells,
                                        std::uint32_t width,
                                        std::uint32_t depth,
                                        std::uint32_t height) noexcept {
    if (!impl_->initialized || width == 0U || depth == 0U || height == 0U ||
        cells.size() != static_cast<std::size_t>(width) * depth * height * 4U) {
        return;
    }
    if ((bgfx::getCaps()->supported & BGFX_CAPS_TEXTURE_3D) == 0U) {
        // Without 3D textures the world simply renders with self-illumination
        // only, which is a degraded look rather than a broken one.
        return;
    }
    if (!bgfx::isValid(impl_->emissive_volume_texture)) {
        impl_->emissive_volume_texture = bgfx::createTexture3D(
            static_cast<std::uint16_t>(width),
            static_cast<std::uint16_t>(depth),
            static_cast<std::uint16_t>(height),
            false,
            bgfx::TextureFormat::RGBA8,
            // Trilinear and clamped: the whole point is a soft wash, and wrapping
            // would bleed light from the far side of the map.
            BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP);
        if (!bgfx::isValid(impl_->emissive_volume_texture)) {
            return;
        }
    }
    bgfx::updateTexture3D(impl_->emissive_volume_texture,
                          0U,
                          0U,
                          0U,
                          0U,
                          static_cast<std::uint16_t>(width),
                          static_cast<std::uint16_t>(depth),
                          static_cast<std::uint16_t>(height),
                          bgfx::copy(cells.data(), static_cast<std::uint32_t>(cells.size())));
    impl_->emissive_volume_resident = true;
}

void WorldRenderer::set_emissive_cast_gain(float gain) noexcept {
    impl_->emissive_cast_gain = std::isfinite(gain) ? std::clamp(gain, 0.0F, 6.0F) : 2.2F;
}

void WorldRenderer::set_skylight_horizon(std::span<const std::uint8_t> horizon,
                                         std::uint32_t edge) noexcept {
    if (!impl_->initialized || edge == 0U || edge > 4096U ||
        horizon.size() != static_cast<std::size_t>(edge) * edge) {
        return;
    }
    const auto side = static_cast<std::uint16_t>(edge);
    if (!bgfx::isValid(impl_->skylight_texture)) {
        // Bilinear so a doorway's shadow edge is a gradient rather than a
        // staircase across the floor. R8 is enough: the stored horizon is a
        // voxel height in 0..239.
        impl_->skylight_texture =
            bgfx::createTexture2D(side,
                                  side,
                                  false,
                                  1U,
                                  bgfx::TextureFormat::R8,
                                  BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        if (!bgfx::isValid(impl_->skylight_texture)) {
            return;
        }
    }
    bgfx::updateTexture2D(impl_->skylight_texture,
                          0U,
                          0U,
                          0U,
                          0U,
                          side,
                          side,
                          bgfx::copy(horizon.data(), static_cast<std::uint32_t>(horizon.size())));
    impl_->skylight_resident = true;
}

const world::MapAtmosphere& WorldRenderer::atmosphere() const noexcept {
    return impl_->atmosphere;
}

const QualityProfile& WorldRenderer::quality_profile() const noexcept {
    return impl_->profile;
}

void WorldRenderer::set_render_tuning(const RenderTuning& tuning) noexcept {
    impl_->tuning = tuning;
}

const RenderTuning& WorldRenderer::render_tuning() const noexcept {
    return impl_->tuning;
}

TerrainMemory WorldRenderer::terrain_memory() const noexcept {
    TerrainMemory memory;
    for (const auto& slot : impl_->chunks) {
        if (!slot.resident) {
            continue;
        }
        memory.vertex_bytes += slot.vertex_bytes;
        memory.index_bytes += slot.index_bytes;
        ++(slot.packed ? memory.packed_chunks : memory.wide_chunks);
    }
    return memory;
}

bool WorldRenderer::upload_chunk(const world::ChunkMesh& mesh) {
    if (!impl_->initialized) {
        return impl_->fail("world renderer upload before initialization");
    }
    if (mesh.key.x >= 32U || mesh.key.y >= 32U) {
        return impl_->fail("chunk key outside the 32x32 world grid");
    }
    if (!valid_mesh_upload(mesh)) return impl_->fail("invalid chunk mesh coordinates, bounds or indices");
    ChunkSlot replacement;
    if (!impl_->create_chunk_buffers(mesh, replacement)) {
        return impl_->fail("bgfx could not create chunk terrain buffers");
    }
    // Keep the resident mesh until both replacement buffers are available.
    // Invalid updates and resource pressure must not erase working terrain.
    auto& slot = impl_->chunks[mesh.key.x + static_cast<std::size_t>(mesh.key.y) * 32U];
    impl_->release_chunk(slot);
    slot = replacement;
    slot.minimum = mesh.minimum;
    slot.maximum = mesh.maximum;
    if (slot.resident) ++impl_->resident_count;
    impl_->last_error.clear();
    return true;
}

void WorldRenderer::remove_chunk(world::ChunkKey key) noexcept {
    if (key.x >= 32U || key.y >= 32U) {
        return;
    }
    impl_->release_chunk(impl_->chunks[key.x + static_cast<std::size_t>(key.y) * 32U]);
}

void WorldRenderer::clear_chunks() noexcept {
    for (auto& slot : impl_->chunks) {
        impl_->release_chunk(slot);
    }
}

std::size_t WorldRenderer::resident_chunks() const noexcept {
    return impl_->resident_count;
}

bool WorldRenderer::set_view_model_mesh(std::uint32_t slot, const world::ChunkMesh& mesh) {
    if (!impl_->initialized) {
        return impl_->fail("viewmodel upload before initialization");
    }
    if (slot >= view_model_slot_count) {
        return impl_->fail("viewmodel slot out of range");
    }
    if (!valid_mesh_upload(mesh)) return impl_->fail("invalid viewmodel mesh coordinates, bounds or indices");
    Impl::ModelSlot replacement;
    if (!impl_->create_model_slot(mesh, replacement)) {
        return impl_->fail("bgfx could not create viewmodel buffers");
    }
    auto& target = impl_->view_model_slots[slot];
    impl_->release_model_slot(target);
    target = replacement;
    impl_->last_error.clear();
    return true;
}

void WorldRenderer::clear_view_model() noexcept {
    for (auto& slot : impl_->view_model_slots) {
        impl_->release_model_slot(slot);
    }
}

bool WorldRenderer::view_model_mesh_resident(std::uint32_t slot) const noexcept {
    return impl_ != nullptr && impl_->initialized && slot < view_model_slot_count &&
           impl_->view_model_slots[slot].resident;
}

bool WorldRenderer::set_world_model_mesh(std::uint32_t slot, const world::ChunkMesh& mesh) {
    if (!impl_->initialized) {
        return impl_->fail("world-model upload before initialization");
    }
    if (slot >= world_model_slot_count) {
        return impl_->fail("world-model slot out of range");
    }
    if (!valid_mesh_upload(mesh)) return impl_->fail("invalid world-model mesh coordinates, bounds or indices");
    Impl::ModelSlot replacement;
    if (!impl_->create_model_slot(mesh, replacement)) {
        return impl_->fail("bgfx could not create world-model buffers");
    }
    // Model-space bounding sphere, from the vertices themselves (not every
    // producer fills ChunkMesh::minimum/maximum), for main-pass culling.
    if (!mesh.vertices.empty()) {
        std::array<float, 3U> low{mesh.vertices.front().x, mesh.vertices.front().y,
                                  mesh.vertices.front().z};
        auto high = low;
        for (const auto& vertex : mesh.vertices) {
            low = {std::min(low[0U], vertex.x), std::min(low[1U], vertex.y),
                   std::min(low[2U], vertex.z)};
            high = {std::max(high[0U], vertex.x), std::max(high[1U], vertex.y),
                    std::max(high[2U], vertex.z)};
        }
        replacement.bound_centre = {(low[0U] + high[0U]) * 0.5F, (low[1U] + high[1U]) * 0.5F,
                                    (low[2U] + high[2U]) * 0.5F};
        const float ex = high[0U] - low[0U];
        const float ey = high[1U] - low[1U];
        const float ez = high[2U] - low[2U];
        replacement.bound_radius = 0.5F * std::sqrt(ex * ex + ey * ey + ez * ez);
    }
    auto& target = impl_->world_model_slots[slot];
    impl_->release_model_slot(target);
    target = replacement;
    impl_->last_error.clear();
    return true;
}

void WorldRenderer::clear_world_models() noexcept {
    for (auto& slot : impl_->world_model_slots) {
        impl_->release_model_slot(slot);
    }
}

bool WorldRenderer::submit(const WorldCamera& camera,
                           UiExtent drawable,
                           std::span<const ViewModelDraw> view_model,
                           std::span<const WorldModelDraw> world_models,
                           std::span<const world::ParticleInstance> particles,
                           std::span<const world::ParticleBatch> particle_batches,
                           std::span<const world::DynamicLight> dynamic_lights,
                           std::span<const LaserBeamDraw> laser_beams,
                           std::span<const SpotShadowDraw> spot_shadows,
                           std::span<const ZoneVolumeDraw> zone_volumes) {
    if (!impl_->initialized) {
        return impl_->fail("world renderer submit before initialization");
    }
    if (!drawable.is_valid()) {
        return true;
    }

    // Build the view matrix directly from the shared camera basis instead of
    // a look-at helper: the basis guarantees screen-right equals the retail
    // strafe vector, so the image can never mirror against A/D movement.
    const auto basis = world_camera_basis(camera.yaw_degrees, camera.pitch_degrees);
    const bx::Vec3 eye{static_cast<float>(camera.eye[0U]),
                       static_cast<float>(camera.eye[1U]),
                       static_cast<float>(camera.eye[2U])};
    const bx::Vec3 right{static_cast<float>(basis.right[0U]),
                         static_cast<float>(basis.right[1U]),
                         static_cast<float>(basis.right[2U])};
    const bx::Vec3 camera_up{static_cast<float>(basis.up[0U]),
                             static_cast<float>(basis.up[1U]),
                             static_cast<float>(basis.up[2U])};
    const bx::Vec3 forward{static_cast<float>(basis.forward[0U]),
                           static_cast<float>(basis.forward[1U]),
                           static_cast<float>(basis.forward[2U])};

    std::array<float, 16U> view{};
    view[0U] = right.x;
    view[4U] = right.y;
    view[8U] = right.z;
    view[12U] = -(eye.x * right.x + eye.y * right.y + eye.z * right.z);
    view[1U] = camera_up.x;
    view[5U] = camera_up.y;
    view[9U] = camera_up.z;
    view[13U] = -(eye.x * camera_up.x + eye.y * camera_up.y + eye.z * camera_up.z);
    view[2U] = -forward.x;
    view[6U] = -forward.y;
    view[10U] = -forward.z;
    view[14U] = eye.x * forward.x + eye.y * forward.y + eye.z * forward.z;
    view[15U] = 1.0F;

    // Retail fog is radial from the eye, so anything past the fog end is
    // fully faded in every direction and a small far-plane margin suffices.
    // Authored skydomes are roughly 600 units in radius. Keep the projection
    // large enough for them while terrain submission remains independently
    // bounded by the server draw-distance/fog cull below.
    const auto far_plane = std::max(static_cast<float>(camera.fog_distance) + 8.0F, 1024.0F);
    const float aspect = static_cast<float>(drawable.width) / static_cast<float>(drawable.height);
    std::array<float, 16U> projection{};
    bx::mtxProj(projection.data(),
                static_cast<float>(camera.fov_y_degrees),
                aspect,
                static_cast<float>(camera.near_plane),
                far_plane,
                bgfx::getCaps()->homogeneousDepth,
                bx::Handedness::Right);

    // With the post chain active the world and first-person views draw into
    // its offscreen scene (render scale applied); otherwise straight into the
    // backbuffer exactly as before.
    const auto post_frame =
        impl_->post.begin(impl_->post_settings, PostExtent{drawable.width, drawable.height});
    const auto target_width =
        static_cast<std::uint16_t>(post_frame.active ? post_frame.scene.width : drawable.width);
    const auto target_height =
        static_cast<std::uint16_t>(post_frame.active ? post_frame.scene.height : drawable.height);
    const std::uint16_t first_person_view =
        post_frame.active ? post_frame.view_model_view : view_model_view_id;
    bgfx::setViewRect(world_view_id, 0U, 0U, target_width, target_height);
    const auto& active_fog = impl_->profile.enhanced_lighting
        ? impl_->fog_bytes : impl_->retail_fog_bytes;
    const auto sky_clear_rgba = (static_cast<std::uint32_t>(active_fog[0U]) << 24U) |
                                (static_cast<std::uint32_t>(active_fog[1U]) << 16U) |
                                (static_cast<std::uint32_t>(active_fog[2U]) << 8U) | 0xFFU;
    bgfx::setViewClear(
        world_view_id, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, sky_clear_rgba, 1.0F, 0U);
    // Sequential keeps the translucent water plane after every opaque chunk.
    bgfx::setViewMode(world_view_id, bgfx::ViewMode::Sequential);
    bgfx::setViewTransform(world_view_id, view.data(), projection.data());
    bgfx::touch(world_view_id);

    if (!impl_->skydome_slots.empty()) {
        std::array<float, 16U> sky_transform{};
        // GameScene.draw passes the camera as SkyDome.draw(x, -z, y) and the
        // dome is translated to (x, y - 35, z) in GL (gameScene.pyd
        // 0x101AB5F8: PyInt_FromLong(0x23)), i.e. 35 blocks BELOW the eye
        // before each layer's own authored translation. The Retail tier keeps
        // that offset; the horizon mountains otherwise sit ~10 px too high.
        const float sky_drop = impl_->profile.enhanced_lighting ? 0.0F : 35.0F;
        const bx::Vec3 sky_center = camera.sky_anchor.has_value()
            ? bx::Vec3{static_cast<float>((*camera.sky_anchor)[0U]),
                       static_cast<float>((*camera.sky_anchor)[1U]),
                       static_cast<float>((*camera.sky_anchor)[2U])}
            : eye;
        bx::mtxTranslate(
            sky_transform.data(), sky_center.x, sky_center.y, sky_center.z + sky_drop);
        const auto elapsed_seconds =
            std::chrono::duration<float>(std::chrono::steady_clock::now() - impl_->skydome_clock)
                .count();
        // Retail's shader receives SkyDome.time_counted, incremented once per
        // draw, rather than seconds. Convert the monotonic clock to equivalent
        // 60 Hz draw ticks so cloud speed is faithful but refresh-rate stable.
        const auto retail_time = retail_skydome_time(elapsed_seconds);
        for (const auto& slot : impl_->skydome_slots) {
            const std::array<float, 4U> uv_time{
                slot.uv_speed[0U], slot.uv_speed[1U], retail_time, 0.0F};
            bgfx::setTransform(sky_transform.data());
            bgfx::setVertexBuffer(0U, slot.vertices);
            bgfx::setTexture(0U, impl_->skydome_sampler, slot.texture,
                             impl_->world_sampler(BGFX_SAMPLER_NONE));
            bgfx::setUniform(impl_->skydome_uv_time, uv_time.data());
            // The exporter deliberately uses negative scale so the camera
            // sees the triangle stream from inside. No culling matches the
            // retail GL pass and keeps sparse billboard layers visible.
            // Blend authored sky layers without exporting their opacity to
            // the native swapchain. Vulkan may inherit the surface composite
            // alpha mode, in which case a translucent sky alpha would make
            // the Windows desktop visible through the game.
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_ALPHA | BGFX_STATE_MSAA);
            bgfx::submit(world_view_id, impl_->skydome_program);
        }
    }

    std::array<float, 16U> view_projection{};
    bx::mtxMul(view_projection.data(), view.data(), projection.data());
    const auto planes = frustum_planes(view_projection);

    const std::array<float, 4U> camera_uniform{eye.x, eye.y, eye.z, 0.0F};
    const std::array<float, 4U> fog_uniform{active_fog[0U] / 255.0F,
                                            active_fog[1U] / 255.0F,
                                            active_fog[2U] / 255.0F,
                                            static_cast<float>(camera.fog_distance)};

    // Terrain shading mode. KV6 models, effect cubes and the viewmodel bake
    // their own face shade at mesh time, so they pass through unlit; only the
    // chunk pass, whose albedo is pure, is shaded here.
    const bool enhanced = impl_->profile.enhanced_lighting;
    // Every lighting value now comes from the map's own sky rather than a
    // constant, so a moonlit night city and a desert noon no longer light
    // their terrain identically.
    const auto& atmosphere = impl_->atmosphere;
    const std::array<float, 4U> terrain_light{
        enhanced ? 2.0F : 1.0F,
        0.85F,
        matte_voxel_specular(atmosphere.specular_strength),
        // A tight highlight reads as a small surface glint. The old exponent
        // of 24 spread the same highlight across a whole axis-aligned face and
        // made dirt, stone, and uniforms look injection-moulded.
        48.0F};
    // Models used to render as pure unlit albedo, so a player in a shadowed
    // trench looked identical to one in full sun. They now take the same
    // lighting as terrain: their meshes carry pure albedo and a face index in
    // the render basis, which is exactly what the shader needs.
    const std::array<float, 4U>& model_light = terrain_light;
    const std::array<float, 4U> sun_direction{atmosphere.sun_direction[0U],
                                              atmosphere.sun_direction[1U],
                                              atmosphere.sun_direction[2U],
                                              atmosphere.key_intensity};
    const std::array<float, 4U> sun_color{atmosphere.sun_color[0U],
                                          atmosphere.sun_color[1U],
                                          atmosphere.sun_color[2U],
                                          atmosphere.ambient_intensity};
    const std::array<float, 4U> sky_ambient{
        atmosphere.sky_ambient[0U], atmosphere.sky_ambient[1U], atmosphere.sky_ambient[2U], 0.0F};
    const std::array<float, 4U> ground_ambient{atmosphere.ground_ambient[0U],
                                               atmosphere.ground_ambient[1U],
                                               atmosphere.ground_ambient[2U],
                                               0.0F};
    // Terrain fades into the colour the sky is painting immediately above the
    // horizon instead of into a flat constant, so there is no seam where the
    // world meets the dome.
    const std::array<float, 4U> fog_horizon{
        static_cast<float>(atmosphere.horizon_color[0U]) / 255.0F,
        static_cast<float>(atmosphere.horizon_color[1U]) / 255.0F,
        static_cast<float>(atmosphere.horizon_color[2U]) / 255.0F,
        0.0F};
    // Retail GameScene configures GL_LINEAR fog from draw_distance / 2 to
    // draw_distance. Enhanced tiers ignore y and retain the authored density.
    const std::array<float, 4U> fog_curve{
        atmosphere.fog_density, 0.5F, atmosphere.exposure, 0.0F};

    // Keep the bounded forward-light array filled with the lights that matter
    // most to this camera. Score by received energy, not only distance: a large
    // rocket blast should outrank a tiny drill spark a little closer to the eye.
    std::array<const world::DynamicLight*, maximum_dynamic_lights> selected_lights{};
    std::array<float, maximum_dynamic_lights> selected_scores{};
    const std::size_t light_limit =
        enhanced ? std::min<std::size_t>(impl_->profile.dynamic_lights, maximum_dynamic_lights)
                 : 0U;
    for (const auto& light : dynamic_lights) {
        if (light.radius <= 0.0F || light.intensity <= 0.0F || light_limit == 0U) {
            continue;
        }
        const float dx = light.position[0U] - eye.x;
        const float dy = light.position[1U] - eye.y;
        const float dz = light.position[2U] - eye.z;
        const float score =
            light.intensity * light.radius * light.radius / (1.0F + dx * dx + dy * dy + dz * dz);
        std::size_t insert = light_limit;
        for (std::size_t index{}; index < light_limit; ++index) {
            if (score > selected_scores[index]) {
                insert = index;
                break;
            }
        }
        if (insert == light_limit) {
            continue;
        }
        for (std::size_t index = light_limit - 1U; index > insert; --index) {
            selected_scores[index] = selected_scores[index - 1U];
            selected_lights[index] = selected_lights[index - 1U];
        }
        selected_scores[insert] = score;
        selected_lights[insert] = &light;
    }
    std::array<std::array<float, 4U>, maximum_dynamic_lights> point_light_position_radius{};
    std::array<std::array<float, 4U>, maximum_dynamic_lights> point_light_color_intensity{};
    for (std::size_t index{}; index < light_limit; ++index) {
        const auto* light = selected_lights[index];
        if (light == nullptr) {
            continue;
        }
        point_light_position_radius[index] = {
            light->position[0U], light->position[1U], light->position[2U], light->radius};
        point_light_color_intensity[index] = {
            light->color[0U], light->color[1U], light->color[2U], light->intensity};
    }

    // ---- Sun shadow cascade -------------------------------------------------
    impl_->stats = {};
    impl_->stats.chunks_resident = impl_->resident_count;
    // One orthographic cascade centred on the camera. The extent is a
    // fraction of the draw distance rather than the whole map: at 512 blocks
    // wide a map-covering cascade would put several blocks in every shadow
    // texel and lose all contact detail, which is the whole point of shadows.
    const bool want_shadows = enhanced && atmosphere.key_intensity > 0.0F &&
                              impl_->profile.shadow_cascades > 0U &&
                              impl_->profile.shadow_resolution > 0U;
    const bool shadows_ready =
        want_shadows && impl_->ensure_shadow_target(impl_->profile.shadow_resolution);
    std::array<float, 16U> shadow_matrix{};
    bx::mtxIdentity(shadow_matrix.data());
    std::array<float, 16U> identity_shadow{};
    bx::mtxIdentity(identity_shadow.data());
    float shadow_bias = 0.0F;
    if (shadows_ready) {
        // QualityProfile::shadow_distance picks the half-extent; zero keeps
        // the fog-relative default.
        const float extent = impl_->profile.shadow_distance > 0.0F
            ? std::clamp(impl_->profile.shadow_distance, 16.0F, 256.0F)
            : std::clamp(static_cast<float>(camera.fog_distance) * 0.55F, 32.0F, 160.0F);
        const auto* caps = bgfx::getCaps();
        const auto shadow = sun_shadow_projection(
            camera.eye, atmosphere.sun_direction,
            {static_cast<float>(world::VxlMap::width),
             static_cast<float>(world::VxlMap::depth),
             static_cast<float>(world::VxlMap::height)},
            extent, impl_->shadow_resolution, caps->homogeneousDepth, caps->originBottomLeft);
        shadow_matrix = shadow.world_to_texture;
        shadow_bias = sun_shadow_depth_bias(shadow.depth_span);

        const auto cascade_view = shadow_view_id_base;
        bgfx::setViewName(cascade_view, "BattleSpades sun cascade");
        bgfx::setViewRect(cascade_view, 0U, 0U, impl_->shadow_resolution, impl_->shadow_resolution);
        bgfx::setViewFrameBuffer(cascade_view, impl_->shadow_target);
        bgfx::setViewClear(cascade_view, BGFX_CLEAR_DEPTH, 0U, 1.0F, 0U);
        bgfx::setViewMode(cascade_view, bgfx::ViewMode::Default);
        bgfx::setViewTransform(cascade_view, shadow.view.data(), shadow.projection.data());
        bgfx::touch(cascade_view);

        // Depth only, and deliberately unculled. Culling one winding here would
        // be slightly cheaper and would push self-shadowing acne behind the
        // visible surface, but it silently records nothing at all if the
        // assumed winding is wrong, which looks identical to shadows being
        // broken. Correctness first; the slope-scaled bias handles the acne.
        constexpr std::uint64_t caster_state = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
        for (const auto& slot : impl_->chunks) {
            if (!slot.resident) {
                continue;
            }
            if (!sun_shadow_intersects(shadow_matrix, slot.minimum, slot.maximum,
                    3.0F / static_cast<float>(impl_->shadow_resolution))) {
                ++impl_->stats.shadow_chunks_culled;
                continue;
            }
            bgfx::setTransform(identity_shadow.data());
            bgfx::setVertexBuffer(0U, slot.vertices);
            bgfx::setIndexBuffer(slot.indices);
            bgfx::setState(caster_state);
            bgfx::submit(cascade_view, impl_->shadow_program);
            ++impl_->stats.shadow_chunks_submitted;
        }
        for (const auto& draw : world_models) {
            if (draw.slot >= world_model_slot_count || draw.opacity < 0.999F) {
                continue;
            }
            const auto& slot = impl_->world_model_slots[draw.slot];
            if (!slot.resident) {
                continue;
            }
            bgfx::setTransform(draw.transform.data());
            bgfx::setVertexBuffer(0U, slot.vertices);
            bgfx::setIndexBuffer(slot.indices);
            bgfx::setState(caster_state);
            bgfx::submit(cascade_view, impl_->shadow_program);
        }
    }
    const std::array<float, 4U> shadow_params{
        shadows_ready ? 1.0F : 0.0F,
        shadows_ready ? 1.0F / static_cast<float>(impl_->shadow_resolution) : 0.0F,
        shadow_bias,
        // Filter radius in texels, and the soft-path switch in one component:
        // zero selects the single hardware tap, anything above it selects the
        // fixed disc. Encoding both keeps the uniform at four floats.
        impl_->profile.shadow_pcf_taps > 1U ? impl_->profile.shadow_softness : 0.0F};

    // Interiors darken over roughly six blocks of depth below cover, bottoming
    // out at a fifth of full skylight. Not zero: a sealed room still receives
    // bounced light through its doorway, and pitch black is unplayable.
    const bool skylight_ready = enhanced && impl_->skylight_resident;
    // How much skylight a fully enclosed interior keeps.
    //
    // A flat fraction compounds badly: on a dusk map whose ambient is already
    // low, 26% of a small number is near-black and the interior is unplayable.
    // Solving for a fixed ABSOLUTE interior brightness instead means a bunker
    // reads about equally legible on a desert noon and a moonlit night, which is
    // both what a player needs and closer to how eyes actually adapt.
    // Raised after playtesting Tokyo: a night map's ambient is already low, so
    // multiplying it again under cover left a neon street reading as dead. An
    // absolute target means a covered area is equally legible on a desert noon
    // and a moonlit city, which is the point of solving for absolute rather than
    // relative brightness.
    constexpr float absolute_interior_light{0.22F};
    const float interior_fraction = std::clamp(
        absolute_interior_light / std::max(atmosphere.ambient_intensity, 1.0e-3F), 0.26F, 0.75F);
    const std::array<float, 4U> skylight_params{
        skylight_ready ? 1.0F : 0.0F,
        1.0F / static_cast<float>(world::VxlMap::width),
        6.0F,
        // With the probe off, w is a flat multiplier, so it must be 1.0 or the
        // whole world would darken by the interior fraction.
        skylight_ready ? interior_fraction : 1.0F};
    // The viewmodel is drawn in VIEW space, so its v_world is not a world
    // position and the horizon probe would read an unrelated column. Disable the
    // lookup for that pass: a held weapon staying readable is also what every
    // shooter does deliberately, rather than dimming the gun into the floor.
    // The viewmodel cannot probe the horizon texture -- it is drawn in view
    // space, so its position is not a world position. Instead the caller samples
    // the horizon at the camera and hands the result over, applied here as a
    // flat multiplier. x = 0 disables the texture lookup; w carries the scale,
    // which the shader uses directly as the fully-enclosed value with depth
    // pinned at 1.0 so it always applies.
    const std::array<float, 4U> viewmodel_skylight{0.0F, 0.0F, 1.0F, impl_->viewmodel_skylight};
    // Legacy leaves this at zero: retail had no emissive map voxels at all, so a
    // parity capture must show none.
    const std::array<float, 4U> emissive_params{
        enhanced ? impl_->profile.emissive_gain : 0.0F,
        // Placed lights ride the same tier gate. 1.6 rather than 1.0 because a
        // lamp you carried and placed should read as a clear pool of light, not
        // a faint tint on the wall.
        enhanced && impl_->profile.emissive_gain > 0.0F ? 1.6F : 0.0F,
        0.0F,
        0.0F};
    const bool volume_ready =
        enhanced && impl_->emissive_volume_resident && impl_->profile.emissive_gain > 0.0F;
    const std::array<float, 4U> indirect_params{
        // Cast light carries more gain than self-illumination: it has already
        // been attenuated by distance and occlusion, so it arrives dim.
        volume_ready ? impl_->emissive_cast_gain : 0.0F,
        // Sun bounce. Scaled by key intensity in the shader, so an overcast map
        // with almost no key gets almost no bounce, which is correct.
        enhanced ? 0.30F : 0.0F,
        // World-to-volume UV. Separate xy and z scales: the map is 512 wide but
        // only 240 tall, so one shared scale would sample the wrong slice.
        1.0F / static_cast<float>(world::VxlMap::width),
        1.0F / static_cast<float>(world::VxlMap::height)};
    // ---- Viewmodel-only overrides -------------------------------------------
    // The viewmodel view is submitted with an IDENTITY view matrix, so each of
    // its draw matrices is a VIEW-space placement: vs_world makes v_world a
    // view-space POSITION and the face normal a view-space NORMAL. Every uniform
    // the fragment shader interprets in world space therefore has to be
    // re-expressed or switched off, for this pass only.
    static constexpr std::array<float, 4U> world_up{0.0F, 0.0F, -1.0F, 0.0F};
    // The view basis is built by hand just above rather than with mtxLookAt, so
    // that screen-right is exactly the retail strafe vector. Under that layout
    // rotating a world direction into view space is three dot products, with the
    // forward axis negated because view space looks down -z.
    const auto to_view = [&](float x, float y, float z) {
        return std::array<float, 4U>{(x * right.x) + (y * right.y) + (z * right.z),
                                     (x * camera_up.x) + (y * camera_up.y) + (z * camera_up.z),
                                     -((x * forward.x) + (y * forward.y) + (z * forward.z)),
                                     0.0F};
    };

    auto viewmodel_point_light_position_radius = point_light_position_radius;
    for (std::size_t index{}; index < light_limit; ++index) {
        if (selected_lights[index] == nullptr) {
            continue;
        }
        const auto& position = selected_lights[index]->position;
        const float x = position[0U] - eye.x;
        const float y = position[1U] - eye.y;
        const float z = position[2U] - eye.z;
        viewmodel_point_light_position_radius[index][0U] = x * right.x + y * right.y + z * right.z;
        viewmodel_point_light_position_radius[index][1U] =
            x * camera_up.x + y * camera_up.y + z * camera_up.z;
        viewmodel_point_light_position_radius[index][2U] =
            -(x * forward.x + y * forward.y + z * forward.z);
    }

    // The key light. A world-space sun dotted against a view-space normal is a
    // sun bolted to the camera, so the hands never change tone as the player
    // turns. Worse: canonical space is z-down, so a sun always carries a large
    // negative z, and read as view space that means "into the screen" -- the
    // fake key sat BEHIND the viewmodel and every camera-facing face was pinned
    // on the max(dot(N,L), 0.28) floor.
    auto viewmodel_sun_direction = to_view(
        atmosphere.sun_direction[0U], atmosphere.sun_direction[1U], atmosphere.sun_direction[2U]);
    viewmodel_sun_direction[3U] = atmosphere.key_intensity;

    // The hemispheric ambient's "up". This is the largest term on the hands, so
    // without it the key fix above is invisible. With up hardcoded to -z, a
    // view-space normal makes the arm's top and its underside both read as
    // side-on and receive identical ambient, while the face pointing at the
    // camera is lit as though it faced the ground.
    const auto viewmodel_up = to_view(0.0F, 0.0F, -1.0F);

    // The specular view point. The eye is the ORIGIN of this space. The previous
    // value was the part's own translation -- the image of the KV6 pivot, which
    // sits INSIDE the mesh -- so the view vector pointed from the surface into
    // the model, roughly reversed from the true eye vector. That drove
    // dot(N, half) negative and made the specular term exactly zero on every
    // camera-facing face. Fog does not need the old trick: viewmodel fragments
    // sit a unit or two out against a fog distance measured in blocks, so the
    // exp2 curve returns far less than one 8-bit step.
    static constexpr std::array<float, 4U> viewmodel_camera{0.0F, 0.0F, 0.0F, 0.0F};

    // Cast shadows off. v_shadow is built from v_world unconditionally, and here
    // that is a view-space point about a unit from the origin -- which lands in
    // the WORLD near the map's corner. Whenever the player stands within the
    // cascade extent of that corner, the arms sample real depth texels from an
    // unrelated column and flicker as they animate. Zeroing this also skips the
    // whole shadow block, saving up to nine taps per viewmodel fragment.
    static constexpr std::array<float, 4U> viewmodel_shadow_params{0.0F, 0.0F, 0.0F, 0.0F};

    // Emissive-volume probe off, same cause: its UV is v_world scaled by the map
    // size, so a view-space position collapses to cell zero. The sun bounce in y
    // uses no position, so it stays.
    const std::array<float, 4U> viewmodel_indirect{
        0.0F, indirect_params[1U], indirect_params[2U], indirect_params[3U]};

    // The viewmodel's own light params, finally split off terrain's -- which was
    // a reference, so there was previously no way to tune the hands without
    // moving the world. x must mirror terrain's mode or Legacy takes the
    // enhanced branch. Specular stays at zero: it is currently zero on every
    // camera-facing face, so the eye-vector fix above would switch it on for the
    // first time, and with six axis-aligned normals it would act as a per-face
    // switch rather than a highlight. The exponent is widened anyway so that
    // when it is turned on it can fall off ACROSS a face instead of flipping it.
    const std::array<float, 4U> viewmodel_light{enhanced ? 2.0F : 1.0F, 1.0F, 0.0F, 8.0F};

    const auto& retail = impl_->retail_lighting;
    const std::array<float, 4U> retail_light0_direction{
        retail.light_direction[0U], retail.light_direction[1U],
        retail.light_direction[2U], 0.0F};
    const std::array<float, 4U> retail_light1_direction{
        retail.back_light_direction[0U], retail.back_light_direction[1U],
        retail.back_light_direction[2U], 0.0F};
    const std::array<float, 4U> retail_light0_color{
        retail.light_color[0U], retail.light_color[1U], retail.light_color[2U], 1.0F};
    const std::array<float, 4U> retail_light1_color{
        retail.back_light_color[0U], retail.back_light_color[1U],
        retail.back_light_color[2U], 1.0F};
    const std::array<float, 4U> retail_ambient{
        retail.ambient_color[0U], retail.ambient_color[1U],
        retail.ambient_color[2U], retail.ambient_intensity};
    // The fixed-function half vector assumes an infinite viewer. Express the
    // direction from a fragment toward that viewer in retail's x/y-up/z basis.
    const std::array<float, 4U> retail_view_direction{
        -forward.x, forward.z, -forward.y, 0.0F};
    const auto retail_to_view = [&](const std::array<float, 3U>& direction) {
        const auto transformed = to_view(direction[0U], direction[2U], -direction[1U]);
        return std::array<float, 4U>{transformed[0U], -transformed[2U], transformed[1U], 0.0F};
    };
    const auto viewmodel_retail_light0 = retail_to_view(retail.light_direction);
    const auto viewmodel_retail_light1 = retail_to_view(retail.back_light_direction);
    static constexpr std::array<float, 4U> viewmodel_retail_eye{0.0F, -1.0F, 0.0F, 0.0F};

    // ---- Map light on models (enhanced tiers) --------------------------------
    // Terrain receives placed flare/fire light through the mesher's per-vertex
    // bake and emissive spill through the volume probe. KV6 models have no
    // bake, and the view model is drawn in view space where the probe reads
    // an unrelated cell, so the hands and player models stayed dark under a
    // lamp that lit the room around them. Sample both sources on the CPU with
    // terrain's own gains. Retail keeps model_frag's packet-45-only lighting.
    static constexpr std::array<float, 4U> no_model_light{};
    const float model_placed_gain = emissive_params[1U];
    const float model_cast_gain = indirect_params[0U];
    const bool model_light_enabled =
        enhanced && (model_placed_gain > 0.0F || model_cast_gain > 0.0F) &&
        (impl_->model_placed_lights != nullptr || impl_->model_cast_lights != nullptr);
    const auto model_light_at = [&](std::array<float, 3U> position, bool include_cast) {
        std::array<float, 4U> result{};
        if (!model_light_enabled) {
            return result;
        }
        const auto sample = world::sample_model_light(impl_->model_placed_lights,
                                                      impl_->model_cast_lights, position);
        const auto rgb = world::model_light_rgb(sample, model_placed_gain,
                                                include_cast ? model_cast_gain : 0.0F);
        result = {rgb[0U], rgb[1U], rgb[2U], 0.0F};
        return result;
    };
    // The view model sits at the eye; it cannot probe the volume in-shader.
    const auto viewmodel_model_light =
        model_light_at({eye.x, eye.y, eye.z}, true);

    // One writer for the whole submit: world, particle and view-model draws
    // are all in Sequential views submitted in render order.
    UniformWriter uniforms{impl_->tuning.cached_uniforms};
    const auto push_atmosphere = [&] {
        uniforms.set(impl_->sun_direction_uniform, sun_direction.data());
        uniforms.set(impl_->sun_color_uniform, sun_color.data());
        // Fail closed: every terrain, world-model and shadow submit gets world
        // up whether or not its call site remembers to.
        uniforms.set(impl_->up_axis_uniform, world_up.data());
        // Terrain carries its own placed light in the vertex bake; only model
        // draws override this after push_atmosphere.
        uniforms.set(impl_->model_light_uniform, no_model_light.data());
        uniforms.set(impl_->sky_ambient_uniform, sky_ambient.data());
        uniforms.set(impl_->ground_ambient_uniform, ground_ambient.data());
        uniforms.set(impl_->fog_horizon_uniform, fog_horizon.data());
        uniforms.set(impl_->fog_curve_uniform, fog_curve.data());
        uniforms.set(impl_->shadow_matrix_uniform, shadow_matrix.data(), 1U, 16U);
        uniforms.set(impl_->shadow_params_uniform, shadow_params.data());
        uniforms.set(impl_->skylight_params_uniform, skylight_params.data());
        uniforms.set(impl_->emissive_params_uniform, emissive_params.data());
        uniforms.set(impl_->indirect_params_uniform, indirect_params.data());
        uniforms.set(impl_->point_light_position_radius_uniform,
                         point_light_position_radius.data(),
                         static_cast<std::uint16_t>(maximum_dynamic_lights));
        uniforms.set(impl_->point_light_color_intensity_uniform,
                         point_light_color_intensity.data(),
                         static_cast<std::uint16_t>(maximum_dynamic_lights));
        uniforms.set(impl_->retail_light0_direction_uniform,
                         retail_light0_direction.data());
        uniforms.set(impl_->retail_light1_direction_uniform,
                         retail_light1_direction.data());
        uniforms.set(impl_->retail_light0_color_uniform, retail_light0_color.data());
        uniforms.set(impl_->retail_light1_color_uniform, retail_light1_color.data());
        uniforms.set(impl_->retail_ambient_uniform, retail_ambient.data());
        uniforms.set(impl_->retail_view_direction_uniform,
                         retail_view_direction.data());
        bgfx::setTexture(0U, impl_->retail_ao_sampler, impl_->retail_ao_texture,
                         impl_->world_sampler(BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP));
        if (volume_ready) {
            bgfx::setTexture(3U, impl_->emissive_volume_sampler, impl_->emissive_volume_texture);
        }
        if (shadows_ready) {
            bgfx::setTexture(1U, impl_->shadow_sampler, impl_->shadow_texture);
        }
        if (skylight_ready) {
            bgfx::setTexture(2U, impl_->skylight_sampler, impl_->skylight_texture);
        }
    };
    static constexpr std::array<float, 4U> opaque_model{1.0F, 0.0F, 0.0F, 0.0F};

    const auto fog_limit = static_cast<float>(camera.fog_distance);
    std::array<float, 16U> identity{};
    bx::mtxIdentity(identity.data());
    auto& visible_chunks = impl_->visible_chunks;
    visible_chunks.clear();
    for (std::size_t chunk{}; chunk < impl_->chunks.size(); ++chunk) {
        const auto& slot = impl_->chunks[chunk];
        if (!slot.resident) {
            continue;
        }
        // Radial fog-distance culling against the nearest AABB point.
        const float nearest_x = std::clamp(eye.x, slot.minimum[0U], slot.maximum[0U]) - eye.x;
        const float nearest_y = std::clamp(eye.y, slot.minimum[1U], slot.maximum[1U]) - eye.y;
        const float nearest_z = std::clamp(eye.z, slot.minimum[2U], slot.maximum[2U]) - eye.z;
        const float nearest_squared =
            nearest_x * nearest_x + nearest_y * nearest_y + nearest_z * nearest_z;
        if (nearest_squared > fog_limit * fog_limit) {
            continue;
        }
        bool culled = false;
        for (const auto& plane : planes) {
            if (aabb_outside(plane, slot.minimum, slot.maximum)) {
                culled = true;
                break;
            }
        }
        if (culled) {
            continue;
        }
        visible_chunks.emplace_back(nearest_squared, static_cast<std::uint16_t>(chunk));
    }
    // Opaque terrain drawn nearest first lets the depth test reject the
    // fragments behind it before they are shaded. The image is the same in any
    // order: chunks never share a surface, and the test is a strict LESS.
    if (impl_->tuning.front_to_back_terrain) {
        std::ranges::stable_sort(visible_chunks, {},
                                 [](const auto& entry) { return entry.first; });
    }
    for (const auto& [distance_squared, chunk] : visible_chunks) {
        static_cast<void>(distance_squared);
        const auto& slot = impl_->chunks[chunk];
        bgfx::setTransform(identity.data());
        bgfx::setVertexBuffer(0U, slot.vertices);
        bgfx::setIndexBuffer(slot.indices);
        uniforms.set(impl_->camera_uniform, camera_uniform.data());
        uniforms.set(impl_->fog_uniform, fog_uniform.data());
        uniforms.set(impl_->light_uniform, terrain_light.data());
        uniforms.set(impl_->model_opacity_uniform, opaque_model.data());
        push_atmosphere();
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                       BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CW | BGFX_STATE_MSAA);
        bgfx::submit(world_view_id, impl_->program);
        ++impl_->stats.chunks_submitted;
    }

    // vxl.pyd draw_sea (sub_10030xxx builds it next to the map): one GL quad
    // from -1000 to 1000 at GL y -239.1, i.e. canonical z 239.6 once the
    // retail half-voxel GL offset is removed, so the bed's own top faces hide
    // it everywhere inside the map. Records are {pos, normal code 100..103,
    // bed colour, AO cell (0.375|0.475, 0.625)}; sea_frag samples the grain
    // at noise*2000 with GL_REPEAT and fogs per fragment (see fs_world.sc).
    if (!enhanced && impl_->retail_sea_color.has_value() &&
        bgfx::getAvailTransientVertexBuffer(4U, impl_->layout) >= 4U &&
        bgfx::getAvailTransientIndexBuffer(6U) >= 6U) {
        const auto& sea = *impl_->retail_sea_color;
        const std::uint32_t abgr = 0xFF000000U | (static_cast<std::uint32_t>(sea[2U]) << 16U) |
                                   (static_cast<std::uint32_t>(sea[1U]) << 8U) | sea[0U];
        constexpr float low = -999.5F;
        constexpr float high = 1000.5F;
        constexpr float level = 239.6F;
        const auto vertex = [&](float x, float y, std::uint8_t noise, float ao_u) {
            return world::ChunkVertex{x, y, level, abgr, 4U, 0U, noise, 0U, 0xFF000000U,
                                      ao_u, 0.625F, ao_u, 0.625F, 1.0F};
        };
        // Emission order and noise corners of the retail record; vertex 1
        // really carries u = 0.475 in the shipped client.
        const std::array<world::ChunkVertex, 4U> quad{
            vertex(low, low, 0U, 0.375F), vertex(low, high, 1U, 0.475F),
            vertex(high, high, 3U, 0.375F), vertex(high, low, 2U, 0.375F)};
        static constexpr std::array<std::uint16_t, 6U> quad_indices{0U, 1U, 2U, 0U, 2U, 3U};
        bgfx::TransientVertexBuffer vertices{};
        bgfx::TransientIndexBuffer index_buffer{};
        bgfx::allocTransientVertexBuffer(&vertices, 4U, impl_->layout);
        bgfx::allocTransientIndexBuffer(&index_buffer, 6U);
        std::memcpy(vertices.data, quad.data(), sizeof(quad));
        std::memcpy(index_buffer.data, quad_indices.data(), sizeof(quad_indices));
        static constexpr std::array<float, 4U> sea_mode{1.0F, 0.0F, 0.0F, 1.0F};
        bgfx::setTransform(identity.data());
        bgfx::setVertexBuffer(0U, &vertices);
        bgfx::setIndexBuffer(&index_buffer);
        uniforms.set(impl_->camera_uniform, camera_uniform.data());
        uniforms.set(impl_->fog_uniform, fog_uniform.data());
        uniforms.set(impl_->light_uniform, terrain_light.data());
        uniforms.set(impl_->model_opacity_uniform, sea_mode.data());
        push_atmosphere();
        // draw_sea binds the atlas with GL_REPEAT (the map pass uses CLAMP).
        bgfx::setTexture(0U, impl_->retail_ao_sampler, impl_->retail_ao_texture,
                         impl_->texture_filtering == TextureFiltering{}
                             ? BGFX_SAMPLER_NONE
                             : impl_->world_sampler(BGFX_SAMPLER_NONE));
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                       BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA);
        bgfx::submit(world_view_id, impl_->program);
    }

    // Retail spot_shadow.tga decals: every live character (local player
    // included) plus entities with needs_shadow, e.g. the HealthCrate.
    // Draw the soft contact decal after terrain establishes depth and before
    // opaque entities, so the crate itself covers the shadow while the decal
    // cannot appear through a nearer wall.
    if (!spot_shadows.empty() && bgfx::isValid(impl_->spot_shadow_texture) &&
        bgfx::isValid(impl_->skydome_program)) {
        static constexpr std::array<std::uint16_t, 6U> indices{
            0U, 1U, 2U, 0U, 2U, 3U};
        static constexpr std::array<float, 4U> static_uv{};
        constexpr std::uint64_t state =
            BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LESS |
            BGFX_STATE_BLEND_ALPHA | BGFX_STATE_MSAA;
        for (const auto& shadow : spot_shadows) {
            if (!(shadow.size > 0.0F) || !(shadow.opacity > 0.0F) ||
                bgfx::getAvailTransientVertexBuffer(4U, impl_->skydome_layout) < 4U ||
                bgfx::getAvailTransientIndexBuffer(6U) < 6U) {
                continue;
            }
            bgfx::TransientVertexBuffer vertices{};
            bgfx::TransientIndexBuffer index_buffer{};
            bgfx::allocTransientVertexBuffer(&vertices, 4U, impl_->skydome_layout);
            bgfx::allocTransientIndexBuffer(&index_buffer, 6U);
            const float half = shadow.size * 0.5F;
            const float alpha = std::clamp(shadow.opacity, 0.0F, 1.0F);
            const auto vertex = [&](float x, float y, float u, float v) {
                return SkydomeVertex{
                    shadow.position[0U] + x,
                    shadow.position[1U] + y,
                    shadow.position[2U],
                    1.0F, 1.0F, 1.0F, alpha, u, v};
            };
            const std::array<SkydomeVertex, 4U> data{
                vertex(-half, -half, 0.0F, 0.0F),
                vertex(half, -half, 1.0F, 0.0F),
                vertex(half, half, 1.0F, 1.0F),
                vertex(-half, half, 0.0F, 1.0F)};
            std::memcpy(vertices.data, data.data(), sizeof(data));
            std::memcpy(index_buffer.data, indices.data(), sizeof(indices));
            bgfx::setTransform(identity.data());
            bgfx::setVertexBuffer(0U, &vertices);
            bgfx::setIndexBuffer(&index_buffer);
            bgfx::setTexture(0U, impl_->skydome_sampler, impl_->spot_shadow_texture,
                             impl_->world_sampler(BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP));
            bgfx::setUniform(impl_->skydome_uv_time, static_uv.data());
            bgfx::setState(state);
            bgfx::submit(world_view_id, impl_->skydome_program);
        }
    }

    // Debug/character models share the world camera, depth and fog with the
    // map. They are separate resident slots so animation/replication can
    // update transforms without rebuilding immutable KV6 geometry.
    // Placement ghosts show one nearest surface, not accumulating alpha from
    // every rear wall and concave layer in arbitrary KV6 order. Establish that
    // depth after opaque geometry, then blend only the surface that owns it.
    for (std::uint32_t pass{}; pass < 3U; ++pass) {
        const bool translucent_pass = pass != 0U;
        for (const auto& draw : world_models) {
            const bool translucent = draw.opacity < 0.999F;
            if (translucent != translucent_pass || draw.slot >= world_model_slot_count ||
                !(draw.opacity > 0.0F)) {
                continue;
            }
            const auto& slot = impl_->world_model_slots[draw.slot];
            if (!slot.resident) {
                continue;
            }
            // Cheap sphere cull: entirely behind the eye plane or entirely
            // past the fog end (where fog paints it out completely). With 32
            // players x ~15 parts every culled part saves a submit and its
            // ~20 uniform uploads.
            if (impl_->model_culling && slot.bound_radius >= 0.0F) {
                const auto& m = draw.transform;
                const auto& c = slot.bound_centre;
                const float wx = c[0U] * m[0U] + c[1U] * m[4U] + c[2U] * m[8U] + m[12U];
                const float wy = c[0U] * m[1U] + c[1U] * m[5U] + c[2U] * m[9U] + m[13U];
                const float wz = c[0U] * m[2U] + c[1U] * m[6U] + c[2U] * m[10U] + m[14U];
                const auto row_length = [&](std::size_t row) {
                    return std::sqrt(m[row * 4U] * m[row * 4U] + m[row * 4U + 1U] * m[row * 4U + 1U] +
                                     m[row * 4U + 2U] * m[row * 4U + 2U]);
                };
                const float radius =
                    slot.bound_radius *
                        std::max({row_length(0U), row_length(1U), row_length(2U)}) +
                    0.5F;
                const float dx = wx - eye.x;
                const float dy = wy - eye.y;
                const float dz = wz - eye.z;
                const float ahead = dx * forward.x + dy * forward.y + dz * forward.z;
                const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (std::isfinite(distance) &&
                    (ahead < -radius || (fog_limit > 0.0F && distance - radius > fog_limit))) {
                    continue;
                }
            }
            bgfx::setTransform(draw.transform.data());
            bgfx::setVertexBuffer(0U, slot.vertices);
            bgfx::setIndexBuffer(slot.indices);
            uniforms.set(impl_->camera_uniform, camera_uniform.data());
            uniforms.set(impl_->fog_uniform, fog_uniform.data());
            uniforms.set(impl_->light_uniform, model_light.data());
            const std::array<float, 4U> model_opacity{
                std::clamp(draw.opacity, 0.0F, 1.0F),
                std::clamp(draw.albedo_gain, 0.0F, 2.0F),
                std::clamp(draw.albedo_contrast, 0.25F, 2.0F),
                0.0F};
            uniforms.set(impl_->model_opacity_uniform, model_opacity.data());
            push_atmosphere();
            // The part origin is its world position (row-vector translation).
            // World-space models probe the emissive volume in the shader, so
            // only the placed light is added here.
            const auto placed_model_light = model_light_at(
                {draw.transform[12U], draw.transform[13U], draw.transform[14U]}, false);
            uniforms.set(impl_->model_light_uniform, placed_model_light.data());
            const auto state = pass == 1U
                                   ? (BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA)
                                   : translucent
                                   ? (BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_EQUAL |
                                      BGFX_STATE_BLEND_ALPHA | BGFX_STATE_MSAA)
                                   : (BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                                      BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS |
                                      BGFX_STATE_MSAA);
            bgfx::setState(state);
            bgfx::submit(world_view_id, impl_->program);
        }
    }

    // Minimap.render_zone_bounds draws visible packet-43 volumes as textured
    // additive cubes after opaque geometry. Depth stays read-only so walls
    // occlude the far faces and the boundary never changes collision/depth.
    if (!zone_volumes.empty() && bgfx::isValid(impl_->zone_texture) &&
        bgfx::isValid(impl_->solid_zone_texture) &&
        bgfx::isValid(impl_->skydome_program)) {
        static constexpr std::array<std::uint16_t, 36U> indices{
            0U, 1U, 2U, 0U, 2U, 3U,       4U, 5U, 6U, 4U, 6U, 7U,
            8U, 9U, 10U, 8U, 10U, 11U,    12U, 13U, 14U, 12U, 14U, 15U,
            16U, 17U, 18U, 16U, 18U, 19U, 20U, 21U, 22U, 20U, 22U, 23U};
        static constexpr std::array<float, 4U> static_uv{};
        constexpr std::uint64_t state =
            BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LESS |
            BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                  BGFX_STATE_BLEND_ONE) |
            BGFX_STATE_MSAA;
        for (const auto& zone : zone_volumes) {
            const float x0 = std::min(zone.minimum[0U], zone.maximum[0U]);
            const float y0 = std::min(zone.minimum[1U], zone.maximum[1U]);
            const float z0 = std::min(zone.minimum[2U], zone.maximum[2U]);
            const float x1 = std::max(zone.minimum[0U], zone.maximum[0U]);
            const float y1 = std::max(zone.minimum[1U], zone.maximum[1U]);
            const float z1 = std::max(zone.minimum[2U], zone.maximum[2U]);
            if (!(x1 > x0) || !(y1 > y0) || !(z1 > z0) ||
                !(zone.opacity > 0.0F) ||
                bgfx::getAvailTransientVertexBuffer(24U, impl_->skydome_layout) < 24U ||
                bgfx::getAvailTransientIndexBuffer(36U) < 36U) {
                continue;
            }
            bgfx::TransientVertexBuffer vertices{};
            bgfx::TransientIndexBuffer index_buffer{};
            bgfx::allocTransientVertexBuffer(&vertices, 24U, impl_->skydome_layout);
            bgfx::allocTransientIndexBuffer(&index_buffer, 36U);
            const float alpha = std::clamp(zone.opacity, 0.0F, 1.0F);
            const auto vertex = [&](float x, float y, float z, float u, float v) {
                return SkydomeVertex{x, y, z,
                                     std::clamp(zone.color[0U], 0.0F, 1.0F),
                                     std::clamp(zone.color[1U], 0.0F, 1.0F),
                                     std::clamp(zone.color[2U], 0.0F, 1.0F),
                                     alpha, u, v};
            };
            const std::array<SkydomeVertex, 24U> data{
                vertex(x0,y0,z0,0,0), vertex(x1,y0,z0,1,0), vertex(x1,y1,z0,1,1), vertex(x0,y1,z0,0,1),
                vertex(x0,y1,z1,0,0), vertex(x1,y1,z1,1,0), vertex(x1,y0,z1,1,1), vertex(x0,y0,z1,0,1),
                vertex(x0,y0,z1,0,0), vertex(x1,y0,z1,1,0), vertex(x1,y0,z0,1,1), vertex(x0,y0,z0,0,1),
                vertex(x1,y1,z1,0,0), vertex(x0,y1,z1,1,0), vertex(x0,y1,z0,1,1), vertex(x1,y1,z0,0,1),
                vertex(x0,y1,z1,0,0), vertex(x0,y0,z1,1,0), vertex(x0,y0,z0,1,1), vertex(x0,y1,z0,0,1),
                vertex(x1,y0,z1,0,0), vertex(x1,y1,z1,1,0), vertex(x1,y1,z0,1,1), vertex(x1,y0,z0,0,1)};
            std::memcpy(vertices.data, data.data(), sizeof(data));
            std::memcpy(index_buffer.data, indices.data(), sizeof(indices));
            bgfx::setTransform(identity.data());
            bgfx::setVertexBuffer(0U, &vertices);
            bgfx::setIndexBuffer(&index_buffer);
            bgfx::setTexture(0U, impl_->skydome_sampler,
                             zone.solid ? impl_->solid_zone_texture
                                        : impl_->zone_texture,
                             impl_->world_sampler(BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP));
            bgfx::setUniform(impl_->skydome_uv_time, static_uv.data());
            bgfx::setState(state);
            bgfx::submit(world_view_id, impl_->skydome_program);
        }
    }

    // Retail LaserAttachment is two crossed textured quads, not a debug line.
    // Its depth test stays enabled while depth writes and culling are disabled,
    // so walls hide it, overlapping beam planes blend, and aiming never marks
    // the authoritative terrain. The CPU has already clipped each ray against
    // the current VXL/player bodies and applied the observer-warning alpha.
    if (!laser_beams.empty() && bgfx::isValid(impl_->skydome_program)) {
        const auto add_scaled = [](std::array<float, 3U> point,
                                   const std::array<float, 3U>& direction,
                                   float amount) {
            return std::array<float, 3U>{
                point[0U] + direction[0U] * amount,
                point[1U] + direction[1U] * amount,
                point[2U] + direction[2U] * amount};
        };
        const auto cross = [](const std::array<float, 3U>& lhs,
                              const std::array<float, 3U>& rhs) {
            return std::array<float, 3U>{
                lhs[1U] * rhs[2U] - lhs[2U] * rhs[1U],
                lhs[2U] * rhs[0U] - lhs[0U] * rhs[2U],
                lhs[0U] * rhs[1U] - lhs[1U] * rhs[0U]};
        };
        const auto normalized = [](std::array<float, 3U> vector) {
            const float length = std::sqrt(vector[0U] * vector[0U] +
                                           vector[1U] * vector[1U] +
                                           vector[2U] * vector[2U]);
            if (length > 1.0e-6F) {
                vector[0U] /= length;
                vector[1U] /= length;
                vector[2U] /= length;
            }
            return vector;
        };
        const auto elapsed_seconds =
            std::chrono::duration<float>(std::chrono::steady_clock::now() - impl_->skydome_clock)
                .count();
        // LaserAttachment increments uv_timer by 0.1 every 60 Hz update.
        const float retail_laser_time = elapsed_seconds * 6.0F;
        static constexpr std::array<std::uint16_t, 12U> crossed_quad_indices{
            0U, 1U, 2U, 0U, 2U, 3U, 4U, 5U, 6U, 4U, 6U, 7U};
        static constexpr std::array<std::uint16_t, 6U> billboard_indices{
            0U, 1U, 2U, 0U, 2U, 3U};
        constexpr std::uint64_t laser_state =
            BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LESS |
            BGFX_STATE_BLEND_ALPHA | BGFX_STATE_MSAA;

        for (const auto& draw : laser_beams) {
            const auto& pose = draw.pose;
            const auto texture_index = static_cast<std::size_t>(pose.color);
            if (!pose.visible || pose.alpha <= 0.0F ||
                pose.distance <= world::SniperLaserPose::start_distance ||
                texture_index >= impl_->laser_beam_textures.size() ||
                !bgfx::isValid(impl_->laser_beam_textures[texture_index])) {
                continue;
            }
            const auto beam_start = add_scaled(
                pose.origin, pose.direction, world::SniperLaserPose::start_distance);
            const auto beam_middle = add_scaled(
                beam_start,
                pose.direction,
                (pose.distance - world::SniperLaserPose::start_distance) * 0.5F);
            const std::array<float, 3U> to_camera{
                eye.x - beam_middle[0U], eye.y - beam_middle[1U], eye.z - beam_middle[2U]};
            auto side = normalized(cross(pose.direction, to_camera));
            if (std::fabs(side[0U]) + std::fabs(side[1U]) + std::fabs(side[2U]) < 1.0e-5F) {
                side = normalized(cross(pose.direction, {0.0F, 0.0F, 1.0F}));
            }
            if (std::fabs(side[0U]) + std::fabs(side[1U]) + std::fabs(side[2U]) < 1.0e-5F) {
                side = normalized(cross(pose.direction, {0.0F, 1.0F, 0.0F}));
            }
            const auto second_side = normalized(cross(pose.direction, side));
            constexpr float half_thickness{world::SniperLaserPose::thickness * 0.5F};

            const auto submit_segment = [&](float start_distance,
                                            float end_distance,
                                            float start_alpha,
                                            float end_alpha,
                                            std::array<float, 2U> uv_speed) {
                if (end_distance <= start_distance ||
                    bgfx::getAvailTransientVertexBuffer(8U, impl_->skydome_layout) < 8U ||
                    bgfx::getAvailTransientIndexBuffer(12U) < 12U) {
                    return;
                }
                bgfx::TransientVertexBuffer vertices{};
                bgfx::TransientIndexBuffer indices{};
                bgfx::allocTransientVertexBuffer(&vertices, 8U, impl_->skydome_layout);
                bgfx::allocTransientIndexBuffer(&indices, 12U);
                const auto start = add_scaled(pose.origin, pose.direction, start_distance);
                const auto end = add_scaled(pose.origin, pose.direction, end_distance);
                const auto vertex = [](const std::array<float, 3U>& point,
                                       const std::array<float, 3U>& offset,
                                       float sign,
                                       float alpha,
                                       float u,
                                       float v) {
                    return SkydomeVertex{point[0U] + offset[0U] * sign,
                                         point[1U] + offset[1U] * sign,
                                         point[2U] + offset[2U] * sign,
                                         1.0F,
                                         1.0F,
                                         1.0F,
                                         alpha,
                                         u,
                                         v};
                };
                std::array<SkydomeVertex, 8U> data{
                    vertex(start, side, -half_thickness, start_alpha, 0.0F, 0.0F),
                    vertex(start, side, half_thickness, start_alpha, 0.0F, 1.0F),
                    vertex(end, side, half_thickness, end_alpha, 1.0F, 1.0F),
                    vertex(end, side, -half_thickness, end_alpha, 1.0F, 0.0F),
                    vertex(start, second_side, -half_thickness, start_alpha, 0.0F, 0.0F),
                    vertex(start, second_side, half_thickness, start_alpha, 0.0F, 1.0F),
                    vertex(end, second_side, half_thickness, end_alpha, 1.0F, 1.0F),
                    vertex(end, second_side, -half_thickness, end_alpha, 1.0F, 0.0F)};
                std::memcpy(vertices.data, data.data(), sizeof(data));
                std::memcpy(indices.data,
                            crossed_quad_indices.data(),
                            sizeof(crossed_quad_indices));
                const std::array<float, 4U> uv_time{
                    uv_speed[0U], uv_speed[1U], retail_laser_time, 0.0F};
                bgfx::setTransform(identity.data());
                bgfx::setVertexBuffer(0U, &vertices);
                bgfx::setIndexBuffer(&indices);
                bgfx::setTexture(
                    0U, impl_->skydome_sampler, impl_->laser_beam_textures[texture_index],
                    impl_->world_sampler(BGFX_SAMPLER_V_CLAMP));
                bgfx::setUniform(impl_->skydome_uv_time, uv_time.data());
                bgfx::setState(laser_state);
                bgfx::submit(world_view_id, impl_->skydome_program);
            };

            const float fade_in_end = std::min(
                pose.distance,
                world::SniperLaserPose::start_distance +
                    world::SniperLaserPose::fade_in_distance);
            submit_segment(world::SniperLaserPose::start_distance,
                           fade_in_end,
                           0.0F,
                           pose.alpha,
                           {-0.05F, 0.0F});
            if (pose.distance > fade_in_end) {
                const float fade_out_start = std::max(
                    fade_in_end, pose.distance - world::SniperLaserPose::fade_out_distance);
                submit_segment(fade_in_end,
                               fade_out_start,
                               pose.alpha,
                               pose.alpha,
                               {-0.0001F, 0.0F});
                submit_segment(fade_out_start,
                               pose.distance,
                               pose.alpha,
                               0.0F,
                               {1.0F, 1.0F});
            }

            // hitscan_player, unlike terrain hits, owns a remote 0.07-block
            // laser spot. Move it 0.01 back from the body exactly as retail
            // does so depth testing cannot bury it in the contacted surface.
            if (pose.player_hit && bgfx::isValid(impl_->laser_spot_textures[texture_index]) &&
                bgfx::getAvailTransientVertexBuffer(4U, impl_->skydome_layout) >= 4U &&
                bgfx::getAvailTransientIndexBuffer(6U) >= 6U) {
                bgfx::TransientVertexBuffer vertices{};
                bgfx::TransientIndexBuffer indices{};
                bgfx::allocTransientVertexBuffer(&vertices, 4U, impl_->skydome_layout);
                bgfx::allocTransientIndexBuffer(&indices, 6U);
                const auto center = add_scaled(
                    pose.origin, pose.direction, std::max(0.0F, pose.distance - 0.01F));
                constexpr float half_spot{0.035F};
                const auto spot_vertex = [&](float horizontal, float vertical, float u, float v) {
                    return SkydomeVertex{
                        center[0U] + right.x * horizontal + camera_up.x * vertical,
                        center[1U] + right.y * horizontal + camera_up.y * vertical,
                        center[2U] + right.z * horizontal + camera_up.z * vertical,
                        1.0F, 1.0F, 1.0F, pose.alpha, u, v};
                };
                const std::array<SkydomeVertex, 4U> data{
                    spot_vertex(-half_spot, -half_spot, 0.0F, 1.0F),
                    spot_vertex(half_spot, -half_spot, 1.0F, 1.0F),
                    spot_vertex(half_spot, half_spot, 1.0F, 0.0F),
                    spot_vertex(-half_spot, half_spot, 0.0F, 0.0F)};
                std::memcpy(vertices.data, data.data(), sizeof(data));
                std::memcpy(indices.data, billboard_indices.data(), sizeof(billboard_indices));
                static constexpr std::array<float, 4U> static_uv{};
                bgfx::setTransform(identity.data());
                bgfx::setVertexBuffer(0U, &vertices);
                bgfx::setIndexBuffer(&indices);
                bgfx::setTexture(
                    0U, impl_->skydome_sampler, impl_->laser_spot_textures[texture_index],
                    impl_->world_sampler(BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP));
                bgfx::setUniform(impl_->skydome_uv_time, static_uv.data());
                bgfx::setState(laser_state);
                bgfx::submit(world_view_id, impl_->skydome_program);
            }
        }
    }

    // Blended particles close the world view. They must follow every opaque
    // draw so they composite against terrain instead of the sky, and they
    // must never write depth or they occlude one another's blending.
    // ParticleSystem has already sorted the alpha buckets back-to-front;
    // world_view_id is ViewMode::Sequential, so this order is the contract.
    if (!particle_batches.empty() && !particles.empty() && bgfx::isValid(impl_->particle_program)) {
        const bool instancing = (bgfx::getCaps()->supported & BGFX_CAPS_INSTANCING) != 0U;
        constexpr std::uint16_t instance_stride{
            static_cast<std::uint16_t>(sizeof(world::ParticleInstance))};
        for (const auto& batch : particle_batches) {
            if (batch.count == 0U ||
                static_cast<std::size_t>(batch.first) + batch.count > particles.size()) {
                continue;
            }
            const auto atlas_index = static_cast<std::size_t>(batch.atlas);
            if (atlas_index >= impl_->particle_textures.size() ||
                !bgfx::isValid(impl_->particle_textures[atlas_index])) {
                continue;
            }
            if (!instancing ||
                bgfx::getAvailInstanceDataBuffer(batch.count, instance_stride) < batch.count) {
                // Losing a cosmetic batch must never be fatal; the frame is
                // still correct without it.
                continue;
            }
            bgfx::InstanceDataBuffer instance_buffer{};
            bgfx::allocInstanceDataBuffer(&instance_buffer, batch.count, instance_stride);
            // draw.pyd sub_10015770 places every particle billboard at
            // GL y = -(z - 0.5 - size): the quad is lifted by its own
            // half-extent, so its bottom edge (not its centre) sits on the
            // simulated point. Map z grows downwards, hence z - size. Debris
            // bouncing on the ground rests on it instead of being half buried.
            for (std::uint32_t offset{}; offset < batch.count; ++offset) {
                world::ParticleInstance lifted =
                    particles[static_cast<std::size_t>(batch.first) + offset];
                lifted.position[2U] -= lifted.size;
                std::memcpy(instance_buffer.data +
                                static_cast<std::size_t>(offset) * instance_stride,
                            &lifted, sizeof(lifted));
            }

            const auto& definition = particle_atlases[atlas_index];
            const std::array<float, 4U> grid{1.0F / definition.frames_x,
                                             1.0F / definition.frames_y,
                                             definition.frames_x,
                                             definition.frames_y};
            const bool uses_lut = batch.color_mode != world::ParticleColorMode::tinted;
            const auto lut_texture =
                batch.color_mode == world::ParticleColorMode::smoke_lut
                    ? impl_->particle_smoke_lut_texture
                    : impl_->particle_glow_lut_texture;
            const std::array<float, 4U> mode{
                batch.blend == world::ParticleBlend::additive ? 1.0F : 0.0F,
                uses_lut ? 1.0F : 0.0F,
                impl_->profile.particle_lighting ? 1.0F : 0.0F,
                0.0F};
            const std::uint64_t blend =
                batch.blend == world::ParticleBlend::additive
                    ? BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE)
                : batch.blend == world::ParticleBlend::premultiplied
                    ? BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA)
                    : BGFX_STATE_BLEND_ALPHA;

            bgfx::setVertexBuffer(0U, impl_->particle_quad);
            bgfx::setIndexBuffer(impl_->particle_quad_indices);
            bgfx::setInstanceDataBuffer(&instance_buffer);
            bgfx::setTexture(0U, impl_->particle_sampler, impl_->particle_textures[atlas_index],
                             impl_->world_sampler(BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP));
            bgfx::setTexture(1U, impl_->particle_lut_sampler, lut_texture);
            uniforms.set(impl_->camera_uniform, camera_uniform.data());
            uniforms.set(impl_->fog_uniform, fog_uniform.data());
            // These are the same bounded lights already selected for opaque
            // world shading.  Particle lighting is profile-gated in
            // u_particleMode, but the arrays are always refreshed so a tier
            // change cannot reuse stale light data from an earlier frame.
            uniforms.set(impl_->point_light_position_radius_uniform,
                             point_light_position_radius.data(),
                             maximum_dynamic_lights);
            uniforms.set(impl_->point_light_color_intensity_uniform,
                             point_light_color_intensity.data(),
                             maximum_dynamic_lights);
            uniforms.set(impl_->particle_grid, grid.data());
            uniforms.set(impl_->particle_mode, mode.data());
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA |
                           blend);
            bgfx::submit(world_view_id, impl_->particle_program);
        }
    }

    // Water surface rendering is deferred: Training's lanes are dry and the
    // open-water bed voxels already carry the recovered water tone. A real
    // z=239 water plane arrives with maps that need it.

    // First-person viewmodel: its own depth-cleared view over the world.
    // The view transform is identity, so each draw's matrix places its part
    // directly in view space; near/far are tightened around arm's reach.
    if (!view_model.empty()) {
        std::array<float, 16U> tool_projection{};
        bx::mtxProj(tool_projection.data(),
                    static_cast<float>(camera.view_model_fov_y_degrees.value_or(camera.fov_y_degrees)),
                    aspect,
                    0.01F,
                    8.0F,
                    bgfx::getCaps()->homogeneousDepth,
                    bx::Handedness::Right);
        bgfx::setViewRect(first_person_view, 0U, 0U, target_width, target_height);
        bgfx::setViewClear(first_person_view, BGFX_CLEAR_DEPTH, 0U, 1.0F, 0U);
        bgfx::setViewMode(first_person_view, bgfx::ViewMode::Sequential);
        bgfx::setViewTransform(first_person_view, identity.data(), tool_projection.data());

        for (const auto& draw : view_model) {
            if (draw.slot >= view_model_slot_count) {
                continue;
            }
            const auto& slot = impl_->view_model_slots[draw.slot];
            if (!slot.resident) {
                continue;
            }
            bgfx::setTransform(draw.transform.data());
            bgfx::setVertexBuffer(0U, slot.vertices);
            bgfx::setIndexBuffer(slot.indices);
            uniforms.set(impl_->camera_uniform, viewmodel_camera.data());
            uniforms.set(impl_->fog_uniform, fog_uniform.data());
            // Lighting mode 0 is fs_world's unlit branch (lit = albedo):
            // retail binds PASSTHROUGH_SHADER around Weapon.draw_muzzle.
            const std::array<float, 4U> unlit_light{0.0F, viewmodel_light[1U],
                                                    viewmodel_light[2U], viewmodel_light[3U]};
            uniforms.set(impl_->light_uniform,
                             draw.unlit ? unlit_light.data() : viewmodel_light.data());
            uniforms.set(impl_->model_opacity_uniform, opaque_model.data());
            push_atmosphere();
            // These MUST follow push_atmosphere: bgfx takes the last value set
            // before submit, and each one replaces something the world pass
            // expresses in a space this pass is not drawn in.
            uniforms.set(impl_->retail_light0_direction_uniform, viewmodel_retail_light0.data());
            uniforms.set(impl_->retail_light1_direction_uniform, viewmodel_retail_light1.data());
            uniforms.set(impl_->retail_view_direction_uniform, viewmodel_retail_eye.data());
            uniforms.set(impl_->skylight_params_uniform, viewmodel_skylight.data());
            uniforms.set(impl_->sun_direction_uniform, viewmodel_sun_direction.data());
            uniforms.set(impl_->up_axis_uniform, viewmodel_up.data());
            uniforms.set(impl_->shadow_params_uniform, viewmodel_shadow_params.data());
            uniforms.set(impl_->indirect_params_uniform, viewmodel_indirect.data());
            uniforms.set(impl_->model_light_uniform,
                             draw.unlit ? no_model_light.data() : viewmodel_model_light.data());
            uniforms.set(impl_->point_light_position_radius_uniform,
                             viewmodel_point_light_position_radius.data(),
                             static_cast<std::uint16_t>(maximum_dynamic_lights));
            // Retail keeps GL_CULL_FACE enabled for model display lists
            // (laserAttachment.py toggles it off and back on). The flash must
            // honour it: Sniper/Sniper2 keep a (0,0,0) zoomed offset, which
            // puts the eye inside the flash KV6 where every face is a back
            // face. Unculled, that one aimed shot painted the whole scope.
            // KV6 winding matches the terrain's CW-culled chunks, so outside
            // views are byte-identical with and without the cull.
            //
            // Lit parts are culled too. Unculled, a high-detail scripted skin
            // (a million vertices) shaded every back face through the full
            // world shader as well and halved the frame rate while held. A
            // mirroring transform (negative determinant) reverses the winding,
            // so it culls the opposite side and still shows its outside.
            const auto& m = draw.transform;
            const float determinant = m[0U] * (m[5U] * m[10U] - m[6U] * m[9U]) -
                                      m[4U] * (m[1U] * m[10U] - m[2U] * m[9U]) +
                                      m[8U] * (m[1U] * m[6U] - m[2U] * m[5U]);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                           BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA |
                           (determinant < 0.0F ? BGFX_STATE_CULL_CCW : BGFX_STATE_CULL_CW));
            bgfx::submit(first_person_view, impl_->program);
        }
    }

    const auto tan_half_fov_y =
        std::tan(static_cast<float>(camera.fov_y_degrees) * bx::kPi / 360.0F);
    PostProcessor::Camera post_camera{};
    post_camera.view_projection = view_projection;
    post_camera.tan_half_fov_y = tan_half_fov_y;
    post_camera.tan_half_fov_x = tan_half_fov_y * aspect;
    post_camera.near_plane = static_cast<float>(camera.near_plane);
    post_camera.far_plane = far_plane;
    post_camera.eye = camera.eye;
    post_camera.forward = basis.forward;
    impl_->post.finish(post_frame, impl_->post_settings,
                       PostExtent{drawable.width, drawable.height}, post_camera);
    impl_->post.note_camera(post_camera);
    impl_->stats.post_passes = impl_->post.last_pass_count();
    impl_->stats.post_scene_width = post_frame.active ? post_frame.scene.width : 0U;
    impl_->stats.post_scene_height = post_frame.active ? post_frame.scene.height : 0U;
    return true;
}

WorldFrameStats WorldRenderer::last_frame_stats() const noexcept {
    return impl_->stats;
}

} // namespace battlespades::render
