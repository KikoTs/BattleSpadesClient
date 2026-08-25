#pragma once

#include "battlespades/frontend/ugc_publish_menu.hpp"
#include "battlespades/ui/draw_list.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace battlespades::frontend {

struct UgcPublishPresentationContext final {
    ui::PixelExtent window{MainMenuModel::reference_width_pixels,
                           MainMenuModel::reference_height_pixels};
    std::uint16_t background_opacity_per_mille{1'000U};
};

/** Top-left 800x600 geometry recovered from ListPreviewMenuBase and panels. */
struct UgcPublishClassicLayout final {
    ui::DrawRect frame{};
    ui::DrawRect title{};
    ui::DrawRect back{};
    ui::DrawRect map_panel{};
    ui::DrawRect map_header{};
    ui::DrawRect map_first_row{};
    ui::DrawRect preview_panel{};
    ui::DrawRect preview_header{};
    ui::DrawRect preview_first_row{};
    ui::DrawRect delete_button{};
    ui::DrawRect delete_tooltip{};
    ui::DrawRect name_edit{};
    ui::DrawRect name_preview{};
    ui::DrawRect primary_background{};
    ui::DrawRect primary_button{};

    [[nodiscard]] friend constexpr bool operator==(const UgcPublishClassicLayout&,
                                                   const UgcPublishClassicLayout&) = default;
};

[[nodiscard]] UgcPublishClassicLayout ugc_publish_classic_layout() noexcept;

/** DrawList reconstruction of the visible Publish Map panels and dialogs. */
class UgcPublishPresentation final {
public:
    [[nodiscard]] ui::DrawList
    build(const UgcPublishMenuModel& model,
          const UgcPublishPresentationContext& context = {}) const;

    /** Foreground-only layer suitable for the shared horizontal compositor. */
    [[nodiscard]] ui::DrawList build_layer(const UgcPublishMenuModel& model) const;
};

namespace ugc_publish_assets {

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
inline constexpr std::string_view message_information{
    "png/ui/common_elements/frames/ui_frame_overlay_confirmation.png"};
inline constexpr std::string_view message_warning{
    "png/ui/common_elements/frames/ui_frame_overlay_warning.png"};
inline constexpr std::string_view message_extended{
    "png/ui/common_elements/frames/ui_frame_overlay_disclaimer.png"};
inline constexpr std::string_view white_pixel{"png/high/white.png"};
inline constexpr std::string_view title_font{"fonts/Spades.ttf"};
inline constexpr std::string_view header_font{"fonts/Edo.ttf"};
inline constexpr std::string_view row_font{"fonts/A750-Sans-Medium.ttf"};

[[nodiscard]] std::span<const MainMenuAsset> required() noexcept;

} // namespace ugc_publish_assets

} // namespace battlespades::frontend
