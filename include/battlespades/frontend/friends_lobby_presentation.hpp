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
inline constexpr std::string_view frame{"png/ui/main_menu/frame_main_menu.png"};
inline constexpr std::string_view white_pixel{"png/high/white.png"};
inline constexpr std::string_view friends_icon{"png/ui/icons/icon_friends.png"};
inline constexpr std::string_view title_font{"fonts/Spades.ttf"};
inline constexpr std::string_view body_font{"fonts/A750-Sans-Medium.ttf"};
inline constexpr std::string_view back_icon{"png/ui/common_elements/nav_bar/back_icon.png"};

} // namespace friends_lobby_assets

} // namespace battlespades::frontend
