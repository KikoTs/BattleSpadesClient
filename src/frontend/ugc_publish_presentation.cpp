#include "battlespades/frontend/ugc_publish_presentation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace battlespades::frontend {
namespace {

using ui::ColorModulation;
using ui::ColorRgba8;
using ui::DrawRect;
using ui::DrawSpace;
using ui::HorizontalTextAlignment;
using ui::SpriteDrawCommand;
using ui::SpriteSizing;
using ui::TextDrawCommand;
using ui::TextFit;
using ui::TextTransform;
using UiTextureAnchor = ui::TextureAnchor;
using ui::TextureFilter;
using ui::VerticalTextAlignment;

constexpr ColorRgba8 white{255U, 255U, 255U, 255U};
constexpr ColorRgba8 cream{244U, 236U, 187U, 255U};
constexpr ColorRgba8 gold{232U, 207U, 78U, 255U};
constexpr ColorRgba8 black{0U, 0U, 0U, 255U};
constexpr ColorRgba8 red{255U, 0U, 0U, 255U};
constexpr ColorRgba8 row_grey{57U, 53U, 44U, 255U};
constexpr ColorRgba8 row_dark{24U, 21U, 14U, 255U};
constexpr ColorRgba8 edit_background{45U, 45U, 45U, 255U};
constexpr ColorRgba8 published_green{157U, 230U, 0U, 255U};
constexpr ColorRgba8 unpublished_grey{146U, 141U, 111U, 255U};
constexpr ColorRgba8 delete_tint{179U, 26U, 26U, 255U};
constexpr double source_scale{0.6};
constexpr double global_scale{0.64};
constexpr double row_height{26.0};

constexpr std::array<std::array<std::string_view, 3U>, 3U> button_assets{{
    {"png/ui/common_elements/buttons/button_large_left.png",
     "png/ui/common_elements/buttons/button_large_mid.png",
     "png/ui/common_elements/buttons/button_large_right.png"},
    {"png/ui/common_elements/buttons/button_large_hover_left.png",
     "png/ui/common_elements/buttons/button_large_hover_mid.png",
     "png/ui/common_elements/buttons/button_large_hover_right.png"},
    {"png/ui/common_elements/buttons/button_large_press_left.png",
     "png/ui/common_elements/buttons/button_large_press_mid.png",
     "png/ui/common_elements/buttons/button_large_press_right.png"},
}};

[[nodiscard]] ColorModulation color(ColorRgba8 value = white,
                                    std::uint16_t intensity = 1'000U,
                                    std::uint16_t opacity = 1'000U) noexcept {
    return ColorModulation{value, intensity, opacity};
}

[[nodiscard]] SpriteDrawCommand sprite(std::string_view asset,
                                       DrawRect destination,
                                       DrawSpace space = DrawSpace::design_pixels,
                                       TextureFilter filter = TextureFilter::linear,
                                       UiTextureAnchor anchor = UiTextureAnchor::top_left,
                                       double retail_scale = source_scale,
                                       ColorModulation modulation = color(),
                                       SpriteSizing sizing = SpriteSizing::stretch) {
    return SpriteDrawCommand{
        std::string{asset}, destination, space, filter, anchor, retail_scale, sizing, modulation};
}

[[nodiscard]] TextDrawCommand text(std::string_view key,
                                   DrawRect destination,
                                   double size,
                                   ColorRgba8 text_color = cream,
                                   HorizontalTextAlignment horizontal =
                                       HorizontalTextAlignment::left,
                                   TextTransform transform = TextTransform::preserve,
                                   std::string_view font = ugc_publish_assets::row_font,
                                   std::uint8_t maximum_lines = 1U,
                                   double line_spacing = 1.0) {
    return TextDrawCommand{std::string{key},
                           std::string{font},
                           destination,
                           DrawSpace::design_pixels,
                           size,
                           line_spacing,
                           maximum_lines,
                           horizontal,
                           VerticalTextAlignment::retail_center,
                           transform,
                           TextFit::shrink_to_fit,
                           color(text_color)};
}

void solid(ui::DrawList& list, DrawRect rectangle, ColorRgba8 fill) {
    list.push(sprite(ugc_publish_assets::white_pixel,
                     rectangle,
                     DrawSpace::design_pixels,
                     TextureFilter::nearest,
                     UiTextureAnchor::top_left,
                     1.0,
                     color(fill)));
}

[[nodiscard]] DrawRect background_cover(ui::PixelExtent window) noexcept {
    constexpr double source_width{768.0};
    constexpr double source_height{576.0};
    const auto width = static_cast<double>(window.width);
    const auto height = static_cast<double>(window.height);
    const auto scale = std::max(width / source_width, height / source_height);
    const auto covered_width = source_width * scale;
    const auto covered_height = source_height * scale;
    return DrawRect{(width - covered_width) * 0.5,
                    (height - covered_height) * 0.5,
                    covered_width,
                    covered_height};
}

void append_panel(ui::DrawList& list, DrawRect panel_bounds, DrawRect header, std::string_view title) {
    list.push(sprite(ugc_publish_assets::panel,
                     panel_bounds,
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
    list.push(sprite(ugc_publish_assets::panel_header,
                     header,
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
    list.push(text(title,
                   {header.x + 14.0, header.y, header.width - 28.0, header.height},
                   19.0,
                   cream,
                   HorizontalTextAlignment::left,
                   TextTransform::preserve,
                   ugc_publish_assets::header_font));
}

void append_button(ui::DrawList& list,
                   DrawRect bounds,
                   std::string_view label,
                   bool enabled,
                   ColorRgba8 tint = white,
                   ColorRgba8 text_color = black,
                   double font_size = 18.0) {
    // Retail scales the already-loaded 36x58 slices to each button height.
    const auto slice_width = std::floor((36.0 / 58.0) * bounds.height);
    const auto middle_width = bounds.width - slice_width * 2.0;
    const auto intensity = enabled ? std::uint16_t{1'000U} : std::uint16_t{700U};
    const std::array destinations{
        DrawRect{bounds.x, bounds.y, slice_width + 1.0, bounds.height},
        DrawRect{bounds.x + slice_width, bounds.y, middle_width + 1.0, bounds.height},
        DrawRect{bounds.x + slice_width + middle_width,
                 bounds.y,
                 slice_width + 1.0,
                 bounds.height},
    };
    for (std::size_t index = 0U; index < destinations.size(); ++index) {
        list.push(sprite(button_assets[0U][index],
                         destinations[index],
                         DrawSpace::design_pixels,
                         TextureFilter::nearest,
                         UiTextureAnchor::top_left,
                         source_scale,
                         color(tint, intensity)));
    }
    // TextButton applies disabled modulation only to its background sprites.
    list.push(text(label,
                   {bounds.x + 14.0, bounds.y + 4.0, bounds.width - 28.0, bounds.height - 8.0},
                   font_size,
                   text_color,
                   HorizontalTextAlignment::center,
                   TextTransform::uppercase,
                   main_menu_assets::button_font,
                   2U,
                   2.0));
}

void append_back(ui::DrawList& list, DrawRect bounds) {
    constexpr double icon_width{25.0};
    constexpr double icon_height{25.0};
    list.push(text("BACK",
                   {bounds.x + 35.0, bounds.y, bounds.width - 35.0, bounds.height},
                   24.0,
                   gold,
                   HorizontalTextAlignment::left,
                   TextTransform::uppercase,
                   main_menu_assets::button_font));
    list.push(sprite(ugc_publish_assets::back_icon,
                     {bounds.x + 2.5, bounds.y + 3.5, icon_width, icon_height},
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale,
                     color(white, 700U)));
}

[[nodiscard]] ColorRgba8 state_color(UgcLocalMapState state) noexcept {
    return state == UgcLocalMapState::published ? published_green : unpublished_grey;
}

void append_map_rows(ui::DrawList& list,
                     const UgcPublishMenuModel& model,
                     const UgcPublishClassicLayout& layout) {
    const auto maps = model.local_maps();
    const auto start = std::min(model.first_visible_row(), maps.size());
    const auto count = std::min(UgcPublishMenuModel::visible_map_rows, maps.size() - start);
    if (count == 0U) {
        list.push(text("NO_LOCAL_UGC_MAPS",
                       {layout.map_panel.x + 20.0,
                        layout.map_header.y + layout.map_header.height + 20.0,
                        layout.map_panel.width - 40.0,
                        60.0},
                       16.0,
                       unpublished_grey,
                       HorizontalTextAlignment::center));
        return;
    }

    const auto selected = model.selected_index();
    for (std::size_t visible = 0U; visible < count; ++visible) {
        const auto index = start + visible;
        const auto& map = maps[index];
        const DrawRect row{layout.map_first_row.x,
                           layout.map_first_row.y + row_height * static_cast<double>(visible),
                           layout.map_first_row.width,
                           row_height};
        solid(list, row, visible % 2U == 0U ? row_grey : row_dark);
        if (selected == index) {
            list.push(sprite(ugc_publish_assets::highlight_line,
                             row,
                             DrawSpace::design_pixels,
                             TextureFilter::nearest,
                             UiTextureAnchor::top_left,
                             source_scale));
            list.push(sprite(ugc_publish_assets::highlight_glow,
                             {row.x - row.width * 0.025,
                              row.y - row.height * 0.15,
                              row.width * 1.05,
                              row.height * 1.3},
                             DrawSpace::design_pixels,
                             TextureFilter::nearest,
                             UiTextureAnchor::top_left,
                             source_scale));
        }
        const auto half = row.width * 0.5;
        const auto status_color = state_color(map.state);
        list.push(text(map.title,
                       {row.x + 14.0, row.y, half - 28.0, row.height},
                       12.0,
                       map.state == UgcLocalMapState::published ? status_color : cream));
        list.push(text(ugc_map_state_localization_key(map.state),
                       {row.x + half, row.y, half - 14.0, row.height},
                       11.0,
                       status_color,
                       HorizontalTextAlignment::right));
    }
}

void append_preview_rows(ui::DrawList& list,
                         const UgcLocalMapRecord* map,
                         const UgcPublishClassicLayout& layout) {
    if (map == nullptr) {
        list.push(text("SELECT_A_LOCAL_MAP",
                       {layout.preview_panel.x + 20.0,
                        layout.preview_header.y + layout.preview_header.height + 20.0,
                        layout.preview_panel.width - 40.0,
                        60.0},
                       16.0,
                       unpublished_grey,
                       HorizontalTextAlignment::center));
        return;
    }
    constexpr std::size_t maximum_preview_rows{9U};
    const auto count = std::min(maximum_preview_rows, map->modes.size());
    for (std::size_t index = 0U; index < count; ++index) {
        const auto& mode = map->modes[index];
        const DrawRect row{layout.preview_first_row.x,
                           layout.preview_first_row.y + row_height * static_cast<double>(index),
                           layout.preview_first_row.width,
                           row_height};
        solid(list, row, index % 2U == 0U ? row_grey : row_dark);
        const auto mode_color = mode.publishable ? published_green : unpublished_grey;
        const auto state_key = mode.publishable
                                   ? std::string_view{"COMPLETED"}
                                   : (mode.reason_key.empty()
                                          ? std::string_view{"CANNOT_BE_PUBLISHED"}
                                          : std::string_view{mode.reason_key});
        const auto half = row.width * 0.5;
        const auto display = mode.display_key.empty() ? std::string_view{mode.mode_id}
                                                       : std::string_view{mode.display_key};
        list.push(text(display,
                       {row.x + 14.0, row.y, half - 28.0, row.height},
                       12.0,
                       mode_color));
        list.push(text(state_key,
                       {row.x + half, row.y, half - 14.0, row.height},
                       10.0,
                       mode_color,
                       HorizontalTextAlignment::right));
    }
}

void append_dialog_buttons(ui::DrawList& list, bool two_buttons, double top) {
    if (two_buttons) {
        append_button(list, {205.0, top, 160.0, 50.0}, "KICK_YES", true);
        append_button(list, {435.0, top, 160.0, 50.0}, "KICK_NO", true);
    } else {
        append_button(list, {320.0, top, 160.0, 50.0}, "OK", true);
    }
}

void append_dialog(ui::DrawList& list, const UgcPublishMenuModel& model) {
    const auto dialog = model.dialog();
    if (dialog == UgcPublishDialog::none) {
        return;
    }

    if (dialog == UgcPublishDialog::confirm_publish) {
        list.push(sprite(ugc_publish_assets::message_extended,
                         {109.0, 110.0, 582.0, 379.0},
                         DrawSpace::design_pixels,
                         TextureFilter::linear,
                         UiTextureAnchor::center,
                         global_scale));
        list.push(text("UGC_PUBLISH_CONFIRMATION",
                       {155.0, 150.0, 490.0, 90.0},
                       20.0,
                       cream,
                       HorizontalTextAlignment::center,
                       TextTransform::preserve,
                       ugc_publish_assets::row_font,
                       3U,
                       10.0));
        list.push(text("STEMWORKS_LICENSE_MESSAGE",
                       {155.0, 260.0, 490.0, 90.0},
                       14.0,
                       ColorRgba8{0U, 0U, 255U, 255U},
                       HorizontalTextAlignment::center,
                       TextTransform::preserve,
                       ugc_publish_assets::row_font,
                       4U,
                       2.0));
        append_dialog_buttons(list, true, 410.0);
        return;
    }

    if (dialog == UgcPublishDialog::uploading || dialog == UgcPublishDialog::deleting) {
        list.push(sprite(ugc_publish_assets::message_information,
                         {109.0, 181.0, 582.0, 238.0},
                         DrawSpace::design_pixels,
                         TextureFilter::linear,
                         UiTextureAnchor::center,
                         global_scale));
        list.push(text(dialog == UgcPublishDialog::uploading ? "UGC_UPLOADING_MAP_MESSAGE"
                                                             : "UGC_DELETING_MAP_MESSAGE",
                       {155.0, 225.0, 490.0, 150.0},
                       20.0,
                       cream,
                       HorizontalTextAlignment::center,
                       TextTransform::preserve,
                       ugc_publish_assets::row_font,
                       3U,
                       10.0));
        return;
    }

    list.push(sprite(ugc_publish_assets::message_warning,
                     {109.0, 137.0, 582.0, 326.0},
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
    std::string_view message{"UGC_OPERATION_ERROR"};
    bool two_buttons{};
    switch (dialog) {
    case UgcPublishDialog::upload_error:
        message = "UGC_UPLOAD_ERROR";
        break;
    case UgcPublishDialog::confirm_delete:
        message = "UGC_DELETE_MAP_CONFIMATION";
        two_buttons = true;
        break;
    case UgcPublishDialog::delete_complete:
        message = "UGC_MAP_DELETED_SUCCESSFULLY";
        break;
    case UgcPublishDialog::operation_error:
        message = "UGC_OPERATION_ERROR";
        break;
    case UgcPublishDialog::none:
    case UgcPublishDialog::confirm_publish:
    case UgcPublishDialog::uploading:
    case UgcPublishDialog::deleting:
        break;
    }
    list.push(text(message,
                   {155.0, 175.0, 490.0, 145.0},
                   20.0,
                   cream,
                   HorizontalTextAlignment::center,
                   TextTransform::preserve,
                   ugc_publish_assets::row_font,
                   4U,
                   10.0));
    if (dialog == UgcPublishDialog::confirm_delete) {
        if (const auto* map = model.selected_map(); map != nullptr) {
            list.push(text(map->title,
                           {155.0, 285.0, 490.0, 35.0},
                           15.0,
                           cream,
                           HorizontalTextAlignment::center));
        }
    }
    append_dialog_buttons(list, two_buttons, 365.0);
}

} // namespace

UgcPublishClassicLayout ugc_publish_classic_layout() noexcept {
    return UgcPublishClassicLayout{
        {25.0, 5.0, 750.0, 589.0},
        {120.0, 25.0, 560.0, 50.0},
        {54.0, 541.0, 78.0, 32.0},
        {56.0, 95.0, 340.0, 413.0},
        {66.0, 105.0, 320.0, 40.0},
        {66.0, 155.0, 320.0, 26.0},
        {401.0, 95.0, 340.0, 354.0},
        {411.0, 105.0, 320.0, 40.0},
        {411.0, 155.0, 320.0, 26.0},
        {411.0, 414.0, 80.0, 25.0},
        {495.0, 414.0, 236.0, 23.0},
        {66.0, 155.0, 320.0, 30.0},
        {66.0, 195.0, 320.0, 303.0},
        {401.0, 452.0, 340.0, 54.0},
        {405.0, 456.0, 332.0, 50.0},
    };
}

ui::DrawList
UgcPublishPresentation::build(const UgcPublishMenuModel& model,
                              const UgcPublishPresentationContext& context) const {
    if (!context.window.is_valid() || context.background_opacity_per_mille > 1'000U) {
        throw std::invalid_argument{"invalid UGC Publish presentation context"};
    }
    auto layer = build_layer(model);
    ui::DrawList list;
    list.reserve(layer.size() + 1U);
    list.push(sprite(main_menu_assets::background,
                     background_cover(context.window),
                     DrawSpace::window_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::top_left,
                     source_scale,
                     color(white, 1'000U, context.background_opacity_per_mille),
                     SpriteSizing::cover));
    for (const auto& command : layer.commands()) {
        list.push(command);
    }
    return list;
}

ui::DrawList UgcPublishPresentation::build_layer(const UgcPublishMenuModel& model) const {
    const auto layout = ugc_publish_classic_layout();
    ui::DrawList list;
    list.reserve(96U);
    list.push(sprite(ugc_publish_assets::frame,
                     layout.frame,
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
    auto title_command = text("PUBLISH",
                              layout.title,
                              44.0,
                              cream,
                              HorizontalTextAlignment::center,
                              TextTransform::uppercase,
                              ugc_publish_assets::title_font);
    list.push(std::move(title_command));
    append_back(list, layout.back);

    const auto* selected = model.selected_map();
    if (model.page() == UgcPublishPage::map_list) {
        append_panel(list, layout.map_panel, layout.map_header, "MAP_LIST");
        append_map_rows(list, model, layout);
    } else {
        append_panel(list, layout.map_panel, layout.map_header, "NAME_MAP");
        solid(list, layout.name_edit, edit_background);
        list.push(text(model.publish_title(),
                       {layout.name_edit.x + 4.0,
                        layout.name_edit.y,
                        layout.name_edit.width - 8.0,
                        layout.name_edit.height},
                       14.0));
        if (selected != nullptr && !selected->preview_asset.empty()) {
            list.push(sprite(selected->preview_asset,
                             layout.name_preview,
                             DrawSpace::design_pixels,
                             TextureFilter::linear,
                             UiTextureAnchor::top_left,
                             1.0));
        } else {
            list.push(text("MAP_PREVIEW_UNAVAILABLE",
                           layout.name_preview,
                           15.0,
                           unpublished_grey,
                           HorizontalTextAlignment::center));
        }
    }

    append_panel(list,
                 layout.preview_panel,
                 layout.preview_header,
                 selected == nullptr ? std::string_view{"MAP_PREVIEW"}
                                     : std::string_view{selected->title});
    append_preview_rows(list, selected, layout);
    if (model.page() == UgcPublishPage::map_list && selected != nullptr) {
        append_button(list,
                      layout.delete_button,
                      "DELETE",
                      true,
                      delete_tint,
                      white,
                      14.0);
        solid(list, layout.delete_tooltip, black);
        list.push(text("DELETE_MAP_MESSAGE",
                       {layout.delete_tooltip.x + 14.0,
                        layout.delete_tooltip.y,
                        layout.delete_tooltip.width - 28.0,
                        layout.delete_tooltip.height},
                       11.0,
                       red,
                       HorizontalTextAlignment::center));
    }

    list.push(sprite(ugc_publish_assets::panel,
                     layout.primary_background,
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
    append_button(list,
                  layout.primary_button,
                  model.primary_localization_key(),
                  model.primary_enabled());
    append_dialog(list, model);
    return list;
}

namespace ugc_publish_assets {

std::span<const MainMenuAsset> required() noexcept {
    constexpr auto texture = MainMenuAssetKind::texture;
    constexpr auto font_asset = MainMenuAssetKind::font;
    constexpr auto music_asset = MainMenuAssetKind::music;
    constexpr auto sound_asset = MainMenuAssetKind::sound;
    constexpr auto linear = TextureSampling::linear;
    constexpr auto nearest = TextureSampling::nearest;
    constexpr auto none = TextureSampling::not_applicable;
    constexpr auto top_left = TextureAnchor::top_left;
    constexpr auto center = TextureAnchor::center;
    constexpr auto no_anchor = TextureAnchor::not_applicable;

    static constexpr std::array assets{
        MainMenuAsset{main_menu_assets::background, texture, linear, top_left, 0.6},
        MainMenuAsset{frame, texture, linear, center, 0.64},
        MainMenuAsset{panel, texture, linear, center, 0.64},
        MainMenuAsset{panel_header, texture, linear, center, 0.64},
        MainMenuAsset{back_icon, texture, linear, center, 0.64},
        MainMenuAsset{highlight_line, texture, nearest, top_left, 0.6},
        MainMenuAsset{highlight_glow, texture, nearest, top_left, 0.6},
        MainMenuAsset{message_information, texture, linear, center, 0.64},
        MainMenuAsset{message_warning, texture, linear, center, 0.64},
        MainMenuAsset{message_extended, texture, linear, center, 0.64},
        MainMenuAsset{white_pixel, texture, nearest, top_left, 1.0},
        MainMenuAsset{main_menu_assets::button_left, texture, nearest, top_left, 0.6},
        MainMenuAsset{main_menu_assets::button_middle, texture, nearest, top_left, 0.6},
        MainMenuAsset{main_menu_assets::button_right, texture, nearest, top_left, 0.6},
        MainMenuAsset{"png/ui/common_elements/buttons/button_large_hover_left.png",
                      texture,
                      nearest,
                      top_left,
                      0.6},
        MainMenuAsset{"png/ui/common_elements/buttons/button_large_hover_mid.png",
                      texture,
                      nearest,
                      top_left,
                      0.6},
        MainMenuAsset{"png/ui/common_elements/buttons/button_large_hover_right.png",
                      texture,
                      nearest,
                      top_left,
                      0.6},
        MainMenuAsset{"png/ui/common_elements/buttons/button_large_press_left.png",
                      texture,
                      nearest,
                      top_left,
                      0.6},
        MainMenuAsset{"png/ui/common_elements/buttons/button_large_press_mid.png",
                      texture,
                      nearest,
                      top_left,
                      0.6},
        MainMenuAsset{"png/ui/common_elements/buttons/button_large_press_right.png",
                      texture,
                      nearest,
                      top_left,
                      0.6},
        MainMenuAsset{title_font, font_asset, none, no_anchor, 1.0},
        MainMenuAsset{header_font, font_asset, none, no_anchor, 1.0},
        MainMenuAsset{row_font, font_asset, none, no_anchor, 1.0},
        MainMenuAsset{main_menu_assets::button_font, font_asset, none, no_anchor, 1.0},
        MainMenuAsset{main_menu_assets::music, music_asset, none, no_anchor, 1.0},
        MainMenuAsset{main_menu_assets::confirmation_sound,
                      sound_asset,
                      none,
                      no_anchor,
                      1.0},
        MainMenuAsset{main_menu_assets::back_sound, sound_asset, none, no_anchor, 1.0},
    };
    return assets;
}

} // namespace ugc_publish_assets

} // namespace battlespades::frontend
