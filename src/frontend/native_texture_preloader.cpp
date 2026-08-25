#include "battlespades/frontend/native_texture_preloader.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace battlespades::frontend {
namespace {

using TokenKey = std::pair<std::uint64_t, std::uint32_t>;

[[nodiscard]] TokenKey key(assets::PreloadToken token) noexcept {
    return {token.generation, token.ordinal};
}

[[nodiscard]] bool safe_relative_path(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) {
        return false;
    }
    return std::ranges::none_of(path, [](const auto& component) {
        return component == std::filesystem::path{".."};
    });
}

} // namespace

struct NativeTexturePreloader::Impl final {
    assets::PreloadService service{{4'096U, 4U, 64U}};
    std::filesystem::path root;
    std::filesystem::path client_root;
    std::vector<render::TextureFilter> filters;
    std::map<TokenKey, render::DecodedUiTexture> decoded;
    mutable std::mutex decoded_mutex;
    std::condition_variable wake;
    std::mutex wake_mutex;
    std::vector<std::thread> workers;
    std::atomic_bool stopping{false};

    void worker_main() {
        while (!stopping.load(std::memory_order_acquire)) {
            const auto claim = service.claim_decode();
            if (!claim.has_value()) {
                const auto state = service.snapshot().state;
                if (state != assets::PreloadBatchState::active) {
                    return;
                }
                std::unique_lock lock{wake_mutex};
                wake.wait_for(lock, std::chrono::milliseconds{5}, [this] {
                    return stopping.load(std::memory_order_acquire);
                });
                continue;
            }

            const auto relative = std::filesystem::path{claim->asset.path};
            render::UiTextureDecodeResult result;
            if (!safe_relative_path(relative)) {
                result.error = "preload manifest contains an unsafe texture path";
            } else {
                result = render::decode_png_rgba8(root / relative);
                // Project-owned UI artwork is an overlay beside the imported
                // retail tree. Asset repair atomically replaces `original`, so
                // custom files must never be hidden inside that directory.
                if (!result && !client_root.empty()) {
                    result = render::decode_png_rgba8(client_root / relative);
                }
            }
            if (result) {
                {
                    std::scoped_lock lock{decoded_mutex};
                    decoded.emplace(key(claim->token), std::move(*result.texture));
                }
                if (service.complete_decode(claim->token, true) !=
                    assets::PreloadCompletionResult::accepted) {
                    std::scoped_lock lock{decoded_mutex};
                    decoded.erase(key(claim->token));
                }
            } else {
                static_cast<void>(
                    service.complete_decode(claim->token, false, std::move(result.error)));
            }
            wake.notify_all();
        }
    }
};

NativeTexturePreloader::NativeTexturePreloader() : impl_{std::make_unique<Impl>()} {}

NativeTexturePreloader::~NativeTexturePreloader() {
    stop();
}

bool NativeTexturePreloader::start(const std::filesystem::path& asset_root,
                                   std::vector<TexturePreloadRequest> manifest,
                                   render::TextureQualityTier texture_quality,
                                   std::size_t worker_count) {
    stop();
    if (asset_root.empty() || !asset_root.is_absolute() || worker_count == 0U ||
        worker_count > 4U) {
        return false;
    }

    std::vector<assets::PreloadAsset> assets;
    assets.reserve(manifest.size());
    impl_->filters.clear();
    impl_->filters.reserve(manifest.size());
    for (auto& request : manifest) {
        if (request.id.empty() || !safe_relative_path(request.relative_path) ||
            request.weight == 0U) {
            return false;
        }
        // Keep the original id: draw commands and the GPU cache continue to
        // address the authored `png/high/...` key while the worker decodes the
        // selected retail resource root.
        request.relative_path =
            render::texture_quality_asset(std::move(request.relative_path), texture_quality);
        impl_->filters.push_back(request.filter);
        assets.push_back(assets::PreloadAsset{std::move(request.id),
                                              request.relative_path.generic_string(),
                                              assets::PreloadAssetKind::texture,
                                              request.weight,
                                              request.required});
    }
    if (!impl_->service.begin(std::move(assets))) {
        impl_->filters.clear();
        return false;
    }

    impl_->root = asset_root.lexically_normal();
    impl_->client_root = (asset_root.parent_path() / "client").lexically_normal();
    impl_->stopping.store(false, std::memory_order_release);
    impl_->workers.reserve(worker_count);
    for (std::size_t index{}; index < worker_count; ++index) {
        impl_->workers.emplace_back([state = impl_.get()] { state->worker_main(); });
    }
    return true;
}

std::vector<UploadedTexture>
NativeTexturePreloader::pump_uploads(render::BgfxUiRenderer& renderer,
                                     std::size_t maximum_uploads) {
    std::vector<UploadedTexture> uploaded;
    uploaded.reserve(maximum_uploads);
    for (std::size_t count{}; count < maximum_uploads; ++count) {
        const auto claim = impl_->service.claim_upload();
        if (!claim.has_value()) {
            break;
        }

        std::optional<render::DecodedUiTexture> decoded;
        {
            std::scoped_lock lock{impl_->decoded_mutex};
            const auto found = impl_->decoded.find(key(claim->token));
            if (found != impl_->decoded.end()) {
                decoded.emplace(std::move(found->second));
                impl_->decoded.erase(found);
            }
        }
        if (!decoded.has_value() || claim->token.ordinal >= impl_->filters.size()) {
            static_cast<void>(impl_->service.complete_upload(
                claim->token, false, "decoded texture payload is unavailable"));
            continue;
        }

        const auto texture = renderer.create_texture_rgba8(
            decoded->rgba8, decoded->extent, impl_->filters[claim->token.ordinal]);
        if (!texture.has_value()) {
            static_cast<void>(impl_->service.complete_upload(
                claim->token, false, std::string{renderer.last_error()}));
            continue;
        }
        if (impl_->service.complete_upload(claim->token, true) !=
            assets::PreloadCompletionResult::accepted) {
            static_cast<void>(renderer.release_texture(texture->texture));
            continue;
        }
        uploaded.push_back(UploadedTexture{claim->asset.id, *texture});
    }
    impl_->wake.notify_all();
    return uploaded;
}

assets::PreloadSnapshot NativeTexturePreloader::snapshot() const noexcept {
    return impl_->service.snapshot();
}

void NativeTexturePreloader::stop() noexcept {
    if (impl_ == nullptr) {
        return;
    }
    impl_->stopping.store(true, std::memory_order_release);
    impl_->service.cancel();
    impl_->wake.notify_all();
    for (auto& worker : impl_->workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    impl_->workers.clear();
    {
        std::scoped_lock lock{impl_->decoded_mutex};
        impl_->decoded.clear();
    }
    impl_->filters.clear();
    impl_->root.clear();
    impl_->client_root.clear();
}

} // namespace battlespades::frontend
