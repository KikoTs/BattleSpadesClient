#pragma once

#include "battlespades/frontend/loading_screen.hpp"
#include "battlespades/ui/draw_list.hpp"

#include <algorithm>
#include <functional>
#include <string>
#include <string_view>

namespace battlespades::frontend {

/** One design-pixel layout shared by presentation and pointer hit testing. */
namespace loading_layout {
inline constexpr ui::DrawRect content{59.52, 135.0, 680.96, 304.0};
inline constexpr ui::DrawRect start{492.0, 449.0, 246.0, 58.0};
inline constexpr ui::DrawRect back{54.0, 541.0, 135.0, 32.0};
// Measured on the retail SCORES tab (800x600): headers start at y 157 on a
// 26 px stride between x 83 and 683; the scroll column is x 695..717 with
// its arrows at y 157 and 395.
inline constexpr ui::DrawRect score_viewport{83.0, 157.0, 634.0, 260.0};
inline constexpr ui::DrawRect score_rows{83.0, 157.0, 600.0, 260.0};
inline constexpr ui::DrawRect score_up{695.0, 157.0, 22.0, 22.0};
inline constexpr ui::DrawRect score_down{695.0, 395.0, 22.0, 22.0};
inline constexpr ui::DrawRect score_track{695.0, 179.0, 22.0, 216.0};
inline constexpr double score_row_height{26.0};
/** loading_map_frame (minimap_bg) 258x258 at (459,184) bottom-origin. */
inline constexpr ui::DrawRect map_preview_frame{459.0, 158.0, 258.0, 258.0};
/** The preview itself, 238x238 at (469,194) bottom-origin. */
inline constexpr ui::DrawRect map_preview{469.0, 168.0, 238.0, 238.0};
/** CUSTOM GAME RULES list: (83,384) bottom-origin, 366 wide, up to 200 tall. */
inline constexpr ui::DrawRect custom_rules{83.0, 216.0, 366.0, 200.0};
inline constexpr double custom_rule_row_height{20.0};
[[nodiscard]] constexpr ui::DrawRect tab(std::size_t index) noexcept {
    // Measured retail frame: both tab sprites span y 95..140 (46 px).
    return {59.52 + 228.0 * static_cast<double>(index), 95.0, 224.0, 46.0};
}
[[nodiscard]] constexpr bool contains(ui::DrawRect rect, double x, double y) noexcept {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}
[[nodiscard]] inline ui::DrawRect score_thumb(const MatchLoadingSnapshot& snapshot) noexcept {
    const auto count = snapshot.score_rows.size();
    const auto maximum = count > MatchLoadingModel::visible_score_rows
        ? count - MatchLoadingModel::visible_score_rows : 0U;
    const auto first = std::min(snapshot.score_scroll, maximum);
    const auto height = std::max(32.0, score_track.height *
        static_cast<double>(std::min(count, MatchLoadingModel::visible_score_rows)) /
        static_cast<double>(std::max<std::size_t>(count, 1U)));
    const auto fraction = maximum == 0U ? 0.0
        : static_cast<double>(first) / static_cast<double>(maximum);
    return {score_track.x, score_track.y + (score_track.height - height) * fraction,
            score_track.width, height};
}
} // namespace loading_layout

struct LoadingPresentationContext final {
    ui::PixelExtent window{800, 600};
    std::uint16_t background_opacity_per_mille{1'000U};
    /** Retail START glow pulses at 0.4 s; the caller supplies the phase. */
    bool start_glow{};
    /**
     * strings.get_by_id for the status line, whose ids carry the map name as
     * "{0}". Without it the status draws its catalogue id.
     */
    std::function<std::string(std::string_view)> localize{};
};

/**
 * The status line under the progress bar: CONNECTING_TO_SERVER, the
 * "{0}"-formatted map stages, ERROR_TIMEOUT, or verbatim host text. Empty
 * once the map is ready.
 */
[[nodiscard]] std::string loading_status_text(
    const MatchLoadingSnapshot& snapshot,
    const std::function<std::string(std::string_view)>& localize);

class BootLoadingPresentation final {
public:
    [[nodiscard]] ui::DrawList build(const BootLoadingSnapshot& snapshot,
                                     const LoadingPresentationContext& context = {}) const;
};

class MatchLoadingPresentation final {
public:
    [[nodiscard]] ui::DrawList build(const MatchLoadingSnapshot& snapshot,
                                     const LoadingPresentationContext& context = {}) const;
};

} // namespace battlespades::frontend
