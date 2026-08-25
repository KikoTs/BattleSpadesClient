#pragma once

#include "battlespades/assets/preload_service.hpp"
#include "battlespades/render/bgfx_ui_renderer.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace battlespades::frontend {

struct TexturePreloadRequest final {
    /** Cache key chosen by the native frontend, normally path plus filter. */
    std::string id;
    std::filesystem::path relative_path;
    render::TextureFilter filter{render::TextureFilter::linear};
    std::uint32_t weight{1U};
    bool required{true};
};

struct UploadedTexture final {
    std::string id;
    render::UiTextureInfo texture{};
};

/**
 * Background PNG decode plus bounded render-thread upload adapter.
 *
 * Worker threads never call bgfx. `pump_uploads` is the only GPU boundary and
 * must run on the renderer's owner thread before begin_frame(). Destruction
 * cancels the manifest and joins every worker before releasing decoded bytes.
 */
class NativeTexturePreloader final {
public:
    NativeTexturePreloader();
    ~NativeTexturePreloader();

    NativeTexturePreloader(const NativeTexturePreloader&) = delete;
    NativeTexturePreloader& operator=(const NativeTexturePreloader&) = delete;

    [[nodiscard]] bool start(const std::filesystem::path& asset_root,
                             std::vector<TexturePreloadRequest> manifest,
                             render::TextureQualityTier texture_quality =
                                 render::TextureQualityTier::medium,
                             std::size_t worker_count = 2U);
    [[nodiscard]] std::vector<UploadedTexture>
    pump_uploads(render::BgfxUiRenderer& renderer, std::size_t maximum_uploads = 2U);
    [[nodiscard]] assets::PreloadSnapshot snapshot() const noexcept;
    void stop() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace battlespades::frontend
