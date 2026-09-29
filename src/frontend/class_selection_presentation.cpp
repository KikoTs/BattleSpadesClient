#include "battlespades/frontend/class_selection_presentation.hpp"

#include "battlespades/world/class_catalog.hpp"
#include "battlespades/world/class_selection.hpp"
#include "battlespades/world/weapon_catalog.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>
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
using ui::TextDrawCommand;

constexpr ColorRgba8 menu_color{244U, 236U, 187U, 255U};
constexpr ColorRgba8 selected_color{232U, 207U, 78U, 255U};
constexpr ColorRgba8 empty_slot_color{46U, 44U, 35U, 255U};
constexpr ColorRgba8 black{0U, 0U, 0U, 255U};
constexpr ColorRgba8 scrollbar_channel{73U, 63U, 7U, 255U};
/** BUTTON_DISABLED_COLOUR used by SquareButton at the ends of travel. */
constexpr ColorRgba8 button_disabled{86U, 86U, 86U, 255U};
constexpr std::string_view select_class_root{"png/ui/in_game_menus/select_class/"};

[[nodiscard]] SpriteDrawCommand sprite(
    std::string asset, DrawRect bounds, ColorRgba8 color = {},
    std::uint16_t intensity = 1'000U) {
    return {std::move(asset), bounds, DrawSpace::design_pixels,
            ui::TextureFilter::linear, ui::TextureAnchor::top_left, 0.64,
            ui::SpriteSizing::stretch, ColorModulation{color, intensity, 1'000U}};
}

[[nodiscard]] SpriteDrawCommand solid(DrawRect bounds, ColorRgba8 color) {
    auto command = sprite("png/high/white.png", bounds, color);
    command.sampling = ui::TextureFilter::nearest;
    return command;
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

/** STANDARD 11 body text (class_loadout_description_font), left aligned. */
[[nodiscard]] TextDrawCommand body_text(std::string value, DrawRect bounds,
                                        ui::VerticalTextAlignment vertical =
                                            ui::VerticalTextAlignment::retail_center) {
    auto command = text(std::move(value), bounds, 11.0, HorizontalTextAlignment::left,
                        menu_color, "fonts/A750-Sans-Medium.ttf",
                        ui::TextTransform::preserve, ui::TextFit::shrink_to_fit);
    command.vertical_alignment = vertical;
    return command;
}

[[nodiscard]] std::string localized(const ClassSelectionAppearance& appearance,
                                    std::string_view key) {
    return appearance.localize ? appearance.localize(key) : std::string{key};
}

[[nodiscard]] std::string class_name_id(std::uint8_t class_id) {
    const auto key = world::class_name_key(class_id);
    if (!key.empty()) return std::string{key};
    const auto* definition = world::find_class_definition(class_id);
    return definition == nullptr ? std::string{} : std::string{definition->display_name};
}

void append_selected_frame(ui::DrawList& list, DrawRect item, double scale_x, double scale_y) {
    // image.py truncates the authored 251x226 frame to 160x144 at load
    // (global_scale 0.64) before CustomButton applies its own scale.
    const double width = 160.0 * scale_x;
    const double height = 144.0 * scale_y;
    list.push(sprite(std::string{select_class_root} + "class_selected_frame.png",
                     {item.x + (item.width - width) * 0.5,
                      item.y + (item.height - height) * 0.5, width, height}));
}

void append_button(ui::DrawList& list, ui::Rect rect, std::string label,
                   bool hovered, bool pressed, bool enabled, double text_size) {
    const DrawRect bounds{static_cast<double>(rect.x), static_cast<double>(rect.y),
                          static_cast<double>(rect.width), static_cast<double>(rect.height)};
    const std::string state = pressed ? "press_" : hovered ? "hover_" : "";
    const auto asset = [&](std::string_view part) {
        return "png/ui/common_elements/buttons/button_large_" + state +
               std::string{part} + ".png";
    };
    // TextButton draws disabled buttons at 0.7 brightness.
    const auto intensity = enabled ? std::uint16_t{1'000U} : std::uint16_t{700U};
    const double cap = 60.0 / 97.0 * bounds.height + 1.0;
    list.push(sprite(asset("left"), {bounds.x, bounds.y, cap, bounds.height}, {}, intensity));
    list.push(sprite(asset("mid"),
                     {bounds.x + cap, bounds.y, bounds.width - cap * 2.0, bounds.height},
                     {}, intensity));
    list.push(sprite(asset("right"),
                     {bounds.x + bounds.width - cap, bounds.y, cap, bounds.height}, {},
                     intensity));
    auto label_command = text(std::move(label),
                              {bounds.x + 8.0, bounds.y + 1.0,
                               bounds.width - 16.0, bounds.height - 2.0},
                              text_size, HorizontalTextAlignment::center,
                              {20U, 20U, 20U, 255U});
    label_command.modulation.intensity_per_mille = intensity;
    list.push(std::move(label_command));
}

void append_navigation_back(ui::DrawList& list) {
    // NavigationBar.draw_item (not hovered): MENU_FONT_COLOR2*0.7 glColor on
    // the icon at x+PAD/2 and on the label in navigation_font (Spades 24).
    list.push(sprite("png/ui/common_elements/nav_bar/back_icon.png",
                     {56.5, 543.0, 26.0, 26.0}, {232U, 207U, 78U, 255U}, 700U));
    list.push(text("BACK", {84.0, 540.0, 110.0, 34.0}, 24.0,
                   HorizontalTextAlignment::left,
                   {162U, 145U, 55U, 255U}));
}

/** SquareButton(size 22): button_square background + 27px arrow glyph. */
void append_square_button(ui::DrawList& list, ui::Rect rect, std::string_view glyph,
                          bool hovered, bool pressed, bool enabled) {
    std::string background = "png/ui/common_elements/buttons/button_square";
    // Pressed art only shows while the pointer is still over the button;
    // it also sinks 1px (floor(-4 * 22 / 90) in y-up).
    const bool sunk = pressed && hovered;
    if (sunk) background += "_press";
    else if (hovered) background += "_hover";
    background += ".png";
    const double y = static_cast<double>(rect.y) + (sunk ? 1.0 : 0.0);
    const DrawRect bounds{static_cast<double>(rect.x), y, static_cast<double>(rect.width),
                          static_cast<double>(rect.height)};
    const auto tint = enabled ? ColorRgba8{} : button_disabled;
    auto back = sprite(std::move(background), bounds, tint);
    back.sampling = ui::TextureFilter::nearest;
    list.push(std::move(back));
    auto arrow = sprite(std::string{glyph}, bounds, tint);
    arrow.sampling = ui::TextureFilter::nearest;
    list.push(std::move(arrow));
}

void append_scrollbar(ui::DrawList& list, const ClassSelectionMenuModel& menu) {
    if (!menu.has_scrollbar()) return;
    const auto geometry = menu.scrollbar_geometry();
    const auto rect = [](ui::Rect value) {
        return DrawRect{static_cast<double>(value.x), static_cast<double>(value.y),
                        static_cast<double>(value.width), static_cast<double>(value.height)};
    };
    list.push(solid(rect(geometry.frame), black));
    list.push(solid(rect(geometry.channel), scrollbar_channel));
    const auto hovered = menu.hovered();
    const auto over = [&](ui::Rect value) {
        return hovered.has_value() && value.contains(*hovered);
    };
    append_square_button(list, geometry.dec_button,
                         "png/ui/common_elements/scroll_bar/scroll_bar_arrow_left.png",
                         over(geometry.dec_button),
                         menu.pressed_scroll_button() == ClassScrollButton::dec,
                         geometry.dec_enabled);
    append_square_button(list, geometry.inc_button,
                         "png/ui/common_elements/scroll_bar/scroll_bar_arrow_right.png",
                         over(geometry.inc_button),
                         menu.pressed_scroll_button() == ClassScrollButton::inc,
                         geometry.inc_enabled);

    // scrollbar_left/hmid/right are 6x27/2x27 loaded at 0.6 (truncated to
    // 3x16 and 1x16, anchor 1/0) and scaled by 20/16: 3.75 px caps.
    constexpr double cap{3.75};
    constexpr double top{250.0};
    constexpr double thickness{20.0};
    const double x = geometry.thumb_x;
    const double length = geometry.thumb_length;
    const auto piece = [&](std::string_view name, DrawRect bounds) {
        auto command = sprite("png/ui/common_elements/scroll_bar/" + std::string{name}, bounds);
        command.sampling = ui::TextureFilter::nearest;
        list.push(std::move(command));
    };
    piece("scrollbar_hmid.png", {x + cap, top, std::max(0.0, length - cap), thickness});
    piece("scrollbar_right.png", {x + length - 3.125, top, cap, thickness});
    piece("scrollbar_left.png", {x + 0.625, top, cap, thickness});
}

[[nodiscard]] std::string item_icon(const ClassSelectionAppearance& appearance,
                                    std::uint16_t item, std::uint8_t team) {
    if (const auto found = appearance.weapon_icons.find(item);
        found != appearance.weapon_icons.end()) {
        return found->second;
    }
    // TOOL_IMAGES[SNIPER_TOOL][team_id - TEAM1] is team coloured.
    if (item == 18U) {
        return team == 3U ? "png/ui/weapons/sniper_green.png" : "png/ui/weapons/sniper_blue.png";
    }
    return class_selection_item_icon(item);
}

[[nodiscard]] std::string construct_icon(std::string_view name) {
    if (name == world::flare_block_construct) {
        return class_selection_item_icon(world::flare_block_tool);
    }
    return "prefabs/" + std::string{name} + ".png";
}

[[nodiscard]] std::vector<std::string> split_lines(std::string_view value) {
    std::vector<std::string> lines;
    std::size_t start{};
    while (start <= value.size()) {
        const auto end = value.find('\n', start);
        const auto line = value.substr(start, end == std::string_view::npos ? value.npos
                                                                              : end - start);
        if (!line.empty()) lines.emplace_back(line);
        if (end == std::string_view::npos) break;
        start = end + 1U;
    }
    return lines;
}

/** Retail y-up to top-left conversion for a rectangle whose bottom is `y`. */
[[nodiscard]] constexpr double top_from_bottom(double y_up, double height) noexcept {
    return 600.0 - y_up - height;
}

/** SelectClass.draw_weapon_info (loadout rows): item_info_frame 243x316. */
void append_weapon_popup(ui::DrawList& list, const ClassSelectionAppearance& appearance,
                         DrawRect button, std::uint16_t item, std::uint8_t team) {
    constexpr double frame_width{243.0};
    constexpr double frame_height{316.0};
    // x = button.x + button.width + frame.width * 0.5 + 7, y = 252 (centre, y-up).
    const double cx = button.x + button.width + frame_width * 0.5 + 7.0;
    constexpr double cy_up{252.0};
    list.push(sprite(std::string{select_class_root} + "item_info_frame.png",
                     {cx - 121.0, 600.0 - cy_up - 158.0, frame_width, frame_height}));
    const double image_up = cy_up + 77.0;
    const double icon = 330.0 * 0.35;
    if (const auto asset = item_icon(appearance, item, team); !asset.empty()) {
        list.push(sprite(asset, {cx + 1.0 - icon * 0.5, 600.0 - image_up - icon * 0.5, icon, icon}));
    }
    double text_up = image_up - 100.0;
    const auto name_key = world::tool_name_key(item);
    list.push(text(std::string{name_key}, {cx - 105.0, top_from_bottom(text_up, 20.0),
                                           frame_width - 40.0, 20.0},
                   28.0, HorizontalTextAlignment::center, menu_color, "fonts/Edo.ttf"));
    const auto description_key = world::tool_description_key(item);
    if (description_key.empty()) return;
    const auto descriptions = appearance.localize
                                  ? split_lines(appearance.localize(description_key))
                                  : std::vector<std::string>{std::string{description_key}};
    text_up -= 19.0;
    const double negative_up = text_up - 65.0;
    bool positive = true;
    // class_loadout_description_font (STANDARD 11): line height 13, advanced
    // by line_height - 2 plus 5 after each single-line entry.
    constexpr double line_advance{11.0};
    for (auto description : descriptions) {
        if (description.starts_with("- ") && positive) {
            text_up = negative_up;
            positive = false;
        }
        if (description.starts_with("+ ") || description.starts_with("- ")) {
            description.erase(0U, 2U);
        }
        list.push(body_text(std::move(description),
                            {cx - 50.0, top_from_bottom(text_up, 20.0), frame_width - 90.0, 20.0}));
        text_up -= line_advance + 5.0;
    }
}

/** SelectClass.draw_prefab_info: prefab_info_frame 211x254, 25/30 px offset. */
void append_prefab_popup(ui::DrawList& list, const ClassSelectionAppearance& appearance,
                         DrawRect button, std::string_view name) {
    constexpr double frame_width{211.0};
    constexpr double frame_height{254.0};
    const double cx = button.x + button.width + 243.0 * 0.5 + 7.0 - 25.0;
    constexpr double cy_up{252.0 - 30.0};
    list.push(sprite(std::string{select_class_root} + "prefab_info_frame.png",
                     {cx - 105.0, 600.0 - cy_up - 127.0, frame_width, frame_height}));
    const bool flare = name == world::flare_block_construct;
    const double icon = flare ? 330.0 * 0.35 : 211.0 * 0.5;
    const double image_up = cy_up + 44.0;
    list.push(sprite(construct_icon(name),
                     {cx - icon * 0.5, 600.0 - image_up - icon * 0.5, icon, icon}));
    std::string name_key;
    std::string cost;
    if (flare) {
        name_key = std::string{world::tool_name_key(world::flare_block_tool)};
        cost = std::to_string(retail_flare_block_cost);
    } else {
        name_key.assign(name);
        std::ranges::transform(name_key, name_key.begin(), [](unsigned char character) {
            return static_cast<char>(character >= 'a' && character <= 'z' ? character - 32 : character);
        });
        if (appearance.prefab_block_count) {
            if (const auto count = appearance.prefab_block_count(name); count.has_value()) {
                cost = std::to_string(*count);
            }
        }
    }
    const double text_x = cx - 80.0;
    const double text_up = cy_up - 55.0;
    list.push(text(std::move(name_key), {text_x, top_from_bottom(text_up, 20.0), frame_width - 40.0, 20.0},
                   28.0, HorizontalTextAlignment::center, menu_color, "fonts/Edo.ttf"));
    const double usage_up = text_up - 45.0;
    auto usage = text("BLOCK_USAGE", {text_x, top_from_bottom(usage_up, 40.0), 110.0, 40.0}, 13.0,
                      HorizontalTextAlignment::left, menu_color, "fonts/Edo.ttf",
                      ui::TextTransform::preserve);
    list.push(std::move(usage));
    if (!cost.empty()) {
        list.push(text(std::move(cost), {text_x + 118.0, top_from_bottom(usage_up, 30.0), 30.0, 30.0},
                       13.0, HorizontalTextAlignment::center, menu_color, "fonts/Edo.ttf",
                       ui::TextTransform::preserve));
    }
}

/** SelectClass.draw_class_info: class_info_frame_left/right 317x185. */
void append_class_popup(ui::DrawList& list, const ClassSelectionMenuModel& menu,
                        const ClassSelectionAppearance& appearance, ui::Rect card,
                        std::uint8_t class_id) {
    constexpr double frame_width{317.0};
    constexpr double frame_height{185.0};
    const double offset = menu.classes_per_page() == 4U ? 26.0 : 64.0;
    const bool right_half = card.x >= 400;
    const double button_x = static_cast<double>(card.x);
    const double button_width = static_cast<double>(card.width);
    const double x = right_half ? button_x - frame_width + button_width + offset
                                : button_x + frame_width - offset;
    // button.y - text_height / 2 + 15 in y-up: the card centre, 15 px higher.
    const double card_top_up = 600.0 - static_cast<double>(card.y);
    double y_up = card_top_up - static_cast<double>(card.height) * 0.5 + 15.0;
    list.push(sprite(std::string{select_class_root} +
                         (right_half ? "class_info_frame_right.png" : "class_info_frame_left.png"),
                     {x - 158.0, 600.0 - y_up - 92.0, frame_width, frame_height}));
    const double name_x = (right_half ? x - 10.0 : x) - 130.0;
    const double name_up = y_up + 52.0;
    list.push(text(class_name_id(class_id),
                   {name_x, top_from_bottom(name_up, 28.0), frame_width - 60.0, 28.0}, 20.0,
                   HorizontalTextAlignment::center, menu_color, "fonts/Edo.ttf"));
    const double body_x = right_half ? x - 135.0 : x - 115.0;
    const auto description_key = world::class_description_key(class_id);
    const std::string description = localized(appearance, description_key);
    // split_text_to_fit_screen(font, text, 317, 60) wraps at ~257 px; the
    // renderer performs the wrap, the line count positions the block row.
    constexpr double wrap_width{257.0};
    constexpr double line_height{13.0};
    const auto estimated_lines = std::max<std::size_t>(
        1U, (description.size() * 55U / 10U + static_cast<std::size_t>(wrap_width) - 1U) /
                static_cast<std::size_t>(wrap_width));
    y_up += 10.0;
    auto body = body_text(std::string{appearance.localize ? std::string_view{description}
                                                          : description_key},
                          {body_x, 600.0 - (y_up + 20.0) - 10.0, wrap_width, line_height * 6.0},
                          ui::VerticalTextAlignment::top);
    body.layout = ui::TextLayout::bounded_wrapped_lines;
    body.maximum_lines = 6U;
    list.push(std::move(body));
    y_up -= line_height * static_cast<double>(estimated_lines);
    const double block_x = body_x + 10.0;
    y_up += 10.0;
    constexpr double block_icon{33.0};
    list.push(sprite(class_selection_item_icon(5U),
                     {block_x - block_icon * 0.5, 600.0 - (y_up + 5.0) - block_icon * 0.5,
                      block_icon, block_icon}));
    if (const auto* definition = world::find_class_definition(class_id); definition != nullptr) {
        const auto initial = static_cast<long long>(
            static_cast<double>(definition->initial_blocks) * appearance.block_wallet_multiplier);
        const auto maximum = static_cast<long long>(
            static_cast<double>(definition->maximum_blocks) * appearance.block_wallet_multiplier);
        auto blocks = body_text(localized(appearance, "BLOCKS") + ": " + std::to_string(initial) +
                                    " / " + std::to_string(maximum),
                                {block_x + 20.0, 600.0 - y_up, 200.0, 0.0},
                                ui::VerticalTextAlignment::baseline);
        list.push(std::move(blocks));
    }
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
    bool in_game, const ClassSelectionAppearance& appearance,
    ClassSelectionMenuModel::Clock::time_point now) const {
    const auto resolve=[](const auto& icons,auto id,std::string_view fallback){
        const auto found=icons.find(id);return found==icons.end()?std::string{fallback}:found->second;
    };
    static_cast<void>(window);
    in_game = in_game || menu.in_game();
    ui::DrawList list;
    list.reserve(192U);
    if (!in_game) {
        // Drawn over the live map (see ChangeTeamPresentation), no backdrop.
        list.push(sprite("png/ui/common_elements/frames/ui_frame_large.png",
                         {25.0, 5.0, 750.0, 589.0}));
    } else {
        list.push(sprite(std::string{select_class_root} + "in_game_class_frame.png",
                         {31.0, 26.0, 739.0, 569.0}));
    }
    list.push(text("CHOOSE_CLASS",
                   {182.0, in_game ? 37.0 : 16.0, 440.0, 59.0},
                   46.0, HorizontalTextAlignment::center));
    if (in_game) {
        // ugc_select_bg (326x74) stretched to (739 - 79) x (45 + 14) behind
        // the BACK button, centred at x=400 (selectClass.on_start).
        list.push(sprite("png/ui/ugc_tools/ugc_select_bg.png", {70.0, 513.0, 660.0, 59.0}));
    }
    list.push(sprite(std::string{select_class_root} +
                         (menu.classes_per_page() == 5U ? "class_background_frame_5.png"
                                                        : "class_background_frame.png"),
                     {31.0, 5.0, 739.0, 589.0}));

    const auto team_index = menu.team() == 3U ? 1U : 0U;
    const auto* selected_definition = world::find_class_definition(menu.selected_class());
    if (selected_definition != nullptr &&
        !selected_definition->team_portrait_assets[team_index].empty()) {
        auto portrait=sprite(
            resolve(appearance.class_portraits,menu.selected_class(),selected_definition->team_portrait_assets[team_index]),
            {604.5, 296.0, 111.0, 160.0});
        if(portrait.asset_id.starts_with("runtime/cosmetic/"))portrait.source_pixels=ClassSelectionAppearance::portrait_source;
        list.push(std::move(portrait));
    }

    if (in_game) {
        const auto hovered = menu.hovered();
        const bool back_hovered = hovered.has_value() &&
                                  ClassSelectionMenuModel::in_game_back_bounds.contains(*hovered);
        append_button(list, ClassSelectionMenuModel::in_game_back_bounds, "BACK", back_hovered,
                      menu.back_pressed() && back_hovered, true, 28.0);
    } else {
        append_navigation_back(list);
    }

    const auto classes = menu.classes();
    const auto offset = menu.visible_class_offset();
    const auto visible =
        std::min(menu.classes_per_page(), classes.size() - offset);
    const auto card_layout = menu.card_layout();
    // KeyDisplay: absolute numbering (str(index + 1)); hidden keys still fire.
    for (std::size_t display_index{}; display_index < visible; ++display_index) {
        const auto class_index = offset + display_index;
        if (class_index >= 10U) break;
        const bool held = menu.held_class_key() == class_index;
        list.push(sprite(
            std::string{"png/ui/icons/"} + (held ? "key_press" : "key") +
                std::to_string(class_index + 1U) + ".png",
            {72.0 + static_cast<double>(display_index) * card_layout.interval,
             99.0, 30.0, 30.0}));
    }
    for (std::size_t display_index{}; display_index < visible;
         ++display_index) {
        const auto class_index = offset + display_index;
        const auto* definition =
            world::find_class_definition(classes[class_index]);
        if (definition == nullptr) continue;
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
            append_selected_frame(list, card, card_layout.frame_scale, card_layout.frame_scale);
        }
    }

    const auto selected = menu.option_indices();
    for (std::size_t group{}; group < selected.size(); ++group) {
        const double top = 294.0 + static_cast<double>(group) * 53.0;
        // SELECT_CLASS_LOADOUT_FRAME: black 378x48 strip behind each row.
        list.push(solid({78.0, top - 3.0, 378.0, 48.0}, black));
        // The complete six-slot strip is always drawn; unused cells stay dark.
        for (std::size_t option{}; option < 6U; ++option) {
            list.push(solid({171.0 + static_cast<double>(option) * 45.0, top, 42.0, 42.0},
                            empty_slot_color));
        }
        const auto options = menu.row_options(group);
        const auto visible_options = std::min<std::size_t>(options.size(), 6U);
        for (std::size_t option{}; option < visible_options; ++option) {
            const DrawRect item{
                171.0 + static_cast<double>(option) * 45.0,
                top, 42.0, 42.0};
            list.push(sprite(std::string{select_class_root} + "loadout_background.png", item));
            const auto icon = item_icon(appearance, options[option], menu.team());
            if (!icon.empty()) {
                const double inset=icon.starts_with("runtime/cosmetic/")?1.5:4.5;
                list.push(sprite(icon,
                                 {item.x + inset, item.y + inset, item.width-2*inset, item.height-2*inset}));
            }
            if (option == selected[group]) {
                append_selected_frame(list, item, 0.3, 0.34);
            }
        }
    }

    // TableSelection frame: black 126x207 at (464, 291).
    list.push(solid({464.0, 291.0, 126.0, 207.0}, black));
    for (std::size_t index{}; index < ClassSelectionMenuModel::constructs_per_page; ++index) {
        const auto column = index % 3U;
        const auto row = index / 3U;
        list.push(solid({469.0 + static_cast<double>(column) * 41.0,
                         313.0 + static_cast<double>(row) * 43.0, 38.0, 38.0},
                        empty_slot_color));
    }
    const auto constructs = menu.construct_options();
    const auto first_construct = menu.construct_page() * ClassSelectionMenuModel::constructs_per_page;
    for (std::size_t cell{}; cell < ClassSelectionMenuModel::constructs_per_page; ++cell) {
        const auto index = first_construct + cell;
        if (index >= constructs.size()) break;
        const auto column = cell % 3U;
        const auto row = cell / 3U;
        const DrawRect item{
            469.0 + static_cast<double>(column) * 41.0,
            313.0 + static_cast<double>(row) * 43.0,
            38.0, 38.0};
        const auto& name = constructs[index];
        list.push(sprite(std::string{select_class_root} + "loadout_background.png", item));
        list.push(sprite(construct_icon(name), {item.x + 3.0, item.y + 3.0, 32.0, 32.0}));
        if (std::ranges::find(menu.selected_prefabs(), name) != menu.selected_prefabs().end()) {
            append_selected_frame(list, item, 0.27, 0.3);
        }
    }
    if (constructs.size() > ClassSelectionMenuModel::constructs_per_page) {
        // TableSelection paging: 14 px arrows plus the page number.
        list.push(sprite("png/ui/common_elements/scroll_bar/scroll_bar_arrow_left.png", {469.0, 484.0, 14.0, 14.0}));
        list.push(sprite("png/ui/common_elements/scroll_bar/scroll_bar_arrow_right.png", {574.0, 484.0, 14.0, 14.0}));
        list.push(text(std::to_string(menu.construct_page() + 1U), {506.0, 484.0, 40.0, 14.0},
                       11.0, HorizontalTextAlignment::center, menu_color,
                       "fonts/A750-Sans-Medium.ttf", ui::TextTransform::preserve));
    }

    const auto hovered = menu.hovered();
    const bool select_hovered =
        hovered.has_value() && ClassSelectionMenuModel::select_bounds.contains(*hovered);
    append_button(list, ClassSelectionMenuModel::select_bounds, "SELECT", select_hovered,
                  menu.select_pressed() && select_hovered, menu.select_enabled(), 28.0);
    // The scrollbar is drawn last so it covers enlarged card icons.
    append_scrollbar(list, menu);

    // Row labels: CLASS_ITEMS_NAME, EDO 10; multi-word labels wrap to two
    // centred lines (draw_text_within_boundaries).
    constexpr std::array<std::string_view, 4U> labels{
        "MELEE", "PRIMARY_WEAPONS", "SECONDARY_WEAPONS", "EQUIPMENT"};
    for (std::size_t group{}; group < labels.size(); ++group) {
        const double top = 294.0 + static_cast<double>(group) * 53.0;
        auto label = text(std::string{labels[group]}, {84.0, top, 80.0, 42.0}, 10.0,
                          HorizontalTextAlignment::center, menu_color, "fonts/Edo.ttf");
        if (group == 1U || group == 2U) {
            label.layout = ui::TextLayout::bounded_wrapped_lines;
            label.maximum_lines = 2U;
            label.vertical_alignment = ui::VerticalTextAlignment::top;
            label.destination = {84.0, top + 8.0, 80.0, 30.0};
        }
        list.push(std::move(label));
    }
    list.push(text("PREFABS", {470.0, 291.0, 114.0, 20.0}, 10.0,
                   HorizontalTextAlignment::center, menu_color, "fonts/Edo.ttf"));
    for (std::size_t display_index{}; display_index < visible; ++display_index) {
        const auto class_index = offset + display_index;
        list.push(text(
            class_name_id(classes[class_index]),
            {card_layout.name_x + static_cast<double>(display_index) * card_layout.interval,
             96.0, static_cast<double>(card_layout.name_width), 31.0},
            20.0, HorizontalTextAlignment::center,
            class_index == menu.selected_class_index() ? selected_color
                                                        : menu_color,
            "fonts/Edo.ttf"));
    }

    if (menu.popup_visible(now)) {
        const auto item = *menu.hovered_item();
        switch (item.kind) {
        case ClassSelectionHover::Kind::class_card:
            if (item.index >= offset && item.index < offset + visible) {
                append_class_popup(list, menu, appearance,
                                   menu.class_card_bounds(item.index - offset),
                                   classes[item.index]);
            }
            break;
        case ClassSelectionHover::Kind::loadout_item: {
            const auto options = menu.row_options(item.group);
            if (item.index < options.size()) {
                const DrawRect button{171.0 + static_cast<double>(item.index) * 45.0,
                                      294.0 + static_cast<double>(item.group) * 53.0, 42.0, 42.0};
                append_weapon_popup(list, appearance, button, options[item.index], menu.team());
                list.push(sprite(std::string{select_class_root} + "frame_arrow.png",
                                 {button.x + button.width - 13.5,
                                  button.y + button.height * 0.5 - 17.0, 27.0, 34.0}));
            }
            break;
        }
        case ClassSelectionHover::Kind::construct:
            if (item.index < constructs.size() && item.index >= first_construct) {
                const auto cell = item.index - first_construct;
                const DrawRect button{469.0 + static_cast<double>(cell % 3U) * 41.0,
                                      313.0 + static_cast<double>(cell / 3U) * 43.0, 38.0, 38.0};
                append_prefab_popup(list, appearance, button, constructs[item.index]);
                list.push(sprite(std::string{select_class_root} + "frame_arrow.png",
                                 {button.x + button.width - 9.0 - 13.5,
                                  button.y + button.height * 0.5 - 17.0, 27.0, 34.0}));
            }
            break;
        }
    }
    return list;
}

} // namespace battlespades::frontend
