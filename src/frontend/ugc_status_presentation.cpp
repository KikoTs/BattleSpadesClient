#include "battlespades/frontend/ugc_status_presentation.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
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
using ui::TextDrawCommand;

constexpr ColorRgba8 menu_color{244U, 236U, 187U, 255U};
constexpr ColorRgba8 row_grey{57U, 53U, 44U, 255U};
constexpr ColorRgba8 row_dark_grey{24U, 21U, 14U, 255U};
// LIGHT_GREEN / LIGHT_RED of UGCObjectiveListItem.draw_name, as the native
// SelectGameData objective panel already renders them.
constexpr ColorRgba8 objective_complete{137U, 179U, 45U, 255U};
constexpr ColorRgba8 objective_incomplete{255U, 0U, 0U, 255U};
constexpr std::string_view literal_prefix{"LITERAL|"};

[[nodiscard]] SpriteDrawCommand sprite(std::string asset, DrawRect bounds, DrawSpace space,
                                       ColorRgba8 color = {}, std::uint16_t opacity = 1'000U,
                                       ui::TextureFilter filter = ui::TextureFilter::linear) {
    return {std::move(asset), bounds, space, filter, ui::TextureAnchor::top_left, 1.0,
            ui::SpriteSizing::stretch, ColorModulation{color, 1'000U, opacity}};
}

[[nodiscard]] TextDrawCommand text(std::string key, DrawRect bounds, DrawSpace space, double size,
                                   HorizontalTextAlignment alignment, ColorRgba8 color,
                                   std::string font, std::uint8_t maximum_lines = 1U) {
    return {std::move(key), std::move(font), bounds, space, size, 0.0, maximum_lines, alignment,
            ui::VerticalTextAlignment::retail_center, ui::TextTransform::preserve,
            ui::TextFit::shrink_to_fit, ColorModulation{color, 1'000U, 1'000U}};
}

void solid(ui::DrawList& list, DrawRect bounds, DrawSpace space, ColorRgba8 color,
           std::uint16_t opacity) {
    list.push(sprite("png/high/white.png", bounds, space, color, opacity,
                     ui::TextureFilter::nearest));
}

struct PanelRow final {
    std::string name;
    std::string value;
    ColorRgba8 value_color{menu_color};
};

struct PanelStyle final {
    double row_height{20.0};
    double title_width{210.0};
    bool transparent_rows{};
    DrawSpace space{DrawSpace::design_pixels};
};

/**
 * One ListPanelBase with a header: the shared panel frame, the header strip
 * and title, and ROW_GREY/ROW_DARK_GREY alternating rows (alpha 120 for
 * transparent_items). Rows that do not fit the list area are not drawn
 * (retail scrolls; the editor lists are short).
 */
void append_panel(ui::DrawList& list, DrawRect bounds, std::string_view title_key,
                  std::span<const PanelRow> rows, const PanelStyle& style) {
    list.push(sprite("png/ui/common_elements/panels/ui_panel_frame.png", bounds, style.space));
    list.push(sprite("png/ui/settings/settings_common/settings_matchsettings_frame.png",
                     {bounds.x + 10.0, bounds.y + 10.0, bounds.width - 20.0, 40.0}, style.space));
    list.push(text(std::string{title_key},
                   {bounds.x + 24.0, bounds.y + 10.0,
                    std::min(style.title_width, bounds.width - 38.0), 40.0},
                   style.space, 20.0, HorizontalTextAlignment::left, menu_color,
                   "fonts/Edo.ttf"));
    const double row_x = bounds.x + 10.0;
    const double row_width = bounds.width - 20.0;
    const double first_row = bounds.y + 60.0;
    const double list_bottom = bounds.y + bounds.height - 10.0;
    const std::uint16_t opacity = style.transparent_rows ? 471U : 1'000U;
    for (std::size_t index{}; index < rows.size(); ++index) {
        const double top = first_row + static_cast<double>(index) * style.row_height;
        if (top + style.row_height > list_bottom + 0.5) break;
        solid(list, {row_x, top, row_width, style.row_height}, style.space,
              index % 2U == 0U ? row_grey : row_dark_grey, opacity);
        const auto& row = rows[index];
        // UGCObjectiveListItem: name 80% of the row, value 15%, both inset
        // by TEXT_BACKGROUND_SPACING (14 px at the retail row width).
        const double name_width = row_width * 0.8 - 14.0;
        const double value_width = row_width * 0.15 - 14.0;
        const double font = style.row_height >= 30.0 ? 14.0 : 11.0;
        list.push(text(row.name, {row_x + 14.0, top, name_width, style.row_height}, style.space,
                       font, HorizontalTextAlignment::left, menu_color,
                       "fonts/A750-Sans-Medium.ttf", 2U));
        if (!row.value.empty()) {
            const double width = row.value.starts_with(literal_prefix)
                                     ? row_width * 0.5 - 14.0
                                     : std::max(value_width, 24.0);
            list.push(text(row.value, {row_x + row_width - width - 14.0, top, width,
                                       style.row_height},
                           style.space, font, HorizontalTextAlignment::right, row.value_color,
                           "fonts/A750-Sans-Medium.ttf"));
        }
    }
}

[[nodiscard]] std::vector<UgcLoadoutObjective>
sorted_objectives(std::span<const UgcLoadoutObjective> objectives, bool incomplete_only) {
    std::vector<UgcLoadoutObjective> rows;
    rows.reserve(objectives.size());
    for (const auto& objective : objectives) {
        if (!incomplete_only || !objective.complete()) rows.push_back(objective);
    }
    std::ranges::stable_sort(rows, {}, &UgcLoadoutObjective::priority);
    return rows;
}

[[nodiscard]] std::vector<PanelRow> objective_rows(std::span<const UgcLoadoutObjective> rows) {
    std::vector<PanelRow> result;
    result.reserve(rows.size());
    for (const auto& objective : rows) {
        result.push_back(PanelRow{
            "UGC_OBJECTIVE_ROW|" + objective.id + "|" + std::to_string(objective.maximum),
            "UGC_OBJECTIVE_VALUE|" + std::to_string(objective.value) + "|" +
                std::to_string(objective.minimum),
            objective.complete() ? objective_complete : objective_incomplete});
    }
    return result;
}

} // namespace

ui::DrawList build_ugc_status_tab(const UgcStatusTabState& state) {
    ui::DrawList list;
    list.reserve(96U);
    // glColor4f(1, 1, 1, 0.8); ugc_tab_frame (1098x658, centre-anchored,
    // int(source * .64)) blitted at the window centre.
    list.push(sprite("png/ui/in_game_menus/ugc_tab_frame.png", {49.0, 89.5, 702.0, 421.0},
                     DrawSpace::design_pixels, {}, 800U));
    // title_font.draw(strings.UGC_TAB_TITLE, 400, 463, MENU_FONT_COLOR, center).
    list.push(TextDrawCommand{"UGC_TAB_TITLE", "fonts/Spades.ttf", DrawRect{74.0, 137.0, 652.0, 0.0},
                              DrawSpace::design_pixels, 46.0, 0.0, 1U,
                              HorizontalTextAlignment::center,
                              ui::VerticalTextAlignment::baseline, ui::TextTransform::preserve,
                              ui::TextFit::retail_width_scale,
                              ColorModulation{menu_color, 1'000U, 1'000U}});

    std::vector<PanelRow> players;
    players.reserve(state.player_names.size());
    for (const auto& name : state.player_names) {
        players.push_back(PanelRow{std::string{literal_prefix} + name, {}, menu_color});
    }
    const PanelStyle list_style{};
    append_panel(list, {404.0, 149.0, 327.0, 341.0}, "UGC_TAB_PLAYERLIST_TITLE", players,
                 list_style);

    // ObjectivesPlayersList.on_start: OptionListItem(MODE, MODE_MAP_TITLES)
    // and OptionListItem(UGC_MAP_NAME, MAP_ROTATION_NEW_TITLE).
    const std::array<PanelRow, 2U> options{
        PanelRow{"MODE", std::string{literal_prefix} + state.mode_title, menu_color},
        PanelRow{"UGC_MAP_NAME", std::string{literal_prefix} + state.map_title, menu_color},
    };
    append_panel(list, {68.0, 149.0, 327.0, 120.0}, "UGC_TAB_MODEOPTIONS_TITLE", options,
                 list_style);

    const auto objectives = sorted_objectives(state.objectives, false);
    const auto rows = objective_rows(objectives);
    append_panel(list, {68.0, 248.0, 327.0, 242.0}, "UGC_TAB_OBJECTIVES_TITLE", rows,
                 PanelStyle{20.0, 277.0, false, DrawSpace::design_pixels});
    return list;
}

double append_ugc_incomplete_objectives(ui::DrawList& list,
                                        std::span<const UgcLoadoutObjective> objectives) {
    const auto incomplete = sorted_objectives(objectives, true);
    if (incomplete.empty()) return 0.0;
    const auto rows = objective_rows(incomplete);
    constexpr double row_height{30.0};
    constexpr double maximum_height{300.0};
    // enable_background_resizing: the frame hugs the header plus its rows.
    const double height =
        std::min(maximum_height, 70.0 + static_cast<double>(rows.size()) * row_height);
    append_panel(list, {10.0, 10.0, 320.0, height}, "UGC_INCOMPLETE_OBJECTIVES_TITLE", rows,
                 PanelStyle{row_height, 270.0, true, DrawSpace::window_pixels});
    return height;
}

} // namespace battlespades::frontend
