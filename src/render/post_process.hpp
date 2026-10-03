#pragma once

#include "battlespades/render/post_math.hpp"
#include "battlespades/render/post_settings.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>

namespace battlespades::render {

/**
 * The world post chain behind PostSettings (private to WorldRenderer).
 *
 * begin() decides, per frame, whether the world and first-person views draw
 * into the backbuffer (nothing active: exactly the pre-post renderer) or into
 * an offscreen scene target; finish() then submits the fullscreen passes and
 * the resolve into the backbuffer at composite_view_id. Everything is created
 * lazily on the first active frame and recreated on size or format change.
 */
class PostProcessor final {
public:
    struct Frame final {
        bool active{};
        /** Ambient occlusion or motion blur runs before the first-person view. */
        bool world_pass{};
        PostExtent scene{};
        std::uint16_t view_model_view{};
    };

    /** The world camera of the frame, for depth reconstruction and blur. */
    struct Camera final {
        std::array<float, 16U> view_projection{};
        float tan_half_fov_x{};
        float tan_half_fov_y{};
        float near_plane{};
        float far_plane{};
        std::array<double, 3U> eye{};
        std::array<double, 3U> forward{};
    };

    PostProcessor() = default;
    ~PostProcessor();
    PostProcessor(const PostProcessor&) = delete;
    PostProcessor& operator=(const PostProcessor&) = delete;

    void set_shader_root(std::filesystem::path backend_root);
    void shutdown() noexcept;

    /** Loads the passes on first use; a failure only disables the chain. */
    [[nodiscard]] PostCapabilities capabilities();

    /** Binds the world/first-person views for this frame. */
    [[nodiscard]] Frame begin(const PostSettings& settings, PostExtent drawable);
    /** Submits the passes for a frame begin() made active. */
    void finish(const Frame& frame, const PostSettings& settings, PostExtent drawable,
                const Camera& camera);
    /** Remembers the camera for the next frame's motion blur (every frame). */
    void note_camera(const Camera& camera) noexcept;

    /** Fullscreen passes submitted by the last finish(). */
    [[nodiscard]] std::uint32_t last_pass_count() const noexcept { return passes_; }

private:
    struct Target final {
        bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
        bgfx::FrameBufferHandle framebuffer = BGFX_INVALID_HANDLE;
        PostExtent extent{};
    };

    [[nodiscard]] bool load();
    void release_targets() noexcept;
    static void release(Target& target) noexcept;
    [[nodiscard]] bool ensure(Target& target, PostExtent extent, bgfx::TextureFormat::Enum format);
    [[nodiscard]] bool ensure_scene(PostExtent extent, bool world_pass);
    void pass(std::uint16_t view, bgfx::FrameBufferHandle framebuffer, PostExtent extent,
              bgfx::ProgramHandle program);
    void bind(std::uint8_t stage, bgfx::UniformHandle sampler, bgfx::TextureHandle texture,
              bool point = false);

    std::filesystem::path root_;
    bool attempted_{};
    PostCapabilities caps_{};
    bgfx::TextureFormat::Enum colour_format_{bgfx::TextureFormat::RGBA8};
    bgfx::TextureFormat::Enum depth_format_{bgfx::TextureFormat::D24S8};
    bool depth_readable_{};

    bgfx::VertexBufferHandle triangle_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle ssao_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle world_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle bright_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle down_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle up_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle composite_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle rcas_ = BGFX_INVALID_HANDLE;

    bgfx::UniformHandle s_colour_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_depth_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_ao_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_low_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_bloom_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_texel_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_proj_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_depth_mode_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_ao_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_flags_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_ao_texel_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_reproject_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_bloom_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_mode_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_grade_ = BGFX_INVALID_HANDLE;
    std::array<bgfx::UniformHandle, 3U> u_cvd_{
        {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    bgfx::UniformHandle u_sharp_ = BGFX_INVALID_HANDLE;

    // The scene: colour + depth the world draws into; a second colour the
    // world pass writes and the first-person view draws over (same depth).
    bgfx::TextureHandle scene_colour_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle scene_depth_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle scene_ = BGFX_INVALID_HANDLE;
    PostExtent scene_extent_{};
    Target world_colour_{};
    bgfx::FrameBufferHandle world_with_depth_ = BGFX_INVALID_HANDLE;
    Target ao_{};
    std::array<Target, 4U> bloom_down_{};
    std::array<Target, 3U> bloom_up_{};
    Target output_{};

    std::optional<Camera> previous_{};
    std::uint32_t passes_{};
    /** The world views point at the scene target (undone when it turns off). */
    bool bound_{};
};

} // namespace battlespades::render
