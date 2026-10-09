#include "battlespades/frontend/settings_presentation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
using ui::TextureAnchor;
using ui::TextureFilter;
using ui::VerticalTextAlignment;

constexpr double design_height{600.0};
constexpr double retail_source_scale{0.6};
constexpr double settings_source_scale{0.64};
constexpr double row_height{32.0};
constexpr double category_height{26.0};
constexpr double row_gap{2.0};
constexpr double list_bottom{416.0};

constexpr ColorRgba8 white{255U, 255U, 255U, 255U};
constexpr ColorRgba8 black{0U, 0U, 0U, 255U};
constexpr ColorRgba8 display_text{20U, 20U, 20U, 255U};
constexpr ColorRgba8 cream{244U, 236U, 187U, 255U};
constexpr ColorRgba8 tab_gold{232U, 207U, 78U, 255U};
constexpr ColorRgba8 category_green{59U, 68U, 25U, 255U};
constexpr ColorRgba8 control_grey{83U, 83U, 83U, 255U};
constexpr ColorRgba8 slider_line_grey{117U, 117U, 117U, 255U};
constexpr ColorRgba8 selected_gold{215U, 189U, 83U, 255U};
constexpr ColorRgba8 idle_toggle{46U, 46U, 46U, 255U};
constexpr ColorRgba8 hovered_toggle{130U, 117U, 64U, 255U};
constexpr ColorRgba8 disabled_tint{86U, 86U, 86U, 255U};
constexpr ColorRgba8 tooltip_error{242U, 53U, 53U, 255U};
constexpr ColorRgba8 scrollbar_rail{73U, 63U, 7U, 255U};

constexpr std::array<std::array<std::string_view, 3U>, 3U> text_button_assets{{
    {
        "png/ui/common_elements/buttons/button_large_left.png",
        "png/ui/common_elements/buttons/button_large_mid.png",
        "png/ui/common_elements/buttons/button_large_right.png",
    },
    {
        "png/ui/common_elements/buttons/button_large_hover_left.png",
        "png/ui/common_elements/buttons/button_large_hover_mid.png",
        "png/ui/common_elements/buttons/button_large_hover_right.png",
    },
    {
        "png/ui/common_elements/buttons/button_large_press_left.png",
        "png/ui/common_elements/buttons/button_large_press_mid.png",
        "png/ui/common_elements/buttons/button_large_press_right.png",
    },
}};

constexpr std::array<std::string_view, 3U> square_button_assets{
    "png/ui/common_elements/buttons/button_square.png",
    "png/ui/common_elements/buttons/button_square_hover.png",
    "png/ui/common_elements/buttons/button_square_press.png",
};

constexpr std::string_view arrow_left{
    "png/ui/common_elements/scroll_bar/scroll_bar_arrow_left.png"};
constexpr std::string_view arrow_right{
    "png/ui/common_elements/scroll_bar/scroll_bar_arrow_right.png"};
constexpr std::string_view arrow_up{
    "png/ui/common_elements/scroll_bar/scroll_bar_arrow_up.png"};
constexpr std::string_view arrow_down{
    "png/ui/common_elements/scroll_bar/scroll_bar_arrow_down.png"};
constexpr std::string_view volume_bar{"png/ui/settings/settings_main/volume_bar.png"};
constexpr std::string_view bullet_hole{
    "png/ui/settings/settings_common/bullet_hole.png"};
constexpr std::string_view bullet_hole_disabled{
    "png/ui/settings/settings_common/bullet_hole_disabled.png"};
constexpr std::string_view bullet_slider{
    "png/ui/settings/settings_common/settings_bullet_slider.png"};
constexpr std::string_view favourite_star{"png/ui/favourite_star_settings.png"};
constexpr std::string_view favourite_star_off{"png/ui/favourite_star_settings_off.png"};
constexpr std::string_view collapse_minus{"png/ui/common_elements/collapse_minus.png"};
constexpr std::string_view collapse_plus{"png/ui/common_elements/collapse_plus.png"};
constexpr std::string_view scrollbar_top{
    "png/ui/common_elements/scroll_bar/scroll_bar_top.png"};
constexpr std::string_view scrollbar_mid{
    "png/ui/common_elements/scroll_bar/scroll_bar_mid.png"};
constexpr std::string_view scrollbar_bottom{
    "png/ui/common_elements/scroll_bar/scroll_bar_bottom.png"};

struct VisibleRow final {
    const SettingsPresentationRow* row{};
    DrawRect bounds{};
};

[[nodiscard]] ColorModulation full_color(ColorRgba8 color = white,
                                         std::uint16_t intensity = 1'000U,
                                         std::uint16_t opacity = 1'000U) noexcept {
    return ColorModulation{color, intensity, opacity};
}

[[nodiscard]] SpriteDrawCommand sprite(std::string_view asset,
                                       DrawRect destination,
                                       DrawSpace space = DrawSpace::design_pixels,
                                       TextureFilter sampling = TextureFilter::linear,
                                       TextureAnchor anchor = TextureAnchor::top_left,
                                       double source_scale = retail_source_scale,
                                       ColorModulation modulation = full_color(),
                                       SpriteSizing sizing = SpriteSizing::stretch) {
    return SpriteDrawCommand{
        std::string{asset},
        destination,
        space,
        sampling,
        anchor,
        source_scale,
        sizing,
        modulation,
    };
}

[[nodiscard]] TextDrawCommand text(std::string_view key,
                                   std::string_view font,
                                   DrawRect destination,
                                   double font_size,
                                   ColorRgba8 color,
                                   HorizontalTextAlignment horizontal =
                                       HorizontalTextAlignment::left,
                                   VerticalTextAlignment vertical =
                                       VerticalTextAlignment::retail_center,
                                   TextTransform transform = TextTransform::preserve,
                                   TextFit fit = TextFit::shrink_to_fit,
                                   std::uint8_t maximum_lines = 1U,
                                   std::uint16_t intensity = 1'000U) {
    return TextDrawCommand{
        std::string{key},
        std::string{font},
        destination,
        DrawSpace::design_pixels,
        font_size,
        1.0,
        maximum_lines,
        horizontal,
        vertical,
        transform,
        fit,
        full_color(color, intensity),
    };
}

void append_solid(ui::DrawList& list,
                  DrawRect destination,
                  ColorRgba8 color,
                  std::uint16_t intensity = 1'000U,
                  std::uint16_t opacity = 1'000U) {
    list.push(sprite(settings_presentation_assets::white_pixel,
                     destination,
                     DrawSpace::design_pixels,
                     TextureFilter::nearest,
                     TextureAnchor::top_left,
                     1.0,
                     full_color(color, intensity, opacity)));
}

[[nodiscard]] DrawRect background_cover(ui::PixelExtent window) noexcept {
    constexpr double source_width{768.0};
    constexpr double source_height{576.0};
    const auto width = static_cast<double>(window.width);
    const auto height = static_cast<double>(window.height);
    const auto scale = std::max(width / source_width, height / source_height);
    const auto covered_width = source_width * scale;
    const auto covered_height = source_height * scale;
    return DrawRect{
        (width - covered_width) * 0.5,
        (height - covered_height) * 0.5,
        covered_width,
        covered_height,
    };
}

[[nodiscard]] std::size_t texture_state_index(SettingsPresentationVisualState state) noexcept {
    switch (state) {
    case SettingsPresentationVisualState::hovered:
        return 1U;
    case SettingsPresentationVisualState::pressed:
        return 2U;
    case SettingsPresentationVisualState::normal:
    case SettingsPresentationVisualState::disabled:
        return 0U;
    }
    return 0U;
}

[[nodiscard]] bool disabled(SettingsPresentationVisualState state) noexcept {
    return state == SettingsPresentationVisualState::disabled;
}

void append_text_button(ui::DrawList& list,
                        DrawRect button,
                        const SettingsPresentationButton& state) {
    constexpr double loaded_slice_width{36.0};
    constexpr double loaded_slice_height{58.0};
    constexpr double seam{1.0};
    const auto slice_width = std::floor(loaded_slice_width / loaded_slice_height * button.height);
    const auto middle_width = std::max(0.0, button.width - slice_width * 2.0);
    const auto state_index = texture_state_index(state.visual_state);
    const auto intensity = disabled(state.visual_state) ? std::uint16_t{700U}
                                                       : std::uint16_t{1'000U};
    const std::array destinations{
        DrawRect{button.x, button.y, slice_width + seam, button.height},
        DrawRect{button.x + slice_width, button.y, middle_width + seam, button.height},
        DrawRect{button.x + slice_width + middle_width,
                 button.y,
                 slice_width + seam,
                 button.height},
    };

    for (std::size_t index = 0U; index < destinations.size(); ++index) {
        list.push(sprite(text_button_assets[state_index][index],
                         destinations[index],
                         DrawSpace::design_pixels,
                         TextureFilter::nearest,
                         TextureAnchor::top_left,
                         retail_source_scale,
                         full_color(white, intensity)));
    }

    // TextButton.draw() offsets content by -2 * (height / loaded-mid-height)
    // in retail's bottom-left coordinates. The 97 px source loads at 0.6 to
    // an integer 58 px, so the equivalent top-left offset is positive here.
    const auto pressed_offset = state.visual_state == SettingsPresentationVisualState::pressed
                                    ? 2.0 * button.height / loaded_slice_height
                                    : 0.0;
    const auto large = button.height - 8.0 > 30.0;
    // TextButton: the text box is width - 2 * TEXT_BACKGROUND_SPACING (14)
    // by height - 2 * UI_CONTROL_SPACING (4), and set_text shrinks the face
    // with get_resized_font_and_formatted_text_to_fit_boundaries (the 41 px
    // in-game CANCEL/DONE end at 27 px, not the 36 px face).
    auto label = text(state.label_key,
                      settings_presentation_assets::title_font,
                      DrawRect{button.x + 14.0,
                               button.y + 4.0 + pressed_offset,
                               button.width - 28.0,
                               button.height - 8.0},
                      large ? 36.0 : 18.0,
                      display_text,
                      HorizontalTextAlignment::center,
                      VerticalTextAlignment::retail_center,
                      TextTransform::uppercase,
                      TextFit::shrink_to_fit,
                      0U,
                      intensity);
    label.layout = ui::TextLayout::retail_wrapped_lines;
    label.line_spacing_pixels = 2.0;
    list.push(std::move(label));
}

void append_square_button(ui::DrawList& list,
                          DrawRect bounds,
                          SettingsPresentationVisualState state,
                          bool enabled,
                          std::string_view icon) {
    const auto effective_state = enabled ? state : SettingsPresentationVisualState::disabled;
    const auto index = texture_state_index(effective_state);
    const auto modulation = disabled(effective_state) ? full_color(disabled_tint)
                                                      : full_color();
    const auto pressed_offset =
        effective_state == SettingsPresentationVisualState::pressed ? 1.0 : 0.0;
    bounds.y += pressed_offset;
    list.push(sprite(square_button_assets[index],
                     bounds,
                     DrawSpace::design_pixels,
                     TextureFilter::nearest,
                     TextureAnchor::top_left,
                     1.0,
                     modulation));
    list.push(sprite(icon,
                     bounds,
                     DrawSpace::design_pixels,
                     TextureFilter::nearest,
                     TextureAnchor::center,
                     1.0,
                     modulation));
}

[[nodiscard]] DrawRect row_control_bounds(DrawRect row) noexcept {
    const auto name_width = row.width / 3.0;
    return DrawRect{
        row.x + name_width + 28.0,
        row.y + 4.0,
        row.width - name_width - 42.0,
        row.height - 8.0,
    };
}

void append_row_label(ui::DrawList& list,
                      const SettingsPresentationRow& row,
                      DrawRect bounds,
                      std::uint16_t intensity) {
    const auto name_width = bounds.width / 3.0;
    list.push(text(row.label_key,
                   settings_presentation_assets::row_font,
                   DrawRect{bounds.x + 14.0, bounds.y + 2.0, name_width, bounds.height - 4.0},
                   16.0,
                   cream,
                   HorizontalTextAlignment::center,
                   VerticalTextAlignment::retail_center,
                   TextTransform::uppercase,
                   TextFit::shrink_to_fit,
                   1U,
                   intensity));
}

void append_range_bar(ui::DrawList& list,
                      const SettingsPresentationRow& row,
                      DrawRect control) {
    append_solid(list, control, black);
    constexpr double option_spacing{4.0};
    constexpr double button_bar_spacing{2.0};
    constexpr double bar_width{6.0};
    constexpr double bar_spacing{1.0};
    const auto arrow_size = control.height - option_spacing * 2.0;
    append_square_button(list,
                         DrawRect{control.x + option_spacing,
                                  control.y + option_spacing,
                                  arrow_size,
                                  arrow_size},
                         row.visual_state,
                         row.left_enabled,
                         arrow_left);
    append_square_button(list,
                         DrawRect{control.x + control.width - option_spacing - arrow_size,
                                  control.y + option_spacing,
                                  arrow_size,
                                  arrow_size},
                         row.visual_state,
                         row.right_enabled,
                         arrow_right);

    const auto track_x =
        control.x + option_spacing + arrow_size + button_bar_spacing;
    const auto track_width = control.width - arrow_size * 2.0 - option_spacing * 2.0 -
                             button_bar_spacing * 2.0;
    const auto stride = bar_width + bar_spacing;
    const auto segment_count = static_cast<std::size_t>(std::ceil(track_width / stride));
    const auto filled = static_cast<std::size_t>(
        std::ceil(row.scalar_value * static_cast<double>(segment_count)));
    const auto bar_tint = row.label_key == "CLASSIC_FOG_RED" ? ColorRgba8{235U, 105U, 100U, 255U}
                        : row.label_key == "CLASSIC_FOG_GREEN" ? ColorRgba8{115U, 210U, 100U, 255U}
                        : row.label_key == "CLASSIC_FOG_BLUE" ? ColorRgba8{105U, 155U, 240U, 255U}
                        : white;
    const bool fog_channel = row.label_key == "CLASSIC_FOG_RED" ||
                             row.label_key == "CLASSIC_FOG_GREEN" || row.label_key == "CLASSIC_FOG_BLUE";
    for (std::size_t index = 0U; index < segment_count; ++index) {
        if (fog_channel) {
            auto tint = bar_tint;
            if (index >= filled || disabled(row.visual_state)) {
                tint.red = static_cast<std::uint8_t>(tint.red / 3U);
                tint.green = static_cast<std::uint8_t>(tint.green / 3U);
                tint.blue = static_cast<std::uint8_t>(tint.blue / 3U);
            }
            append_solid(list, {track_x + stride * static_cast<double>(index),
                               control.y + option_spacing, bar_width, arrow_size}, tint);
            continue;
        }
        list.push(sprite(volume_bar,
                         DrawRect{track_x + stride * static_cast<double>(index),
                                  control.y + option_spacing,
                                  bar_width,
                                  arrow_size},
                         DrawSpace::design_pixels,
                         TextureFilter::nearest,
                         TextureAnchor::center,
                         settings_source_scale,
                         full_color(bar_tint, index < filled ? std::uint16_t{1'000U}
                                                         : std::uint16_t{500U})));
    }
}

void append_choice(ui::DrawList& list,
                   const SettingsPresentationRow& row,
                   DrawRect control) {
    append_solid(list, control, black);
    constexpr double option_spacing{4.0};
    constexpr double button_bar_spacing{2.0};
    const auto arrow_size = control.height - option_spacing * 2.0;
    append_square_button(list,
                         DrawRect{control.x + option_spacing,
                                  control.y + option_spacing,
                                  arrow_size,
                                  arrow_size},
                         row.visual_state,
                         row.left_enabled,
                         arrow_left);
    append_square_button(list,
                         DrawRect{control.x + control.width - option_spacing - arrow_size,
                                  control.y + option_spacing,
                                  arrow_size,
                                  arrow_size},
                         row.visual_state,
                         row.right_enabled,
                         arrow_right);
    const auto bar_x = std::floor(control.x + option_spacing + arrow_size) +
                       button_bar_spacing;
    const auto right_button_x = control.x + control.width - option_spacing - arrow_size;
    const auto bar_width = right_button_x - button_bar_spacing - bar_x;
    append_solid(list,
                 DrawRect{bar_x,
                          control.y + option_spacing,
                          bar_width,
                          arrow_size},
                 control_grey);
    list.push(text(row.value_key,
                   settings_presentation_assets::title_font,
                   DrawRect{bar_x + option_spacing,
                            control.y,
                            bar_width - option_spacing * 2.0,
                            control.height},
                   11.0,
                   cream,
                   HorizontalTextAlignment::center,
                   VerticalTextAlignment::retail_center,
                   TextTransform::uppercase));
}

void append_toggle(ui::DrawList& list,
                   const SettingsPresentationRow& row,
                   DrawRect control,
                   std::uint16_t intensity) {
    constexpr double spacing{4.0};
    const auto box_width = control.width * 0.5 - spacing * 0.5;
    const auto image_size = control.height - spacing * 2.0;
    const auto inner_width = box_width - spacing * 3.0 - image_size;
    const auto inner_y = control.y + spacing;
    for (std::size_t index = 0U; index < 2U; ++index) {
        const auto selected = row.checked == (index == 1U);
        const auto box_x = index == 0U ? control.x : control.x + control.width - box_width;
        const auto inner_x = box_x + spacing * 2.0 + image_size;
        const DrawRect half{
            box_x,
            control.y,
            box_width,
            control.height,
        };
        append_solid(list, half, black, intensity);
        append_solid(list,
                     DrawRect{inner_x, inner_y, inner_width, image_size},
                     selected ? (disabled(row.visual_state) ? control_grey : selected_gold)
                              : (row.unselected_half_hovered ? hovered_toggle : idle_toggle),
                     intensity);
    }
    const auto selected_box_x = row.checked ? control.x + control.width - box_width : control.x;
    list.push(sprite(disabled(row.visual_state) ? bullet_hole_disabled : bullet_hole,
                     DrawRect{selected_box_x + spacing,
                              control.y + spacing,
                              image_size,
                              image_size},
                     DrawSpace::design_pixels,
                     TextureFilter::nearest,
                     TextureAnchor::center,
                     1.0,
                     full_color(white, intensity)));
    for (std::size_t index = 0U; index < 2U; ++index) {
        const auto box_x = index == 0U ? control.x : control.x + control.width - box_width;
        const auto inner_x = box_x + spacing * 2.0 + image_size;
        const auto key = index == 0U ? std::string_view{"OFF"} : std::string_view{"ON"};
        list.push(text(key,
                       settings_presentation_assets::title_font,
                       DrawRect{inner_x + spacing,
                                inner_y,
                                inner_width - spacing * 2.0,
                                image_size},
                       11.0,
                       black,
                       HorizontalTextAlignment::center,
                       VerticalTextAlignment::retail_center,
                       TextTransform::uppercase,
                       TextFit::shrink_to_fit,
                       1U,
                       intensity));
    }
}

void append_checkbox(ui::DrawList& list,
                     const SettingsPresentationRow& row,
                     DrawRect control,
                     std::uint16_t intensity) {
    constexpr double spacing{4.0};
    constexpr double text_spacing{14.0};
    append_solid(list, control, black, intensity);
    const auto check_size = control.height - spacing * 2.0;
    const auto check_x = control.x + control.width - spacing - check_size;
    const auto disabled_control = disabled(row.visual_state);
    list.push(text(row.value_key,
                   settings_presentation_assets::standard_font,
                   DrawRect{control.x + text_spacing,
                            control.y + spacing,
                            control.width - spacing - text_spacing * 2.0 - check_size,
                            check_size},
                   11.0,
                   disabled_control ? control_grey : cream,
                   HorizontalTextAlignment::center,
                    VerticalTextAlignment::retail_center,
                   TextTransform::preserve,
                   TextFit::shrink_to_fit,
                   1U,
                   1'000U));
    list.push(sprite(row.checked ? favourite_star : favourite_star_off,
                     DrawRect{check_x, control.y + spacing, check_size, check_size},
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     TextureAnchor::top_left,
                     settings_source_scale,
                     full_color(disabled_control ? control_grey : white)));
}

void append_dropdown(ui::DrawList& list,
                     const SettingsPresentationRow& row,
                     DrawRect control,
                     std::uint16_t intensity) {
    constexpr double spacing{4.0};
    constexpr double button_spacing{2.0};
    constexpr double text_spacing{14.0};
    append_solid(list, control, black, intensity);
    const auto button_size = control.height - spacing * 2.0;
    const auto grey_width =
        control.width - spacing * 2.0 - button_size - button_spacing;
    append_solid(list,
                 DrawRect{control.x + spacing,
                          control.y + spacing,
                          grey_width,
                          button_size},
                 control_grey,
                 intensity);
    append_square_button(list,
                         DrawRect{control.x + control.width - spacing - button_size,
                                  control.y + spacing,
                                  button_size,
                                  button_size},
                         row.visual_state,
                         !disabled(row.visual_state),
                         arrow_down);
    const auto text_width =
        control.width - text_spacing * 2.0 - button_size - button_spacing;
    list.push(text(row.value_key,
                   settings_presentation_assets::title_font,
                   DrawRect{control.x + text_spacing,
                            control.y,
                            text_width,
                            control.height},
                   11.0,
                   cream,
                   HorizontalTextAlignment::left,
                    VerticalTextAlignment::retail_center,
                   TextTransform::uppercase,
                   TextFit::shrink_to_fit,
                   1U,
                   intensity));
    if (!row.supplementary_value_key.empty()) {
        list.push(text(row.supplementary_value_key,
                       settings_presentation_assets::standard_font,
                       DrawRect{control.x + text_spacing,
                                control.y,
                                text_width,
                                control.height},
                       11.0,
                       cream,
                       HorizontalTextAlignment::right,
                       VerticalTextAlignment::retail_center,
                       TextTransform::preserve,
                       TextFit::shrink_to_fit,
                       1U,
                       intensity));
    }
}

void append_key_binding(ui::DrawList& list,
                        const SettingsPresentationRow& row,
                        DrawRect control,
                        std::uint16_t intensity) {
    constexpr double spacing{4.0};
    constexpr double button_spacing{2.0};
    // The row paints this full key background. KeyControl only adds a black
    // overlay while it owns keyboard focus; `pressed` represents that state.
    append_solid(list,
                 control,
                 row.visual_state == SettingsPresentationVisualState::pressed ? black : cream,
                 intensity);
    list.push(text(row.value_key,
                   settings_presentation_assets::row_font,
                   DrawRect{control.x + spacing,
                            control.y + button_spacing,
                            control.width - spacing * 2.0,
                            control.height - button_spacing * 2.0},
                   11.0,
                   row.visual_state == SettingsPresentationVisualState::pressed ? cream : black,
                   HorizontalTextAlignment::center,
                    VerticalTextAlignment::retail_center,
                   // KeyControl.draw prints translate_key() text verbatim
                   // ("Left", "MWheel", "press key", "None").
                   TextTransform::preserve,
                   TextFit::shrink_to_fit,
                   1U,
                   intensity));
}

void append_scalar_slider(ui::DrawList& list,
                          const SettingsPresentationRow& row,
                          DrawRect control,
                          std::uint16_t intensity) {
    constexpr double spacing{4.0};
    constexpr double line_size{2.0};
    constexpr double bullet_width{4.0};
    append_solid(list, control, black, intensity);
    const auto edit_width = control.width / 6.0;
    const auto edit_x = control.x + control.width - spacing - edit_width;
    const auto slider_x = control.x + spacing * 2.0;
    const auto slider_width = control.width - edit_width - spacing * 5.0;
    const auto slider_height = control.height - spacing * 2.0;
    const auto line_height = std::floor(slider_height * 0.5);
    const auto horizontal_y = control.y + control.height - spacing - line_size;
    const auto vertical_y = control.y + control.height - spacing - line_height;
    append_solid(list,
                 DrawRect{slider_x, horizontal_y, slider_width, line_size},
                 slider_line_grey,
                 intensity);
    append_solid(list,
                 DrawRect{slider_x, vertical_y, line_size, line_height},
                 slider_line_grey,
                 intensity);
    append_solid(list,
                 DrawRect{slider_x + slider_width, vertical_y, line_size, line_height},
                 slider_line_grey,
                 intensity);
    const auto bullet_center_x = slider_x + slider_width * row.scalar_value;
    constexpr double bullet_height{spacing * 2.0 + line_size};
    list.push(text("0.0",
                   settings_presentation_assets::standard_font,
                   DrawRect{slider_x, control.y, line_height, line_height},
                   11.0,
                   cream,
                   HorizontalTextAlignment::center));
    list.push(text("1.0",
                   settings_presentation_assets::standard_font,
                   DrawRect{slider_x + slider_width - line_height,
                            control.y,
                            line_height,
                            line_height},
                   11.0,
                   cream,
                   HorizontalTextAlignment::center));
    list.push(sprite(bullet_slider,
                     DrawRect{bullet_center_x - bullet_width * 0.5,
                              control.y + 11.0,
                              bullet_width,
                              bullet_height},
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     TextureAnchor::center,
                     settings_source_scale,
                     full_color(white, intensity)));
    append_solid(list,
                 DrawRect{edit_x,
                          control.y + spacing,
                          edit_width,
                          slider_height},
                 ColorRgba8{45U, 45U, 45U, 255U},
                 intensity);
    list.push(text(row.value_key,
                   settings_presentation_assets::standard_font,
                   DrawRect{edit_x,
                            control.y + spacing,
                            edit_width,
                            slider_height},
                   11.0,
                   cream,
                   HorizontalTextAlignment::center,
                    VerticalTextAlignment::retail_center,
                   TextTransform::preserve,
                   TextFit::shrink_to_fit,
                   1U,
                   intensity));
}

void append_category(ui::DrawList& list,
                     const SettingsPresentationRow& row,
                     DrawRect bounds,
                     std::uint16_t intensity) {
    append_solid(list, bounds, category_green, intensity);
    list.push(text(row.label_key,
                   settings_presentation_assets::row_font,
                   DrawRect{bounds.x + 14.0, bounds.y, bounds.width - 28.0, bounds.height},
                   16.0,
                   cream,
                   HorizontalTextAlignment::left,
                    VerticalTextAlignment::retail_center,
                   TextTransform::uppercase,
                   TextFit::shrink_to_fit,
                   1U,
                   intensity));
    const DrawRect button{bounds.x + bounds.width - 32.0, bounds.y + 4.0, 18.0, 18.0};
    append_square_button(list,
                         button,
                         row.visual_state,
                         !disabled(row.visual_state),
                         row.expanded ? collapse_minus : collapse_plus);
}

[[nodiscard]] std::vector<VisibleRow>
visible_rows(const SettingsPresentationSnapshot& snapshot, bool has_scrollbar) {
    std::vector<VisibleRow> result;
    auto y = 143.0;
    const auto width = has_scrollbar ? 442.0 : 474.0;
    for (auto index = snapshot.first_visible_row; index < snapshot.rows.size(); ++index) {
        const auto& row = snapshot.rows[index];
        const auto height =
            row.kind == SettingsPresentationRowKind::category ? category_height : row_height;
        if (y + height > list_bottom) {
            break;
        }
        result.push_back(VisibleRow{&row, DrawRect{162.0, y, width, height}});
        y += height + row_gap;
    }
    return result;
}

[[nodiscard]] bool needs_scrollbar(const SettingsPresentationSnapshot& snapshot) {
    if (snapshot.first_visible_row > 0U) {
        return true;
    }
    auto y = 143.0;
    for (const auto& row : snapshot.rows) {
        const auto height =
            row.kind == SettingsPresentationRowKind::category ? category_height : row_height;
        if (y + height > list_bottom) {
            return true;
        }
        y += height + row_gap;
    }
    return false;
}

void append_row(ui::DrawList& list, const VisibleRow& visible) {
    const auto& row = *visible.row;
    constexpr std::uint16_t intensity{1'000U};
    if (row.kind == SettingsPresentationRowKind::category) {
        append_category(list, row, visible.bounds, intensity);
        return;
    }

    list.push(sprite(disabled(row.visual_state) ? settings_presentation_assets::disabled_row
                                                : settings_presentation_assets::enabled_row,
                     visible.bounds,
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     TextureAnchor::center,
                     settings_source_scale,
                     full_color(white, intensity)));
    append_row_label(list, row, visible.bounds, intensity);
    const auto control = row_control_bounds(visible.bounds);
    switch (row.kind) {
    case SettingsPresentationRowKind::range_bar:
        append_range_bar(list, row, control);
        break;
    case SettingsPresentationRowKind::choice:
        append_choice(list, row, control);
        break;
    case SettingsPresentationRowKind::toggle:
        append_toggle(list, row, control, intensity);
        break;
    case SettingsPresentationRowKind::checkbox:
        append_checkbox(list, row, control, intensity);
        break;
    case SettingsPresentationRowKind::dropdown:
        append_dropdown(list, row, control, intensity);
        break;
    case SettingsPresentationRowKind::key_binding:
        append_key_binding(list, row, control, intensity);
        break;
    case SettingsPresentationRowKind::scalar_slider:
        append_scalar_slider(list, row, control, intensity);
        break;
    case SettingsPresentationRowKind::category:
        break;
    }
}

void append_scrollbar(ui::DrawList& list,
                      const SettingsPresentationSnapshot& snapshot,
                      std::size_t visible_count) {
    constexpr DrawRect scroll_bounds{614.0, 143.0, 22.0, 273.0};
    constexpr double arrow_height{22.0};
    constexpr double track_y{167.0};
    constexpr double track_height{225.0};
    append_solid(list, scroll_bounds, black);
    append_solid(list, DrawRect{615.0, track_y, 20.0, track_height}, scrollbar_rail);
    append_square_button(list,
                         DrawRect{614.0, 143.0, 22.0, arrow_height},
                         SettingsPresentationVisualState::normal,
                         snapshot.first_visible_row > 0U,
                         arrow_up);
    const auto maximum_offset = snapshot.rows.size() > visible_count
                                    ? snapshot.rows.size() - visible_count
                                    : std::size_t{};
    append_square_button(list,
                         DrawRect{614.0, 394.0, 22.0, arrow_height},
                         SettingsPresentationVisualState::normal,
                         snapshot.first_visible_row < maximum_offset,
                         arrow_down);

    const auto total = std::max<std::size_t>(1U, snapshot.rows.size());
    const auto thumb_height = std::clamp(
        std::floor(track_height * static_cast<double>(visible_count) /
                   static_cast<double>(total)),
        18.0,
        track_height);
    const auto travel = track_height - thumb_height;
    const auto ratio = maximum_offset == 0U
                           ? 0.0
                           : static_cast<double>(std::min(snapshot.first_visible_row,
                                                          maximum_offset)) /
                                 static_cast<double>(maximum_offset);
    const DrawRect thumb{615.0, track_y + travel * ratio, 20.0, thumb_height};
    constexpr double cap_height{4.0};
    list.push(sprite(scrollbar_top,
                     DrawRect{thumb.x, thumb.y, thumb.width, cap_height},
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     TextureAnchor::top_left,
                     retail_source_scale));
    list.push(sprite(scrollbar_mid,
                     DrawRect{thumb.x,
                              thumb.y + cap_height,
                              thumb.width,
                              std::max(0.0, thumb.height - cap_height * 2.0)},
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     TextureAnchor::top_left,
                     retail_source_scale));
    list.push(sprite(scrollbar_bottom,
                     DrawRect{thumb.x,
                              thumb.y + thumb.height - cap_height,
                              thumb.width,
                              cap_height},
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     TextureAnchor::top_left,
                     retail_source_scale));
}

void append_dropdown_overlays(ui::DrawList& list, const std::vector<VisibleRow>& rows) {
    constexpr double option_height{20.0};
    for (const auto& visible : rows) {
        const auto& row = *visible.row;
        if (row.kind != SettingsPresentationRowKind::dropdown || !row.dropdown_open ||
            row.dropdown_options.empty()) {
            continue;
        }
        const auto control = row_control_bounds(visible.bounds);
        const auto panel_height = option_height * static_cast<double>(row.dropdown_options.size());
        const auto panel_y = std::min(control.y + control.height, design_height - panel_height - 4.0);
        const DrawRect panel{control.x, panel_y, control.width, panel_height};
        append_solid(list, panel, black);
        for (std::size_t index = 0U; index < row.dropdown_options.size(); ++index) {
            const DrawRect option{
                panel.x + 2.0,
                panel.y + 2.0 + option_height * static_cast<double>(index),
                panel.width - 4.0,
                option_height - 2.0,
            };
            append_solid(list,
                         option,
                         row.dropdown_selection_visible &&
                                 index == row.dropdown_selected_index
                             ? selected_gold
                             : control_grey);
            list.push(text(row.dropdown_options[index],
                           settings_presentation_assets::title_font,
                           DrawRect{option.x + 6.0,
                                    option.y,
                                    option.width - 12.0,
                                    option.height},
                           11.0,
                           black,
                           HorizontalTextAlignment::left,
                           VerticalTextAlignment::retail_center,
                           TextTransform::uppercase));
        }
    }
}

[[nodiscard]] std::string_view content_asset(SettingsPresentationTab tab) noexcept {
    switch (tab) {
    case SettingsPresentationTab::main:
        return settings_presentation_assets::main_content;
    case SettingsPresentationTab::graphics:
        return settings_presentation_assets::graphics_content;
    case SettingsPresentationTab::controls:
        return settings_presentation_assets::controls_content;
    }
    return settings_presentation_assets::main_content;
}

void append_tabs(ui::DrawList& list, SettingsPresentationTab selected) {
    constexpr std::array<std::string_view, 3U> keys{"MAIN", "GRAPHICS", "CONTROLS"};
    constexpr std::array<double, 3U> x_positions{160.0, 325.0, 490.0};
    for (std::size_t index = 0U; index < keys.size(); ++index) {
        const auto active = index == static_cast<std::size_t>(selected);
        list.push(text(keys[index],
                       settings_presentation_assets::row_font,
                       DrawRect{x_positions[index], 104.0, 145.0, 26.0},
                       18.0,
                       active ? tab_gold : cream,
                       HorizontalTextAlignment::center,
                       VerticalTextAlignment::retail_center,
                       TextTransform::uppercase,
                       TextFit::shrink_to_fit,
                       1U,
                       // Inactive tabs are full-strength MENU_FONT_COLOR cream
                       // (244,236,187) in retail, in and out of game; the
                       // 70% dimming read as a greyed-out GRAPHICS tab.
                       std::uint16_t{1'000U}));
    }
}

void append_settings(ui::DrawList& list,
                     const SettingsPresentationSnapshot& snapshot,
                     const SettingsClassicLayout& layout) {
    list.push(text("SETTINGS",
                   settings_presentation_assets::title_font,
                   layout.title,
                   46.0,
                   cream,
                   HorizontalTextAlignment::center,
                    VerticalTextAlignment::retail_center,
                   TextTransform::uppercase));
    list.push(sprite(content_asset(snapshot.selected_tab),
                     layout.content_frame,
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     TextureAnchor::center,
                     settings_source_scale));

    const auto scrollbar = needs_scrollbar(snapshot);
    const auto rows = visible_rows(snapshot, scrollbar);
    for (const auto& row : rows) {
        append_row(list, row);
    }
    if (scrollbar) {
        append_scrollbar(list, snapshot, rows.size());
    }
    append_dropdown_overlays(list, rows);
    append_tabs(list, snapshot.selected_tab);

    const auto default_enabled = !disabled(snapshot.defaults_button.visual_state);
    const auto tooltip_bounds = default_enabled
                                    ? layout.tooltip
                                    : DrawRect{layout.defaults_button.x,
                                               layout.tooltip.y,
                                               layout.tooltip.width +
                                                   (layout.tooltip.x - layout.defaults_button.x),
                                               layout.tooltip.height};
    const auto tooltip_asset = snapshot.in_game ? settings_presentation_assets::in_game_tooltip
                                                : settings_presentation_assets::tooltip;
    list.push(sprite(tooltip_asset,
                     tooltip_bounds,
                     DrawSpace::design_pixels,
                     TextureFilter::nearest,
                     TextureAnchor::center,
                     1.0));
    const auto tooltip_key = !snapshot.error_key.empty() ? snapshot.error_key : snapshot.tooltip_key;
    const auto tooltip_color =
        snapshot.tooltip_is_error || !snapshot.error_key.empty() ? tooltip_error : cream;
    list.push(text(tooltip_key,
                   settings_presentation_assets::standard_font,
                   DrawRect{tooltip_bounds.x + 14.0,
                            tooltip_bounds.y,
                            tooltip_bounds.width - 28.0,
                            tooltip_bounds.height},
                   11.0,
                   tooltip_color,
                   HorizontalTextAlignment::center,
                    VerticalTextAlignment::retail_center,
                   TextTransform::preserve,
                   TextFit::shrink_to_fit,
                   (tooltip_key == "CLASSIC_SKY_DESCRIPTION" || tooltip_key == "CLASSIC_FOG_DESCRIPTION") ? 2U : 1U));
    if (default_enabled) {
        append_text_button(list, layout.defaults_button, snapshot.defaults_button);
    }
    append_text_button(list, layout.cancel_button, snapshot.cancel_button);
    append_text_button(list, layout.done_button, snapshot.done_button);
}

void append_resolution_confirmation(ui::DrawList& list,
                                    const SettingsPresentationSnapshot& snapshot,
                                    const SettingsClassicLayout& layout) {
    list.push(text("CONFIRM",
                   settings_presentation_assets::title_font,
                   layout.title,
                   46.0,
                   cream,
                   HorizontalTextAlignment::center,
                    VerticalTextAlignment::retail_center,
                   TextTransform::uppercase));
    list.push(text("KEEP_RESOLUTION_PROMPT",
                   settings_presentation_assets::standard_font,
                   DrawRect{154.0, 176.0, 492.0, 92.0},
                   18.0,
                   cream,
                   HorizontalTextAlignment::center,
                    VerticalTextAlignment::retail_center,
                   TextTransform::preserve,
                   TextFit::shrink_to_fit,
                   2U));
    // ChangeResolutionMenu formats int(revert_countdown), i.e. truncates the
    // positive timer instead of rounding it to the next second.
    const auto seconds = static_cast<std::uint32_t>(snapshot.resolution_countdown_seconds);
    const auto countdown = std::string{"Reverting to previous resolution in "} +
                           std::to_string(seconds) + " seconds.";
    list.push(text(countdown,
                   settings_presentation_assets::standard_font,
                   DrawRect{154.0, 275.0, 492.0, 40.0},
                   15.0,
                   cream,
                   HorizontalTextAlignment::center,
                    VerticalTextAlignment::retail_center));
    append_text_button(list,
                       DrawRect{161.0, 431.0, 236.0, 61.0},
                       SettingsPresentationButton{"KEEP_SETTING",
                                                  snapshot.done_button.visual_state});
    append_text_button(list,
                       DrawRect{402.0, 431.0, 236.0, 61.0},
                       SettingsPresentationButton{"REVERT",
                                                  snapshot.cancel_button.visual_state});
}

void validate_snapshot(const SettingsPresentationSnapshot& snapshot,
                       const SettingsPresentationContext& context) {
    if (!context.window.is_valid()) {
        throw std::invalid_argument{"settings presentation requires a valid window extent"};
    }
    if (context.background_opacity_per_mille > 1'000U) {
        throw std::invalid_argument{"settings background opacity exceeds one"};
    }
    if (snapshot.first_visible_row > snapshot.rows.size()) {
        throw std::invalid_argument{"settings first visible row is out of range"};
    }
    if (!std::isfinite(snapshot.resolution_countdown_seconds) ||
        snapshot.resolution_countdown_seconds < 0.0) {
        throw std::invalid_argument{"resolution confirmation countdown is invalid"};
    }
    for (const auto& row : snapshot.rows) {
        if (!std::isfinite(row.scalar_value) || row.scalar_value < 0.0 ||
            row.scalar_value > 1.0) {
            throw std::invalid_argument{"settings row scalar is outside [0, 1]"};
        }
        if (row.dropdown_open && row.dropdown_selection_visible &&
            !row.dropdown_options.empty() &&
            row.dropdown_selected_index >= row.dropdown_options.size()) {
            throw std::invalid_argument{"settings dropdown selection is out of range"};
        }
    }
}

} // namespace

SettingsClassicLayout settings_classic_layout(bool in_game) noexcept {
    if (in_game) {
        return SettingsClassicLayout{
            DrawRect{133.0, 28.0, 534.0, 524.0},
            DrawRect{112.0, 10.0, 552.0, 579.0},
            DrawRect{250.0, 26.0, 300.0, 80.0},
            DrawRect{152.0, 100.0, 494.0, 33.0},
            DrawRect{162.0, 143.0, 474.0, 273.0},
            DrawRect{151.0, 434.0, 80.0, 30.0},
            DrawRect{238.0, 434.0, 407.0, 28.0},
            DrawRect{160.0, 480.0, 232.0, 41.0},
            DrawRect{403.0, 480.0, 232.0, 41.0},
        };
    }
    return SettingsClassicLayout{
        DrawRect{112.0, 10.0, 576.0, 579.0},
        DrawRect{112.0, 10.0, 552.0, 579.0},
        DrawRect{250.0, 11.0, 300.0, 80.0},
        DrawRect{152.0, 100.0, 494.0, 33.0},
        DrawRect{162.0, 143.0, 474.0, 273.0},
        DrawRect{151.0, 434.0, 80.0, 30.0},
        DrawRect{238.0, 434.0, 407.0, 28.0},
        DrawRect{152.0, 492.0, 240.0, 60.0},
        DrawRect{405.0, 492.0, 240.0, 60.0},
    };
}

ui::DrawList SettingsPresentation::build(const SettingsPresentationSnapshot& snapshot,
                                         const SettingsPresentationContext& context) const {
    validate_snapshot(snapshot, context);
    ui::DrawList list;
    list.reserve(256U);

    if (snapshot.include_frontend_background) {
        list.push(sprite(settings_presentation_assets::background,
                         background_cover(context.window),
                         DrawSpace::window_pixels,
                         TextureFilter::linear,
                         TextureAnchor::top_left,
                         retail_source_scale,
                         full_color(white, 1'000U, context.background_opacity_per_mille),
                         SpriteSizing::cover));
    }

    const auto layout = settings_classic_layout(snapshot.in_game);
    list.push(sprite(snapshot.in_game ? settings_presentation_assets::in_game_outer_frame
                                     : settings_presentation_assets::outer_frame,
                     layout.outer_frame,
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     TextureAnchor::center,
                     settings_source_scale));

    if (snapshot.screen == SettingsPresentationScreen::resolution_confirmation) {
        append_resolution_confirmation(list, snapshot, layout);
    } else {
        append_settings(list, snapshot, layout);
    }
    return list;
}

} // namespace battlespades::frontend
