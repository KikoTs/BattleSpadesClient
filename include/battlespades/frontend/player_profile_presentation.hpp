#pragma once

#include "battlespades/frontend/achievements.hpp"
#include "battlespades/frontend/player_profile_menu.hpp"
#include "battlespades/ui/draw_list.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace battlespades::frontend {

struct PlayerProfilePresentationContext final {
    ui::PixelExtent window{800, 600};
    std::uint16_t background_opacity_per_mille{1'000U};
    enum class ControlState : std::uint8_t {
        normal,
        hovered,
        pressed,
    };
    ControlState cancel_state{ControlState::normal};
    ControlState achievements_state{ControlState::normal};
    ControlState filter_button_state{ControlState::normal};
    /** Drawn in place of the statistics while the model's achievements list is open. */
    std::span<const AchievementListRow> achievements{};
};

struct PlayerProfileClassicLayout final {
    ui::DrawRect outer_frame{};
    ui::DrawRect content_frame{};
    ui::DrawRect title{};
    ui::DrawRect tab_strip{};
    ui::DrawRect player_name{};
    ui::DrawRect kill_death_ratio{};
    ui::DrawRect filter_dropdown{};
    ui::DrawRect list_area{};
    ui::DrawRect summary_list_area{};
    ui::DrawRect scrollbar{};
    ui::DrawRect summary_scrollbar{};
    ui::DrawRect cancel_button{};
    ui::DrawRect achievements_button{};

    [[nodiscard]] friend constexpr bool operator==(const PlayerProfileClassicLayout&,
                                                   const PlayerProfileClassicLayout&) = default;
};

[[nodiscard]] PlayerProfileClassicLayout player_profile_classic_layout() noexcept;

class PlayerProfilePresentation final {
public:
    [[nodiscard]] ui::DrawList build(const PlayerProfileMenuModel& model,
                                     const PlayerProfilePresentationContext& context = {}) const;
};

namespace player_profile_presentation_assets {

inline constexpr std::string_view background{"png/ui/ugc_splash.png"};
inline constexpr std::string_view outer_frame{"png/ui/common_elements/frames/ui_frame_small.png"};
inline constexpr std::string_view content_frame{
    "png/ui/settings/tab_frames/player_profile_stats_bg.png"};
inline constexpr std::string_view tab_active{"png/ui/settings/tab_frames/generic_tab_active.png"};
inline constexpr std::string_view tab_inactive{
    "png/ui/settings/tab_frames/generic_tab_inactive.png"};
inline constexpr std::string_view red_header_left{
    "png/ui/common_elements/header/red_header_left.png"};
inline constexpr std::string_view red_header_right{
    "png/ui/common_elements/header/red_header_right.png"};
inline constexpr std::string_view square_button{"png/ui/common_elements/buttons/button_square.png"};
inline constexpr std::string_view square_button_hover{
    "png/ui/common_elements/buttons/button_square_hover.png"};
inline constexpr std::string_view square_button_press{
    "png/ui/common_elements/buttons/button_square_press.png"};
inline constexpr std::string_view arrow_up{
    "png/ui/common_elements/scroll_bar/scroll_bar_arrow_up.png"};
inline constexpr std::string_view arrow_down{
    "png/ui/common_elements/scroll_bar/scroll_bar_arrow_down.png"};
inline constexpr std::string_view scrollbar_top{
    "png/ui/common_elements/scroll_bar/scrollbar_top.png"};
inline constexpr std::string_view scrollbar_mid{
    "png/ui/common_elements/scroll_bar/scrollbar_mid.png"};
inline constexpr std::string_view scrollbar_bottom{
    "png/ui/common_elements/scroll_bar/scrollbar_bottom.png"};
inline constexpr std::string_view white_pixel{"png/high/white.png"};
inline constexpr std::string_view title_font{"fonts/Spades.ttf"};
inline constexpr std::string_view tab_font{"fonts/Edo.ttf"};
inline constexpr std::string_view row_font{"fonts/A750-Sans-Medium.ttf"};

} // namespace player_profile_presentation_assets

} // namespace battlespades::frontend
