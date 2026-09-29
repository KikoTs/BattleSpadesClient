#include "battlespades/frontend/ugc_loadout_presentation.hpp"
#include "battlespades/frontend/main_menu.hpp"

#include <algorithm>
#include <cmath>
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
using ui::TextDrawCommand;

constexpr ColorRgba8 menu_color{244U, 236U, 187U, 255U};
constexpr ColorRgba8 selected_color{232U, 207U, 78U, 255U};
constexpr ColorRgba8 button_text_color{20U, 20U, 20U, 255U};
constexpr ColorRgba8 black{0U, 0U, 0U, 255U};
constexpr ColorRgba8 row_grey{57U, 53U, 44U, 255U};
constexpr ColorRgba8 row_dark_grey{24U, 21U, 14U, 255U};
constexpr ColorRgba8 scrollbar_track{73U, 63U, 7U, 255U};
constexpr ColorRgba8 objective_complete{137U, 179U, 45U, 255U};
constexpr ColorRgba8 objective_incomplete{255U, 0U, 0U, 255U};

[[nodiscard]] SpriteDrawCommand sprite(
    std::string asset, DrawRect bounds, ColorRgba8 color = {},
    std::uint16_t intensity = 1'000U,
    std::uint16_t opacity = 1'000U,
    ui::TextureFilter filter = ui::TextureFilter::linear) {
    return {std::move(asset), bounds, DrawSpace::design_pixels, filter,
            ui::TextureAnchor::top_left, 1.0, ui::SpriteSizing::stretch,
            ColorModulation{color, intensity, opacity}};
}

[[nodiscard]] TextDrawCommand text(
    std::string key, DrawRect bounds, double size,
    HorizontalTextAlignment alignment = HorizontalTextAlignment::center,
    ColorRgba8 color = menu_color,
    std::string font = "fonts/Edo.ttf",
    std::uint8_t maximum_lines = 2U) {
    return {std::move(key), std::move(font), bounds, DrawSpace::design_pixels,
            size, 0.0, maximum_lines, alignment,
            ui::VerticalTextAlignment::retail_center, ui::TextTransform::preserve,
            ui::TextFit::shrink_to_fit,
            ColorModulation{color, 1'000U, 1'000U}};
}

void solid(ui::DrawList& list, DrawRect bounds, ColorRgba8 color,
           std::uint16_t opacity = 1'000U) {
    list.push(sprite("png/high/white.png", bounds, color, 1'000U, opacity,
                     ui::TextureFilter::nearest));
}

void append_button(ui::DrawList& list, DrawRect bounds, std::string label,
                   bool hovered) {
    const std::string state = hovered ? "hover_" : "";
    const auto asset = [&](std::string_view part) {
        return "png/ui/common_elements/buttons/button_large_" + state +
               std::string{part} + ".png";
    };
    const double cap = 60.0 / 97.0 * bounds.height + 1.0;
    list.push(sprite(asset("left"), {bounds.x, bounds.y, cap, bounds.height}));
    list.push(sprite(asset("mid"),
                     {bounds.x + cap, bounds.y,
                      bounds.width - cap * 2.0, bounds.height}));
    list.push(sprite(asset("right"),
                     {bounds.x + bounds.width - cap, bounds.y, cap, bounds.height}));
    list.push(text(std::move(label),
                   {bounds.x + 14.0, bounds.y + 4.0,
                    bounds.width - 28.0, bounds.height - 8.0},
                   30.0, HorizontalTextAlignment::center,
                   button_text_color, "fonts/Spades.ttf", 2U));
}

void append_navigation_back(ui::DrawList& list, bool hovered) {
    const auto intensity = hovered ? std::uint16_t{1'000U}
                                   : std::uint16_t{700U};
    // NavigationBar.draw_item: MENU_FONT_COLOR2 (x0.7 unless hovered) on the
    // icon at x+PAD/2 and on the Spades-24 label PAD/2 after it, the same
    // navbar the loader and team/class screens draw.
    constexpr ColorRgba8 menu_font_color2{232U, 207U, 78U, 255U};
    list.push(sprite("png/ui/common_elements/nav_bar/back_icon.png",
                     {56.5, 543.0, 26.0, 26.0}, menu_font_color2, intensity));
    auto command = text("BACK", {84.0, 540.0, 110.0, 34.0}, 24.0,
                        HorizontalTextAlignment::left,
                        menu_font_color2, "fonts/Spades.ttf", 1U);
    command.modulation.intensity_per_mille = intensity;
    list.push(std::move(command));
}

[[nodiscard]] bool hovered(const UgcLoadoutMenuModel& menu,
                           ui::Rect bounds) noexcept {
    return menu.hovered().has_value() && bounds.contains(*menu.hovered());
}

void append_scrollbar(ui::DrawList& list,
                      const UgcLoadoutMenuModel& menu) {
    const auto* tab = menu.current_tab();
    if (tab == nullptr || tab->items.size() <= menu.items_per_page()) return;
    const auto columns = menu.columns();
    const auto visible_rows = menu.items_per_page() / columns;
    // GridSelection passes int(item_count / columns) + 1 as max_lines. This
    // intentionally retains retail's extra final scroll position when the
    // item count is an exact multiple of the column count.
    const auto total_rows = tab->items.size() / columns + 1U;
    const auto first_row = tab->first_visible_item / columns;
    const double center_x = menu.library() == UgcLoadoutLibrary::constructs
                                ? 726.0
                                : 398.0;
    const double top = menu.library() == UgcLoadoutLibrary::constructs
                           ? 138.0
                           : 139.0;
    const double height = menu.library() == UgcLoadoutLibrary::constructs
                              ? 274.0
                              : 272.0;
    constexpr double button{18.0};
    constexpr double bar_thickness{16.0};
    constexpr double gap{2.0};
    solid(list, {center_x - button * 0.5, top, button, height}, black);
    const DrawRect track{center_x - bar_thickness * 0.5,
                         top + button + gap,
                         bar_thickness,
                         height - button * 2.0 - gap * 2.0};
    solid(list, track, scrollbar_track);
    const auto square_button = [&](DrawRect bounds, std::string_view icon,
                                   bool enabled) {
        const auto intensity = enabled ? std::uint16_t{1'000U}
                                       : std::uint16_t{500U};
        list.push(sprite(
            "png/ui/common_elements/buttons/button_square.png", bounds, {},
            intensity, 1'000U, ui::TextureFilter::nearest));
        list.push(sprite(std::string{icon}, bounds, {}, intensity, 1'000U,
                         ui::TextureFilter::nearest));
    };
    square_button({center_x - button * 0.5, top, button, button},
                  "png/ui/common_elements/scroll_bar/scroll_bar_arrow_up.png",
                  first_row > 0U);
    const auto max_row = total_rows - visible_rows;
    square_button({center_x - button * 0.5, top + height - button,
                   button, button},
                  "png/ui/common_elements/scroll_bar/scroll_bar_arrow_down.png",
                  first_row < max_row);
    auto thumb_height = std::floor(track.height *
                                   static_cast<double>(visible_rows) /
                                   static_cast<double>(total_rows));
    thumb_height = std::max(32.0, thumb_height);
    const double fraction = max_row == 0U
                                ? 0.0
                                : static_cast<double>(first_row) /
                                      static_cast<double>(max_row);
    const auto thumb_top = track.y + 1.0 +
                           (track.height - thumb_height) * fraction;
    constexpr double cap{16.0};
    list.push(sprite("png/ui/common_elements/scroll_bar/scroll_bar_top.png",
                     {track.x, thumb_top, track.width, cap}, {}, 1'000U,
                     1'000U, ui::TextureFilter::nearest));
    if (thumb_height > cap * 2.0) {
        list.push(sprite("png/ui/common_elements/scroll_bar/scroll_bar_mid.png",
                         {track.x, thumb_top + cap, track.width,
                          thumb_height - cap * 2.0}, {}, 1'000U, 1'000U,
                         ui::TextureFilter::nearest));
    }
    list.push(sprite("png/ui/common_elements/scroll_bar/scroll_bar_bottom.png",
                     {track.x, thumb_top + thumb_height - cap,
                      track.width, cap}, {}, 1'000U, 1'000U,
                     ui::TextureFilter::nearest));
}

void append_objectives_panel(ui::DrawList& list,
                             const UgcLoadoutMenuModel& menu) {
    if (menu.library() != UgcLoadoutLibrary::game_data ||
        menu.objectives().empty()) {
        return;
    }

    // UGCObjectivesListPanel(manager, 414, 503, 325, 321), converted from
    // Pyglet's bottom-left coordinate system to the native 800x600 canvas.
    list.push(sprite("png/ui/common_elements/panels/ui_panel_frame.png",
                     {414.0, 97.0, 325.0, 321.0}));
    list.push(sprite(
        "png/ui/settings/settings_common/settings_matchsettings_frame.png",
        {424.0, 107.0, 305.0, 40.0}));
    list.push(text("UGC_TAB_OBJECTIVES_TITLE", {438.0, 107.0, 275.0, 40.0},
                   20.0, HorizontalTextAlignment::left, menu_color,
                   "fonts/Edo.ttf", 1U));

    constexpr double row_x{424.0};
    constexpr double row_top{157.0};
    constexpr double row_width{305.0};
    constexpr double row_height{20.0};
    constexpr double name_width{230.0};
    constexpr double value_width{31.75};
    const auto rows = menu.objectives().first(
        std::min<std::size_t>(menu.objectives().size(), 12U));
    for (std::size_t index{}; index < rows.size(); ++index) {
        const auto top = row_top + static_cast<double>(index) * row_height;
        solid(list, {row_x, top, row_width, row_height},
              index % 2U == 0U ? row_grey : row_dark_grey);
        const auto& objective = rows[index];
        const auto name = "UGC_OBJECTIVE_ROW|" + objective.id + "|" +
                          std::to_string(objective.maximum);
        const auto value = "UGC_OBJECTIVE_VALUE|" +
                           std::to_string(objective.value) + "|" +
                           std::to_string(objective.minimum);
        list.push(text(name, {row_x + 14.0, top, name_width, row_height},
                       11.0, HorizontalTextAlignment::left, menu_color,
                       "fonts/A750-Sans-Medium.ttf", 2U));
        list.push(text(value,
                       {row_x + row_width - value_width - 14.0,
                        top, value_width, row_height},
                       11.0, HorizontalTextAlignment::right,
                       objective.complete() ? objective_complete
                                            : objective_incomplete,
                       "fonts/A750-Sans-Medium.ttf", 1U));
    }
}

} // namespace

ui::DrawList UgcLoadoutPresentation::build(
    const UgcLoadoutMenuModel& menu, ui::PixelExtent window) const {
    ui::DrawList list;
    list.reserve(160U);

    if (!menu.in_game()) {
        const auto width = static_cast<double>(window.width);
        const auto height = static_cast<double>(window.height);
        const auto cover = std::max(width / 768.0, height / 576.0);
        auto background = sprite(std::string{main_menu_assets::background},
            {(width - 768.0 * cover) * 0.5, (height - 576.0 * cover) * 0.5,
             768.0 * cover, 576.0 * cover});
        background.space = DrawSpace::window_pixels;
        list.push(std::move(background));
        list.push(sprite("png/ui/common_elements/frames/ui_frame_large.png",
                         {25.0, 5.0, 750.0, 589.0}));
    } else {
        list.push(sprite(
            "png/ui/in_game_menus/select_class/in_game_class_frame.png",
            {31.0, 26.0, 739.0, 569.0}));
        // SelectUGC stretches this 326x74 footer independently in x/y to fill
        // the in-game class frame behind its Back control.
        list.push(sprite("png/ui/ugc_tools/ugc_select_bg.png",
                         {60.5, 512.0, 679.0, 59.0}));
    }

    const bool constructs = menu.library() == UgcLoadoutLibrary::constructs;
    list.push(text(constructs ? "PREFABS_MENU" : "UGC_GAME_DATA",
                   {menu.in_game() ? 220.0 : 120.0,
                    menu.in_game() ? 35.0 : 20.0,
                    menu.in_game() ? 360.0 : 560.0, 50.0},
                   46.0, HorizontalTextAlignment::center,
                   menu_color, "fonts/Spades.ttf", 1U));

    list.push(sprite(constructs
                         ? "png/ui/ugc_tools/pf_template_bg.png"
                         : "png/ui/ugc_tools/gdata_template_bg.png",
                     constructs ? DrawRect{59.5, 131.5, 681.0, 287.0}
                                : DrawRect{59.5, 132.0, 351.0, 286.0}));

    const auto tabs = menu.tabs();
    for (std::size_t index{}; index < tabs.size(); ++index) {
        const bool current = index == menu.current_tab_index();
        const auto bounds = menu.tab_bounds(index);
        list.push(sprite(
            constructs
                ? (current ? "png/ui/ugc_tools/pf_selected_tab.png"
                           : "png/ui/ugc_tools/pf_unselected_tab.png")
                : (current ? "png/ui/ugc_tools/gdata_selected_tab.png"
                           : "png/ui/ugc_tools/gdata_unselected_tab.png"),
            {static_cast<double>(bounds.x), static_cast<double>(bounds.y),
             static_cast<double>(bounds.width), static_cast<double>(bounds.height)}));
        const auto tab_top = current ? 96.5 : 96.0;
        list.push(text(tabs[index].label_key,
                       {static_cast<double>(bounds.x) + 4.25,
                        tab_top,
                        static_cast<double>(bounds.width) - 8.5,
                        static_cast<double>(bounds.height)},
                       16.0, HorizontalTextAlignment::center,
                       current ? selected_color : menu_color,
                       "fonts/Edo.ttf", 2U));
    }

    const auto visible = menu.visible_items();
    for (std::size_t index{}; index < visible.size(); ++index) {
        const auto hit = menu.visible_item_bounds(index);
        const DrawRect bounds{static_cast<double>(hit.x),
                              static_cast<double>(hit.y),
                              static_cast<double>(hit.width),
                              static_cast<double>(hit.height)};
        list.push(sprite(
            constructs
                ? "png/ui/ugc_tools/pf_blueprint_bg_default.png"
                : "png/ui/ugc_tools/gdata_blueprint_bg_default.png",
            bounds));
        // Keep the original square portraits inside their blueprint cards;
        // a fixed 99px image overflows the narrower seven-column construct grid.
        const double preview_size = std::min(99.0, bounds.width - 8.0);
        const double offset_y = constructs ? -5.0 : 13.0;
        list.push(sprite(
            visible[index].preview_asset,
            {bounds.x + bounds.width * 0.5 - preview_size * 0.5,
             bounds.y + bounds.height * 0.5 - preview_size * 0.5 + offset_y,
             preview_size, preview_size}));
        if (menu.selected(visible[index].choice)) {
            list.push(sprite(
                constructs ? "png/ui/ugc_tools/pf_select_marker.png"
                           : "png/ui/ugc_tools/gd_select_marker.png",
                {bounds.x - 3.0, bounds.y - (constructs ? 2.0 : 2.5),
                 constructs ? 92.0 : 113.0, 138.0}));
        }
        list.push(text(visible[index].label_key,
                       {bounds.x + (constructs ? 5.0 : 8.0),
                        bounds.y + (constructs ? -4.0 : 0.0),
                        constructs ? 76.0 : 88.0, 24.0},
                       16.0, HorizontalTextAlignment::center,
                       menu_color, "fonts/Edo.ttf", 1U));
        if (constructs) {
            list.push(text("UGC_PREFAB_SIZE",
                           {bounds.x + 4.0, bounds.y + 100.0, 32.0, 30.0},
                           16.0, HorizontalTextAlignment::center,
                           menu_color, "fonts/Edo.ttf", 1U));
            list.push(text(visible[index].size_label_key,
                           {bounds.x + 41.0, bounds.y + 103.0, 37.0, 24.0},
                           14.0, HorizontalTextAlignment::center,
                           menu_color, "fonts/Spades.ttf", 1U));
        }
    }
    append_scrollbar(list, menu);
    append_objectives_panel(list, menu);

    // Source blits the selected-item blueprint beside the SELECT background.
    list.push(sprite("png/ui/ugc_tools/prefab_selection_blueprint.png",
                     {59.5, 428.0, 351.0, 74.0}));
    list.push(sprite("png/high/white.png", {129.5, 434.0, 270.0, 25.0},
                     {0U, 0U, 0U, 255U}, 1'000U, 588U));
    list.push(text("UGC_BACKPACK_HINT", {143.5, 434.0, 242.0, 25.0},
                   16.0, HorizontalTextAlignment::center,
                   menu_color, "fonts/A750-Sans-Medium.ttf", 2U));

    for (std::size_t index{}; index < UgcLoadoutMenuModel::maximum_selection;
         ++index) {
        const auto slot = UgcLoadoutMenuModel::inventory_bounds(index);
        if (index >= menu.inventory().size()) continue;
        const auto* item = menu.item(menu.inventory()[index]);
        if (item == nullptr) continue;
        const auto left = 137.5 + 55.5 * static_cast<double>(index);
        list.push(sprite(item->preview_asset,
                         {left,
                          static_cast<double>(slot.y) -
                              (constructs ? 2.0 : 4.0),
                          static_cast<double>(slot.width),
                          static_cast<double>(slot.height)}));
    }

    list.push(sprite("png/ui/ugc_tools/ugc_select_bg.png",
                     {414.0, 428.0, 326.0, 74.0}));
    const auto select = UgcLoadoutMenuModel::select_bounds();
    append_button(list,
                  {static_cast<double>(select.x),
                   static_cast<double>(select.y),
                   static_cast<double>(select.width),
                   static_cast<double>(select.height)},
                  "SELECT", hovered(menu, select));

    const auto back = UgcLoadoutMenuModel::back_bounds(menu.in_game());
    if (menu.in_game()) {
        append_button(list,
                      {static_cast<double>(back.x),
                       static_cast<double>(back.y),
                       static_cast<double>(back.width),
                       static_cast<double>(back.height)},
                      "BACK", hovered(menu, back));
    } else {
        append_navigation_back(list, hovered(menu, back));
    }
    return list;
}

} // namespace battlespades::frontend
