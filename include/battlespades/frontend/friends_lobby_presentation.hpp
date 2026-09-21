#pragma once

#include "battlespades/frontend/friends_lobby_menu.hpp"
#include "battlespades/ui/draw_list.hpp"

namespace battlespades::frontend {

struct FriendsLobbyPresentationContext final {
    ui::PixelExtent window{800, 600};
    std::uint16_t background_opacity_per_mille{1'000U};
};

/** Builds the native friends/lobby screen from safe UI-thread state only. */
class FriendsLobbyPresentation final {
public:
    [[nodiscard]] ui::DrawList build(
        const FriendsLobbyMenuModel& model,
        const FriendsLobbyPresentationContext& context) const;
};

namespace friends_lobby_assets {

inline constexpr std::string_view background{"png/ui/ugc_splash.png"};
inline constexpr std::string_view frame{"png/ui/common_elements/frames/ui_frame_large.png"};
inline constexpr std::string_view panel{"png/ui/common_elements/panels/ui_panel_frame.png"};
inline constexpr std::string_view panel_header{"png/ui/settings/settings_common/settings_matchsettings_frame.png"};
inline constexpr std::string_view active_tab{"png/ui/settings/tab_frames/generic_tab_active.png"};
inline constexpr std::string_view inactive_tab{"png/ui/settings/tab_frames/generic_tab_inactive.png"};
inline constexpr std::string_view selection_line{"png/ui/common_elements/buttons/highlight_line.png"};
inline constexpr std::string_view selection_glow{"png/ui/common_elements/buttons/highlight_glow.png"};
inline constexpr std::string_view white_pixel{"png/high/white.png"};
inline constexpr std::string_view friends_icon{"png/ui/icons/deuce_head_1.png"};
inline constexpr std::string_view title_font{"fonts/Spades.ttf"};
inline constexpr std::string_view tab_font{"fonts/Edo.ttf"};
inline constexpr std::string_view body_font{"fonts/A750-Sans-Medium.ttf"};
inline constexpr std::string_view back_icon{"png/ui/common_elements/nav_bar/back_icon.png"};

} // namespace friends_lobby_assets

} // namespace battlespades::frontend
