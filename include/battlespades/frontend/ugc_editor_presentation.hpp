#pragma once

#include "battlespades/frontend/ugc_editor_menu.hpp"
#include "battlespades/ui/draw_list.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace battlespades::frontend {

struct UgcEditorPresentationContext final {
    ui::PixelExtent window{MainMenuModel::reference_width_pixels,
                           MainMenuModel::reference_height_pixels};
    std::uint16_t background_opacity_per_mille{1'000U};
    std::string_view player_name{"Player"};
};

/** Top-left conversion of the shared retail ListPreviewMenuBase geometry. */
struct UgcEditorBrowserClassicLayout final {
    ui::DrawRect frame{};
    ui::DrawRect title{};
    ui::DrawRect back{};
    ui::DrawRect list_panel{};
    ui::DrawRect list_header{};
    ui::DrawRect source_filter{};
    ui::DrawRect first_lobby_row{};
    ui::DrawRect preview_panel{};
    ui::DrawRect preview_header{};
    ui::DrawRect action_background{};
    ui::DrawRect join_button{};
    ui::DrawRect new_lobby_button{};

    [[nodiscard]] friend constexpr bool operator==(const UgcEditorBrowserClassicLayout&,
                                                   const UgcEditorBrowserClassicLayout&) = default;
};

[[nodiscard]] UgcEditorBrowserClassicLayout ugc_editor_browser_classic_layout() noexcept;

/** Retail `UGCSquadsMenu`: available lobbies, preview, Join and New Lobby. */
class UgcEditorBrowserPresentation final {
public:
    [[nodiscard]] ui::DrawList
    build(const UgcEditorBrowserModel& model,
          const UgcEditorPresentationContext& context = {}) const;
    [[nodiscard]] ui::DrawList build_layer(const UgcEditorBrowserModel& model) const;
};

struct UgcEditorLobbyClassicLayout final {
    ui::DrawRect frame{};
    ui::DrawRect title{};
    ui::DrawRect back{};
    ui::DrawRect members_panel{};
    ui::DrawRect members_header{};
    ui::DrawRect first_member_row{};
    ui::DrawRect invite_button{};
    ui::DrawRect settings_panel{};
    ui::DrawRect settings_header{};
    ui::DrawRect first_setting_row{};
    ui::DrawRect action_background{};
    ui::DrawRect start_button{};

    [[nodiscard]] friend constexpr bool operator==(const UgcEditorLobbyClassicLayout&,
                                                   const UgcEditorLobbyClassicLayout&) = default;
};

[[nodiscard]] UgcEditorLobbyClassicLayout ugc_editor_lobby_classic_layout() noexcept;

/** Retail `UGCSquadLobbyMenu` host view with its UGC-only settings enabled. */
class UgcEditorLobbyPresentation final {
public:
    [[nodiscard]] ui::DrawList
    build(const UgcEditorLobbyModel& model,
          const UgcEditorPresentationContext& context = {}) const;
    [[nodiscard]] ui::DrawList build_layer(const UgcEditorLobbyModel& model,
                                           std::string_view player_name = "Player") const;
};

/**
 * Top-left conversion of retail `UGCSettings` at its authored 800x600
 * coordinate system.  The two frame images retain the integer dimensions
 * produced by `global_scale = 0.64`; the button rectangles are the original
 * TextButton hit boxes rather than approximations of the surrounding art.
 */
struct UgcIngameSettingsClassicLayout final {
    ui::DrawRect outer_frame{};
    ui::DrawRect content_frame{};
    ui::DrawRect title{};
    ui::DrawRect list_panel{};
    ui::DrawRect scrollbar{};
    ui::DrawRect cancel_button{};
    ui::DrawRect apply_button{};

    [[nodiscard]] friend constexpr bool
    operator==(const UgcIngameSettingsClassicLayout&,
               const UgcIngameSettingsClassicLayout&) = default;
};

[[nodiscard]] UgcIngameSettingsClassicLayout
ugc_ingame_settings_classic_layout() noexcept;

/**
 * Retail `UGCSettings` renderer-neutral layer.  It owns only presentation:
 * the host-only model remains the authority for values and atomic Apply /
 * Cancel behavior.  `field_bounds()` shares the exact visible row geometry
 * with pointer routing so drawing and input cannot drift apart.
 */
class UgcIngameSettingsPresentation final {
public:
    [[nodiscard]] ui::DrawList build_layer(const UgcIngameSettingsModel& model) const;
    [[nodiscard]] std::optional<ui::DrawRect>
    field_bounds(const UgcIngameSettingsModel& model,
                 UgcIngameSettingField field) const noexcept;
};

namespace ugc_editor_assets {

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
inline constexpr std::string_view white_pixel{"png/high/white.png"};
inline constexpr std::string_view title_font{"fonts/Spades.ttf"};
inline constexpr std::string_view header_font{"fonts/Edo.ttf"};
inline constexpr std::string_view row_font{"fonts/A750-Sans-Medium.ttf"};
inline constexpr std::string_view settings_outer_frame{
    "png/ui/settings/in_game_settings_frame.png"};
inline constexpr std::string_view settings_content_frame{
    "png/ui/settings/in_game_settings_content_frame.png"};
inline constexpr std::string_view settings_row{
    "png/ui/settings/settings_common/settings_matchsettings_frame.png"};

[[nodiscard]] std::span<const MainMenuAsset> required() noexcept;

} // namespace ugc_editor_assets

} // namespace battlespades::frontend
