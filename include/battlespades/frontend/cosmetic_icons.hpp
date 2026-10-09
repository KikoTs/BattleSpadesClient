#pragma once
#include "battlespades/frontend/inventory_session.hpp"
#include "battlespades/render/bgfx_ui_renderer.hpp"
#include <memory>

namespace battlespades::frontend {
enum class CosmeticIconKind { weapon, class_head, class_body };
struct CosmeticIconRequest {
    InventoryCosmetic item;
    std::optional<InventoryCosmetic> hat;
    world::SkinVariantSelection variants;
    CosmeticIconKind kind{CosmeticIconKind::weapon};
    std::uint8_t class_id{};
    bool blue_team{true};
    std::optional<world::VxlColor> team_color;
};
inline constexpr std::uint32_t cosmetic_icon_size=256;
[[nodiscard]] std::string cosmetic_icon_key(const CosmeticIconRequest& request);
[[nodiscard]] std::vector<std::uint8_t> render_cosmetic_icon(const CosmeticIconRequest& request,const std::filesystem::path& root);
/** One CPU worker, bounded cache, and GPU uploads only before begin_frame. */
class CosmeticIconCache {
public:
    explicit CosmeticIconCache(std::filesystem::path root);
    ~CosmeticIconCache();
    [[nodiscard]] std::string request(const CosmeticIconRequest& request,std::string_view fallback);
    void pump(render::BgfxUiRenderer& renderer);
    [[nodiscard]] std::optional<render::UiTextureInfo> texture(std::string_view asset) const;
    void clear(render::BgfxUiRenderer& renderer);
    [[nodiscard]] std::size_t rendered_count() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
