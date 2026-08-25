#pragma once

#include "battlespades/frontend/leaderboard_menu.hpp"
#include "battlespades/ui/draw_list.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace battlespades::frontend {

struct LeaderboardPresentationContext final {
    ui::PixelExtent window{800, 600};
    std::uint16_t background_opacity_per_mille{1'000U};
    /** Measures already-localized Edo text at the requested design-pixel size. */
    std::function<double(std::string_view key, double size)> measure_header_text{};
    /** Measures a literal A750 row string for retail name ellipsis handling. */
    std::function<double(std::string_view text, double size)> measure_row_text{};
};

/** Stable Classic-profile geometry, converted to top-left 800x600 coordinates. */
struct LeaderboardClassicLayout final {
    ui::DrawRect frame{};
    ui::DrawRect title{};
    ui::DrawRect type_dropdown{};
    ui::DrawRect scope_dropdown{};
    ui::DrawRect table{};
    ui::DrawRect back_button{};
    ui::DrawRect horizontal_scrollbar{};

    [[nodiscard]] friend constexpr bool operator==(const LeaderboardClassicLayout&,
                                                   const LeaderboardClassicLayout&) = default;
};

[[nodiscard]] LeaderboardClassicLayout leaderboard_classic_layout() noexcept;

/** One visible retail column after Rank/Name pinning and stat-window clipping. */
struct LeaderboardVisibleColumn final {
    std::size_t source_index{};
    ui::DrawRect bounds{};

    [[nodiscard]] friend constexpr bool operator==(const LeaderboardVisibleColumn&,
                                                   const LeaderboardVisibleColumn&) = default;
};

/** Source-derived list-panel geometry shared by rendering and hit testing. */
struct LeaderboardGridLayout final {
    ui::DrawRect header{};
    ui::DrawRect vertical_scrollbar{};
    ui::DrawRect horizontal_scrollbar{};
    std::vector<LeaderboardVisibleColumn> columns;
    std::size_t visible_rows{};
    std::size_t visible_stat_columns{};
    bool shows_vertical_scrollbar{};
    bool shows_horizontal_scrollbar{};
};

[[nodiscard]] LeaderboardGridLayout
leaderboard_grid_layout(const LeaderboardMenuModel& model,
                        const LeaderboardPresentationContext& context = {});

/** Builds an ordered draw list without owning endpoint or input behavior. */
class LeaderboardPresentation final {
public:
    [[nodiscard]] ui::DrawList build(const LeaderboardMenuModel& model,
                                     const LeaderboardPresentationContext& context = {}) const;
};

namespace leaderboard_presentation_assets {

inline constexpr std::string_view background{"png/ui/ugc_splash.png"};
inline constexpr std::string_view frame{"png/ui/leaderboard_menu/ui_frame_leaderboard.png"};
inline constexpr std::string_view white_pixel{"png/high/white.png"};
inline constexpr std::string_view filter_up{"png/ui/common_elements/filter_arrow_up.png"};
inline constexpr std::string_view filter_down{"png/ui/common_elements/filter_arrow_down.png"};
inline constexpr std::string_view filter_down_white{
    "png/ui/common_elements/filter_arrow_down_white.png"};
inline constexpr std::string_view back_icon{"png/ui/common_elements/nav_bar/back_icon.png"};
inline constexpr std::string_view red_header_left{
    "png/ui/common_elements/header/red_header_left.png"};
inline constexpr std::string_view red_header_right{
    "png/ui/common_elements/header/red_header_right.png"};
inline constexpr std::string_view selection_line{
    "png/ui/common_elements/buttons/highlight_line.png"};
inline constexpr std::string_view selection_glow{
    "png/ui/common_elements/buttons/highlight_glow.png"};
inline constexpr std::string_view square_button{"png/ui/common_elements/buttons/button_square.png"};
inline constexpr std::string_view arrow_down{
    "png/ui/common_elements/scroll_bar/scroll_bar_arrow_down.png"};
inline constexpr std::string_view arrow_up{
    "png/ui/common_elements/scroll_bar/scroll_bar_arrow_up.png"};
inline constexpr std::string_view arrow_left{
    "png/ui/common_elements/scroll_bar/scroll_bar_arrow_left.png"};
inline constexpr std::string_view arrow_right{
    "png/ui/common_elements/scroll_bar/scroll_bar_arrow_right.png"};
inline constexpr std::string_view scrollbar_hmid{
    "png/ui/common_elements/scroll_bar/scrollbar_hmid.png"};
inline constexpr std::string_view scrollbar_left{
    "png/ui/common_elements/scroll_bar/scrollbar_left.png"};
inline constexpr std::string_view scrollbar_right{
    "png/ui/common_elements/scroll_bar/scrollbar_right.png"};
inline constexpr std::string_view scrollbar_top{
    "png/ui/common_elements/scroll_bar/scrollbar_top.png"};
inline constexpr std::string_view scrollbar_mid{
    "png/ui/common_elements/scroll_bar/scrollbar_mid.png"};
inline constexpr std::string_view scrollbar_bottom{
    "png/ui/common_elements/scroll_bar/scrollbar_bottom.png"};
inline constexpr std::string_view title_font{"fonts/Spades.ttf"};
inline constexpr std::string_view dropdown_font{"fonts/Spades.ttf"};
inline constexpr std::string_view header_font{"fonts/Edo.ttf"};
inline constexpr std::string_view row_font{"fonts/A750-Sans-Medium.ttf"};

} // namespace leaderboard_presentation_assets

} // namespace battlespades::frontend
