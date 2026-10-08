#include "battlespades/frontend/player_profile_presentation.hpp"
#include "battlespades/frontend/inventory_menu.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
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
using ui::TextureAnchor;
using ui::TextureFilter;
using ui::VerticalTextAlignment;

constexpr ColorRgba8 white{255U, 255U, 255U, 255U};
constexpr ColorRgba8 cream{244U, 236U, 187U, 255U};
constexpr ColorRgba8 gold{232U, 207U, 78U, 255U};
constexpr ColorRgba8 button_text{20U, 20U, 20U, 255U};
constexpr ColorRgba8 red{162U, 58U, 30U, 255U};
constexpr ColorRgba8 row_light{57U, 53U, 44U, 255U};
constexpr ColorRgba8 row_dark{24U, 21U, 14U, 255U};
constexpr ColorRgba8 menu_option{34U, 32U, 33U, 255U};
constexpr ColorRgba8 scrollbar_track{73U, 63U, 7U, 255U};
constexpr ColorRgba8 level_behind{86U, 100U, 21U, 255U};
constexpr ColorRgba8 level_front{137U, 179U, 45U, 255U};
constexpr ColorRgba8 black{0U, 0U, 0U, 255U};

constexpr double row_height{20.0};
constexpr double scrollbar_button_size{22.0};

[[nodiscard]] ColorModulation color(ColorRgba8 value = white,
                                    std::uint16_t opacity = 1'000U) noexcept {
    return {value, 1'000U, opacity};
}

[[nodiscard]] SpriteDrawCommand sprite(std::string_view asset,
                                       DrawRect bounds,
                                       DrawSpace space = DrawSpace::design_pixels,
                                       TextureAnchor anchor = TextureAnchor::top_left,
                                       double scale = 0.6,
                                       ColorModulation modulation = color(),
                                       SpriteSizing sizing = SpriteSizing::stretch,
                                       TextureFilter sampling = TextureFilter::linear) {
    return {std::string{asset}, bounds, space, sampling, anchor, scale, sizing, modulation};
}

[[nodiscard]] TextDrawCommand
text(std::string_view key,
     DrawRect bounds,
     double size,
     ColorRgba8 text_color = cream,
     HorizontalTextAlignment alignment = HorizontalTextAlignment::left,
     std::string_view font = player_profile_presentation_assets::row_font,
     TextTransform transform = TextTransform::preserve) {
    return {std::string{key},
            std::string{font},
            bounds,
            DrawSpace::design_pixels,
            size,
            1.0,
            1U,
            alignment,
            VerticalTextAlignment::retail_center,
            transform,
            TextFit::shrink_to_fit,
            color(text_color)};
}

void solid(ui::DrawList& list, DrawRect bounds, ColorRgba8 fill) {
    list.push(sprite(player_profile_presentation_assets::white_pixel,
                     bounds,
                     DrawSpace::design_pixels,
                     TextureAnchor::top_left,
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
    return {(width - covered_width) * 0.5,
            (height - covered_height) * 0.5,
            covered_width,
            covered_height};
}

[[nodiscard]] std::array<std::string_view, 3U>
button_parts(PlayerProfilePresentationContext::ControlState state) noexcept {
    using State = PlayerProfilePresentationContext::ControlState;
    if (state == State::pressed) {
        return {"png/ui/common_elements/buttons/button_large_press_left.png",
                "png/ui/common_elements/buttons/button_large_press_mid.png",
                "png/ui/common_elements/buttons/button_large_press_right.png"};
    }
    if (state == State::hovered) {
        return {"png/ui/common_elements/buttons/button_large_hover_left.png",
                "png/ui/common_elements/buttons/button_large_hover_mid.png",
                "png/ui/common_elements/buttons/button_large_hover_right.png"};
    }
    return {"png/ui/common_elements/buttons/button_large_left.png",
            "png/ui/common_elements/buttons/button_large_mid.png",
            "png/ui/common_elements/buttons/button_large_right.png"};
}

void append_button(ui::DrawList& list,
                   DrawRect bounds,
                   std::string_view label,
                   PlayerProfilePresentationContext::ControlState state) {
    const auto parts = button_parts(state);
    // The retail textures become 36x58 after their 0.6 loader scale. TextButton
    // requests a 60-pixel control and truncates 36 / 58 * 60 to 37-pixel caps.
    constexpr double cap{37.0};
    list.push(sprite(parts[0], {bounds.x, bounds.y, cap + 1.0, bounds.height}));
    list.push(sprite(parts[1],
                     {bounds.x + cap, bounds.y, bounds.width - cap * 2.0 + 1.0, bounds.height}));
    list.push(
        sprite(parts[2], {bounds.x + bounds.width - cap, bounds.y, cap + 1.0, bounds.height}));
    const auto pressed_offset =
        state == PlayerProfilePresentationContext::ControlState::pressed ? 2.0 : 0.0;
    list.push(text(label,
                   {bounds.x + 14.0,
                    bounds.y + 4.0 + pressed_offset,
                    bounds.width - 28.0,
                    bounds.height - 8.0},
                   36.0,
                   button_text,
                   HorizontalTextAlignment::center,
                   player_profile_presentation_assets::title_font,
                   TextTransform::uppercase));
}

[[nodiscard]] std::string ratio_text(double ratio) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << ratio;
    return stream.str();
}

[[nodiscard]] std::string number_text(double value) {
    std::ostringstream stream;
    if (std::floor(value) == value) {
        stream << static_cast<long long>(value);
    } else {
        stream << value;
    }
    return stream.str();
}

void append_square_button(ui::DrawList& list,
                          DrawRect bounds,
                          std::string_view icon,
                          PlayerProfilePresentationContext::ControlState state,
                          bool enabled = true) {
    using State = PlayerProfilePresentationContext::ControlState;
    const auto background =
        state == State::pressed   ? player_profile_presentation_assets::square_button_press
        : state == State::hovered ? player_profile_presentation_assets::square_button_hover
                                  : player_profile_presentation_assets::square_button;
    const auto tint = enabled ? white : ColorRgba8{179U, 179U, 179U, 255U};
    const auto pressed_offset = state == State::pressed ? 2.0 : 0.0;
    list.push(sprite(background,
                     {bounds.x, bounds.y + pressed_offset, bounds.width, bounds.height},
                     DrawSpace::design_pixels,
                     TextureAnchor::top_left,
                     1.0,
                     color(tint),
                     SpriteSizing::stretch,
                     TextureFilter::nearest));
    list.push(sprite(icon,
                     {bounds.x, bounds.y + pressed_offset, bounds.width, bounds.height},
                     DrawSpace::design_pixels,
                     TextureAnchor::center,
                     1.0,
                     color(tint),
                     SpriteSizing::stretch,
                     TextureFilter::nearest));
}

void append_scrollbar(ui::DrawList& list,
                      DrawRect bounds,
                      std::size_t total_rows,
                      std::size_t visible_rows,
                      std::size_t first_visible) {
    solid(list, bounds, black);
    const auto full_length = std::max(0.0, bounds.height - scrollbar_button_size * 2.0 - 4.0);
    solid(list, {bounds.x + 1.0, bounds.y + 24.0, 20.0, full_length}, scrollbar_track);

    const auto maximum = total_rows > visible_rows ? total_rows - visible_rows : 0U;
    append_square_button(list,
                         {bounds.x, bounds.y, scrollbar_button_size, scrollbar_button_size},
                         player_profile_presentation_assets::arrow_up,
                         PlayerProfilePresentationContext::ControlState::normal,
                         first_visible > 0U);
    append_square_button(list,
                         {bounds.x,
                          bounds.y + bounds.height - scrollbar_button_size,
                          scrollbar_button_size,
                          scrollbar_button_size},
                         player_profile_presentation_assets::arrow_down,
                         PlayerProfilePresentationContext::ControlState::normal,
                         first_visible < maximum);

    auto thumb_length = total_rows == 0U
                            ? full_length
                            : std::floor(full_length * static_cast<double>(visible_rows) /
                                         static_cast<double>(total_rows));
    thumb_length = std::clamp(thumb_length, std::min(40.0, full_length), full_length);
    const auto travel = full_length - thumb_length;
    const auto progress = maximum == 0U ? 0.0
                                        : static_cast<double>(std::min(first_visible, maximum)) /
                                              static_cast<double>(maximum);
    const auto thumb_top = bounds.y + 25.0 + travel * progress;
    constexpr double cap{20.0};
    if (thumb_length >= cap * 2.0) {
        list.push(sprite(player_profile_presentation_assets::scrollbar_top,
                         {bounds.x + 1.0, thumb_top, 20.0, cap},
                         DrawSpace::design_pixels,
                         TextureAnchor::center,
                         0.6,
                         color(),
                         SpriteSizing::stretch,
                         TextureFilter::nearest));
        // At the minimum thumb size the two caps touch. A zero-height middle
        // is not a drawable quad and would reject the entire frontend frame.
        if (thumb_length > cap * 2.0) {
            list.push(sprite(player_profile_presentation_assets::scrollbar_mid,
                             {bounds.x + 1.0, thumb_top + cap, 20.0, thumb_length - cap * 2.0},
                             DrawSpace::design_pixels,
                             TextureAnchor::center,
                             0.6,
                             color(),
                             SpriteSizing::stretch,
                             TextureFilter::nearest));
        }
        list.push(sprite(player_profile_presentation_assets::scrollbar_bottom,
                         {bounds.x + 1.0, thumb_top + thumb_length - cap, 20.0, cap},
                         DrawSpace::design_pixels,
                         TextureAnchor::center,
                         0.6,
                         color(),
                         SpriteSizing::stretch,
                         TextureFilter::nearest));
    }
}

void append_red_header(ui::DrawList& list, DrawRect bounds) {
    constexpr double cap{24.0};
    list.push(sprite(player_profile_presentation_assets::red_header_left,
                     {bounds.x, bounds.y, cap, bounds.height},
                     DrawSpace::design_pixels,
                     TextureAnchor::center,
                     0.6,
                     color(),
                     SpriteSizing::stretch,
                     TextureFilter::nearest));
    solid(list, {bounds.x + cap, bounds.y, bounds.width - cap * 2.0, bounds.height}, red);
    list.push(sprite(player_profile_presentation_assets::red_header_right,
                     {bounds.x + bounds.width - cap, bounds.y, cap, bounds.height},
                     DrawSpace::design_pixels,
                     TextureAnchor::center,
                     0.6,
                     color(),
                     SpriteSizing::stretch,
                     TextureFilter::nearest));
}

void append_two_column_text(ui::DrawList& list,
                            DrawRect row,
                            std::string_view left,
                            std::string_view right,
                            std::string_view font,
                            double font_size,
                            TextTransform transform = TextTransform::preserve) {
    const auto column_width = row.width * 0.5;
    const auto padding = column_width / 24.0;
    list.push(text(left,
                   {row.x + padding, row.y, column_width - padding * 2.0 - 1.0, row.height},
                   font_size,
                   cream,
                   HorizontalTextAlignment::left,
                   font,
                   transform));
    list.push(
        text(right,
             {row.x + column_width + padding, row.y, column_width - padding * 2.0, row.height},
             font_size,
             cream,
             HorizontalTextAlignment::left,
             font,
             transform));
}

void append_progress(ui::DrawList& list, DrawRect row, const PlayerProfileRow& value) {
    const auto column_width = row.width * 0.5;
    constexpr double horizontal_padding{2.0};
    const auto bar = DrawRect{row.x + column_width + horizontal_padding,
                              row.y + 2.0,
                              column_width - horizontal_padding * 2.0 - 1.0,
                              row.height - 4.0};
    auto fraction = value.level_progress.value_or(0.0);
    if (value.level_details.has_value()) {
        const auto& details = *value.level_details;
        const auto range = details.next_level_max - details.next_level_min;
        fraction = range == 0.0 ? 0.0 : (details.current - details.next_level_min) / range;
    }
    fraction = std::clamp(fraction, 0.0, 1.0);
    solid(list, bar, level_behind);
    // New players and exact level boundaries have an empty bar. Keep the
    // background and labels while omitting its non-drawable zero-width fill.
    if (fraction > 0.0) {
        solid(list, {bar.x, bar.y, bar.width * fraction, bar.height}, level_front);
    }
    if (value.level_details.has_value()) {
        const auto& details = *value.level_details;
        const auto margin = column_width / 24.0;
        list.push(text("LEVEL " + std::to_string(details.level),
                       {bar.x + margin, row.y, 92.0, row.height},
                       11.0));
        list.push(text(number_text(details.current) + " / " + number_text(details.next_level_max),
                       {bar.x + bar.width - 100.0, row.y, 100.0 - margin, row.height},
                       11.0,
                       cream,
                       HorizontalTextAlignment::right));
    } else if (!value.value.empty()) {
        list.push(text(value.value,
                       {bar.x + bar.width - 100.0, row.y, 96.0, row.height},
                       11.0,
                       cream,
                       HorizontalTextAlignment::right));
    }
}

void append_filter(ui::DrawList& list,
                   const PlayerProfileMenuModel& model,
                   const PlayerProfileClassicLayout& layout,
                   PlayerProfilePresentationContext::ControlState button_state) {
    if (!model.filter_visible()) {
        return;
    }
    const auto& definition = player_profile_tab_definition(model.selected_tab());
    const auto selected = model.selected_filter();
    const auto selected_key =
        selected == 0U ? std::string_view{"ALL"} : definition.filter_keys[selected - 1U];

    solid(list, layout.filter_dropdown, black);
    solid(list,
          {layout.filter_dropdown.x + 4.0,
           layout.filter_dropdown.y + 4.0,
           168.0,
           layout.filter_dropdown.height - 8.0},
          menu_option);
    list.push(text(selected_key,
                   {layout.filter_dropdown.x + 14.0,
                    layout.filter_dropdown.y,
                    148.0,
                    layout.filter_dropdown.height},
                   14.0,
                   cream,
                   HorizontalTextAlignment::left,
                   player_profile_presentation_assets::title_font,
                   TextTransform::uppercase));
    append_square_button(
        list,
        {layout.filter_dropdown.x + 174.0, layout.filter_dropdown.y + 4.0, 22.0, 22.0},
        player_profile_presentation_assets::arrow_down,
        button_state);

    if (!model.filter_open()) {
        return;
    }

    const auto option_count = definition.filter_keys.size() + 1U;
    const DrawRect option_area{layout.filter_dropdown.x,
                               layout.filter_dropdown.y + layout.filter_dropdown.height + 1.0,
                               layout.filter_dropdown.width,
                               row_height * static_cast<double>(option_count)};
    solid(list, option_area, black);
    for (std::size_t option = 0U; option < option_count; ++option) {
        const auto key =
            option == 0U ? std::string_view{"ALL"} : definition.filter_keys[option - 1U];
        const DrawRect row{option_area.x,
                           option_area.y + static_cast<double>(option) * row_height,
                           option_area.width,
                           row_height};
        solid(list, row, option % 2U == 0U ? row_light : row_dark);
        list.push(text(key,
                       {row.x + 14.0, row.y, row.width - 28.0, row.height},
                       11.0,
                       cream,
                       HorizontalTextAlignment::left,
                       player_profile_presentation_assets::row_font));
    }
    append_scrollbar(
        list,
        {option_area.x + option_area.width - 22.0, option_area.y, 22.0, option_area.height},
        option_count,
        option_count,
        0U);
}

} // namespace

PlayerProfileClassicLayout player_profile_classic_layout() noexcept {
    // load_texture truncates dimensions after applying the retail 0.6 scale,
    // then installs Python-2 integer center anchors.
    return PlayerProfileClassicLayout{
        {130.0, 28.0, 540.0, 543.0},
        {166.0, 146.0, 464.0, 308.0},
        {250.0, 11.0, 300.0, 80.0},
        {152.0, 100.0, 496.0, 33.0},
        {160.0, 147.0, 200.0, 20.0},
        {400.0, 147.0, 210.0, 20.0},
        {420.0, 143.0, 200.0, 30.0},
        {162.0, 185.0, 476.0, 260.0},
        {162.0, 185.0, 476.0, 263.0},
        {614.0, 185.0, 22.0, 260.0},
        {614.0, 185.0, 22.0, 263.0},
        {152.0, 492.0, 240.0, 60.0},
        {405.0, 492.0, 240.0, 60.0},
    };
}

ui::DrawList
PlayerProfilePresentation::build(const PlayerProfileMenuModel& model,
                                 const PlayerProfilePresentationContext& context) const {
    if (context.window.width <= 0 || context.window.height <= 0 ||
        context.background_opacity_per_mille > 1'000U) {
        throw std::invalid_argument{"invalid player profile presentation context"};
    }
    const auto layout = player_profile_classic_layout();
    ui::DrawList list;
    list.reserve(150U);
    list.push(sprite(player_profile_presentation_assets::background,
                     background_cover(context.window),
                     DrawSpace::window_pixels,
                     TextureAnchor::top_left,
                     0.6,
                     color(white, context.background_opacity_per_mille),
                     SpriteSizing::cover));
    list.push(sprite(player_profile_presentation_assets::outer_frame,
                     model.selected_tab() == PlayerProfileTab::inventory ? DrawRect{28.0,28.0,744.0,543.0} : layout.outer_frame,
                     DrawSpace::design_pixels,
                     TextureAnchor::center));
    list.push(sprite(player_profile_presentation_assets::content_frame,
                     model.selected_tab() == PlayerProfileTab::inventory ? DrawRect{50.0,137.0,700.0,343.0} : layout.content_frame,
                     DrawSpace::design_pixels,
                     TextureAnchor::center));
    list.push(text("PLAYER_PROFILE",
                   layout.title,
                   46.0,
                   cream,
                   HorizontalTextAlignment::center,
                   player_profile_presentation_assets::title_font,
                   TextTransform::uppercase));

    constexpr double tab_width{96.0};
    constexpr double tab_height{24.0};
    constexpr double tab_start{155.0};
    constexpr double tab_end{551.0};
    const auto tabs = player_profile_tab_definitions();
    for (std::size_t tab = 0U; tab < tabs.size(); ++tab) {
        const auto x = tab_start + static_cast<double>(tab) * (tab_end - tab_start) /
                                       static_cast<double>(tabs.size() - 1U);
        const auto image_center = static_cast<double>(static_cast<int>(x + tab_width * 0.5));
        const auto selected = static_cast<std::size_t>(model.selected_tab()) == tab;
        list.push(sprite(selected ? player_profile_presentation_assets::tab_active
                                  : player_profile_presentation_assets::tab_inactive,
                         {image_center - 48.0, 102.0, 96.0, 33.0},
                         DrawSpace::design_pixels,
                         TextureAnchor::center));
        list.push(text(tabs[tab].label_key,
                       {x, 106.0, tab_width, tab_height},
                       16.0,
                       selected ? gold : cream,
                       HorizontalTextAlignment::center,
                       player_profile_presentation_assets::tab_font));
    }

    if (model.selected_tab() == PlayerProfileTab::inventory) return list;

    const auto summary = model.selected_tab() == PlayerProfileTab::player_stats;
    const auto rows = model.displayed_rows();
    const auto capacity = model.visible_row_capacity();
    const auto total_panel_rows = rows.size() + (summary ? 1U : 0U);
    const auto& scroll_bounds = summary ? layout.summary_scrollbar : layout.scrollbar;
    append_scrollbar(list,
                     scroll_bounds,
                     total_panel_rows,
                     capacity + (summary ? 1U : 0U),
                     model.first_visible_row());

    if (model.state() == PlayerProfileLoadState::loading) {
        list.push(text("CONNECTING_PLEASE_WAIT",
                       {180.0, 280.0, 440.0, 40.0},
                       18.0,
                       cream,
                       HorizontalTextAlignment::center,
                       player_profile_presentation_assets::tab_font));
    } else if (model.state() == PlayerProfileLoadState::not_found) {
        list.push(text("PROFILE_NOT_FOUND",
                       {180.0, 280.0, 440.0, 40.0},
                       18.0,
                       cream,
                       HorizontalTextAlignment::center,
                       player_profile_presentation_assets::tab_font));
    } else if (const auto* data = model.data(); data != nullptr) {
        list.push(text(data->player_name,
                       layout.player_name,
                       26.0,
                       cream,
                       HorizontalTextAlignment::left,
                       player_profile_presentation_assets::title_font));
        if (summary) {
            list.push(text("KILL_DEATH_RATIO: " + ratio_text(data->kill_death_ratio),
                           layout.kill_death_ratio,
                           11.0,
                           cream,
                           HorizontalTextAlignment::right));
        }

        const auto start = std::min(model.first_visible_row(), rows.size());
        const auto count = std::min(capacity, rows.size() - start);
        auto row_width = 476.0;
        if (total_panel_rows > capacity + (summary ? 1U : 0U)) {
            row_width = 444.0;
        }
        if (summary) {
            const DrawRect header{
                layout.summary_list_area.x, layout.summary_list_area.y, row_width, row_height};
            append_red_header(list, header);
            append_two_column_text(list,
                                   header,
                                   "CLASS / MODE",
                                   "RANK",
                                   player_profile_presentation_assets::tab_font,
                                   11.0,
                                   TextTransform::uppercase);
        }

        for (std::size_t visible = 0U; visible < count; ++visible) {
            const auto source_index = start + visible;
            const auto& row_value = rows[source_index];
            const auto y = summary ? layout.summary_list_area.y + 23.0 + row_height * static_cast<double>(visible)
                                   : layout.list_area.y + row_height * static_cast<double>(visible);
            const DrawRect row{layout.list_area.x, y, row_width, row_height};
            const auto retail_index = source_index + (summary ? 1U : 0U);
            if (row_value.kind == PlayerProfileRowKind::category) {
                append_red_header(list, row);
                const auto padding = row.width / 48.0;
                list.push(
                    text(row_value.label_key,
                         {row.x + padding, row.y, row.width * 0.5 - padding * 2.0, row.height},
                         11.0,
                         cream,
                         HorizontalTextAlignment::left,
                         player_profile_presentation_assets::tab_font));
            } else {
                solid(list, row, retail_index % 2U == 0U ? row_light : row_dark);
                if (row_value.level_progress.has_value() || row_value.level_details.has_value()) {
                    append_two_column_text(list,
                                           row,
                                           row_value.label_key,
                                           "",
                                           player_profile_presentation_assets::row_font,
                                           11.0);
                    append_progress(list, row, row_value);
                } else {
                    append_two_column_text(list,
                                           row,
                                           row_value.label_key,
                                           row_value.value,
                                           player_profile_presentation_assets::row_font,
                                           11.0);
                }
            }
        }
    }

    // DropBoxControl is appended after the list panel in retail, so its open
    // option panel must be emitted last and visually cover the rows beneath it.
    append_filter(list, model, layout, context.filter_button_state);
    append_button(list, layout.cancel_button, "CANCEL", context.cancel_state);
    append_button(list, layout.achievements_button, "ACHIEVEMENTS", context.achievements_state);
    return list;
}

} // namespace battlespades::frontend
