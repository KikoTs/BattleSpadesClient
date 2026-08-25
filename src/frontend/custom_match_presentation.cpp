#include "battlespades/frontend/custom_match_presentation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

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
using ui::TextureFilter;
using ui::VerticalTextAlignment;
using UiTextureAnchor = ui::TextureAnchor;

constexpr ColorRgba8 white{255U, 255U, 255U, 255U};
constexpr ColorRgba8 cream{244U, 236U, 187U, 255U};
constexpr ColorRgba8 gold{232U, 207U, 78U, 255U};
constexpr ColorRgba8 black{0U, 0U, 0U, 255U};
constexpr ColorRgba8 button_text{20U, 20U, 20U, 255U};
constexpr ColorRgba8 row_grey{57U, 53U, 44U, 255U};
constexpr ColorRgba8 row_dark{24U, 21U, 14U, 255U};
constexpr ColorRgba8 drop_background{34U, 32U, 33U, 255U};
constexpr ColorRgba8 unavailable{146U, 141U, 111U, 255U};
constexpr ColorRgba8 friend_green{146U, 241U, 141U, 255U};
constexpr double source_scale{0.6};
constexpr double global_scale{0.64};
constexpr double lobby_row_height{25.0};
constexpr double preview_row_height{25.0};

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
                                   std::string_view font = custom_match_assets::row_font,
                                   std::uint8_t maximum_lines = 1U,
                                   double line_spacing = 1.0,
                                   std::uint16_t intensity = 1'000U) {
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
                           color(text_color, intensity)};
}

void solid(ui::DrawList& list, DrawRect bounds, ColorRgba8 fill) {
    list.push(sprite(custom_match_assets::white_pixel,
                     bounds,
                     DrawSpace::design_pixels,
                     TextureFilter::nearest,
                     UiTextureAnchor::top_left,
                     1.0,
                     color(fill)));
}

[[nodiscard]] std::size_t button_state_index(WidgetVisualState state) noexcept {
    switch (state) {
    case WidgetVisualState::hovered:
    case WidgetVisualState::focused:
        return 1U;
    case WidgetVisualState::pressed:
        return 2U;
    case WidgetVisualState::normal:
    case WidgetVisualState::disabled:
        return 0U;
    }
    return 0U;
}

void append_button(ui::DrawList& list,
                   DrawRect bounds,
                   std::string_view label,
                   WidgetVisualState state) {
    // Retail scales the loaded 36x58 button slices to the requested height.
    const auto slice_width = std::floor((36.0 / 58.0) * bounds.height);
    const auto middle_width = bounds.width - slice_width * 2.0;
    const auto index = button_state_index(state);
    const auto intensity =
        state == WidgetVisualState::disabled ? std::uint16_t{700U} : std::uint16_t{1'000U};
    const std::array destinations{
        DrawRect{bounds.x, bounds.y, slice_width + 1.0, bounds.height},
        DrawRect{bounds.x + slice_width, bounds.y, middle_width + 1.0, bounds.height},
        DrawRect{bounds.x + slice_width + middle_width,
                 bounds.y,
                 slice_width + 1.0,
                 bounds.height},
    };
    for (std::size_t slice_index = 0U; slice_index < destinations.size(); ++slice_index) {
        list.push(sprite(button_assets[index][slice_index],
                         destinations[slice_index],
                         DrawSpace::design_pixels,
                         TextureFilter::nearest,
                         UiTextureAnchor::top_left,
                         source_scale,
                         color(white, intensity)));
    }
    const auto pressed_offset = state == WidgetVisualState::pressed ? 2.0 : 0.0;
    list.push(text(label,
                   {bounds.x + 14.0,
                    bounds.y + 4.0 + pressed_offset,
                    bounds.width - 28.0,
                    bounds.height - 8.0},
                   18.0,
                   button_text,
                   HorizontalTextAlignment::center,
                   TextTransform::uppercase,
                   main_menu_assets::button_font,
                   2U,
                   2.0));
}

void append_back(ui::DrawList& list, DrawRect bounds, WidgetVisualState state) {
    const auto highlighted =
        state == WidgetVisualState::hovered || state == WidgetVisualState::pressed;
    const auto intensity = highlighted ? std::uint16_t{1'000U} : std::uint16_t{700U};
    const auto pressed_offset = state == WidgetVisualState::pressed ? 1.0 : 0.0;
    list.push(text("BACK",
                   {bounds.x + 35.0,
                    bounds.y + pressed_offset,
                    bounds.width - 35.0,
                    bounds.height},
                   24.0,
                   gold,
                   HorizontalTextAlignment::left,
                   TextTransform::uppercase,
                   main_menu_assets::button_font,
                   1U,
                   1.0,
                   intensity));
    list.push(sprite(custom_match_assets::back_icon,
                     {bounds.x + 2.5, bounds.y + 3.5 + pressed_offset, 25.0, 25.0},
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale,
                     color(white, intensity)));
}

void append_panel(ui::DrawList& list,
                  DrawRect panel,
                  DrawRect header,
                  std::string_view title_key) {
    list.push(sprite(custom_match_assets::panel,
                     panel,
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
    list.push(sprite(custom_match_assets::panel_header,
                     header,
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
    if (!title_key.empty()) {
        list.push(text(title_key,
                       {header.x + 14.0, header.y, header.width - 28.0, header.height},
                       19.0,
                       cream,
                       HorizontalTextAlignment::left,
                       TextTransform::preserve,
                       custom_match_assets::header_font));
    }
}

void append_source_filter(ui::DrawList& list,
                          const CustomMatchMenuModel& model,
                          const CustomMatchClassicLayout& layout) {
    const auto bounds = layout.source_filter;
    solid(list, bounds, black);
    // MenuOptionControl: 4 px border, 2 px gap, and a 16 px SquareButton.
    solid(list, {bounds.x + 4.0, bounds.y + 4.0, 104.0, 16.0}, drop_background);
    const auto state = model.visual_state(model.controls()[1U].widget.id);
    const auto intensity =
        state == WidgetVisualState::disabled ? std::uint16_t{700U} : std::uint16_t{1'000U};
    const auto pressed_offset = state == WidgetVisualState::pressed ? 1.0 : 0.0;
    list.push(sprite(custom_match_assets::square_button,
                     {352.0, 118.0 + pressed_offset, 16.0, 16.0},
                     DrawSpace::design_pixels,
                     TextureFilter::nearest,
                     UiTextureAnchor::center,
                     source_scale,
                     color(white, intensity)));
    list.push(sprite(custom_match_assets::down_arrow,
                     {352.0, 118.0 + pressed_offset, 16.0, 16.0},
                     DrawSpace::design_pixels,
                     TextureFilter::nearest,
                     UiTextureAnchor::center,
                     1.0,
                     color(white, intensity)));
    list.push(text(localization_key(model.source()),
                   {bounds.x + 14.0, bounds.y, 84.0, bounds.height},
                   12.0,
                   cream,
                   HorizontalTextAlignment::left,
                   TextTransform::uppercase,
                   custom_match_assets::row_font,
                   1U,
                   1.0,
                   intensity));
}

[[nodiscard]] std::string members_label(const CustomMatchLobbyRecord& lobby) {
    return std::to_string(lobby.member_count) + '/' + std::to_string(lobby.maximum_members);
}

[[nodiscard]] std::string friends_label(const CustomMatchLobbyRecord& lobby) {
    if (lobby.friend_count == 0U) {
        return {};
    }
    return '(' + std::to_string(lobby.friend_count) + ' ' +
           (lobby.friend_count == 1U ? "FRIEND)" : "FRIENDS)");
}

void append_lobby_rows(ui::DrawList& list,
                       const CustomMatchMenuModel& model,
                       const CustomMatchClassicLayout& layout) {
    const auto lobbies = model.lobbies();
    const auto first = std::min(model.first_visible_row(), lobbies.size());
    const auto count =
        std::min(CustomMatchMenuModel::visible_lobby_rows, lobbies.size() - first);
    if (count == 0U) {
        const auto status = model.status_localization_key();
        if (!status.empty()) {
            list.push(text(status,
                           {layout.list_panel.x + 20.0,
                            layout.first_lobby_row.y + 40.0,
                            layout.list_panel.width - 40.0,
                            70.0},
                           15.0,
                           unavailable,
                           HorizontalTextAlignment::center));
        }
        return;
    }

    const auto selected = model.selected_index();
    for (std::size_t visible = 0U; visible < count; ++visible) {
        const auto index = first + visible;
        const auto& lobby = lobbies[index];
        const DrawRect row{layout.first_lobby_row.x,
                           layout.first_lobby_row.y +
                               lobby_row_height * static_cast<double>(visible),
                           layout.first_lobby_row.width,
                           lobby_row_height};
        solid(list, row, visible % 2U == 0U ? row_grey : row_dark);
        if (selected == index) {
            list.push(sprite(custom_match_assets::highlight_line,
                             row,
                             DrawSpace::design_pixels,
                             TextureFilter::nearest,
                             UiTextureAnchor::top_left,
                             source_scale));
            list.push(sprite(custom_match_assets::highlight_glow,
                             {row.x - row.width * 0.025,
                              row.y - row.height * 0.15,
                              row.width * 1.05,
                              row.height * 1.3},
                             DrawSpace::design_pixels,
                             TextureFilter::nearest,
                             UiTextureAnchor::top_left,
                             source_scale));
        }

        // Exact SquadListItem horizontal subdivisions for a 320 px row.
        list.push(text(lobby.name, {80.0, row.y, 145.0, row.height}, 11.0));
        if (lobby.ping_milliseconds > 0U) {
            list.push(text(std::to_string(lobby.ping_milliseconds),
                           {201.0, row.y, 25.0, row.height},
                           10.0,
                           cream,
                           HorizontalTextAlignment::center));
        }
        const auto friend_text = friends_label(lobby);
        if (!friend_text.empty()) {
            list.push(text(friend_text,
                           {241.0, row.y, 90.0, row.height},
                           10.0,
                           friend_green,
                           HorizontalTextAlignment::center));
        }
        list.push(text(members_label(lobby),
                       {346.0, row.y, 25.0, row.height},
                       10.0,
                       cream,
                       HorizontalTextAlignment::center));
    }
}

void append_preview_rows(ui::DrawList& list,
                         const CustomMatchLobbyRecord* selected,
                         const CustomMatchClassicLayout& layout) {
    if (selected == nullptr) {
        list.push(text("SELECT_MATCH_LOBBY",
                       {layout.preview_panel.x + 20.0,
                        layout.first_preview_row.y + 40.0,
                        layout.preview_panel.width - 40.0,
                        70.0},
                       15.0,
                       unavailable,
                       HorizontalTextAlignment::center));
        return;
    }
    if (!selected->details_ready()) {
        list.push(text("LOBBY_DETAILS_UNAVAILABLE",
                       {layout.preview_panel.x + 20.0,
                        layout.first_preview_row.y + 40.0,
                        layout.preview_panel.width - 40.0,
                        70.0},
                       15.0,
                       unavailable,
                       HorizontalTextAlignment::center));
        return;
    }

    std::size_t visible{};
    const auto append_category = [&](std::string_view category,
                                     const std::vector<std::string>& values) {
        if (values.empty() || visible >= 11U) {
            return;
        }
        DrawRect row{layout.first_preview_row.x,
                     layout.first_preview_row.y +
                         preview_row_height * static_cast<double>(visible),
                     layout.first_preview_row.width,
                     preview_row_height};
        solid(list, row, row_dark);
        list.push(text(category,
                       {row.x + 14.0, row.y, row.width - 28.0, row.height},
                       12.0,
                       cream,
                       HorizontalTextAlignment::left,
                       TextTransform::preserve,
                       custom_match_assets::header_font));
        ++visible;
        for (const auto& value : values) {
            if (visible >= 11U) {
                break;
            }
            row.y = layout.first_preview_row.y +
                    preview_row_height * static_cast<double>(visible);
            solid(list, row, visible % 2U == 0U ? row_grey : row_dark);
            list.push(text(value,
                           {row.x + 14.0, row.y, row.width - 28.0, row.height},
                           11.0));
            ++visible;
        }
    };

    append_category("GAME_INFO", selected->game_info);
    append_category("GAME_RULES", selected->game_rules);
}

[[nodiscard]] DrawRect background_cover(ui::PixelExtent window) noexcept {
    constexpr double source_width{768.0};
    constexpr double source_height{576.0};
    const auto width = static_cast<double>(window.width);
    const auto height = static_cast<double>(window.height);
    const auto scale = std::max(width / source_width, height / source_height);
    const auto covered_width = source_width * scale;
    const auto covered_height = source_height * scale;
    return {(width - covered_width) * 0.5,
            (height - covered_height) * 0.5,
            covered_width,
            covered_height};
}

} // namespace

CustomMatchClassicLayout custom_match_classic_layout() noexcept {
    return CustomMatchClassicLayout{
        {25.0, 5.0, 750.0, 589.0},
        {120.0, 25.0, 560.0, 50.0},
        {54.0, 541.0, 78.0, 32.0},
        {56.0, 95.0, 340.0, 413.0},
        {66.0, 105.0, 320.0, 40.0},
        {242.0, 114.0, 130.0, 24.0},
        {66.0, 155.0, 320.0, 25.0},
        {401.0, 95.0, 340.0, 354.0},
        {411.0, 105.0, 320.0, 40.0},
        {411.0, 155.0, 320.0, 25.0},
        {401.0, 452.0, 340.0, 54.0},
        {405.0, 456.0, 332.0, 50.0},
    };
}

ui::DrawList
CustomMatchPresentation::build(const CustomMatchMenuModel& model,
                               const CustomMatchPresentationContext& context) const {
    if (!context.window.is_valid() || context.background_opacity_per_mille > 1'000U) {
        throw std::invalid_argument{"invalid Custom Match presentation context"};
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

ui::DrawList
CustomMatchPresentation::build_layer(const CustomMatchMenuModel& model) const {
    const auto layout = custom_match_classic_layout();
    ui::DrawList list;
    list.reserve(128U);
    list.push(sprite(custom_match_assets::frame,
                     layout.frame,
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
    list.push(text("SQUAD_LIST",
                   layout.title,
                   44.0,
                   cream,
                   HorizontalTextAlignment::center,
                   TextTransform::uppercase,
                   custom_match_assets::title_font));
    append_back(list, layout.back, model.visual_state(model.controls()[0U].widget.id));

    append_panel(list, layout.list_panel, layout.list_header, "AVAILABLE_SQUADS");
    append_source_filter(list, model, layout);
    append_lobby_rows(list, model, layout);

    const auto* selected = model.selected_lobby();
    append_panel(list,
                 layout.preview_panel,
                 layout.preview_header,
                 selected == nullptr ? std::string_view{} : std::string_view{selected->name});
    append_preview_rows(list, selected, layout);

    list.push(sprite(custom_match_assets::panel,
                     layout.action_background,
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
    append_button(list,
                  layout.join_button,
                  model.selected_requires_purchase() ? std::string_view{"CONTENT_REQUIRED"}
                                                     : std::string_view{"JOIN_SQUAD"},
                  model.visual_state(model.controls()[2U].widget.id));
    return list;
}

namespace custom_match_assets {

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
        MainMenuAsset{main_menu_assets::background, texture, linear, top_left, source_scale},
        MainMenuAsset{frame, texture, linear, center, global_scale},
        MainMenuAsset{panel, texture, linear, center, global_scale},
        MainMenuAsset{panel_header, texture, linear, center, global_scale},
        MainMenuAsset{back_icon, texture, linear, center, global_scale},
        MainMenuAsset{highlight_line, texture, nearest, top_left, source_scale},
        MainMenuAsset{highlight_glow, texture, nearest, top_left, source_scale},
        MainMenuAsset{square_button, texture, nearest, center, source_scale},
        MainMenuAsset{down_arrow, texture, nearest, center, 1.0},
        MainMenuAsset{white_pixel, texture, nearest, top_left, 1.0},
        MainMenuAsset{main_menu_assets::button_left, texture, nearest, top_left, source_scale},
        MainMenuAsset{main_menu_assets::button_middle,
                      texture,
                      nearest,
                      top_left,
                      source_scale},
        MainMenuAsset{main_menu_assets::button_right, texture, nearest, top_left, source_scale},
        MainMenuAsset{"png/ui/common_elements/buttons/button_large_hover_left.png",
                      texture,
                      nearest,
                      top_left,
                      source_scale},
        MainMenuAsset{"png/ui/common_elements/buttons/button_large_hover_mid.png",
                      texture,
                      nearest,
                      top_left,
                      source_scale},
        MainMenuAsset{"png/ui/common_elements/buttons/button_large_hover_right.png",
                      texture,
                      nearest,
                      top_left,
                      source_scale},
        MainMenuAsset{"png/ui/common_elements/buttons/button_large_press_left.png",
                      texture,
                      nearest,
                      top_left,
                      source_scale},
        MainMenuAsset{"png/ui/common_elements/buttons/button_large_press_mid.png",
                      texture,
                      nearest,
                      top_left,
                      source_scale},
        MainMenuAsset{"png/ui/common_elements/buttons/button_large_press_right.png",
                      texture,
                      nearest,
                      top_left,
                      source_scale},
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

} // namespace custom_match_assets

} // namespace battlespades::frontend
