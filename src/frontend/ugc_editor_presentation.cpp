#include "battlespades/frontend/ugc_editor_presentation.hpp"

#include "battlespades/frontend/custom_match_presentation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
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
constexpr ColorRgba8 unavailable{146U, 141U, 111U, 255U};
constexpr ColorRgba8 selected_green{146U, 241U, 141U, 255U};
constexpr ColorRgba8 category_green{59U, 68U, 25U, 255U};
constexpr ColorRgba8 control_grey{83U, 83U, 83U, 255U};
constexpr ColorRgba8 slider_line_grey{117U, 117U, 117U, 255U};
constexpr ColorRgba8 edit_grey{45U, 45U, 45U, 255U};
constexpr ColorRgba8 scrollbar_rail{73U, 63U, 7U, 255U};
constexpr double source_scale{0.6};
constexpr double global_scale{0.64};
constexpr double browser_row_height{25.0};
constexpr double settings_row_height{42.0};
constexpr double ugc_settings_row_height{32.0};
constexpr double ugc_category_row_height{26.0};
constexpr double ugc_row_spacing{2.0};
constexpr double ugc_list_top{115.0};
constexpr double ugc_list_bottom{446.0};

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

constexpr std::array<std::string_view, 3U> square_button_assets{
    "png/ui/common_elements/buttons/button_square.png",
    "png/ui/common_elements/buttons/button_square_hover.png",
    "png/ui/common_elements/buttons/button_square_press.png",
};
constexpr std::string_view arrow_down{
    "png/ui/common_elements/scroll_bar/scroll_bar_arrow_down.png"};
constexpr std::string_view arrow_up{
    "png/ui/common_elements/scroll_bar/scroll_bar_arrow_up.png"};
constexpr std::string_view arrow_left{
    "png/ui/common_elements/scroll_bar/scroll_bar_arrow_left.png"};
constexpr std::string_view arrow_right{
    "png/ui/common_elements/scroll_bar/scroll_bar_arrow_right.png"};
constexpr std::string_view collapse_minus{
    "png/ui/common_elements/collapse_minus.png"};
constexpr std::string_view bullet_slider{
    "png/ui/settings/settings_common/settings_bullet_slider.png"};
constexpr std::string_view scrollbar_top{
    "png/ui/common_elements/scroll_bar/scroll_bar_top.png"};
constexpr std::string_view scrollbar_mid{
    "png/ui/common_elements/scroll_bar/scroll_bar_mid.png"};
constexpr std::string_view scrollbar_bottom{
    "png/ui/common_elements/scroll_bar/scroll_bar_bottom.png"};

enum class UgcSettingsRowKind : std::uint8_t {
    category,
    dropdown,
    slider,
    colour_preview,
    edit_text,
    button,
};

struct UgcSettingsRow final {
    UgcSettingsRowKind kind{};
    std::string_view label{};
    std::optional<UgcIngameSettingField> field{};
};

constexpr std::array<UgcSettingsRow, 11U> ugc_settings_rows{{
    {UgcSettingsRowKind::category, "SKY", std::nullopt},
    {UgcSettingsRowKind::dropdown, "SKYDOME", UgcIngameSettingField::skydome},
    {UgcSettingsRowKind::category, "WATER", std::nullopt},
    {UgcSettingsRowKind::slider, "RED", UgcIngameSettingField::water_red},
    {UgcSettingsRowKind::slider, "GREEN", UgcIngameSettingField::water_green},
    {UgcSettingsRowKind::slider, "BLUE", UgcIngameSettingField::water_blue},
    {UgcSettingsRowKind::colour_preview, "PREVIEW", std::nullopt},
    {UgcSettingsRowKind::category, "CONFIG", std::nullopt},
    {UgcSettingsRowKind::dropdown, "CHOOSE_GAME_MODE", UgcIngameSettingField::target_mode},
    {UgcSettingsRowKind::edit_text, "UGC_MAP_TITLE", UgcIngameSettingField::map_title},
    {UgcSettingsRowKind::button, "MAP_PREVIEW_IMAGE", UgcIngameSettingField::map_preview},
}};

struct VisibleUgcSettingsRow final {
    const UgcSettingsRow* row{};
    DrawRect bounds{};
};

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
                                   std::string_view font = ugc_editor_assets::row_font,
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
    list.push(sprite(ugc_editor_assets::white_pixel,
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
    case WidgetVisualState::focused: return 1U;
    case WidgetVisualState::pressed: return 2U;
    case WidgetVisualState::normal:
    case WidgetVisualState::disabled: return 0U;
    }
    return 0U;
}

void append_button(ui::DrawList& list,
                   DrawRect bounds,
                   std::string_view label,
                   WidgetVisualState state,
                   double font_size = 18.0) {
    const auto slice_width = std::floor((36.0 / 58.0) * bounds.height);
    const auto middle_width = bounds.width - slice_width * 2.0;
    const auto state_index = button_state_index(state);
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
    for (std::size_t slice = 0U; slice < destinations.size(); ++slice) {
        list.push(sprite(button_assets[state_index][slice],
                         destinations[slice],
                         DrawSpace::design_pixels,
                         TextureFilter::nearest,
                         UiTextureAnchor::top_left,
                         source_scale,
                         color(white, intensity)));
    }
    const auto pressed_offset = state == WidgetVisualState::pressed ? 2.0 : 0.0;
    list.push(text(label,
                   {bounds.x + 10.0,
                    bounds.y + 3.0 + pressed_offset,
                    bounds.width - 20.0,
                    bounds.height - 6.0},
                   font_size,
                   button_text,
                   HorizontalTextAlignment::center,
                   TextTransform::uppercase,
                   main_menu_assets::button_font,
                   2U,
                   2.0,
                   intensity));
}

void append_square_button(ui::DrawList& list,
                          DrawRect bounds,
                          std::string_view icon,
                          bool enabled = true) {
    const auto intensity = enabled ? std::uint16_t{1'000U} : std::uint16_t{700U};
    list.push(sprite(square_button_assets[0U],
                     bounds,
                     DrawSpace::design_pixels,
                     TextureFilter::nearest,
                     UiTextureAnchor::top_left,
                     1.0,
                     color(enabled ? white : control_grey, intensity)));
    list.push(sprite(icon,
                     bounds,
                     DrawSpace::design_pixels,
                     TextureFilter::nearest,
                     UiTextureAnchor::center,
                     1.0,
                     color(enabled ? white : control_grey, intensity)));
}

[[nodiscard]] double ugc_row_height(UgcSettingsRowKind kind) noexcept {
    return kind == UgcSettingsRowKind::category ? ugc_category_row_height
                                                : ugc_settings_row_height;
}

[[nodiscard]] std::size_t ugc_first_visible_row(const UgcIngameSettingsModel& model) noexcept {
    // Retail's list fits ten of the eleven expanded rows.  Moving to the final
    // Map Preview row advances the list by one row, just as ListPanelBase does.
    return model.focused_field() == UgcIngameSettingField::map_preview ? 1U : 0U;
}

[[nodiscard]] std::vector<VisibleUgcSettingsRow>
visible_ugc_settings_rows(const UgcIngameSettingsModel& model) {
    std::vector<VisibleUgcSettingsRow> result;
    auto y = ugc_list_top;
    for (auto index = ugc_first_visible_row(model); index < ugc_settings_rows.size(); ++index) {
        const auto& row = ugc_settings_rows[index];
        const auto height = ugc_row_height(row.kind);
        if (y + height > ugc_list_bottom) break;
        result.push_back(VisibleUgcSettingsRow{&row, DrawRect{162.0, y, 442.0, height}});
        y += height + ugc_row_spacing;
    }
    return result;
}

[[nodiscard]] DrawRect ugc_control_bounds(DrawRect row) noexcept {
    const auto name_width = row.width / 3.0;
    return DrawRect{row.x + name_width + 28.0,
                    row.y + 4.0,
                    row.width - name_width - 42.0,
                    row.height - 8.0};
}

void append_ugc_row_label(ui::DrawList& list, const UgcSettingsRow& row, DrawRect bounds) {
    const auto name_width = bounds.width / 3.0;
    list.push(text(row.label,
                   {bounds.x + 14.0, bounds.y + 2.0, name_width, bounds.height - 4.0},
                   16.0,
                   cream,
                   HorizontalTextAlignment::center,
                   TextTransform::uppercase,
                   ugc_editor_assets::header_font));
}

void append_ugc_category(ui::DrawList& list, const UgcSettingsRow& row, DrawRect bounds) {
    solid(list, bounds, category_green);
    list.push(text(row.label,
                   {bounds.x + 14.0, bounds.y, bounds.width - 28.0, bounds.height},
                   16.0,
                   cream,
                   HorizontalTextAlignment::left,
                   TextTransform::uppercase,
                   ugc_editor_assets::header_font));
    append_square_button(list,
                         {bounds.x + bounds.width - 32.0, bounds.y + 4.0, 18.0, 18.0},
                         collapse_minus);
}

void append_ugc_dropdown(ui::DrawList& list, DrawRect control, std::string_view value) {
    constexpr double spacing{4.0};
    constexpr double button_spacing{2.0};
    constexpr double text_spacing{14.0};
    solid(list, control, black);
    const auto button_size = control.height - spacing * 2.0;
    const auto grey_width = control.width - spacing * 2.0 - button_size - button_spacing;
    solid(list,
          {control.x + spacing, control.y + spacing, grey_width, button_size},
          control_grey);
    append_square_button(list,
                         {control.x + control.width - spacing - button_size,
                          control.y + spacing,
                          button_size,
                          button_size},
                         arrow_down);
    list.push(text(value,
                   {control.x + text_spacing,
                    control.y,
                    control.width - text_spacing * 2.0 - button_size - button_spacing,
                    control.height},
                   11.0,
                   cream,
                   HorizontalTextAlignment::left,
                   TextTransform::uppercase,
                   ugc_editor_assets::title_font));
}

void append_ugc_slider(ui::DrawList& list, DrawRect control, std::uint8_t value) {
    constexpr double spacing{4.0};
    constexpr double line_size{2.0};
    constexpr double bullet_width{4.0};
    solid(list, control, black);
    const auto slider_x = control.x + spacing * 2.0;
    const auto slider_width = control.width - spacing * 4.0;
    const auto slider_height = control.height - spacing * 2.0;
    const auto line_height = std::floor(slider_height * 0.5);
    const auto horizontal_y = control.y + control.height - spacing - line_size;
    const auto vertical_y = control.y + control.height - spacing - line_height;
    solid(list, {slider_x, horizontal_y, slider_width, line_size}, slider_line_grey);
    solid(list, {slider_x, vertical_y, line_size, line_height}, slider_line_grey);
    solid(list,
          {slider_x + slider_width, vertical_y, line_size, line_height},
          slider_line_grey);
    list.push(text("0",
                   {slider_x, control.y, line_height, line_height},
                   11.0,
                   cream,
                   HorizontalTextAlignment::center,
                   TextTransform::preserve));
    list.push(text("255",
                   {slider_x + slider_width - line_height,
                    control.y,
                    line_height,
                    line_height},
                   11.0,
                   cream,
                   HorizontalTextAlignment::center,
                   TextTransform::preserve));
    const auto ratio = static_cast<double>(value) / 255.0;
    const auto bullet_x = slider_x + slider_width * ratio;
    list.push(sprite(bullet_slider,
                     {bullet_x - bullet_width * 0.5,
                      control.y + 11.0,
                      bullet_width,
                      spacing * 2.0 + line_size},
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
}

void append_ugc_colour_preview(ui::DrawList& list,
                               DrawRect control,
                               const std::array<std::uint8_t, 4U>& value) {
    solid(list, control, black);
    solid(list,
          {control.x + 3.0, control.y + 3.0, control.width - 6.0, control.height - 6.0},
          {value[0U], value[1U], value[2U], value[3U]});
}

void append_ugc_edit_text(ui::DrawList& list,
                          DrawRect control,
                          std::string value,
                          bool editing) {
    solid(list, control, edit_grey);
    if (editing) value.push_back('_');
    list.push(text(value,
                   {control.x + 4.0, control.y, control.width - 8.0, control.height},
                   11.0,
                   cream,
                   HorizontalTextAlignment::left,
                   TextTransform::preserve,
                   ugc_editor_assets::row_font));
}

void append_ugc_scrollbar(ui::DrawList& list, std::size_t first_visible_row) {
    constexpr DrawRect bounds{614.0, 115.0, 22.0, 331.0};
    constexpr double track_y{139.0};
    constexpr double track_height{283.0};
    constexpr double thumb_height{257.0};
    solid(list, bounds, black);
    solid(list, {615.0, track_y, 20.0, track_height}, scrollbar_rail);
    append_square_button(list, {614.0, 115.0, 22.0, 22.0}, arrow_up, first_visible_row > 0U);
    append_square_button(list, {614.0, 424.0, 22.0, 22.0}, arrow_down,
                         first_visible_row == 0U);
    const auto thumb_y = first_visible_row == 0U ? 140.0 : 166.0;
    constexpr double cap_height{4.0};
    list.push(sprite(scrollbar_top,
                     {615.0, thumb_y, 20.0, cap_height},
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::top_left,
                     source_scale));
    list.push(sprite(scrollbar_mid,
                     {615.0, thumb_y + cap_height, 20.0, thumb_height - cap_height * 2.0},
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::top_left,
                     source_scale));
    list.push(sprite(scrollbar_bottom,
                     {615.0, thumb_y + thumb_height - cap_height, 20.0, cap_height},
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::top_left,
                     source_scale));
}

void append_back(ui::DrawList& list,
                 DrawRect bounds,
                 std::string_view label,
                 WidgetVisualState state) {
    const auto highlighted = state == WidgetVisualState::hovered ||
                             state == WidgetVisualState::focused ||
                             state == WidgetVisualState::pressed;
    const auto intensity = highlighted ? std::uint16_t{1'000U} : std::uint16_t{700U};
    const auto pressed_offset = state == WidgetVisualState::pressed ? 1.0 : 0.0;
    list.push(text(label,
                   {bounds.x + 35.0,
                    bounds.y + pressed_offset,
                    bounds.width - 35.0,
                    bounds.height},
                   22.0,
                   gold,
                   HorizontalTextAlignment::left,
                   TextTransform::uppercase,
                   main_menu_assets::button_font,
                   1U,
                   1.0,
                   intensity));
    list.push(sprite(ugc_editor_assets::back_icon,
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
                  std::string_view title_key,
                  bool centered = false,
                  double title_width = 0.0) {
    list.push(sprite(ugc_editor_assets::panel,
                     panel,
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
    list.push(sprite(ugc_editor_assets::panel_header,
                     header,
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
    list.push(text(title_key,
                   {header.x + 14.0, header.y,
                    title_width > 0.0 ? title_width : header.width - 28.0, header.height},
                   19.0,
                   cream,
                   centered ? HorizontalTextAlignment::center
                            : HorizontalTextAlignment::left,
                   TextTransform::preserve,
                   ugc_editor_assets::header_font));
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

void validate_context(const UgcEditorPresentationContext& context) {
    if (!context.window.is_valid() || context.background_opacity_per_mille > 1'000U) {
        throw std::invalid_argument{"invalid UGC editor presentation context"};
    }
}

void prepend_background(ui::DrawList& destination,
                        ui::DrawList layer,
                        const UgcEditorPresentationContext& context) {
    destination.reserve(layer.size() + 1U);
    destination.push(sprite(main_menu_assets::background,
                            background_cover(context.window),
                            DrawSpace::window_pixels,
                            TextureFilter::linear,
                            UiTextureAnchor::top_left,
                            source_scale,
                            color(white, 1'000U, context.background_opacity_per_mille),
                            SpriteSizing::cover));
    for (const auto& command : layer.commands()) {
        destination.push(command);
    }
}

} // namespace

UgcEditorBrowserClassicLayout ugc_editor_browser_classic_layout() noexcept {
    return UgcEditorBrowserClassicLayout{
        {25.0, 5.0, 750.0, 589.0},
        {120.0, 25.0, 560.0, 50.0},
        {54.0, 541.0, 120.0, 32.0},
        {56.0, 95.0, 340.0, 413.0},
        {66.0, 105.0, 320.0, 40.0},
        {242.0, 114.0, 130.0, 24.0},
        {66.0, 155.0, 320.0, 25.0},
        {401.0, 95.0, 340.0, 354.0},
        {411.0, 105.0, 320.0, 40.0},
        {401.0, 452.0, 340.0, 54.0},
        {405.0, 456.0, 162.0, 50.0},
        {575.0, 456.0, 162.0, 50.0},
    };
}

ui::DrawList
UgcEditorBrowserPresentation::build(const UgcEditorBrowserModel& model,
                                    const UgcEditorPresentationContext& context) const {
    validate_context(context);
    ui::DrawList result;
    prepend_background(result, build_layer(model), context);
    return result;
}

ui::DrawList
UgcEditorBrowserPresentation::build_layer(const UgcEditorBrowserModel& model) const {
    const auto layout = ugc_editor_browser_classic_layout();
    ui::DrawList list;
    list.reserve(128U);
    list.push(sprite(ugc_editor_assets::frame,
                     layout.frame,
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
    list.push(text("UGC_SQUADS_MENU_TITLE",
                   layout.title,
                   44.0,
                   cream,
                   HorizontalTextAlignment::center,
                   TextTransform::uppercase,
                   ugc_editor_assets::title_font));
    append_back(list, layout.back, "BACK", model.visual_state(model.controls()[0U].widget.id));

    append_panel(list, layout.list_panel, layout.list_header, "UGC_OPEN_LOBBIES", false,
                 layout.source_filter.x - layout.list_header.x - 24.0);
    solid(list, layout.source_filter, black);
    list.push(text(localization_key(model.source()),
                   {layout.source_filter.x + 10.0,
                    layout.source_filter.y,
                    layout.source_filter.width - 20.0,
                    layout.source_filter.height},
                   12.0,
                   cream,
                   HorizontalTextAlignment::center,
                   TextTransform::uppercase,
                   ugc_editor_assets::row_font,
                   1U,
                   1.0,
                   model.visual_state(model.controls()[1U].widget.id) ==
                           WidgetVisualState::disabled
                       ? 700U
                       : 1'000U));

    const auto lobbies = model.lobbies();
    const auto first = std::min(model.first_visible_row(), lobbies.size());
    const auto count =
        std::min(UgcEditorBrowserModel::visible_lobby_rows, lobbies.size() - first);
    if (count == 0U) {
        list.push(text(model.status_localization_key(),
                       {layout.list_panel.x + 20.0,
                        layout.first_lobby_row.y + 40.0,
                        layout.list_panel.width - 40.0,
                        70.0},
                       15.0,
                       unavailable,
                       HorizontalTextAlignment::center));
    } else {
        const auto selected = model.selected_index();
        for (std::size_t visible = 0U; visible < count; ++visible) {
            const auto index = first + visible;
            const auto& lobby = lobbies[index];
            const DrawRect row{layout.first_lobby_row.x,
                               layout.first_lobby_row.y +
                                   browser_row_height * static_cast<double>(visible),
                               layout.first_lobby_row.width,
                               browser_row_height};
            solid(list, row, visible % 2U == 0U ? row_grey : row_dark);
            if (selected == index) {
                list.push(sprite(ugc_editor_assets::highlight_line,
                                 row,
                                 DrawSpace::design_pixels,
                                 TextureFilter::nearest,
                                 UiTextureAnchor::top_left,
                                 source_scale));
                list.push(sprite(ugc_editor_assets::highlight_glow,
                                 {row.x - row.width * 0.025,
                                  row.y - row.height * 0.15,
                                  row.width * 1.05,
                                  row.height * 1.3},
                                 DrawSpace::design_pixels,
                                 TextureFilter::nearest,
                                 UiTextureAnchor::top_left,
                                 source_scale));
            }
            list.push(text(lobby.name, {80.0, row.y, 210.0, row.height}, 11.0));
            if (lobby.ping_milliseconds > 0U) {
                list.push(text(std::to_string(lobby.ping_milliseconds),
                               {294.0, row.y, 40.0, row.height},
                               10.0,
                               cream,
                               HorizontalTextAlignment::right));
            }
            list.push(text(std::to_string(lobby.member_count) + '/' +
                               std::to_string(lobby.maximum_members),
                           {340.0, row.y, 35.0, row.height},
                           10.0,
                           cream,
                           HorizontalTextAlignment::right));
        }
    }

    const auto* selected = model.selected_lobby();
    append_panel(list,
                 layout.preview_panel,
                 layout.preview_header,
                 selected == nullptr ? std::string_view{"MAP_CREATOR_LOBBY_INFO"}
                                     : std::string_view{selected->name});
    if (selected == nullptr) {
        list.push(text("SELECT_UGC_LOBBY",
                       {421.0, 175.0, 300.0, 70.0},
                       15.0,
                       unavailable,
                       HorizontalTextAlignment::center));
    } else {
        list.push(text("PLAYERS",
                       {421.0, 165.0, 120.0, 25.0},
                       13.0,
                       cream,
                       HorizontalTextAlignment::left,
                       TextTransform::preserve,
                       ugc_editor_assets::header_font));
        list.push(text(std::to_string(selected->member_count) + '/' +
                           std::to_string(selected->maximum_members),
                       {551.0, 165.0, 160.0, 25.0},
                       13.0,
                       selected->full() ? unavailable : selected_green,
                       HorizontalTextAlignment::right));
    }

    list.push(sprite(ugc_editor_assets::panel,
                     layout.action_background,
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
    append_button(list,
                  layout.join_button,
                  "UGC_SQUADS_MENU_JOIN",
                  model.visual_state(model.controls()[2U].widget.id),
                  15.0);
    append_button(list,
                  layout.new_lobby_button,
                  "UGC_SQUADS_MENU_NEW_LOBBY",
                  model.visual_state(model.controls()[3U].widget.id),
                  15.0);
    return list;
}

UgcEditorLobbyClassicLayout ugc_editor_lobby_classic_layout() noexcept {
    return UgcEditorLobbyClassicLayout{
        {25.0, 5.0, 750.0, 589.0},
        {120.0, 25.0, 560.0, 50.0},
        {54.0, 541.0, 170.0, 32.0},
        {56.0, 95.0, 340.0, 355.0},
        {66.0, 105.0, 320.0, 40.0},
        {66.0, 155.0, 320.0, 25.0},
        {60.0, 456.0, 332.0, 50.0},
        {401.0, 95.0, 340.0, 355.0},
        {411.0, 105.0, 320.0, 40.0},
        {411.0, 145.0, 320.0, 42.0},
        {401.0, 452.0, 340.0, 54.0},
        {405.0, 456.0, 332.0, 50.0},
    };
}

ui::DrawList
UgcEditorLobbyPresentation::build(const UgcEditorLobbyModel& model,
                                  const UgcEditorPresentationContext& context) const {
    validate_context(context);
    ui::DrawList result;
    prepend_background(result, build_layer(model, context.player_name), context);
    return result;
}

ui::DrawList
UgcEditorLobbyPresentation::build_layer(const UgcEditorLobbyModel& model,
                                        std::string_view player_name) const {
    const auto layout = ugc_editor_lobby_classic_layout();
    ui::DrawList list;
    list.reserve(128U);
    list.push(sprite(ugc_editor_assets::frame,
                     layout.frame,
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
    list.push(text("UGC_SQUADS_LOBBY_TITLE",
                   layout.title,
                   44.0,
                   cream,
                   HorizontalTextAlignment::center,
                   TextTransform::uppercase,
                   ugc_editor_assets::title_font));
    append_back(list,
                layout.back,
                "LEAVE_LOBBY",
                model.visual_state(model.controls()[0U].widget.id));

    append_panel(list,
                 layout.members_panel,
                 layout.members_header,
                 model.configuration().map_title);
    if (model.members().empty()) {
        solid(list, layout.first_member_row, row_grey);
        list.push(text(player_name,
                   {layout.first_member_row.x + 14.0,
                    layout.first_member_row.y,
                    215.0,
                    layout.first_member_row.height},
                   12.0));
        list.push(text("HOST",
                   {layout.first_member_row.x + 230.0,
                    layout.first_member_row.y,
                    75.0,
                    layout.first_member_row.height},
                   11.0,
                   selected_green,
                   HorizontalTextAlignment::right));
    } else {
        const auto& members = model.members();
        const auto columns = members.size() > 12U ? 2U : 1U;
        const auto width = layout.first_member_row.width / static_cast<double>(columns);
        for (std::size_t index = 0U; index < members.size(); ++index) {
            const auto& member = members[index];
            const ui::DrawRect row{layout.first_member_row.x + static_cast<double>(index / 12U) * width,
                layout.first_member_row.y + static_cast<double>(index % 12U) * 22.0, width, 22.0};
            solid(list, row, index % 2U == 0U ? row_grey : row_dark);
            list.push(text(member.first, {row.x + 5.0, row.y, width - 35.0, row.height}, 11.0));
            if (member.second) list.push(text("HOST", {row.x + width - 33.0, row.y, 30.0, row.height},
                8.0, selected_green, HorizontalTextAlignment::right));
        }
    }
    append_button(list,
                  layout.invite_button,
                  "INVITE",
                  model.visual_state(model.controls()[1U].widget.id),
                  22.0);

    append_panel(list,
                 layout.settings_panel,
                 layout.settings_header,
                 "UGC_SETTINGS",
                 true);
    const auto definitions = model.settings();
    for (std::size_t row_index = 0U; row_index < definitions.size(); ++row_index) {
        const auto& definition = definitions[row_index];
        const DrawRect row{layout.first_setting_row.x,
                           layout.first_setting_row.y +
                               settings_row_height * static_cast<double>(row_index),
                           layout.first_setting_row.width,
                           settings_row_height};
        const auto control_index = row_index + 3U;
        const auto state = model.visual_state(model.controls()[control_index].widget.id);
        solid(list, row, row_index % 2U == 0U ? row_grey : row_dark);
        if (state == WidgetVisualState::hovered || state == WidgetVisualState::focused ||
            state == WidgetVisualState::pressed) {
            list.push(sprite(ugc_editor_assets::highlight_line,
                             row,
                             DrawSpace::design_pixels,
                             TextureFilter::nearest,
                             UiTextureAnchor::top_left,
                             source_scale));
        }
        list.push(text(definition.label_key,
                       {row.x + 12.0, row.y, 124.0, row.height},
                       12.0,
                       state == WidgetVisualState::disabled ? unavailable : cream,
                       HorizontalTextAlignment::left,
                       TextTransform::preserve,
                       ugc_editor_assets::header_font));
        auto value = model.value_text(definition.id);
        if (definition.editable_text && model.title_editing()) {
            value.push_back('_');
        }
        if (definition.editable_text) {
            solid(list, {row.x + 136.0, row.y + 7.0, 176.0, 28.0}, black);
        }
        list.push(text(value,
                       {row.x + (definition.editable_text ? 142.0 : 162.0), row.y,
                        definition.editable_text ? 164.0 : 124.0, row.height},
                       12.0,
                       state == WidgetVisualState::disabled ? unavailable :
                           (definition.editable_text && model.title_editing() ? selected_green : cream),
                       HorizontalTextAlignment::center));
        if (!definition.editable_text) {
            constexpr auto scale = static_cast<double>(MainMenuModel::subpixels_per_pixel);
            for (const auto direction : {-1, 1}) {
                const auto arrow = ugc_editor_setting_arrow(
                    model.controls()[control_index].widget.bounds, direction);
                append_square_button(list,
                    {arrow.x / scale, arrow.y / scale, arrow.width / scale, arrow.height / scale},
                    direction < 0 ? arrow_left : arrow_right, state != WidgetVisualState::disabled);
            }
        }
    }

    list.push(sprite(ugc_editor_assets::panel,
                     layout.action_background,
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
    append_button(list,
                  layout.start_button,
                  "START_GAME",
                  model.visual_state(model.controls()[2U].widget.id),
                  22.0);
    return list;
}

UgcIngameSettingsClassicLayout ugc_ingame_settings_classic_layout() noexcept {
    // `global_images` truncates source dimensions after multiplying by 0.64.
    // The source frames are 835x820 and 863x906, producing 534x524 and
    // 552x579 before their centered anchors are applied.
    return UgcIngameSettingsClassicLayout{
        {133.0, 28.0, 534.0, 524.0},
        {112.0, 10.0, 552.0, 579.0},
        {250.0, 26.0, 300.0, 80.0},
        {152.0, 105.0, 494.0, 351.0},
        {614.0, 115.0, 22.0, 331.0},
        {160.0, 439.0, 232.0, 41.0},
        {403.0, 439.0, 232.0, 41.0},
    };
}

std::optional<ui::DrawRect> UgcIngameSettingsPresentation::field_bounds(
    const UgcIngameSettingsModel& model,
    UgcIngameSettingField field) const noexcept {
    if (!model.visible()) return std::nullopt;
    for (const auto& visible : visible_ugc_settings_rows(model)) {
        if (visible.row->field.has_value() && *visible.row->field == field) {
            return visible.bounds;
        }
    }
    return std::nullopt;
}

ui::DrawList
UgcIngameSettingsPresentation::build_layer(const UgcIngameSettingsModel& model) const {
    ui::DrawList list;
    if (!model.visible()) return list;

    const auto layout = ugc_ingame_settings_classic_layout();
    list.reserve(150U);
    list.push(sprite(ugc_editor_assets::settings_outer_frame,
                     layout.outer_frame,
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
    list.push(sprite(ugc_editor_assets::settings_content_frame,
                     layout.content_frame,
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
    list.push(text("SETTINGS",
                   layout.title,
                   46.0,
                   cream,
                   HorizontalTextAlignment::center,
                   TextTransform::uppercase,
                   ugc_editor_assets::title_font));

    const auto& draft = model.draft();
    for (const auto& visible : visible_ugc_settings_rows(model)) {
        const auto& row = *visible.row;
        if (row.kind == UgcSettingsRowKind::category) {
            append_ugc_category(list, row, visible.bounds);
            continue;
        }
        list.push(sprite(ugc_editor_assets::settings_row,
                         visible.bounds,
                         DrawSpace::design_pixels,
                         TextureFilter::linear,
                         UiTextureAnchor::center,
                         global_scale));
        append_ugc_row_label(list, row, visible.bounds);
        const auto control = ugc_control_bounds(visible.bounds);
        switch (row.kind) {
        case UgcSettingsRowKind::dropdown:
            append_ugc_dropdown(
                list,
                control,
                row.field == UgcIngameSettingField::skydome
                    ? std::string_view{draft.skydome}
                    : ugc_ingame_mode_name(draft.target_mode));
            break;
        case UgcSettingsRowKind::slider: {
            const auto channel = row.field == UgcIngameSettingField::water_red     ? 0U
                                 : row.field == UgcIngameSettingField::water_green ? 1U
                                                                                   : 2U;
            append_ugc_slider(list, control, draft.water[channel]);
            break;
        }
        case UgcSettingsRowKind::colour_preview:
            append_ugc_colour_preview(list, control, draft.water);
            break;
        case UgcSettingsRowKind::edit_text:
            append_ugc_edit_text(list, control, draft.map_title, model.title_editing());
            break;
        case UgcSettingsRowKind::button:
            append_button(list, control, "SET", WidgetVisualState::normal, 16.0);
            break;
        case UgcSettingsRowKind::category: break;
        }
    }
    append_ugc_scrollbar(list, ugc_first_visible_row(model));
    append_button(list,
                  layout.cancel_button,
                  "CANCEL",
                  WidgetVisualState::normal,
                  30.0);
    append_button(list,
                  layout.apply_button,
                  "APPLY",
                  WidgetVisualState::normal,
                  30.0);
    return list;
}

namespace ugc_editor_assets {

std::span<const MainMenuAsset> required() noexcept {
    static const std::vector<MainMenuAsset> assets = [] {
        const auto shared = custom_match_assets::required();
        std::vector<MainMenuAsset> result{shared.begin(), shared.end()};
        constexpr auto texture = MainMenuAssetKind::texture;
        constexpr auto linear = TextureSampling::linear;
        constexpr auto nearest = TextureSampling::nearest;
        constexpr auto top_left = TextureAnchor::top_left;
        constexpr auto center = TextureAnchor::center;
        result.push_back({settings_outer_frame, texture, linear, center, global_scale});
        result.push_back({settings_content_frame, texture, linear, center, global_scale});
        result.push_back({settings_row, texture, linear, center, global_scale});
        result.push_back({collapse_minus, texture, nearest, center, global_scale});
        result.push_back({arrow_up, texture, nearest, center, 1.0});
        result.push_back({arrow_left, texture, nearest, center, 1.0});
        result.push_back({arrow_right, texture, nearest, center, 1.0});
        result.push_back({bullet_slider, texture, nearest, center, global_scale});
        result.push_back({scrollbar_top, texture, linear, top_left, source_scale});
        result.push_back({scrollbar_mid, texture, linear, top_left, source_scale});
        result.push_back({scrollbar_bottom, texture, linear, top_left, source_scale});
        return result;
    }();
    return assets;
}

} // namespace ugc_editor_assets

} // namespace battlespades::frontend
