#include "battlespades/frontend/ugc_select_presentation.hpp"

#include <algorithm>
#include <array>
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
using ui::TextureFilter;
using ui::VerticalTextAlignment;
using UiTextureAnchor = ui::TextureAnchor;

constexpr double design_height{600.0};
constexpr double source_scale{0.6};
constexpr double navigation_scale{0.64};
constexpr double subpixels{static_cast<double>(MainMenuModel::subpixels_per_pixel)};
constexpr ColorRgba8 white{255U, 255U, 255U, 255U};
constexpr ColorRgba8 button_text{20U, 20U, 20U, 255U};
constexpr ColorRgba8 navigation_text{232U, 207U, 78U, 255U};

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

[[nodiscard]] TextDrawCommand text(std::string_view value,
                                   DrawRect destination,
                                   double size,
                                   ColorRgba8 text_color,
                                   HorizontalTextAlignment horizontal,
                                   std::uint16_t intensity,
                                   std::uint8_t maximum_lines = 1U,
                                   double line_spacing = 0.0) {
    return TextDrawCommand{std::string{value},
                           std::string{main_menu_assets::button_font},
                           destination,
                           DrawSpace::design_pixels,
                           size,
                           line_spacing,
                           maximum_lines,
                           horizontal,
                           VerticalTextAlignment::retail_center,
                           TextTransform::uppercase,
                           TextFit::shrink_to_fit,
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
                        WidgetVisualState state) {
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

    // Pressed retail TextButton art remains fixed while its label drops 2 px.
    const auto pressed_offset = state == WidgetVisualState::pressed ? 2.0 : 0.0;
    list.push(text(label,
                   DrawRect{bounds.x + 14.0,
                            bounds.y + 4.0 + pressed_offset,
                            bounds.width - 28.0,
                            bounds.height - 8.0},
                   36.0,
                   button_text,
                   HorizontalTextAlignment::center,
                   // Retail dims only the sliced background art. TextButton
                   // draws its label afterward using the configured color.
                   1'000U,
                   2U,
                   2.0));
}

void append_back_item(ui::DrawList& list, WidgetVisualState state) {
    constexpr double bar_x{248.0};
    constexpr double bar_bottom{32.0};
    constexpr double bar_height{26.0};
    constexpr double icon_width{25.0};
    constexpr double icon_height{25.0};
    constexpr double icon_anchor{12.0};
    constexpr double padding{5.0};
    const auto highlighted =
        state == WidgetVisualState::hovered || state == WidgetVisualState::pressed;
    const auto intensity = highlighted ? std::uint16_t{1'000U} : std::uint16_t{700U};
    const auto icon_center_x = bar_x + padding * 0.5 + icon_width * 0.5;
    const auto icon_center_y = bar_bottom + bar_height * 0.5;

    // NavigationBar draws text first and then the centered icon.
    list.push(text("BACK",
                   bottom_left(bar_x + padding * 0.5 + icon_width + padding * 0.5,
                               bar_bottom,
                               78.0,
                               bar_height),
                   24.0,
                   navigation_text,
                   HorizontalTextAlignment::left,
                   intensity));
    list.push(sprite(ugc_select_assets::back_icon,
                     bottom_left(icon_center_x - icon_anchor,
                                 icon_center_y - icon_anchor,
                                 icon_width,
                                 icon_height),
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     navigation_scale,
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

void validate_context(const UgcSelectPresentationContext& context) {
    if (!context.window.is_valid()) {
        throw std::invalid_argument{"UGC Select presentation requires a valid window extent"};
    }
    if (context.background_opacity_per_mille > 1'000U) {
        throw std::invalid_argument{"UGC Select background opacity exceeds one"};
    }
}

} // namespace

ui::DrawList UgcSelectPresentation::build(const UgcSelectMenuModel& menu,
                                          const UgcSelectPresentationContext& context) const {
    validate_context(context);
    auto layer = build_layer(menu);
    ui::DrawList list;
    list.reserve(complete_command_count);
    list.push(sprite(main_menu_assets::background,
                     background_cover(context.window),
                     DrawSpace::window_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::top_left,
                     source_scale,
                     color(white, 1'000U, context.background_opacity_per_mille),
                     SpriteSizing::cover));
    for (const auto& command : layer.commands()) {
        std::visit([&list](const auto& value) { list.push(value); }, command);
    }
    return list;
}

ui::DrawList UgcSelectPresentation::build_layer(const UgcSelectMenuModel& menu) const {
    ui::DrawList list;
    list.reserve(layer_command_count);

    // Pyglet truncates each source dimension before applying Python 2 anchors.
    list.push(sprite(ugc_select_assets::three_button_frame,
                     bottom_left(231.0, 217.5, 339.0, 253.0),
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     source_scale));
    list.push(sprite(ugc_select_assets::small_navigation_frame,
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
    append_back_item(list, menu.visual_state(controls[3U].widget.id));

    // Loaded 391x258 splash, then the recovered translate/0.75 scene transform.
    list.push(sprite(main_menu_assets::splash,
                     bottom_left(265.75, 413.25, 293.25, 193.5),
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     source_scale));
    return list;
}

} // namespace battlespades::frontend
