#pragma once

#include "battlespades/frontend/quick_play_menu.hpp"
#include "battlespades/ui/draw_list.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace battlespades::frontend {

struct QuickPlayPresentationContext final {
    ui::PixelExtent window{MainMenuModel::reference_width_pixels,
                           MainMenuModel::reference_height_pixels};
    std::uint16_t background_opacity_per_mille{1'000U};
};

/** Renderer-neutral reconstruction of the retail ranked-playlist screen. */
class QuickPlayPresentation final {
public:
    /** Complete standalone frame, including the stationary frontend background. */
    [[nodiscard]] ui::DrawList build(const QuickPlayMenuModel& menu,
                                     const QuickPlayPresentationContext& context = {}) const;

    /** Design-space layer consumed by the shared horizontal slide compositor. */
    [[nodiscard]] ui::DrawList build_layer(const QuickPlayMenuModel& menu) const;
};

[[nodiscard]] std::string_view
quick_play_mode_image_asset(const QuickPlayPlaylistDefinition& definition) noexcept;

namespace quick_play_assets {

inline constexpr std::string_view background{"png/ui/ugc_splash.png"};
inline constexpr std::string_view large_frame{
    "png/ui/common_elements/frames/ui_frame_large.png"};
inline constexpr std::string_view panel_frame{
    "png/ui/common_elements/panels/ui_panel_frame.png"};
inline constexpr std::string_view subtitle_frame{
    "png/ui/common_elements/panels/subtitle_bg.png"};
inline constexpr std::string_view white_pixel{"png/high/white.png"};
inline constexpr std::string_view selection_line{
    "png/ui/common_elements/buttons/highlight_line.png"};
inline constexpr std::string_view selection_glow{
    "png/ui/common_elements/buttons/highlight_glow.png"};
inline constexpr std::string_view back_icon{
    "png/ui/common_elements/nav_bar/back_icon.png"};
inline constexpr std::string_view random_mode_image{
    "png/ui/game_loading/letterbox_images/letterbox_random.png"};
inline constexpr std::string_view multiple_mode_image{
    "png/ui/game_loading/letterbox_images/letterbox_multiplegamemodes.png"};

[[nodiscard]] std::span<const MainMenuAsset> required() noexcept;

} // namespace quick_play_assets

} // namespace battlespades::frontend
