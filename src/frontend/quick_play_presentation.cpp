#include "battlespades/frontend/quick_play_presentation.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

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

constexpr double source_scale{0.6};
constexpr double global_scale{0.64};
constexpr ColorRgba8 white{255U, 255U, 255U, 255U};
constexpr ColorRgba8 button_text{20U, 20U, 20U, 255U};
constexpr ColorRgba8 menu_text{244U, 236U, 187U, 255U};
constexpr ColorRgba8 navigation_text{232U, 207U, 78U, 255U};
constexpr ColorRgba8 row_grey{57U, 53U, 44U, 255U};
constexpr ColorRgba8 row_dark_grey{24U, 21U, 14U, 255U};
constexpr ColorRgba8 row_hover{175U, 172U, 161U, 255U};
constexpr ColorRgba8 category_green{59U, 68U, 25U, 255U};

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
                                       TextureFilter sampling = TextureFilter::linear,
                                       UiTextureAnchor anchor = UiTextureAnchor::top_left,
                                       double retail_scale = source_scale,
                                       ColorModulation modulation = color(),
                                       SpriteSizing sizing = SpriteSizing::stretch) {
    return SpriteDrawCommand{
        std::string{asset}, destination, space, sampling, anchor, retail_scale, sizing, modulation};
}

[[nodiscard]] TextDrawCommand text(std::string_view value,
                                   std::string_view font,
                                   DrawRect destination,
                                   double size,
                                   ColorRgba8 text_color = menu_text,
                                   HorizontalTextAlignment horizontal =
                                       HorizontalTextAlignment::left,
                                   VerticalTextAlignment vertical =
                                       VerticalTextAlignment::retail_center,
                                   TextTransform transform = TextTransform::preserve,
                                   TextFit fit = TextFit::shrink_to_fit,
                                   std::uint16_t intensity = 1'000U) {
    return TextDrawCommand{std::string{value},
                           std::string{font},
                           destination,
                           DrawSpace::design_pixels,
                           size,
                           0.0,
                           1U,
                           horizontal,
                           vertical,
                           transform,
                           fit,
                           color(text_color, intensity)};
}

[[nodiscard]] std::size_t state_index(WidgetVisualState state) noexcept {
    switch (state) {
    case WidgetVisualState::hovered:
        return 1U;
    case WidgetVisualState::pressed:
        return 2U;
    case WidgetVisualState::normal:
    case WidgetVisualState::focused:
    case WidgetVisualState::disabled:
        return 0U;
    }
    return 0U;
}

void append_text_button(ui::DrawList& list,
                        DrawRect bounds,
                        std::string_view label,
                        WidgetVisualState state,
                        ColorRgba8 tint = white,
                        ColorRgba8 label_color = button_text) {
    constexpr double slice_width{31.0};
    constexpr double seam{1.0};
    const auto middle_width = bounds.width - slice_width * 2.0;
    const auto index = state_index(state);
    const auto intensity =
        state == WidgetVisualState::disabled ? std::uint16_t{700U} : std::uint16_t{1'000U};
    const std::array destinations{
        DrawRect{bounds.x, bounds.y, slice_width + seam, bounds.height},
        DrawRect{bounds.x + slice_width, bounds.y, middle_width + seam, bounds.height},
        DrawRect{
            bounds.x + slice_width + middle_width, bounds.y, slice_width + seam, bounds.height},
    };
    for (std::size_t slice = 0U; slice < destinations.size(); ++slice) {
        list.push(sprite(button_assets[index][slice],
                         destinations[slice],
                         DrawSpace::design_pixels,
                         TextureFilter::nearest,
                         UiTextureAnchor::top_left,
                         source_scale,
                         color(tint, intensity)));
    }
    const auto pressed_offset = state == WidgetVisualState::pressed ? 2.0 : 0.0;
    list.push(text(label,
                   main_menu_assets::button_font,
                   DrawRect{bounds.x + 14.0,
                            bounds.y + 4.0 + pressed_offset,
                            bounds.width - 28.0,
                            bounds.height - 8.0},
                   30.0,
                   label_color,
                   HorizontalTextAlignment::center,
                   VerticalTextAlignment::retail_center,
                   TextTransform::uppercase,
                   TextFit::shrink_to_fit,
                   intensity));
}

void append_back(ui::DrawList& list, WidgetVisualState state) {
    const auto highlighted =
        state == WidgetVisualState::hovered || state == WidgetVisualState::pressed;
    const auto intensity = highlighted ? std::uint16_t{1'000U} : std::uint16_t{700U};
    list.push(sprite(quick_play_assets::back_icon,
                     DrawRect{56.5, 544.5, 25.0, 25.0},
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale,
                     color(white, intensity)));
    list.push(text("BACK",
                   main_menu_assets::button_font,
                   DrawRect{84.0, 541.0, 48.0, 32.0},
                   24.0,
                   navigation_text,
                   HorizontalTextAlignment::left,
                   VerticalTextAlignment::retail_center,
                   TextTransform::uppercase,
                   TextFit::shrink_to_fit,
                   intensity));
}

[[nodiscard]] DrawRect background_cover(ui::PixelExtent window) noexcept {
    constexpr double loaded_width{768.0};
    constexpr double loaded_height{576.0};
    const auto width = static_cast<double>(window.width);
    const auto height = static_cast<double>(window.height);
    const auto cover_scale = std::max(width / loaded_width, height / loaded_height);
    const auto covered_width = loaded_width * cover_scale;
    const auto covered_height = loaded_height * cover_scale;
    return DrawRect{(width - covered_width) * 0.5,
                    (height - covered_height) * 0.5,
                    covered_width,
                    covered_height};
}

void append_background(ui::DrawList& list, ui::PixelExtent window, std::uint16_t opacity) {
    list.push(sprite(quick_play_assets::background,
                     background_cover(window),
                     DrawSpace::window_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::top_left,
                     source_scale,
                     color(white, 1'000U, opacity),
                     SpriteSizing::cover));
}

void validate_context(const QuickPlayPresentationContext& context) {
    if (!context.window.is_valid()) {
        throw std::invalid_argument{"Quick Play presentation requires a valid window extent"};
    }
    if (context.background_opacity_per_mille > 1'000U) {
        throw std::invalid_argument{"Quick Play background opacity exceeds one"};
    }
}

[[nodiscard]] std::string search_status(QuickPlaySearchState state) {
    switch (state) {
    case QuickPlaySearchState::searching:
        return "SEARCHING_FOR_PUBLIC_MATCH";
    case QuickPlaySearchState::failed:
        return "SERVER_SEARCH_FAILED";
    case QuickPlaySearchState::unavailable:
        return "NETWORK_UNAVAILABLE";
    case QuickPlaySearchState::idle:
    case QuickPlaySearchState::complete:
        return {};
    }
    return {};
}

void append_playlist_rows(ui::DrawList& list, const QuickPlayMenuModel& menu) {
    const auto rows = menu.rows();
    const auto visible = std::min(rows.size(), QuickPlayMenuModel::visible_playlist_rows);
    for (std::size_t row_index = 0U; row_index < visible; ++row_index) {
        const auto y = 175.0 + static_cast<double>(row_index) * 24.0;
        const auto& row = rows[row_index];
        list.push(sprite(quick_play_assets::white_pixel,
                         DrawRect{66.0, y, 320.0, 24.0},
                         DrawSpace::design_pixels,
                         TextureFilter::nearest,
                         UiTextureAnchor::top_left,
                         1.0,
                         color((row_index % 2U) == 0U ? row_grey : row_dark_grey)));
        if (menu.hovered_row() == row_index) {
            list.push(sprite(quick_play_assets::white_pixel,
                             DrawRect{66.0, y, 320.0, 24.0},
                             DrawSpace::design_pixels,
                             TextureFilter::nearest,
                             UiTextureAnchor::top_left,
                             1.0,
                             color(row_hover)));
        }
        if (menu.selected_row() == row_index) {
            list.push(sprite(quick_play_assets::selection_line,
                             DrawRect{66.0, y, 320.0, 24.0},
                             DrawSpace::design_pixels,
                             TextureFilter::linear,
                             UiTextureAnchor::top_left,
                             source_scale));
            list.push(sprite(quick_play_assets::selection_glow,
                             DrawRect{58.0, y - 3.6, 336.0, 31.2},
                             DrawSpace::design_pixels,
                             TextureFilter::linear,
                             UiTextureAnchor::top_left,
                             source_scale));
        }
        list.push(text(row.definition->name_key,
                       "fonts/A750-Sans-Medium.ttf",
                       DrawRect{80.0, y, 172.0, 24.0},
                       12.0));
        if (row.displayed_ping_milliseconds.has_value()) {
            list.push(text(std::to_string(*row.displayed_ping_milliseconds),
                           "fonts/A750-Sans-Medium.ttf",
                           DrawRect{264.0, y, 52.0, 24.0},
                           11.0));
        }
        if (!row.displayed_players.empty()) {
            list.push(text(row.displayed_players,
                           "fonts/A750-Sans-Medium.ttf",
                           DrawRect{324.0, y, 52.0, 24.0},
                           11.0));
        }
    }
}

void append_category(ui::DrawList& list,
                     std::string_view key,
                     double y,
                     std::span<const std::string_view> values,
                     bool expanded) {
    list.push(sprite(quick_play_assets::white_pixel,
                     DrawRect{411.0, y, 320.0, 26.0},
                     DrawSpace::design_pixels,
                     TextureFilter::nearest,
                     UiTextureAnchor::top_left,
                     1.0,
                     color(category_green)));
    list.push(text(key,
                   "fonts/A750-Sans-Medium.ttf",
                   DrawRect{425.0, y, 292.0, 26.0},
                   12.0));
    if (!expanded) {
        return;
    }
    const auto count = std::min<std::size_t>(values.size(), 7U);
    for (std::size_t index = 0U; index < count; ++index) {
        const auto row_y = y + 26.0 * static_cast<double>(index + 1U);
        list.push(sprite(quick_play_assets::white_pixel,
                         DrawRect{411.0, row_y, 320.0, 26.0},
                         DrawSpace::design_pixels,
                         TextureFilter::nearest,
                         UiTextureAnchor::top_left,
                         1.0,
                         color((index % 2U) == 0U ? row_grey : row_dark_grey)));
        list.push(text(values[index],
                       "fonts/A750-Sans-Medium.ttf",
                       DrawRect{425.0, row_y, 292.0, 26.0},
                       11.0));
    }
}

void append_preview(ui::DrawList& list, const QuickPlayPlaylistRow& row) {
    list.push(text(row.definition->name_key,
                   "fonts/Edo.ttf",
                   DrawRect{425.0, 105.0, 292.0, 40.0},
                   22.0,
                   menu_text,
                   HorizontalTextAlignment::center));
    list.push(sprite(quick_play_mode_image_asset(*row.definition),
                     DrawRect{411.0, 155.0, 320.0, 66.0},
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));

    if (!row.owned) {
        list.push(text("DLC_PACK_3_DESCRIPTION_1",
                       "fonts/A750-Sans-Medium.ttf",
                       DrawRect{423.0, 245.0, 296.0, 140.0},
                       12.0,
                       white,
                       HorizontalTextAlignment::center,
                       VerticalTextAlignment::top));
        return;
    }

    if (row.definition->rules.empty()) {
        append_category(list, "MAPS", 231.0, row.definition->maps, true);
    } else {
        // set_display_data(... collapse_rows=True) collapses both categories
        // whenever the selected playlist has both map and rule data.
        append_category(list, "MAPS", 231.0, row.definition->maps, false);
        list.push(sprite(quick_play_assets::white_pixel,
                         DrawRect{411.0, 257.0, 320.0, 26.0},
                         DrawSpace::design_pixels,
                         TextureFilter::nearest,
                         UiTextureAnchor::top_left,
                         1.0,
                         color(category_green)));
        list.push(text("GAME_RULES",
                       "fonts/A750-Sans-Medium.ttf",
                       DrawRect{425.0, 257.0, 292.0, 26.0},
                       12.0));
    }
}

} // namespace

ui::DrawList QuickPlayPresentation::build(const QuickPlayMenuModel& menu,
                                          const QuickPlayPresentationContext& context) const {
    validate_context(context);
    auto layer = build_layer(menu);
    ui::DrawList list;
    list.reserve(layer.size() + 1U);
    append_background(list, context.window, context.background_opacity_per_mille);
    for (const auto& command : layer.commands()) {
        list.push(command);
    }
    return list;
}

ui::DrawList QuickPlayPresentation::build_layer(const QuickPlayMenuModel& menu) const {
    ui::DrawList list;
    list.reserve(90U);
    list.push(sprite(quick_play_assets::large_frame,
                     DrawRect{25.0, 5.5, 750.0, 589.0},
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
    list.push(sprite(quick_play_assets::panel_frame,
                     DrawRect{56.0, 95.0, 340.0, 354.0},
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
    list.push(sprite(quick_play_assets::panel_frame,
                     DrawRect{401.0, 95.0, 340.0, 354.0},
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
    list.push(sprite(quick_play_assets::panel_frame,
                     DrawRect{56.0, 452.0, 340.0, 54.0},
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
    list.push(sprite(quick_play_assets::panel_frame,
                     DrawRect{401.0, 452.0, 340.0, 54.0},
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
    list.push(text("PUBLIC_MATCH",
                   "fonts/Spades.ttf",
                   DrawRect{120.0, 25.0, 560.0, 50.0},
                   42.0,
                   menu_text,
                   HorizontalTextAlignment::left,
                   VerticalTextAlignment::retail_center,
                   TextTransform::uppercase));
    list.push(sprite(quick_play_assets::subtitle_frame,
                     DrawRect{66.0, 105.0, 320.0, 40.0},
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
    list.push(sprite(quick_play_assets::subtitle_frame,
                     DrawRect{411.0, 105.0, 320.0, 40.0},
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
    list.push(text("PLAYLISTS",
                   "fonts/Edo.ttf",
                   DrawRect{80.0, 105.0, 292.0, 40.0},
                   22.0,
                   menu_text,
                   HorizontalTextAlignment::center));
    list.push(text("MODE", "fonts/Edo.ttf", DrawRect{79.0, 135.0, 185.0, 30.0}, 13.0));
    list.push(text("PING", "fonts/Edo.ttf", DrawRect{264.0, 135.0, 60.0, 30.0}, 13.0));
    list.push(text("PLAYERS", "fonts/Edo.ttf", DrawRect{324.0, 135.0, 70.0, 30.0}, 13.0));

    append_playlist_rows(list, menu);
    append_preview(list, menu.selected());

    const auto status = menu.search_state() == QuickPlaySearchState::complete &&
                                menu.selected().chosen_server() == nullptr
                            ? std::string{"No servers available for this playlist."}
                            : search_status(menu.search_state());
    if (!status.empty()) {
        list.push(text(status,
                       "fonts/A750-Sans-Medium.ttf",
                       DrawRect{66.0, 438.0, 320.0, 18.0},
                       10.0,
                       menu_text,
                       HorizontalTextAlignment::center));
    }
    append_text_button(list,
                       DrawRect{60.0, 456.0, 332.0, 50.0},
                       "REFRESH",
                       menu.visual_state(QuickPlayFixedControl::refresh));
    if (menu.primary_kind() == QuickPlayPrimaryKind::buy) {
        append_text_button(list,
                           DrawRect{405.0, 456.0, 332.0, 50.0},
                           "BUY_NOW",
                           menu.visual_state(QuickPlayFixedControl::primary),
                           ColorRgba8{51U, 255U, 51U, 255U},
                           white);
    } else if (menu.primary_kind() == QuickPlayPrimaryKind::start) {
        append_text_button(list,
                           DrawRect{405.0, 456.0, 332.0, 50.0},
                           "START_GAME",
                           menu.visual_state(QuickPlayFixedControl::primary));
    }
    append_back(list, menu.visual_state(QuickPlayFixedControl::back));
    return list;
}

std::string_view
quick_play_mode_image_asset(const QuickPlayPlaylistDefinition& definition) noexcept {
    if (definition.name_key == "RANDOM") {
        return quick_play_assets::random_mode_image;
    }
    if (definition.modes.size() != 1U) {
        return quick_play_assets::multiple_mode_image;
    }
    const auto mode = definition.modes.front();
    if (mode == "zom") {
        return "png/ui/game_loading/letterbox_images/letterbox_zombie.png";
    }
    if (mode == "tdm") {
        return "png/ui/game_loading/letterbox_images/letterbox_tdm.png";
    }
    if (mode == "dia") {
        return "png/ui/game_loading/letterbox_images/letterbox_diamondmine.png";
    }
    if (mode == "mh") {
        return "png/ui/game_loading/letterbox_images/letterbox_multihill.png";
    }
    if (mode == "oc") {
        return "png/ui/game_loading/letterbox_images/letterbox_occupation.png";
    }
    if (mode == "dem") {
        return "png/ui/game_loading/letterbox_images/letterbox_demolition.png";
    }
    if (mode == "ctf") {
        return definition.classic
                   ? "png/ui/game_loading/letterbox_images/letterbox_classic.png"
                   : "png/ui/game_loading/letterbox_images/letterbox_ctf.png";
    }
    if (mode == "tc") {
        return "png/ui/game_loading/letterbox_images/letterbox_territory.png";
    }
    if (mode == "vip") {
        return "png/ui/game_loading/letterbox_images/letterbox_vip_gangster.png";
    }
    return quick_play_assets::multiple_mode_image;
}

namespace quick_play_assets {

std::span<const MainMenuAsset> required() noexcept {
    constexpr auto texture = MainMenuAssetKind::texture;
    constexpr auto font = MainMenuAssetKind::font;
    constexpr auto sound = MainMenuAssetKind::sound;
    constexpr auto linear = TextureSampling::linear;
    constexpr auto nearest = TextureSampling::nearest;
    constexpr auto no_sampling = TextureSampling::not_applicable;
    constexpr auto top_left = TextureAnchor::top_left;
    constexpr auto center = TextureAnchor::center;
    constexpr auto no_anchor = TextureAnchor::not_applicable;

    static constexpr std::array assets{
        MainMenuAsset{background, texture, linear, top_left, source_scale},
        MainMenuAsset{large_frame, texture, linear, center, global_scale},
        MainMenuAsset{panel_frame, texture, linear, center, global_scale},
        MainMenuAsset{subtitle_frame, texture, linear, center, global_scale},
        MainMenuAsset{white_pixel, texture, nearest, top_left, 1.0},
        MainMenuAsset{selection_line, texture, linear, top_left, source_scale},
        MainMenuAsset{selection_glow, texture, linear, top_left, source_scale},
        MainMenuAsset{back_icon, texture, linear, center, global_scale},
        MainMenuAsset{"png/ui/common_elements/buttons/button_large_left.png",
                      texture,
                      nearest,
                      top_left,
                      source_scale},
        MainMenuAsset{"png/ui/common_elements/buttons/button_large_mid.png",
                      texture,
                      nearest,
                      top_left,
                      source_scale},
        MainMenuAsset{"png/ui/common_elements/buttons/button_large_right.png",
                      texture,
                      nearest,
                      top_left,
                      source_scale},
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
        MainMenuAsset{random_mode_image, texture, linear, center, global_scale},
        MainMenuAsset{multiple_mode_image, texture, linear, center, global_scale},
        MainMenuAsset{"png/ui/game_loading/letterbox_images/letterbox_zombie.png",
                      texture,
                      linear,
                      center,
                      global_scale},
        MainMenuAsset{"png/ui/game_loading/letterbox_images/letterbox_tdm.png",
                      texture,
                      linear,
                      center,
                      global_scale},
        MainMenuAsset{"png/ui/game_loading/letterbox_images/letterbox_diamondmine.png",
                      texture,
                      linear,
                      center,
                      global_scale},
        MainMenuAsset{"png/ui/game_loading/letterbox_images/letterbox_multihill.png",
                      texture,
                      linear,
                      center,
                      global_scale},
        MainMenuAsset{"png/ui/game_loading/letterbox_images/letterbox_occupation.png",
                      texture,
                      linear,
                      center,
                      global_scale},
        MainMenuAsset{"png/ui/game_loading/letterbox_images/letterbox_demolition.png",
                      texture,
                      linear,
                      center,
                      global_scale},
        MainMenuAsset{"png/ui/game_loading/letterbox_images/letterbox_classic.png",
                      texture,
                      linear,
                      center,
                      global_scale},
        MainMenuAsset{"png/ui/game_loading/letterbox_images/letterbox_ctf.png",
                      texture,
                      linear,
                      center,
                      global_scale},
        MainMenuAsset{"png/ui/game_loading/letterbox_images/letterbox_territory.png",
                      texture,
                      linear,
                      center,
                      global_scale},
        MainMenuAsset{"png/ui/game_loading/letterbox_images/letterbox_vip_gangster.png",
                      texture,
                      linear,
                      center,
                      global_scale},
        MainMenuAsset{"fonts/Spades.ttf", font, no_sampling, no_anchor, 1.0},
        MainMenuAsset{"fonts/Edo.ttf", font, no_sampling, no_anchor, 1.0},
        MainMenuAsset{"fonts/A750-Sans-Medium.ttf", font, no_sampling, no_anchor, 1.0},
        MainMenuAsset{main_menu_assets::button_font, font, no_sampling, no_anchor, 1.0},
        MainMenuAsset{main_menu_assets::confirmation_sound, sound, no_sampling, no_anchor, 1.0},
        MainMenuAsset{main_menu_assets::back_sound, sound, no_sampling, no_anchor, 1.0},
        MainMenuAsset{"sounds/menu_scrollA.ogg", sound, no_sampling, no_anchor, 1.0},
        MainMenuAsset{"sounds/menu_buyA.ogg", sound, no_sampling, no_anchor, 1.0},
    };
    return assets;
}

} // namespace quick_play_assets
} // namespace battlespades::frontend
