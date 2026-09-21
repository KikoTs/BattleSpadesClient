#pragma once

#include "battlespades/frontend/loading_screen.hpp"
#include "battlespades/ui/draw_list.hpp"

#include <algorithm>

namespace battlespades::frontend {

/** One design-pixel layout shared by presentation and pointer hit testing. */
namespace loading_layout {
inline constexpr ui::DrawRect content{59.52, 135.0, 680.96, 304.0};
inline constexpr ui::DrawRect start{492.0, 449.0, 246.0, 58.0};
inline constexpr ui::DrawRect back{54.0, 541.0, 135.0, 32.0};
inline constexpr ui::DrawRect score_viewport{73.0, 147.0, 654.0, 280.0};
inline constexpr ui::DrawRect score_rows{73.0, 147.0, 617.0, 270.0};
inline constexpr ui::DrawRect score_up{700.0, 147.0, 22.0, 24.0};
inline constexpr ui::DrawRect score_down{700.0, 403.0, 22.0, 24.0};
inline constexpr ui::DrawRect score_track{700.0, 171.0, 22.0, 232.0};
inline constexpr double score_row_height{27.0};
[[nodiscard]] constexpr ui::DrawRect tab(std::size_t index) noexcept {
    return {59.52 + 228.0 * static_cast<double>(index), 99.0, 224.0, 42.0};
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
};

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
