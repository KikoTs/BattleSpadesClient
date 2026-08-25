#include "battlespades/frontend/create_match_presentation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
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
using ui::TextureAnchor;
using ui::TextureFilter;
using ui::VerticalTextAlignment;

constexpr ColorRgba8 white{255U, 255U, 255U, 255U};
constexpr ColorRgba8 black{0U, 0U, 0U, 255U};
constexpr ColorRgba8 cream{244U, 236U, 187U, 255U};
constexpr ColorRgba8 green{59U, 68U, 25U, 255U};
constexpr ColorRgba8 row_grey{57U, 53U, 44U, 255U};
constexpr ColorRgba8 row_dark{24U, 21U, 14U, 255U};
constexpr ColorRgba8 control_grey{83U, 83U, 83U, 255U};
constexpr ColorRgba8 control_hover{111U, 105U, 76U, 255U};
constexpr ColorRgba8 selected_gold{215U, 189U, 83U, 255U};
constexpr ColorRgba8 disabled_grey{104U, 104U, 104U, 255U};
constexpr ColorRgba8 player_bar{27U, 30U, 1U, 255U};
constexpr ColorRgba8 scrollbar_rail{73U, 63U, 7U, 255U};
constexpr ColorRgba8 team_one{81U, 171U, 255U, 255U};
constexpr ColorRgba8 team_two{174U, 255U, 2U, 255U};
constexpr ColorRgba8 host_red{255U, 84U, 84U, 255U};

constexpr std::array<std::array<std::string_view, 3U>, 3U> button_assets{{
    {{"png/ui/common_elements/buttons/button_large_left.png",
      "png/ui/common_elements/buttons/button_large_mid.png",
      "png/ui/common_elements/buttons/button_large_right.png"}},
    {{"png/ui/common_elements/buttons/button_large_hover_left.png",
      "png/ui/common_elements/buttons/button_large_hover_mid.png",
      "png/ui/common_elements/buttons/button_large_hover_right.png"}},
    {{"png/ui/common_elements/buttons/button_large_press_left.png",
      "png/ui/common_elements/buttons/button_large_press_mid.png",
      "png/ui/common_elements/buttons/button_large_press_right.png"}},
}};

constexpr std::array<std::array<std::string_view, 3U>, 3U> start_button_assets{{
    {{"png/ui/common_elements/buttons/button_large_start_left_default.png",
      "png/ui/common_elements/buttons/button_large_start_mid_default.png",
      "png/ui/common_elements/buttons/button_large_start_right_default.png"}},
    {{"png/ui/common_elements/buttons/button_large_start_left_hover.png",
      "png/ui/common_elements/buttons/button_large_start_mid_hover.png",
      "png/ui/common_elements/buttons/button_large_start_right_hover.png"}},
    {{"png/ui/common_elements/buttons/button_large_start_left_press.png",
      "png/ui/common_elements/buttons/button_large_start_mid_press.png",
      "png/ui/common_elements/buttons/button_large_start_right_press.png"}},
}};

[[nodiscard]] ColorModulation color(ColorRgba8 value = white,
                                    std::uint16_t intensity = 1'000U,
                                    std::uint16_t opacity = 1'000U) noexcept {
    return ColorModulation{value, intensity, opacity};
}

[[nodiscard]] DrawRect rect(ui::Rect value) noexcept {
    return DrawRect{static_cast<double>(value.x),
                    static_cast<double>(value.y),
                    static_cast<double>(value.width),
                    static_cast<double>(value.height)};
}

[[nodiscard]] SpriteDrawCommand sprite(std::string_view asset,
                                       DrawRect destination,
                                       DrawSpace space = DrawSpace::design_pixels,
                                       TextureAnchor anchor = TextureAnchor::top_left,
                                       double source_scale = 0.6,
                                       ColorModulation modulation = color(),
                                       SpriteSizing sizing = SpriteSizing::stretch) {
    return SpriteDrawCommand{std::string{asset},
                             destination,
                             space,
                             TextureFilter::linear,
                             anchor,
                             source_scale,
                             sizing,
                             modulation};
}

[[nodiscard]] TextDrawCommand text(std::string_view key,
                                   DrawRect destination,
                                   double size,
                                   ColorRgba8 text_color = cream,
                                   HorizontalTextAlignment horizontal =
                                       HorizontalTextAlignment::left,
                                   TextTransform transform = TextTransform::preserve,
                                   std::string_view font =
                                       create_match_presentation_assets::row_font) {
    return TextDrawCommand{std::string{key},
                           std::string{font},
                           destination,
                           DrawSpace::design_pixels,
                           size,
                           1.0,
                           1U,
                           horizontal,
                           VerticalTextAlignment::retail_center,
                           transform,
                           TextFit::shrink_to_fit,
                           color(text_color)};
}

void solid(ui::DrawList& list, DrawRect bounds, ColorRgba8 fill,
           std::uint16_t intensity = 1'000U) {
    list.push(sprite(create_match_presentation_assets::white_pixel,
                     bounds,
                     DrawSpace::design_pixels,
                     TextureAnchor::top_left,
                     1.0,
                     color(fill, intensity)));
}

[[nodiscard]] DrawRect cover(ui::PixelExtent window) noexcept {
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

[[nodiscard]] std::size_t visual_index(CreateMatchVisualState state) noexcept {
    return state == CreateMatchVisualState::hovered ||
                   state == CreateMatchVisualState::focused
               ? 1U
               : 0U;
}

void append_button(ui::DrawList& list, const CreateMatchButtonPresentation& button) {
    const auto bounds = rect(button.bounds);
    constexpr double loaded_height{58.0};
    constexpr double loaded_width{36.0};
    const auto cap = std::floor(bounds.height * loaded_width / loaded_height);
    const auto middle = std::max(0.0, bounds.width - cap * 2.0);
    const auto state = visual_index(button.visual_state);
    const auto& slices = button.stable_key == "START_GAME" ? start_button_assets : button_assets;
    const auto intensity = button.enabled ? std::uint16_t{1'000U} : std::uint16_t{650U};
    const std::array destinations{
        DrawRect{bounds.x, bounds.y, cap + 1.0, bounds.height},
        DrawRect{bounds.x + cap, bounds.y, middle + 1.0, bounds.height},
        DrawRect{bounds.x + cap + middle, bounds.y, cap + 1.0, bounds.height},
    };
    for (std::size_t index{}; index < destinations.size(); ++index) {
        list.push(sprite(slices[state][index],
                         destinations[index],
                         DrawSpace::design_pixels,
                         TextureAnchor::top_left,
                         0.6,
                         color(white, intensity)));
    }
    list.push(text(button.label_key,
                   {bounds.x + 10.0, bounds.y + 4.0, bounds.width - 20.0, bounds.height - 8.0},
                   bounds.height >= 42.0 ? 24.0 : 15.0,
                   ColorRgba8{20U, 20U, 20U, 255U},
                   HorizontalTextAlignment::center,
                   TextTransform::uppercase,
                   create_match_presentation_assets::row_font));
}

void append_navigation_button(ui::DrawList& list,
                              const CreateMatchButtonPresentation& button) {
    const auto bounds = rect(button.bounds);
    const auto highlighted = button.visual_state == CreateMatchVisualState::hovered ||
                             button.visual_state == CreateMatchVisualState::focused;
    const auto intensity = highlighted ? std::uint16_t{1'000U} : std::uint16_t{700U};
    list.push(sprite(create_match_presentation_assets::back_icon,
                     {bounds.x + 2.5, bounds.y + 3.5, 25.0, 25.0},
                     DrawSpace::design_pixels,
                     TextureAnchor::center,
                     0.64,
                     color(white, intensity)));
    list.push(text(button.label_key,
                   {bounds.x + 35.0, bounds.y, bounds.width - 35.0, bounds.height},
                   20.0,
                   selected_gold,
                   HorizontalTextAlignment::left,
                   TextTransform::uppercase,
                   create_match_presentation_assets::title_font));
}

void append_arrow(ui::DrawList& list,
                  DrawRect bounds,
                  bool right,
                  bool enabled,
                  bool highlighted) {
    solid(list, bounds, black, enabled ? 1'000U : 600U);
    list.push(sprite(right ? create_match_presentation_assets::arrow_right
                           : create_match_presentation_assets::arrow_left,
                     bounds,
                     DrawSpace::design_pixels,
                     TextureAnchor::center,
                     1.0,
                     color(highlighted ? selected_gold : white,
                           enabled ? 1'000U : 600U)));
}

void append_row(ui::DrawList& list,
                const CreateMatchRowPresentation& row,
                std::size_t visible_index) {
    const auto bounds = rect(row.bounds);
    const auto disabled = !row.enabled;
    const auto highlighted = row.visual_state == CreateMatchVisualState::hovered ||
                             row.visual_state == CreateMatchVisualState::focused;
    if (row.kind == CreateMatchRowKind::category) {
        solid(list,
              bounds,
              highlighted ? ColorRgba8{80U, 91U, 35U, 255U} : green,
              disabled ? 650U : 1'000U);
        list.push(text(row.label_key,
                       {bounds.x + 10.0, bounds.y, bounds.width - 42.0, bounds.height},
                       15.0,
                       highlighted ? white : cream,
                       HorizontalTextAlignment::left,
                       TextTransform::uppercase));
        const DrawRect icon{bounds.x + bounds.width - 27.0, bounds.y + 5.0, 16.0, 16.0};
        list.push(sprite(row.expanded ? create_match_presentation_assets::collapse_minus
                                      : create_match_presentation_assets::collapse_plus,
                         icon,
                         DrawSpace::design_pixels,
                         TextureAnchor::center,
                         1.0));
        return;
    }

    if (row.kind == CreateMatchRowKind::selectable_item) {
        solid(list, bounds, visible_index % 2U == 0U ? row_grey : row_dark,
              disabled ? 650U : 1'000U);
        if (row.selected) {
            list.push(sprite(create_match_presentation_assets::selection_line,
                             bounds,
                             DrawSpace::design_pixels,
                             TextureAnchor::top_left,
                             0.6));
            list.push(sprite(create_match_presentation_assets::selection_glow,
                             {bounds.x - bounds.width * 0.025,
                              bounds.y - bounds.height * 0.15,
                              bounds.width * 1.05,
                              bounds.height * 1.3},
                             DrawSpace::design_pixels,
                             TextureAnchor::top_left,
                             0.6));
        }
        if (highlighted && !row.selected) {
            list.push(sprite(create_match_presentation_assets::selection_glow,
                             {bounds.x - bounds.width * 0.015,
                              bounds.y - bounds.height * 0.1,
                              bounds.width * 1.03,
                              bounds.height * 1.2},
                             DrawSpace::design_pixels,
                             TextureAnchor::top_left,
                             0.6));
        }
        list.push(text(row.label_key,
                       {bounds.x + 12.0, bounds.y, bounds.width - 24.0, bounds.height},
                       15.0,
                       disabled ? disabled_grey : highlighted ? white : cream,
                       HorizontalTextAlignment::left,
                       TextTransform::preserve));
        return;
    }

    const auto match_setting = bounds.height >= 30.0;
    if (match_setting) {
        list.push(sprite(create_match_presentation_assets::row_frame,
                         bounds,
                         DrawSpace::design_pixels,
                         TextureAnchor::center,
                         0.64,
                         color(white, disabled ? 650U : 1'000U)));
        const auto label_width = bounds.width / 3.0;
        list.push(text(row.label_key,
                       {bounds.x + 10.0, bounds.y, label_width, bounds.height},
                       14.0,
                       disabled ? disabled_grey : highlighted ? white : cream,
                       HorizontalTextAlignment::center,
                       TextTransform::uppercase,
                       create_match_presentation_assets::title_font));
    } else {
        solid(list,
              bounds,
              visible_index % 2U == 0U ? row_grey : row_dark,
              disabled ? 650U : 1'000U);
        list.push(text(row.label_key,
                       {bounds.x + 14.0,
                        bounds.y,
                        std::max(0.0, static_cast<double>(row.control_bounds.x) - bounds.x - 24.0),
                        bounds.height},
                       12.0,
                       disabled ? disabled_grey : cream,
                       HorizontalTextAlignment::left,
                       TextTransform::preserve));
    }
    const auto control = rect(row.control_bounds);

    if (row.kind == CreateMatchRowKind::toggle) {
        const DrawRect checkbox{control.x, control.y, control.width, control.height};
        solid(list, checkbox, black, disabled ? 650U : 1'000U);
        list.push(sprite(create_match_presentation_assets::checkbox,
                         checkbox,
                         DrawSpace::design_pixels,
                         TextureAnchor::center,
                         1.0,
                         color(row.selected ? selected_gold : white,
                               disabled ? 650U : 1'000U)));
        return;
    }

    if (row.kind == CreateMatchRowKind::menu_link) {
        solid(list, control, black, disabled ? 650U : 1'000U);
        constexpr double spacing{4.0};
        const auto button_size = control.height - spacing * 2.0;
        const DrawRect value{control.x + spacing,
                             control.y + spacing,
                             control.width - button_size - spacing * 3.0,
                             button_size};
        solid(list,
              value,
              highlighted ? control_hover : control_grey,
              disabled ? 650U : 1'000U);
        list.push(text(row.value_text,
                       {value.x + 6.0, value.y, value.width - 12.0, value.height},
                       12.0,
                       disabled ? disabled_grey : cream,
                       HorizontalTextAlignment::center,
                       TextTransform::preserve));
        const DrawRect edit{control.x + control.width - spacing - button_size,
                            control.y + spacing,
                            button_size,
                            button_size};
        list.push(sprite(highlighted
                             ? create_match_presentation_assets::square_button_hover
                             : create_match_presentation_assets::square_button,
                         edit,
                         DrawSpace::design_pixels,
                         TextureAnchor::center,
                         0.6,
                         color(white, disabled ? 650U : 1'000U)));
        list.push(sprite(create_match_presentation_assets::edit_icon,
                         edit,
                         DrawSpace::design_pixels,
                         TextureAnchor::center,
                         0.64,
                         color(white, disabled ? 650U : 1'000U)));
        return;
    }

    solid(list, control, black, disabled ? 650U : 1'000U);
    constexpr double padding{3.0};
    const auto arrow_size = control.height - padding * 2.0;
    append_arrow(list,
                 {control.x + padding, control.y + padding, arrow_size, arrow_size},
                 false,
                 row.enabled && row.value_count > 1U,
                 highlighted);
    append_arrow(list,
                 {control.x + control.width - padding - arrow_size,
                  control.y + padding,
                  arrow_size,
                  arrow_size},
                 true,
                 row.enabled && row.value_count > 1U,
                 highlighted);
    const DrawRect value{control.x + arrow_size + padding * 2.0,
                         control.y + padding,
                         control.width - arrow_size * 2.0 - padding * 4.0,
                         arrow_size};
    solid(list,
          value,
          highlighted ? control_hover : control_grey,
          disabled ? 650U : 1'000U);
    list.push(text(row.value_text,
                   value,
                   11.0,
                   disabled ? disabled_grey : cream,
                   HorizontalTextAlignment::center,
                   TextTransform::preserve));
}

void append_scrollbar(ui::DrawList& list, const CreateMatchMenuPresentation& snapshot) {
    const auto bounds = rect(snapshot.scrollbar_bounds);
    constexpr double arrow{22.0};
    const DrawRect track{bounds.x + 1.0,
                         bounds.y + arrow + 2.0,
                         bounds.width - 2.0,
                         bounds.height - arrow * 2.0 - 4.0};
    solid(list, bounds, black);
    solid(list, track, scrollbar_rail);
    list.push(sprite(create_match_presentation_assets::arrow_up,
                     {bounds.x, bounds.y, bounds.width, arrow},
                     DrawSpace::design_pixels,
                     TextureAnchor::center,
                     1.0,
                     color(white, snapshot.first_visible_row > 0U ? 1'000U : 600U)));
    list.push(sprite(create_match_presentation_assets::arrow_down,
                     {bounds.x, bounds.y + bounds.height - arrow, bounds.width, arrow},
                     DrawSpace::design_pixels,
                     TextureAnchor::center,
                     1.0,
                     color(white,
                           snapshot.first_visible_row < snapshot.maximum_scroll ? 1'000U : 600U)));
    const auto total = snapshot.maximum_scroll + snapshot.rows.size();
    const auto thumb_height = std::clamp(
        std::floor(track.height * static_cast<double>(snapshot.rows.size()) /
                   static_cast<double>(std::max<std::size_t>(1U, total))),
        18.0,
        track.height);
    const auto ratio = snapshot.maximum_scroll == 0U
                           ? 0.0
                           : static_cast<double>(snapshot.first_visible_row) /
                                 static_cast<double>(snapshot.maximum_scroll);
    const auto thumb_y = track.y + (track.height - thumb_height) * ratio;
    constexpr double cap{4.0};
    list.push(sprite(create_match_presentation_assets::scrollbar_top,
                     {track.x, thumb_y, track.width, cap}));
    list.push(sprite(create_match_presentation_assets::scrollbar_mid,
                     {track.x, thumb_y + cap, track.width, std::max(0.0, thumb_height - cap * 2.0)}));
    list.push(sprite(create_match_presentation_assets::scrollbar_bottom,
                     {track.x, thumb_y + thumb_height - cap, track.width, cap}));
}

void append_players(ui::DrawList& list, const CreateMatchMenuPresentation& snapshot) {
    list.push(sprite(create_match_presentation_assets::panel_frame,
                     rect(snapshot.player_panel),
                     DrawSpace::design_pixels,
                     TextureAnchor::center,
                     0.6));
    const auto header = rect(snapshot.player_header);
    list.push(sprite(create_match_presentation_assets::row_frame,
                     header,
                     DrawSpace::design_pixels,
                     TextureAnchor::center,
                     0.64));
    solid(list, rect(snapshot.lobby_name_field), ColorRgba8{45U, 45U, 45U, 255U});
    list.push(text(snapshot.lobby_name,
                   {static_cast<double>(snapshot.lobby_name_field.x) + 4.0,
                    static_cast<double>(snapshot.lobby_name_field.y),
                    static_cast<double>(snapshot.lobby_name_field.width) - 8.0,
                    static_cast<double>(snapshot.lobby_name_field.height)},
                   14.0,
                   cream,
                   HorizontalTextAlignment::left));
    auto y = 155.0;
    const auto visible = std::min<std::size_t>(8U, snapshot.players.size());
    for (std::size_t index{}; index < visible; ++index) {
        const auto& player = snapshot.players[index];
        const DrawRect row{66.0, y, 320.0, 25.0};
        solid(list, row, index % 2U == 0U ? row_grey : row_dark);
        auto team_color = cream;
        if (player.team_key == "TEAM1_COLOR") team_color = team_one;
        else if (player.team_key == "TEAM2_COLOR") team_color = team_two;
        list.push(text(player.display_name,
                       {row.x + 14.0, row.y, 120.0, row.height},
                       12.0,
                       team_color));
        if (player.host) {
            list.push(sprite(create_match_presentation_assets::host_icon,
                             {row.x + 141.0, row.y + 5.0, 15.0, 15.0},
                             DrawSpace::design_pixels,
                             TextureAnchor::center,
                             0.64,
                             color(host_red)));
        }
        if (player.in_game) {
            list.push(text("IN_GAME",
                           {row.x + 163.0, row.y, 42.0, row.height},
                           9.0,
                           host_red,
                           HorizontalTextAlignment::left,
                           TextTransform::uppercase));
        }
        const DrawRect team_box{row.x + row.width - 109.0, row.y + 2.5, 100.0, 20.0};
        solid(list, team_box, black);
        list.push(text(player.team_key,
                       {team_box.x + 7.0, team_box.y, team_box.width - 27.0, team_box.height},
                       9.0,
                       team_color,
                       HorizontalTextAlignment::left,
                       TextTransform::uppercase));
        list.push(sprite(create_match_presentation_assets::arrow_down,
                         {team_box.x + team_box.width - 20.0, team_box.y, 20.0, 20.0},
                         DrawSpace::design_pixels,
                         TextureAnchor::center,
                         1.0));
        y += 25.0;
    }
    solid(list, rect(snapshot.player_count_bar), player_bar);
    const std::array teams{
        std::pair{std::string_view{"TEAM1_COLOR"}, team_one},
        std::pair{std::string_view{"TEAM_NEUTRAL"}, cream},
        std::pair{std::string_view{"TEAM2_COLOR"}, team_two},
    };
    auto team_x = static_cast<double>(snapshot.player_count_bar.x) + 14.0;
    for (const auto& [key, team_color] : teams) {
        const auto count = static_cast<std::size_t>(std::count_if(
            snapshot.players.begin(), snapshot.players.end(),
            [&](const auto& player) { return player.team_key == key; }));
        list.push(sprite(create_match_presentation_assets::head_icon,
                         {team_x, static_cast<double>(snapshot.player_count_bar.y) + 3.0, 14.0, 14.0},
                         DrawSpace::design_pixels,
                         TextureAnchor::center,
                         0.64));
        list.push(sprite(create_match_presentation_assets::head_color_icon,
                         {team_x, static_cast<double>(snapshot.player_count_bar.y) + 3.0, 14.0, 14.0},
                         DrawSpace::design_pixels,
                         TextureAnchor::center,
                         0.64,
                         color(team_color)));
        list.push(text(std::to_string(count),
                       {team_x + 12.0,
                        static_cast<double>(snapshot.player_count_bar.y),
                        14.0,
                        static_cast<double>(snapshot.player_count_bar.height)},
                       9.0,
                       team_color,
                       HorizontalTextAlignment::right));
        team_x += 33.0;
    }
    const auto count = std::to_string(snapshot.players.size()) + " PLAYERS";
    list.push(sprite(create_match_presentation_assets::player_count_icon,
                     {369.0,
                      static_cast<double>(snapshot.player_count_bar.y) + 4.0,
                      12.0,
                      12.0},
                     DrawSpace::design_pixels,
                     TextureAnchor::center,
                     0.64));
    list.push(text(count,
                   {290.0,
                    static_cast<double>(snapshot.player_count_bar.y),
                    70.0,
                    static_cast<double>(snapshot.player_count_bar.height)},
                   11.0,
                   white,
                   HorizontalTextAlignment::right));
    list.push(sprite(create_match_presentation_assets::panel_frame,
                     rect(snapshot.chat_panel),
                     DrawSpace::design_pixels,
                     TextureAnchor::center,
                     0.6));
    solid(list,
          {static_cast<double>(snapshot.chat_panel.x) + 6.0,
           static_cast<double>(snapshot.chat_input.y),
           static_cast<double>(snapshot.chat_panel.width) - 10.0,
           22.0},
          ColorRgba8{34U, 32U, 33U, 255U});
    list.push(text("CHAT_MESSAGE",
                   rect(snapshot.chat_input),
                   11.0,
                   disabled_grey,
                   HorizontalTextAlignment::left));
}

void validate(const CreateMatchMenuPresentation& snapshot,
              const CreateMatchPresentationContext& context) {
    if (!context.window.is_valid()) {
        throw std::invalid_argument{"Create Match presentation requires a valid window"};
    }
    if (context.background_opacity_per_mille > 1'000U) {
        throw std::invalid_argument{"Create Match background opacity exceeds one"};
    }
    if (snapshot.first_visible_row > snapshot.first_visible_row + snapshot.maximum_scroll) {
        throw std::invalid_argument{"Create Match scroll state overflowed"};
    }
    for (const auto& row : snapshot.rows) {
        if (!row.bounds.is_valid() || !row.control_bounds.is_valid()) {
            throw std::invalid_argument{"Create Match row has invalid geometry"};
        }
        if (row.value_count > 0U && row.value_index >= row.value_count) {
            throw std::invalid_argument{"Create Match row value index is out of range"};
        }
    }
}

} // namespace

ui::DrawList CreateMatchPresentation::build(
    const CreateMatchMenuPresentation& snapshot,
    const CreateMatchPresentationContext& context) const {
    validate(snapshot, context);
    ui::DrawList list;
    list.reserve(180U);
    list.push(sprite(create_match_presentation_assets::background,
                     cover(context.window),
                     DrawSpace::window_pixels,
                     TextureAnchor::top_left,
                     0.6,
                     color(white, 1'000U, context.background_opacity_per_mille),
                     SpriteSizing::cover));
    list.push(sprite(create_match_presentation_assets::outer_frame,
                     rect(snapshot.outer_frame),
                     DrawSpace::design_pixels,
                     TextureAnchor::center,
                     0.6));
    list.push(text(snapshot.title_key,
                   rect(snapshot.title_bounds),
                   40.0,
                   cream,
                   HorizontalTextAlignment::center,
                   TextTransform::uppercase,
                   create_match_presentation_assets::row_font));

    append_players(list, snapshot);
    // Retail's right-side LobbyPanel owns a dark-green canvas independently
    // of the large outer frame. Rows are drawn over this surface; leaving it
    // transparent exposes the much brighter frame texture underneath.
    solid(list, rect(snapshot.content_panel), player_bar);
    if (snapshot.show_content_frame) {
        list.push(sprite(create_match_presentation_assets::panel_frame,
                         rect(snapshot.content_panel),
                         DrawSpace::design_pixels,
                         TextureAnchor::center,
                         0.6));
    }
    const auto content_header = rect(snapshot.content_header);
    list.push(sprite(create_match_presentation_assets::row_frame,
                     content_header,
                     DrawSpace::design_pixels,
                     TextureAnchor::center,
                     0.64));
    list.push(text(snapshot.panel_title_key,
                   {content_header.x + 10.0,
                    content_header.y,
                    content_header.width - 20.0,
                    content_header.height},
                   18.0,
                   cream,
                   HorizontalTextAlignment::center,
                   TextTransform::uppercase,
                   create_match_presentation_assets::title_font));

    for (std::size_t index{}; index < snapshot.rows.size(); ++index) {
        append_row(list, snapshot.rows[index], index);
    }
    if (snapshot.show_scrollbar) append_scrollbar(list, snapshot);
    if (snapshot.show_defaults_help) {
        solid(list, rect(snapshot.defaults_help), black);
        list.push(text("RESET_OPTIONS",
                       rect(snapshot.defaults_help),
                       10.0,
                       cream,
                       HorizontalTextAlignment::center));
    }
    list.push(sprite(create_match_presentation_assets::panel_frame,
                     rect(snapshot.action_panel),
                     DrawSpace::design_pixels,
                     TextureAnchor::center,
                     0.6));
    for (const auto& button : snapshot.buttons) {
        if (button.stable_key == "BACK") append_navigation_button(list, button);
        else append_button(list, button);
    }
    return list;
}

} // namespace battlespades::frontend
