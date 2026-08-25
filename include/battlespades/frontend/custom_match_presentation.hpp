#pragma once

#include "battlespades/frontend/custom_match_menu.hpp"
#include "battlespades/ui/draw_list.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace battlespades::frontend {

struct CustomMatchPresentationContext final {
    ui::PixelExtent window{MainMenuModel::reference_width_pixels,
                           MainMenuModel::reference_height_pixels};
    std::uint16_t background_opacity_per_mille{1'000U};
};

/** Top-left 800x600 geometry recovered from the retail list/panel classes. */
struct CustomMatchClassicLayout final {
    ui::DrawRect frame{};
    ui::DrawRect title{};
    ui::DrawRect back{};
    ui::DrawRect list_panel{};
    ui::DrawRect list_header{};
    ui::DrawRect source_filter{};
    ui::DrawRect first_lobby_row{};
    ui::DrawRect preview_panel{};
    ui::DrawRect preview_header{};
    ui::DrawRect first_preview_row{};
    ui::DrawRect action_background{};
    ui::DrawRect join_button{};

    [[nodiscard]] friend constexpr bool operator==(const CustomMatchClassicLayout&,
                                                   const CustomMatchClassicLayout&) = default;
};

[[nodiscard]] CustomMatchClassicLayout custom_match_classic_layout() noexcept;

/** DrawList reconstruction of the visible Custom Match lobby-list root. */
class CustomMatchPresentation final {
public:
    [[nodiscard]] ui::DrawList
    build(const CustomMatchMenuModel& model,
          const CustomMatchPresentationContext& context = {}) const;

    /** Foreground-only layer suitable for the shared horizontal compositor. */
    [[nodiscard]] ui::DrawList build_layer(const CustomMatchMenuModel& model) const;
};

namespace custom_match_assets {

inline constexpr std::string_view frame{
    "png/ui/common_elements/frames/ui_frame_large.png"};
inline constexpr std::string_view panel{
    "png/ui/common_elements/panels/ui_panel_frame.png"};
inline constexpr std::string_view panel_header{
    "png/ui/settings/settings_common/settings_matchsettings_frame.png"};
inline constexpr std::string_view back_icon{
    "png/ui/common_elements/nav_bar/back_icon.png"};
inline constexpr std::string_view highlight_line{
    "png/ui/common_elements/buttons/highlight_line.png"};
inline constexpr std::string_view highlight_glow{
    "png/ui/common_elements/buttons/highlight_glow.png"};
inline constexpr std::string_view square_button{
    "png/ui/common_elements/buttons/button_square.png"};
inline constexpr std::string_view down_arrow{
    "png/ui/common_elements/scroll_bar/scroll_bar_arrow_down.png"};
inline constexpr std::string_view white_pixel{"png/high/white.png"};
inline constexpr std::string_view title_font{"fonts/Spades.ttf"};
inline constexpr std::string_view header_font{"fonts/Edo.ttf"};
inline constexpr std::string_view row_font{"fonts/A750-Sans-Medium.ttf"};

[[nodiscard]] std::span<const MainMenuAsset> required() noexcept;

} // namespace custom_match_assets

} // namespace battlespades::frontend
