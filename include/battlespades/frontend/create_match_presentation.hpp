#pragma once

#include "battlespades/frontend/create_match_menu.hpp"
#include "battlespades/ui/draw_list.hpp"

#include <cstdint>
#include <string_view>

namespace battlespades::frontend {

struct CreateMatchPresentationContext final {
    ui::PixelExtent window{CreateMatchMenuPresentation::reference_width,
                           CreateMatchMenuPresentation::reference_height};
    std::uint16_t background_opacity_per_mille{1'000U};
};

namespace create_match_presentation_assets {
inline constexpr std::string_view background{"png/ui/ugc_splash.png"};
inline constexpr std::string_view outer_frame{
    "png/ui/common_elements/frames/ui_frame_large.png"};
inline constexpr std::string_view panel_frame{
    "png/ui/common_elements/panels/ui_panel_frame.png"};
inline constexpr std::string_view row_frame{
    "png/ui/settings/settings_common/settings_matchsettings_frame.png"};
inline constexpr std::string_view collapse_minus{
    "png/ui/common_elements/collapse_minus.png"};
inline constexpr std::string_view collapse_plus{
    "png/ui/common_elements/collapse_plus.png"};
inline constexpr std::string_view checkbox{
    "png/ui/common_elements/menu_item_checkbox.png"};
inline constexpr std::string_view square_button{
    "png/ui/common_elements/buttons/button_square.png"};
inline constexpr std::string_view square_button_hover{
    "png/ui/common_elements/buttons/button_square_hover.png"};
inline constexpr std::string_view edit_icon{
    "png/ui/common_elements/icon_edit.png"};
inline constexpr std::string_view arrow_left{
    "png/ui/common_elements/scroll_bar/scroll_bar_arrow_left.png"};
inline constexpr std::string_view arrow_right{
    "png/ui/common_elements/scroll_bar/scroll_bar_arrow_right.png"};
inline constexpr std::string_view arrow_up{
    "png/ui/common_elements/scroll_bar/scroll_bar_arrow_up.png"};
inline constexpr std::string_view arrow_down{
    "png/ui/common_elements/scroll_bar/scroll_bar_arrow_down.png"};
inline constexpr std::string_view scrollbar_top{
    "png/ui/common_elements/scroll_bar/scroll_bar_top.png"};
inline constexpr std::string_view scrollbar_mid{
    "png/ui/common_elements/scroll_bar/scroll_bar_mid.png"};
inline constexpr std::string_view scrollbar_bottom{
    "png/ui/common_elements/scroll_bar/scroll_bar_bottom.png"};
inline constexpr std::string_view back_icon{
    "png/ui/common_elements/nav_bar/back_icon.png"};
inline constexpr std::string_view host_icon{"png/ui/icons/leader_icon.png"};
inline constexpr std::string_view head_icon{"png/ui/icons/deuce_head_1.png"};
inline constexpr std::string_view head_color_icon{
    "png/ui/icons/deuce_head_colour_1.png"};
inline constexpr std::string_view player_count_icon{"png/ui/icons/playercount_icon.png"};
inline constexpr std::string_view selection_line{
    "png/ui/common_elements/buttons/highlight_line.png"};
inline constexpr std::string_view selection_glow{
    "png/ui/common_elements/buttons/highlight_glow.png"};
inline constexpr std::string_view white_pixel{"png/high/white.png"};
inline constexpr std::string_view title_font{"fonts/Edo.ttf"};
inline constexpr std::string_view row_font{"fonts/A750-Sans-Bold.ttf"};
inline constexpr std::string_view body_font{"fonts/A750-Sans-Medium.ttf"};
} // namespace create_match_presentation_assets

/** Translates CreateMatchMenuPresentation into the shared ordered DrawList. */
class CreateMatchPresentation final {
public:
    [[nodiscard]] ui::DrawList build(const CreateMatchMenuPresentation& snapshot,
                                     const CreateMatchPresentationContext& context) const;
};

} // namespace battlespades::frontend
