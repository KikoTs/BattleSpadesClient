#include "battlespades/frontend/class_selection_presentation.hpp"

#include "battlespades/world/class_catalog.hpp"
#include "battlespades/world/weapon_catalog.hpp"

#include <algorithm>
#include <array>
#include <ranges>
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
constexpr ColorRgba8 flat_join_background{112U, 216U, 224U, 255U};
constexpr ColorRgba8 empty_slot_color{46U, 44U, 35U, 255U};

[[nodiscard]] std::string_view retail_class_display_name(
    std::uint8_t class_id, std::string_view fallback) {
    // These are presentation names from SelectClass, not protocol/class-table
    // identifiers. Keeping the mapping here prevents UI terminology from
    // changing the class byte sent to the retail server.
    switch (class_id) {
    case 0U: return "Commando";
    case 1U: return "Marksman";
    case 2U: return "Rocketeer";
    case 3U: return "Miner";
    case 12U: return "Engineer";
    case 16U: return "Specialist";
    case 17U: return "Medic";
    default: return fallback;
    }
}

[[nodiscard]] SpriteDrawCommand sprite(
    std::string asset, DrawRect bounds, ColorRgba8 color = {},
    std::uint16_t intensity = 1'000U) {
    return {std::move(asset), bounds, DrawSpace::design_pixels,
            ui::TextureFilter::linear, ui::TextureAnchor::top_left, 0.64,
            ui::SpriteSizing::stretch, ColorModulation{color, intensity, 1'000U}};
}

[[nodiscard]] TextDrawCommand text(
    std::string value, DrawRect bounds, double size,
    HorizontalTextAlignment alignment = HorizontalTextAlignment::left,
    ColorRgba8 color = menu_color,
    std::string font = "fonts/Spades.ttf",
    ui::TextTransform transform = ui::TextTransform::uppercase,
    ui::TextFit fit = ui::TextFit::shrink_to_fit,
    std::uint8_t maximum_lines = 1U) {
    return {std::move(value), std::move(font), bounds, DrawSpace::design_pixels,
            size, 0.0, maximum_lines, alignment,
            ui::VerticalTextAlignment::retail_center, transform, fit,
            {color, 1'000U, 1'000U}};
}

void append_selected_frame(ui::DrawList& list, DrawRect item,
                           double class_frame_scale = 0.0) {
    if (class_frame_scale > 0.0) {
        // image.py truncates the authored dimensions when applying .64 at
        // load time (251x226 -> 160x144), before CustomButton scales them.
        const double width = 160.0 * class_frame_scale;
        const double height = 144.0 * class_frame_scale;
        list.push(sprite(
            "png/ui/in_game_menus/select_class/class_selected_frame.png",
            {item.x + (item.width - width) * 0.5,
             item.y + (item.height - height) * 0.5, width, height}));
        return;
    }
    list.push(sprite(
        "png/ui/in_game_menus/select_class/class_selected_frame.png",
        {item.x - 5.0, item.y - 4.0, item.width + 10.0,
         item.height + 8.0}));
}

void append_button(ui::DrawList& list, DrawRect bounds, std::string label,
                   bool hovered) {
    const std::string state = hovered ? "hover_" : "";
    const auto asset = [&](std::string_view part) {
        return "png/ui/common_elements/buttons/button_large_" + state +
               std::string{part} + ".png";
    };
    const double cap = 60.0 / 97.0 * bounds.height + 1.0;
    list.push(sprite(asset("left"),
                     {bounds.x, bounds.y, cap, bounds.height}));
    list.push(sprite(asset("mid"),
                     {bounds.x + cap, bounds.y,
                      bounds.width - cap * 2.0, bounds.height}));
    list.push(sprite(asset("right"),
                     {bounds.x + bounds.width - cap, bounds.y,
                      cap, bounds.height}));
    list.push(text(std::move(label),
                   {bounds.x + 8.0, bounds.y + 1.0,
                    bounds.width - 16.0, bounds.height - 2.0},
                   28.0, HorizontalTextAlignment::center,
                   {20U, 20U, 20U, 255U}));
}

void append_navigation_back(ui::DrawList& list) {
    list.push(sprite("png/ui/common_elements/nav_bar/back_icon.png",
                     {54.0, 543.0, 26.0, 26.0}));
    list.push(text("Back", {82.0, 540.0, 100.0, 34.0}, 22.0,
                   HorizontalTextAlignment::left,
                   {180U, 165U, 75U, 255U}));
}

void append_scrollbar(ui::DrawList& list,
                      const ClassSelectionMenuModel& menu) {
    if (menu.classes().size() <= menu.classes_per_page()) return;
    constexpr double left{70.0};
    constexpr double top{250.0};
    constexpr double button{22.0};
    constexpr double track_width{616.0};
    list.push(sprite(
        "png/ui/common_elements/scroll_bar/scrollbar_hmid.png",
        {left + button, top, track_width, button}));
    list.push(sprite(
        "png/ui/common_elements/scroll_bar/scroll_bar_arrow_left.png",
        {left, top, button, button}));
    list.push(sprite(
        "png/ui/common_elements/scroll_bar/scroll_bar_arrow_right.png",
        {left + button + track_width, top, button, button}));

    const auto count = static_cast<double>(menu.classes().size());
    const auto visible = static_cast<double>(menu.classes_per_page());
    const double thumb_width = track_width * visible / count;
    const auto max_offset = menu.classes().size() - menu.classes_per_page();
    const double fraction =
        max_offset == 0U
            ? 0.0
            : static_cast<double>(menu.visible_class_offset()) /
                  static_cast<double>(max_offset);
    const double thumb_x = left + button +
                           (track_width - thumb_width) * fraction;
    list.push(sprite(
        "png/ui/common_elements/scroll_bar/scrollbar_square_button_default.png",
        {thumb_x, top, thumb_width, button}));
}

} // namespace

std::string class_selection_item_icon(std::uint16_t raw_item_id) {
    if (raw_item_id <= 64U) {
        if (const auto* tool = world::find_weapon_definition(
                static_cast<std::uint8_t>(raw_item_id));
            tool != nullptr && !tool->toolbar_icon_asset.empty()) {
            return std::string{tool->toolbar_icon_asset};
        }
        return {};
    }
    // These ids live in SetClassLoadout but not in ClientData's 0..64 held
    // tool catalog. They have distinct retail art; collapsing all of them to
    // jetpack.png is what made the Commando parachute appear as a rocket pack.
    switch (raw_item_id) {
    case 66U: return "png/ui/weapons/jetpack.png";
    case 67U: return "png/ui/weapons/jetpack2.png";
    case 68U: return "png/ui/weapons/jetpack_engineer.png";
    case 69U: return "png/ui/weapons/jetpack_ugcbuilder.png";
    case 72U: return "png/ui/weapons/parachute.png";
    default: return {}; // NO_JETPACK and A369_UNKNOWN have no retail icon.
    }
}

ui::DrawList ClassSelectionPresentation::build(
    const ClassSelectionMenuModel& menu, ui::PixelExtent window,
    bool in_game, const ClassSelectionAppearance& appearance) const {
    const auto resolve=[](const auto& icons,auto id,std::string_view fallback){
        const auto found=icons.find(id);return found==icons.end()?std::string{fallback}:found->second;
    };
    static_cast<void>(window);
    ui::DrawList list;
    list.reserve(128U);
    if (!in_game) {
        list.push(sprite("png/high/white.png", {0.0, 0.0, 800.0, 600.0},
                         flat_join_background));
        list.push(sprite("png/ui/common_elements/frames/ui_frame_large.png",
                         {25.0, 5.0, 750.0, 589.0}));
    } else {
        list.push(sprite(
            "png/ui/in_game_menus/select_class/in_game_class_frame.png",
            {31.0, 26.0, 739.0, 569.0}));
    }
    list.push(sprite(
        menu.classes_per_page() == 5U
            ? "png/ui/in_game_menus/select_class/class_background_frame_5.png"
            : "png/ui/in_game_menus/select_class/class_background_frame.png",
        {31.0, 5.0, 739.0, 589.0}));
    list.push(text("CHOOSE CLASS",
                   {180.0, in_game ? 37.0 : 14.0, 440.0, 59.0},
                   46.0, HorizontalTextAlignment::center));

    const auto classes = menu.classes();
    const auto offset = menu.visible_class_offset();
    const auto visible =
        std::min(menu.classes_per_page(), classes.size() - offset);
    const auto card_layout = menu.card_layout();
    for (std::size_t display_index{}; display_index < visible;
         ++display_index) {
        const auto class_index = offset + display_index;
        const auto* definition =
            world::find_class_definition(classes[class_index]);
        if (definition == nullptr) continue;
        const auto team_index = menu.team() == 3U ? 1U : 0U;
        const auto bounds = menu.class_card_bounds(display_index);
        const DrawRect card{static_cast<double>(bounds.x), static_cast<double>(bounds.y),
                            static_cast<double>(bounds.width), static_cast<double>(bounds.height)};
        const bool selected_class = class_index == menu.selected_class_index();
        const bool hovered_class = menu.hovered().has_value() && bounds.contains(*menu.hovered());
        const double image_size = 147.0 *
                                  (card_layout.image_scale +
                                   (selected_class || hovered_class ? 0.22 : 0.0));
        // A malformed/custom class table must not turn a missing optional
        // image into an empty texture request that terminates the frontend.
        if (!definition->team_icon_assets[team_index].empty()) {
            list.push(sprite(
                resolve(appearance.class_icons,classes[class_index],definition->team_icon_assets[team_index]),
                {card.x + (card.width - image_size) * 0.5,
                 card.y + (card.height - image_size) * 0.5, image_size, image_size}));
        }
        if (selected_class) {
            append_selected_frame(list, card, card_layout.frame_scale);
        }
        list.push(sprite(
            "png/ui/icons/key" + std::to_string(display_index + 1U) + ".png",
            {72.0 + static_cast<double>(display_index) * card_layout.interval,
             99.0, 30.0, 30.0}));
        list.push(text(
            std::string{retail_class_display_name(
                classes[class_index], definition->display_name)},
            {card_layout.name_x + static_cast<double>(display_index) * card_layout.interval,
             96.0, static_cast<double>(card_layout.name_width), 31.0},
            20.0, HorizontalTextAlignment::center,
            class_index == menu.selected_class_index() ? selected_color
                                                        : menu_color,
            "fonts/Edo.ttf"));
    }
    append_scrollbar(list, menu);

    const auto* definition =
        world::find_class_definition(menu.selected_class());
    if (definition == nullptr) return list;

    constexpr std::array<std::string_view, 4U> labels{
        "MELEE", "PRIMARY", "SECONDARY", "EQUIPMENT"};
    const auto selected = menu.option_indices();
    for (std::size_t group{}; group < selected.size(); ++group) {
        const double top = 294.0 + static_cast<double>(group) * 53.0;
        const bool two_lines = group == 1U || group == 2U;
        list.push(text(std::string{labels[group]},
                       {84.0, top + (two_lines ? 6.0 : 0.0),
                        80.0, two_lines ? 16.0 : 42.0},
                       10.0, HorizontalTextAlignment::center, menu_color,
                       "fonts/Edo.ttf"));
        if (two_lines) {
            list.push(text("WEAPONS", {84.0, top + 21.0, 80.0, 16.0},
                           10.0, HorizontalTextAlignment::center, menu_color,
                           "fonts/Edo.ttf"));
        }
        // SelectClass always exposes the complete six-slot strip. Unused
        // cells stay dark; omitting them makes short loadouts collapse and no
        // longer match the retail menu's fixed grid.
        for (std::size_t option{}; option < 6U; ++option) {
            list.push(sprite(
                "png/high/white.png",
                {171.0 + static_cast<double>(option) * 45.0,
                 top, 42.0, 42.0},
                empty_slot_color));
        }
        const auto options = definition->item_groups[group];
        const auto visible_options = std::min<std::size_t>(options.size(), 6U);
        for (std::size_t option{}; option < visible_options; ++option) {
            const DrawRect item{
                171.0 + static_cast<double>(option) * 45.0,
                top, 42.0, 42.0};
            list.push(sprite(
                "png/ui/in_game_menus/select_class/loadout_background.png",
                item));
            const auto icon = resolve(appearance.weapon_icons,options[option],class_selection_item_icon(options[option]));
            if (!icon.empty()) {
                const double inset=icon.starts_with("runtime/cosmetic/")?1.5:4.5;
                list.push(sprite(icon,
                                 {item.x + inset, item.y + inset, item.width-2*inset, item.height-2*inset}));
            }
            if (option == selected[group]) {
                append_selected_frame(list, item);
            }
        }
    }

    list.push(text("CONSTRUCTS", {468.0, 290.0, 122.0, 22.0}, 10.0,
                   HorizontalTextAlignment::center, menu_color,
                   "fonts/Edo.ttf"));
    // The stock HUD keeps a fixed 3x4 prefab grid, including empty cells.
    for (std::size_t index{}; index < 12U; ++index) {
        const auto column = index % 3U;
        const auto row = index / 3U;
        list.push(sprite(
            "png/high/white.png",
            {469.0 + static_cast<double>(column) * 41.0,
             313.0 + static_cast<double>(row) * 43.0,
             38.0, 38.0},
            empty_slot_color));
    }
    const auto prefab_options =
        world::class_prefab_options(menu.selected_class());
    for (std::size_t index{}; index < prefab_options.size() && index < 12U;
         ++index) {
        const auto column = index % 3U;
        const auto row = index / 3U;
        const DrawRect item{
            469.0 + static_cast<double>(column) * 41.0,
            313.0 + static_cast<double>(row) * 43.0,
            38.0, 38.0};
        const std::string name{prefab_options[index]};
        list.push(sprite(
            "png/ui/in_game_menus/select_class/loadout_background.png",
            item));
        list.push(sprite("prefabs/" + name + ".png",
                         {item.x + 3.0, item.y + 3.0, 32.0, 32.0}));
        if (std::ranges::find(menu.selected_prefabs(), name) !=
            menu.selected_prefabs().end()) {
            append_selected_frame(list, item);
        }
    }

    const auto team_index = menu.team() == 3U ? 1U : 0U;
    if (!definition->team_portrait_assets[team_index].empty()) {
        auto portrait=sprite(
            resolve(appearance.class_portraits,menu.selected_class(),definition->team_portrait_assets[team_index]),
            {604.5, 296.0, 111.0, 160.0});
        if(portrait.asset_id.starts_with("runtime/cosmetic/"))portrait.source_pixels=ClassSelectionAppearance::portrait_source;
        list.push(std::move(portrait));
    }

    const DrawRect select_bounds{599.0, 461.0, 124.0, 40.0};
    const auto hovered = menu.hovered();
    const bool select_hovered =
        hovered.has_value() && hovered->x >= 599 && hovered->x < 723 &&
        hovered->y >= 461 && hovered->y < 501;
    append_button(list, select_bounds, "SELECT", select_hovered);
    if (!in_game) append_navigation_back(list);
    return list;
}

} // namespace battlespades::frontend
