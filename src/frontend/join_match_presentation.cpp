#include "battlespades/frontend/join_match_presentation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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

constexpr double design_height{600.0};
constexpr double source_scale{0.6};
constexpr double global_scale{0.64};
constexpr double subpixels{static_cast<double>(MainMenuModel::subpixels_per_pixel)};
constexpr ColorRgba8 white{255U, 255U, 255U, 255U};
constexpr ColorRgba8 button_text{20U, 20U, 20U, 255U};
constexpr ColorRgba8 menu_text{244U, 236U, 187U, 255U};
constexpr ColorRgba8 navigation_text{232U, 207U, 78U, 255U};
constexpr ColorRgba8 selected_header{247U, 210U, 51U, 255U};
constexpr std::string_view white_pixel{"png/high/white.png"};

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

[[nodiscard]] DrawRect to_design_rect(const ui::Rect& rect) noexcept {
    return DrawRect{static_cast<double>(rect.x) / subpixels,
                    static_cast<double>(rect.y) / subpixels,
                    static_cast<double>(rect.width) / subpixels,
                    static_cast<double>(rect.height) / subpixels};
}

[[nodiscard]] DrawRect bottom_left(double x, double y, double width, double height) noexcept {
    return DrawRect{x, design_height - y - height, width, height};
}

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

[[nodiscard]] TextDrawCommand
text(std::string_view value,
     std::string_view font,
     DrawRect destination,
     double size,
     ColorRgba8 text_color = menu_text,
     HorizontalTextAlignment horizontal = HorizontalTextAlignment::left,
     VerticalTextAlignment vertical = VerticalTextAlignment::retail_center,
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

[[nodiscard]] bool contains(DrawRect bounds, std::optional<ui::Point> point) noexcept {
    if (!point.has_value())
        return false;
    const auto x = static_cast<double>(point->x);
    const auto y = static_cast<double>(point->y);
    return x >= bounds.x && x < bounds.x + bounds.width && y >= bounds.y &&
           y < bounds.y + bounds.height;
}

[[nodiscard]] WidgetVisualState pointer_state(DrawRect bounds,
                                              const ServerBrowserPresentationContext& context,
                                              bool enabled = true) noexcept {
    if (!enabled)
        return WidgetVisualState::disabled;
    if (!contains(bounds, context.pointer))
        return WidgetVisualState::normal;
    return context.pointer_down ? WidgetVisualState::pressed : WidgetVisualState::hovered;
}

void append_text_button(ui::DrawList& list,
                        DrawRect bounds,
                        std::string_view label,
                        WidgetVisualState state = WidgetVisualState::normal,
                        double requested_size = 36.0) {
    constexpr double slice_width{36.0};
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
                         color(white, intensity)));
    }
    const auto pressed_offset = state == WidgetVisualState::pressed ? 2.0 : 0.0;
    auto command = text(label,
                        main_menu_assets::button_font,
                        DrawRect{bounds.x + 14.0,
                                 bounds.y + 4.0 + pressed_offset,
                                 bounds.width - 28.0,
                                 bounds.height - 8.0},
                        requested_size,
                        button_text,
                        HorizontalTextAlignment::center,
                        VerticalTextAlignment::retail_center,
                        TextTransform::uppercase,
                        TextFit::shrink_to_fit,
                        intensity);
    command.line_spacing_pixels = 2.0;
    command.maximum_lines = 2U;
    list.push(std::move(command));
}

void append_back_item(ui::DrawList& list,
                      double bar_x,
                      double bar_bottom,
                      double bar_height,
                      WidgetVisualState state) {
    constexpr double icon_width{25.0};
    constexpr double icon_height{25.0};
    constexpr double icon_anchor{12.0};
    constexpr double padding{5.0};
    const auto highlighted =
        state == WidgetVisualState::hovered || state == WidgetVisualState::pressed;
    const auto intensity = highlighted ? std::uint16_t{1'000U} : std::uint16_t{700U};
    const auto icon_center_x = bar_x + padding * 0.5 + icon_width * 0.5;
    const auto icon_center_y = bar_bottom + bar_height * 0.5;
    list.push(
        text("BACK",
             main_menu_assets::button_font,
             bottom_left(
                 bar_x + padding * 0.5 + icon_width + padding * 0.5, bar_bottom, 78.0, bar_height),
             24.0,
             navigation_text,
             HorizontalTextAlignment::left,
             VerticalTextAlignment::retail_center,
             TextTransform::uppercase,
             TextFit::shrink_to_fit,
             intensity));
    list.push(sprite(
        join_match_assets::back_icon,
        bottom_left(
            icon_center_x - icon_anchor, icon_center_y - icon_anchor, icon_width, icon_height),
        DrawSpace::design_pixels,
        TextureFilter::linear,
        UiTextureAnchor::center,
        global_scale,
        color(white, intensity)));
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
    list.push(sprite(main_menu_assets::background,
                     background_cover(window),
                     DrawSpace::window_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::top_left,
                     source_scale,
                     color(white, 1'000U, opacity),
                     SpriteSizing::cover));
}

void validate_context(ui::PixelExtent window, std::uint16_t opacity) {
    if (!window.is_valid()) {
        throw std::invalid_argument{"join-match presentation requires a valid window extent"};
    }
    if (opacity > 1'000U) {
        throw std::invalid_argument{"join-match background opacity exceeds one"};
    }
}

[[nodiscard]] std::string players_label(const ServerBrowserEntry& server) {
    return std::to_string(server.players) + '/' + std::to_string(server.maximum_players);
}

[[nodiscard]] std::size_t
visible_row_capacity(const ServerBrowserPresentationContext& context) noexcept {
    return std::max<std::size_t>(1U, context.maximum_visible_rows);
}

[[nodiscard]] std::size_t
maximum_first_visible_row(const ServerBrowserModel& browser,
                          const ServerBrowserPresentationContext& context) noexcept {
    const auto capacity = visible_row_capacity(context);
    return browser.visible_indices().size() > capacity ? browser.visible_indices().size() - capacity
                                                       : 0U;
}

void append_server_scrollbar(ui::DrawList& list,
                             const ServerBrowserModel& browser,
                             const ServerBrowserPresentationContext& context) {
    constexpr double button_x{512.0};
    constexpr double up_y{163.0};
    constexpr double down_y{445.0};
    constexpr double button_size{20.0};
    constexpr double thumb_x{516.0};
    constexpr double thumb_top{182.0};
    constexpr double thumb_width{12.0};
    constexpr double thumb_height{60.0};
    constexpr double thumb_cap_height{6.0};
    constexpr double thumb_travel{204.0};

    const auto maximum = maximum_first_visible_row(browser, context);
    const auto first = std::min(context.first_visible_row, maximum);
    const auto append_scroll_button =
        [&list](double y, std::string_view arrow, bool enabled) {
            const auto intensity = enabled ? std::uint16_t{1'000U} : std::uint16_t{700U};
            const DrawRect bounds{button_x, y, button_size, button_size};
            list.push(sprite("png/ui/common_elements/buttons/button_square.png",
                             bounds,
                             DrawSpace::design_pixels,
                             TextureFilter::nearest,
                             UiTextureAnchor::center,
                             source_scale,
                             color(white, intensity)));
            list.push(sprite(arrow,
                             bounds,
                             DrawSpace::design_pixels,
                             TextureFilter::nearest,
                             UiTextureAnchor::center,
                             1.0,
                             color(white, intensity)));
        };
    append_scroll_button(up_y, join_match_assets::scroll_up, first > 0U);
    append_scroll_button(down_y, join_match_assets::scroll_down, first < maximum);

    if (maximum == 0U) {
        return;
    }
    const auto fraction = static_cast<double>(first) / static_cast<double>(maximum);
    const auto y = thumb_top + thumb_travel * fraction;
    list.push(sprite(join_match_assets::scroll_thumb_top,
                     DrawRect{thumb_x, y, thumb_width, thumb_cap_height},
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::top_left,
                     1.0));
    list.push(sprite(
        join_match_assets::scroll_thumb_middle,
        DrawRect{thumb_x, y + thumb_cap_height, thumb_width, thumb_height - thumb_cap_height * 2.0},
        DrawSpace::design_pixels,
        TextureFilter::linear,
        UiTextureAnchor::top_left,
        1.0));
    list.push(sprite(
        join_match_assets::scroll_thumb_bottom,
        DrawRect{thumb_x, y + thumb_height - thumb_cap_height, thumb_width, thumb_cap_height},
        DrawSpace::design_pixels,
        TextureFilter::linear,
        UiTextureAnchor::top_left,
        1.0));
}

void append_server_table(ui::DrawList& list,
                         const ServerBrowserModel& browser,
                         const ServerBrowserPresentationContext& context) {
    constexpr std::array<std::string_view, 5U> headers{"NAME", "PLAYERS", "MAP", "MODE", "PING"};
    constexpr std::array<double, 5U> widths{103.0, 68.0, 100.0, 132.0, 45.0};
    constexpr double table_x{69.0};
    constexpr double header_y{164.0};
    constexpr double header_height{27.0};
    constexpr double row_height{18.0};
    double x = table_x;
    for (std::size_t index = 0U; index < headers.size(); ++index) {
        const auto active = static_cast<std::size_t>(browser.sort_column()) == index;
        list.push(text(headers[index],
                       "fonts/Edo.ttf",
                       DrawRect{x, header_y, widths[index], header_height},
                       10.0,
                       active ? selected_header : menu_text));
        if (active && !browser.visible_indices().empty()) {
            list.push(sprite(browser.sort_descending() ? join_match_assets::sort_down
                                                       : join_match_assets::sort_up,
                             DrawRect{x + widths[index] - 15.0, header_y + 9.0, 9.0, 7.0},
                             DrawSpace::design_pixels,
                             TextureFilter::linear,
                             UiTextureAnchor::top_left,
                             global_scale));
        }
        x += widths[index];
    }

    const auto visible = browser.visible_indices();
    const auto first =
        std::min(context.first_visible_row, maximum_first_visible_row(browser, context));
    const auto stop = std::min(visible.size(), first + visible_row_capacity(context));
    const auto selected = browser.selected_visible_row();
    std::optional<std::size_t> hovered;
    if (context.pointer.has_value() && context.pointer->x >= 69 && context.pointer->x < 517 &&
        context.pointer->y >= 191 && context.pointer->y < 461) {
        hovered = first + static_cast<std::size_t>((context.pointer->y - 191) / 18);
        if (*hovered >= stop)
            hovered.reset();
    }
    double y = header_y + header_height;
    for (std::size_t row = first; row < stop; ++row) {
        const auto& server = browser.servers()[visible[row]];
        const std::array values{server.name,
                                players_label(server),
                                server.map,
                                server.mode,
                                std::to_string(server.ping_milliseconds)};
        x = table_x;
        const auto highlighted = selected == row || hovered == row;
        const auto highlight_opacity = selected == row ? std::uint16_t{392U} : std::uint16_t{196U};
        for (std::size_t column = 0U; column < values.size(); ++column) {
            const auto background =
                row % 2U == 0U ? ColorRgba8{72U, 68U, 54U, 255U} : ColorRgba8{47U, 45U, 36U, 255U};
            list.push(sprite(white_pixel,
                             DrawRect{x, y, widths[column] - 1.0, row_height},
                             DrawSpace::design_pixels,
                             TextureFilter::nearest,
                             UiTextureAnchor::top_left,
                             1.0,
                             color(background)));
            list.push(text(values[column],
                           "fonts/A750-Sans-Medium.ttf",
                           DrawRect{x, y, widths[column], row_height},
                           10.0));
            x += widths[column];
        }
        if (highlighted) {
            list.push(sprite(white_pixel,
                             DrawRect{table_x, y, 448.0, row_height},
                             DrawSpace::design_pixels,
                             TextureFilter::nearest,
                             UiTextureAnchor::top_left,
                             1.0,
                             color(white, 1'000U, highlight_opacity)));
        }
        if (server.favourite) {
            list.push(sprite(join_match_assets::favourite_star,
                             DrawRect{156.0, y + 3.0, 12.0, 11.0},
                             DrawSpace::design_pixels,
                             TextureFilter::linear,
                             UiTextureAnchor::center,
                             global_scale));
        }
        y += row_height;
    }
    append_server_scrollbar(list, browser, context);
}

} // namespace

ui::DrawList JoinMatchPresentation::build(const JoinMatchMenuModel& menu,
                                          const JoinMatchPresentationContext& context) const {
    validate_context(context.window, context.background_opacity_per_mille);
    auto layer = build_layer(menu);
    ui::DrawList list;
    list.reserve(complete_command_count);
    append_background(list, context.window, context.background_opacity_per_mille);
    for (const auto& command : layer.commands()) {
        std::visit([&list](const auto& value) { list.push(value); }, command);
    }
    return list;
}

ui::DrawList JoinMatchPresentation::build_layer(const JoinMatchMenuModel& menu) const {
    ui::DrawList list;
    list.reserve(layer_command_count);
    // Loaded sizes are integer-truncated before Python 2 center anchors apply.
    list.push(sprite(join_match_assets::three_button_frame,
                     bottom_left(231.0, 217.5, 339.0, 253.0),
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     source_scale));
    list.push(sprite(join_match_assets::small_navigation_frame,
                     bottom_left(230.0, 12.0, 340.0, 68.0),
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     source_scale));

    const auto controls = menu.controls();
    for (std::size_t index = 0U; index < 3U; ++index) {
        append_text_button(list,
                           to_design_rect(controls[index].widget.bounds),
                           controls[index].localization_key,
                           menu.visual_state(controls[index].widget.id));
    }
    append_back_item(list, 248.0, 32.0, 26.0, menu.visual_state(controls[3U].widget.id));

    list.push(sprite(main_menu_assets::splash,
                     bottom_left(265.75, 413.25, 293.25, 193.5),
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     source_scale));
    return list;
}

ui::DrawList DirectConnectPresentation::build(const DirectConnectMenuModel& menu,
                                              const JoinMatchPresentationContext& context) const {
    validate_context(context.window, context.background_opacity_per_mille);
    auto layer = build_layer(menu);
    ui::DrawList list;
    list.reserve(layer.size() + 1U);
    append_background(list, context.window, context.background_opacity_per_mille);
    for (const auto& command : layer.commands()) {
        std::visit([&list](const auto& value) { list.push(value); }, command);
    }
    return list;
}

ui::DrawList DirectConnectPresentation::build_layer(const DirectConnectMenuModel& menu) const {
    ui::DrawList list;
    list.reserve(22U);
    list.push(sprite(join_match_assets::three_button_frame,
                     bottom_left(231.0, 217.5, 339.0, 253.0),
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     source_scale));
    list.push(sprite(join_match_assets::small_navigation_frame,
                     bottom_left(230.0, 12.0, 340.0, 68.0),
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     source_scale));
    list.push(sprite(white_pixel,
                     DrawRect{267.0, 175.0, 270.0, 100.0},
                     DrawSpace::design_pixels,
                     TextureFilter::nearest,
                     UiTextureAnchor::top_left,
                     1.0,
                     color(menu.input_hovered() || menu.input_focused()
                               ? ColorRgba8{92U, 88U, 72U, 255U}
                               : ColorRgba8{65U, 62U, 52U, 255U})));
    const auto endpoint = menu.endpoint().empty() ? std::string_view{"IP:PORT"} : menu.endpoint();
    list.push(text(endpoint,
                   "fonts/A750-Sans-Medium.ttf",
                   DrawRect{274.0, 182.0, 256.0, 86.0},
                   22.0,
                   menu.endpoint().empty() ? ColorRgba8{174U, 166U, 133U, 255U} : menu_text,
                   HorizontalTextAlignment::left,
                   VerticalTextAlignment::retail_center));
    if (menu.input_focused()) {
        const auto caret_x =
            std::min(526.0, 278.0 + static_cast<double>(menu.endpoint().size()) * 11.0);
        list.push(sprite(white_pixel,
                         DrawRect{caret_x, 207.0, 2.0, 36.0},
                         DrawSpace::design_pixels,
                         TextureFilter::nearest,
                         UiTextureAnchor::top_left,
                         1.0,
                         color(menu_text)));
    }
    append_text_button(list, DrawRect{269.0, 292.0, 262.0, 58.0}, "CONNECT", menu.connect_state());
    append_text_button(
        list, DrawRect{269.0, 355.0, 262.0, 58.0}, "ADD FAVORITE", menu.favourite_state(), 22.0);
    append_back_item(list, 248.0, 32.0, 26.0, menu.back_state());
    if (!menu.error().empty()) {
        auto error = text(menu.error(),
                          "fonts/A750-Sans-Medium.ttf",
                          DrawRect{250.0, 360.0, 300.0, 45.0},
                          13.0,
                          ColorRgba8{255U, 130U, 100U, 255U},
                          HorizontalTextAlignment::center,
                          VerticalTextAlignment::top);
        error.maximum_lines = 2U;
        error.line_spacing_pixels = 2.0;
        list.push(std::move(error));
    }
    list.push(sprite(main_menu_assets::splash,
                     bottom_left(265.75, 413.25, 293.25, 193.5),
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     source_scale));
    return list;
}

ui::DrawList
ServerBrowserPresentation::build(const ServerBrowserModel& browser,
                                 const ServerBrowserPresentationContext& context) const {
    validate_context(context.window, context.background_opacity_per_mille);
    auto layer = build_layer(browser, context);
    ui::DrawList list;
    list.reserve(layer.size() + 1U);
    append_background(list, context.window, context.background_opacity_per_mille);
    for (const auto& command : layer.commands()) {
        std::visit([&list](const auto& value) { list.push(value); }, command);
    }
    return list;
}

ui::DrawList
ServerBrowserPresentation::build_layer(const ServerBrowserModel& browser,
                                       const ServerBrowserPresentationContext& context) const {
    if (context.maximum_visible_rows == 0U) {
        throw std::invalid_argument{"server browser must expose at least one visible row"};
    }
    ui::DrawList list;
    list.reserve(60U + context.maximum_visible_rows * 6U);
    list.push(sprite("png/ui/common_elements/frames/ui_frame_large.png",
                     bottom_left(25.0, 6.0, 750.0, 589.0),
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
    list.push(sprite(join_match_assets::server_content_frame,
                     bottom_left(31.0, 6.0, 739.0, 589.0),
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
    list.push(text("PLAY_ONLINE",
                   main_menu_assets::button_font,
                   DrawRect{175.0, 14.0, 450.0, 55.0},
                   46.0,
                   menu_text,
                   HorizontalTextAlignment::center,
                   VerticalTextAlignment::retail_center,
                   TextTransform::uppercase));
    append_back_item(
        list, 54.0, 27.0, 32.0, pointer_state(DrawRect{45.0, 532.0, 135.0, 45.0}, context));

    list.push(text("NETWORK", "fonts/Edo.ttf", DrawRect{71.0, 93.0, 80.0, 25.0}, 14.0));
    list.push(text(localization_key(browser.source()),
                   main_menu_assets::button_font,
                   DrawRect{165.0, 90.0, 110.0, 28.0},
                   19.0,
                   menu_text,
                   HorizontalTextAlignment::center,
                   VerticalTextAlignment::retail_center,
                   TextTransform::uppercase));
    constexpr std::array arrow_centers{154.0, 287.0};
    constexpr std::array<std::string_view, 2U> arrow_assets{
        "png/ui/common_elements/scroll_bar/scroll_bar_arrow_left.png",
        "png/ui/common_elements/scroll_bar/scroll_bar_arrow_right.png"};
    for (std::size_t index = 0U; index < arrow_centers.size(); ++index) {
        const DrawRect bounds{arrow_centers[index] - 10.0, 96.0, 20.0, 20.0};
        const auto state =
            pointer_state(DrawRect{index == 0U ? 140.0 : 273.0, 88.0, 28.0, 36.0}, context);
        const auto button_asset = state == WidgetVisualState::pressed
                                      ? "png/ui/common_elements/buttons/button_square_press.png"
                                  : state == WidgetVisualState::hovered
                                      ? "png/ui/common_elements/buttons/button_square_hover.png"
                                      : "png/ui/common_elements/buttons/button_square.png";
        list.push(sprite(button_asset,
                         bounds,
                         DrawSpace::design_pixels,
                         TextureFilter::nearest,
                         UiTextureAnchor::center,
                         source_scale));
        list.push(sprite(arrow_assets[index],
                         bounds,
                         DrawSpace::design_pixels,
                         TextureFilter::nearest,
                         UiTextureAnchor::center,
                         1.0));
    }

    list.push(text("EMPTY_SERVERS",
                   "fonts/Edo.ttf",
                   DrawRect{400.0, 92.0, 119.0, 28.0},
                   14.0,
                   menu_text,
                   HorizontalTextAlignment::right));
    list.push(text("FULL_SERVERS",
                   "fonts/Edo.ttf",
                   DrawRect{577.0, 92.0, 119.0, 28.0},
                   14.0,
                   menu_text,
                   HorizontalTextAlignment::right));
    if (browser.show_empty_servers()) {
        list.push(sprite("png/ui/common_elements/tick.png",
                         DrawRect{540.0, 91.0, 23.0, 27.0},
                         DrawSpace::design_pixels,
                         TextureFilter::linear,
                         UiTextureAnchor::top_left,
                         global_scale));
    }
    if (browser.show_full_servers()) {
        list.push(sprite("png/ui/common_elements/tick.png",
                         DrawRect{717.0, 91.0, 23.0, 27.0},
                         DrawSpace::design_pixels,
                         TextureFilter::linear,
                         UiTextureAnchor::top_left,
                         global_scale));
    }

    if (browser.region_tabs_visible()) {
        constexpr std::array regions{ServerBrowserRegion::us_west,
                                     ServerBrowserRegion::us_east,
                                     ServerBrowserRegion::europe,
                                     ServerBrowserRegion::australia};
        double center_x = 116.0;
        for (const auto region : regions) {
            const auto selected = browser.region() == region;
            if (selected) {
                list.push(sprite(join_match_assets::server_tab_frame,
                                 bottom_left(center_x - 57.0, 442.0, 115.0, 35.0),
                                 DrawSpace::design_pixels,
                                 TextureFilter::linear,
                                 UiTextureAnchor::center,
                                 global_scale));
            }
            list.push(text(localization_key(region),
                           "fonts/Edo.ttf",
                           DrawRect{center_x - 55.0, 125.0, 110.0, 25.0},
                           14.0,
                           selected ? navigation_text : menu_text,
                           HorizontalTextAlignment::center));
            center_x += 120.0;
        }
    }

    append_server_table(list, browser, context);
    if (!context.status_text.empty()) {
        list.push(text(context.status_text,
                       "fonts/A750-Sans-Medium.ttf",
                       DrawRect{76.0, 470.0, 200.0, 30.0},
                       11.0));
    }

    append_text_button(list,
                       DrawRect{288.0, 470.0, 120.0, 30.0},
                       "REFRESH",
                       pointer_state(DrawRect{288.0, 470.0, 120.0, 30.0}, context),
                       22.0);

    const auto* selected = browser.selected();
    append_text_button(
        list,
        DrawRect{417.0, 470.0, 113.0, 30.0},
        "FAVORITE",
        pointer_state(DrawRect{417.0, 470.0, 113.0, 30.0}, context, selected != nullptr),
        20.0);
    if (selected != nullptr) {
        list.push(text(selected->map,
                       "fonts/Edo.ttf",
                       DrawRect{542.0, 132.0, 192.0, 25.0},
                       18.0,
                       menu_text,
                       HorizontalTextAlignment::center));
        const auto preview = context.selected_map_preview_asset.empty()
                                 ? join_match_assets::map_placeholder
                                 : std::string_view{context.selected_map_preview_asset};
        list.push(sprite(preview,
                         DrawRect{542.0, 162.0, 192.0, 192.0},
                         DrawSpace::design_pixels,
                         TextureFilter::linear,
                         UiTextureAnchor::top_left,
                         global_scale));
        list.push(
            text(selected->mode_description.empty() ? selected->mode : selected->mode_description,
                 "fonts/A750-Sans-Medium.ttf",
                 DrawRect{547.0, 359.0, 184.0, 70.0},
                 11.0));
        if (browser.can_connect()) {
            append_text_button(list,
                               DrawRect{543.0, 447.0, 190.0, 54.0},
                               "CONNECT",
                               pointer_state(DrawRect{543.0, 447.0, 190.0, 54.0}, context),
                               26.0);
        }
    }
    return list;
}

} // namespace battlespades::frontend
