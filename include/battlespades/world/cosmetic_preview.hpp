#pragma once
#include "battlespades/world/kv6_model.hpp"
#include <vector>
namespace battlespades::world {
struct CosmeticPreviewStyle {
    std::uint32_t width{540U},height{264U};
    double padding{12.0},fit_width{},roll{},outline{2.0};
    double pitch{0.25};
    unsigned samples{2U};
    bool internal_contours{true};
    bool boost_colors{true};
};
[[nodiscard]] std::vector<std::uint8_t>
cosmetic_preview(const ChunkMesh& mesh, double yaw, double zoom,
                 const CosmeticPreviewStyle& style = {});
/** Small orthographic KV6 thumbnail rendered on a worker, never on the UI thread. */
[[nodiscard]] std::vector<std::uint8_t>
cosmetic_preview(Kv6Model model,
                 std::optional<std::array<std::uint8_t, 3U>> palette,
                 bool blue_team,
                 double yaw,
                 double zoom);
inline constexpr std::uint32_t cosmetic_preview_width{540U}, cosmetic_preview_height{264U};
} // namespace battlespades::world
