#include "battlespades/frontend/parity_debug_presentation.hpp"

#include "battlespades/frontend/custom_match_presentation.hpp"
#include "battlespades/frontend/main_menu.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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
constexpr ColorRgba8 green{118U, 222U, 145U, 255U};
constexpr ColorRgba8 red{238U, 103U, 103U, 255U};
constexpr ColorRgba8 row_grey{57U, 53U, 44U, 255U};
constexpr ColorRgba8 row_dark{24U, 21U, 14U, 255U};
constexpr ColorRgba8 row_selected{77U, 66U, 31U, 255U};
constexpr ColorRgba8 button_text{20U, 20U, 20U, 255U};
constexpr double source_scale{0.6};
constexpr double global_scale{0.64};

constexpr DrawRect frame{25.0, 5.0, 750.0, 589.0};
constexpr DrawRect title{120.0, 25.0, 560.0, 50.0};
constexpr DrawRect back{54.0, 541.0, 105.0, 32.0};
constexpr DrawRect list_panel{56.0, 95.0, 340.0, 413.0};
constexpr DrawRect list_header{66.0, 105.0, 320.0, 40.0};
constexpr DrawRect filter{242.0, 114.0, 130.0, 24.0};
constexpr DrawRect first_list_row{66.0, 155.0, 320.0, 25.0};
constexpr DrawRect detail_panel{401.0, 95.0, 340.0, 354.0};
constexpr DrawRect detail_header{411.0, 105.0, 320.0, 40.0};
constexpr DrawRect first_detail_row{411.0, 155.0, 320.0, 25.0};
constexpr DrawRect action_panel{401.0, 452.0, 340.0, 54.0};
constexpr DrawRect open_button{405.0, 456.0, 162.0, 50.0};
constexpr DrawRect hitbox_button{575.0, 456.0, 162.0, 50.0};

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

[[nodiscard]] ColorModulation color(ColorRgba8 value = white,
                                    std::uint16_t intensity = 1'000U,
                                    std::uint16_t opacity = 1'000U) noexcept {
    return ColorModulation{value, intensity, opacity};
}

[[nodiscard]] SpriteDrawCommand sprite(std::string_view asset,
                                       DrawRect destination,
                                       DrawSpace space = DrawSpace::design_pixels,
                                       TextureFilter filter_mode = TextureFilter::linear,
                                       UiTextureAnchor anchor = UiTextureAnchor::top_left,
                                       double retail_scale = source_scale,
                                       ColorModulation modulation = color(),
                                       SpriteSizing sizing = SpriteSizing::stretch) {
    return SpriteDrawCommand{std::string{asset},
                             destination,
                             space,
                             filter_mode,
                             anchor,
                             retail_scale,
                             sizing,
                             modulation};
}

[[nodiscard]] TextDrawCommand text(std::string_view value,
                                   DrawRect destination,
                                   double size,
                                   ColorRgba8 text_color = cream,
                                   HorizontalTextAlignment horizontal =
                                       HorizontalTextAlignment::left,
                                   std::string_view font = custom_match_assets::row_font,
                                   std::uint8_t maximum_lines = 1U,
                                   TextTransform transform = TextTransform::preserve,
                                   std::uint16_t intensity = 1'000U) {
    return TextDrawCommand{std::string{value},
                           std::string{font},
                           destination,
                           DrawSpace::design_pixels,
                           size,
                           1.0,
                           maximum_lines,
                           horizontal,
                           VerticalTextAlignment::center,
                           transform,
                           TextFit::shrink_to_fit,
                           color(text_color, intensity)};
}

void solid(ui::DrawList& list, DrawRect bounds, ColorRgba8 fill,
           std::uint16_t opacity = 1'000U) {
    list.push(sprite(custom_match_assets::white_pixel,
                     bounds,
                     DrawSpace::design_pixels,
                     TextureFilter::nearest,
                     UiTextureAnchor::top_left,
                     1.0,
                     color(fill, 1'000U, opacity)));
}

void outline(ui::DrawList& list, DrawRect bounds, ColorRgba8 stroke) {
    constexpr double width{1.0};
    solid(list, {bounds.x, bounds.y, bounds.width, width}, stroke);
    solid(list, {bounds.x, bounds.y + bounds.height - width, bounds.width, width}, stroke);
    solid(list, {bounds.x, bounds.y, width, bounds.height}, stroke);
    solid(list, {bounds.x + bounds.width - width, bounds.y, width, bounds.height}, stroke);
}

void append_panel(ui::DrawList& list,
                  DrawRect panel,
                  DrawRect header,
                  std::string_view label) {
    list.push(sprite(custom_match_assets::panel,
                     panel,
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
    list.push(sprite(custom_match_assets::panel_header,
                     header,
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
    list.push(text(label,
                   {header.x + 14.0, header.y, header.width - 28.0, header.height},
                   18.0,
                   cream,
                   HorizontalTextAlignment::left,
                   custom_match_assets::header_font,
                   1U,
                   TextTransform::uppercase));
}

void append_button(ui::DrawList& list,
                   DrawRect bounds,
                   std::string_view label,
                   bool enabled,
                   bool hovered) {
    const auto state = hovered && enabled ? 1U : 0U;
    const auto intensity = enabled ? std::uint16_t{1'000U} : std::uint16_t{600U};
    const auto slice_width = std::floor((36.0 / 58.0) * bounds.height);
    const auto middle_width = bounds.width - slice_width * 2.0;
    const std::array destinations{
        DrawRect{bounds.x, bounds.y, slice_width + 1.0, bounds.height},
        DrawRect{bounds.x + slice_width, bounds.y, middle_width + 1.0, bounds.height},
        DrawRect{bounds.x + slice_width + middle_width,
                 bounds.y,
                 slice_width + 1.0,
                 bounds.height},
    };
    for (std::size_t slice_index{}; slice_index < destinations.size(); ++slice_index) {
        list.push(sprite(button_assets[state][slice_index],
                         destinations[slice_index],
                         DrawSpace::design_pixels,
                         TextureFilter::nearest,
                         UiTextureAnchor::top_left,
                         source_scale,
                         color(white, intensity)));
    }
    list.push(text(label,
                   {bounds.x + 12.0, bounds.y + 4.0, bounds.width - 24.0, bounds.height - 8.0},
                   16.0,
                   button_text,
                   HorizontalTextAlignment::center,
                   main_menu_assets::button_font,
                   2U,
                   TextTransform::uppercase,
                   intensity));
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

struct DetailLine final {
    std::string value;
    ColorRgba8 value_color{cream};
};

[[nodiscard]] std::vector<DetailLine> detail_lines(const ParityCatalogEntry& entry) {
    std::vector<DetailLine> lines;
    lines.reserve(7U + entry.widgets.size() + entry.assets.size() + entry.substates.size());
    lines.push_back({"ID: " + entry.id, gold});
    lines.push_back({"CLASS: " + entry.retail_class, cream});
    lines.push_back({"KIND: " + std::string{parity_kind_label(entry.kind)}, cream});
    lines.push_back({"ROUTE: " + (entry.route.empty() ? std::string{"-"} : entry.route), cream});
    lines.push_back({"SOURCE: " + entry.source, cream});
    lines.push_back({"PARENT: " + (entry.parent_screen.empty() ? std::string{"-"}
                                                               : entry.parent_screen),
                     cream});
    lines.push_back({entry.native_fixture ? "STATUS: NATIVE FIXTURE"
                                          : "STATUS: CATALOG ONLY",
                     entry.native_fixture ? green : red});
    for (const auto& widget : entry.widgets) {
        lines.push_back({"CONTROL: " + widget, cream});
    }
    for (const auto& state : entry.substates) {
        lines.push_back({"STATE: " + state, cream});
    }
    for (const auto& asset : entry.assets) {
        lines.push_back({"ASSET: " + asset, cream});
    }
    return lines;
}

} // namespace

ui::DrawList ParityDebugPresentation::build(
    const ParityDebugMenuModel& model,
    const ParityDebugPresentationContext& context) const {
    if (!context.window.is_valid() || context.background_opacity_per_mille > 1'000U) {
        throw std::invalid_argument{"invalid parity debug presentation context"};
    }

    ui::DrawList list;
    list.reserve(180U);
    list.push(sprite(main_menu_assets::background,
                     background_cover(context.window),
                     DrawSpace::window_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::top_left,
                     source_scale,
                     color(white, 1'000U, context.background_opacity_per_mille),
                     SpriteSizing::cover));
    list.push(sprite(custom_match_assets::frame,
                     frame,
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
    list.push(text("RETAIL UI PARITY",
                   title,
                   40.0,
                   cream,
                   HorizontalTextAlignment::center,
                   custom_match_assets::title_font,
                   1U,
                   TextTransform::uppercase));

    append_panel(list, list_panel, list_header, "CATALOG");
    solid(list, filter, row_dark);
    list.push(text("< " + std::string{model.filter_label()} + " >",
                   filter,
                   12.0,
                   model.filter_hovered() ? gold : cream,
                   HorizontalTextAlignment::center,
                   custom_match_assets::row_font,
                   1U,
                   TextTransform::uppercase));

    const auto catalog = retail_frontend_catalog();
    const auto filtered = model.filtered_indices();
    const auto first = std::min(model.first_visible_row(), filtered.size());
    const auto count = std::min(ParityDebugMenuModel::visible_rows, filtered.size() - first);
    const auto selected = model.selected_filtered_row();
    for (std::size_t visible{}; visible < count; ++visible) {
        const auto filtered_row = first + visible;
        const auto catalog_index = filtered[filtered_row];
        if (catalog_index >= catalog.size()) {
            continue;
        }
        const auto& entry = catalog[catalog_index];
        const DrawRect row{first_list_row.x,
                           first_list_row.y + first_list_row.height *
                                                      static_cast<double>(visible),
                           first_list_row.width,
                           first_list_row.height};
        const bool is_selected = selected.has_value() && *selected == filtered_row;
        const bool is_hovered = model.hovered_visible_row().has_value() &&
                                *model.hovered_visible_row() == visible;
        solid(list,
              row,
              is_selected ? row_selected : (visible % 2U == 0U ? row_grey : row_dark));
        if (is_hovered) {
            outline(list, row, gold);
        }
        list.push(text(entry.retail_class,
                       {row.x + 8.0, row.y, 205.0, row.height},
                       11.0,
                       entry.native_fixture ? green : cream));
        list.push(text(parity_kind_label(entry.kind),
                       {row.x + 218.0, row.y, 94.0, row.height},
                       9.0,
                       entry.native_fixture ? green : cream,
                       HorizontalTextAlignment::right));
    }

    const auto* entry = model.selected_entry();
    append_panel(list,
                 detail_panel,
                 detail_header,
                 entry == nullptr ? std::string_view{"DETAILS"}
                                  : std::string_view{entry->retail_class});
    if (entry != nullptr) {
        const auto lines = detail_lines(*entry);
        const auto detail_first = std::min(model.detail_first_row(), lines.size());
        constexpr std::size_t visible_details{11U};
        const auto detail_count = std::min(visible_details, lines.size() - detail_first);
        for (std::size_t visible{}; visible < detail_count; ++visible) {
            const auto& line = lines[detail_first + visible];
            const DrawRect row{first_detail_row.x,
                               first_detail_row.y + first_detail_row.height *
                                                          static_cast<double>(visible),
                               first_detail_row.width,
                               first_detail_row.height};
            solid(list, row, visible % 2U == 0U ? row_grey : row_dark);
            list.push(text(line.value,
                           {row.x + 8.0, row.y, row.width - 16.0, row.height},
                           10.0,
                           line.value_color));
        }
        if (lines.size() > visible_details) {
            list.push(text(std::to_string(detail_first + 1U) + "-" +
                               std::to_string(detail_first + detail_count) + "/" +
                               std::to_string(lines.size()),
                           {detail_header.x + 225.0,
                            detail_header.y,
                            82.0,
                            detail_header.height},
                           9.0,
                           gold,
                           HorizontalTextAlignment::right));
        }
    }

    list.push(sprite(custom_match_assets::panel,
                     action_panel,
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale));
    append_button(list,
                  open_button,
                  "OPEN NATIVE",
                  entry != nullptr && entry->native_fixture,
                  model.open_hovered());
    append_button(list,
                  hitbox_button,
                  model.show_hitboxes() ? "HITBOXES ON" : "HITBOXES OFF",
                  true,
                  model.hitbox_hovered());

    list.push(text("BACK",
                   {back.x + 35.0, back.y, back.width - 35.0, back.height},
                   22.0,
                   gold,
                   HorizontalTextAlignment::left,
                   main_menu_assets::button_font,
                   1U,
                   TextTransform::uppercase,
                   850U));
    list.push(sprite(custom_match_assets::back_icon,
                     {back.x + 2.5, back.y + 3.5, 25.0, 25.0},
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     global_scale,
                     color(white, 850U)));

    if (model.show_hitboxes()) {
        outline(list, filter, gold);
        outline(list, back, gold);
        outline(list, open_button, entry != nullptr && entry->native_fixture ? green : red);
        outline(list, hitbox_button, gold);
        outline(list, list_panel, red);
        outline(list, detail_panel, red);
    }
    return list;
}

} // namespace battlespades::frontend
