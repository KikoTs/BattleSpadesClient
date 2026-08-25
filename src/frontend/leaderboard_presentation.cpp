#include "battlespades/frontend/leaderboard_presentation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
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
constexpr ColorRgba8 cream{244U, 236U, 187U, 255U};
constexpr ColorRgba8 red_header{162U, 58U, 30U, 255U};
constexpr ColorRgba8 row_grey{57U, 53U, 44U, 255U};
constexpr ColorRgba8 row_dark{24U, 21U, 14U, 255U};
constexpr ColorRgba8 row_hovered{175U, 172U, 161U, 255U};
constexpr ColorRgba8 dropdown_grey{34U, 32U, 33U, 255U};
constexpr ColorRgba8 scrollbar_track{73U, 63U, 7U, 255U};
constexpr ColorRgba8 disabled{86U, 86U, 86U, 255U};
constexpr ColorRgba8 black{0U, 0U, 0U, 255U};

constexpr double row_height{25.0};
constexpr double header_top{150.0};
constexpr double rank_width_minimum{50.0};
constexpr double name_width{143.0};
constexpr double column_text_padding{25.0};
constexpr double column_side_padding{10.0};
constexpr double scrollbar_button{22.0};

[[nodiscard]] ColorModulation color(ColorRgba8 value = white,
                                    std::uint16_t opacity = 1'000U) noexcept {
    return ColorModulation{value, 1'000U, opacity};
}

[[nodiscard]] SpriteDrawCommand sprite(std::string_view asset,
                                       DrawRect destination,
                                       DrawSpace space = DrawSpace::design_pixels,
                                       TextureFilter filter = TextureFilter::linear,
                                       TextureAnchor anchor = TextureAnchor::top_left,
                                       double source_scale = 0.6,
                                       ColorModulation modulation = color(),
                                       SpriteSizing sizing = SpriteSizing::stretch) {
    return SpriteDrawCommand{
        std::string{asset}, destination, space, filter, anchor, source_scale, sizing, modulation};
}

[[nodiscard]] TextDrawCommand text(std::string_view key,
                                   DrawRect destination,
                                   double size,
                                   std::string_view font,
                                   ColorRgba8 text_color = cream,
                                   HorizontalTextAlignment horizontal =
                                       HorizontalTextAlignment::left,
                                   TextTransform transform = TextTransform::preserve,
                                   VerticalTextAlignment vertical =
                                       VerticalTextAlignment::retail_center) {
    return TextDrawCommand{std::string{key},
                           std::string{font},
                           destination,
                           DrawSpace::design_pixels,
                           size,
                           1.0,
                           1U,
                           horizontal,
                           vertical,
                           transform,
                           TextFit::retail_width_scale,
                           color(text_color)};
}

[[nodiscard]] std::string retail_fitted_player_name(
    std::string_view original,
    const LeaderboardPresentationContext& context) {
    // LeaderboardListPanel.populate calls modify_name_to_fix_width(name, 143,
    // small_standard_ui_font) before the row's ordinary width validation. It
    // removes one Unicode character at a time and measures the candidate plus
    // three literal periods. Preserve that two-stage behavior instead of
    // merely shrinking the whole name into the 122px text interior.
    if (!context.measure_row_text) return std::string{original};
    const auto width = context.measure_row_text(original, 11.0);
    if (!std::isfinite(width) || width <= name_width) return std::string{original};

    std::string prefix{original};
    while (!prefix.empty()) {
        auto boundary = prefix.size() - 1U;
        while (boundary > 0U &&
               (static_cast<unsigned char>(prefix[boundary]) & 0xC0U) == 0x80U) {
            --boundary;
        }
        prefix.resize(boundary);
        auto candidate = prefix + "...";
        const auto candidate_width = context.measure_row_text(candidate, 11.0);
        if (std::isfinite(candidate_width) && candidate_width <= name_width) {
            return candidate;
        }
    }
    return "...";
}

void solid(ui::DrawList& list, DrawRect rectangle, ColorRgba8 fill) {
    list.push(sprite(leaderboard_presentation_assets::white_pixel,
                     rectangle,
                     DrawSpace::design_pixels,
                     TextureFilter::nearest,
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

[[nodiscard]] std::string_view scope_key(LeaderboardScope scope) noexcept {
    constexpr std::array keys{
        std::string_view{"GLOBAL"},
        std::string_view{"LOCAL"},
        std::string_view{"FRIENDS"},
    };
    const auto selected = static_cast<std::size_t>(scope);
    return keys[selected < keys.size() ? selected : 0U];
}

[[nodiscard]] double fallback_header_width(std::string_view key) noexcept {
    // Widths are from the shipped Edo.ttf at the retail 11-pixel face. The
    // native runtime supplies shaped/localized measurements; these keep unit
    // tests and fail-closed renderer paths deterministic.
    constexpr std::array widths{
        std::pair{std::string_view{"LEADERBOARD_RANK"}, 24.0},
        std::pair{std::string_view{"LEADERBOARD_NAME"}, 26.0},
        std::pair{std::string_view{"LEADERBOARD_TOTAL"}, 29.0},
        std::pair{std::string_view{"LEADERBOARD_KILLS"}, 26.0},
        std::pair{std::string_view{"LEADERBOARD_DEATHS"}, 34.0},
        std::pair{std::string_view{"LEADERBOARD_KDR"}, 17.0},
        std::pair{std::string_view{"LEADERBOARD_WINS"}, 22.0},
        std::pair{std::string_view{"LEADERBOARD_LOSSES"}, 34.0},
        std::pair{std::string_view{"LEADERBOARD_HEADSHOT"}, 47.0},
        std::pair{std::string_view{"LEADERBOARD_MELEE"}, 31.0},
        std::pair{std::string_view{"LEADERBOARD_ASSIST"}, 29.0},
        std::pair{std::string_view{"LEADERBOARD_RETRIBUTION"}, 58.0},
        std::pair{std::string_view{"LEADERBOARD_DEFENCE"}, 42.0},
        std::pair{std::string_view{"LEADERBOARD_SURVIVAL"}, 43.0},
        std::pair{std::string_view{"LEADERBOARD_ASSAULT"}, 39.0},
        std::pair{std::string_view{"LEADERBOARD_DEFEND"}, 36.0},
        std::pair{std::string_view{"LEADERBOARD_ESCORT"}, 34.0},
        std::pair{std::string_view{"LEADERBOARD_OCCUPY"}, 36.0},
        std::pair{std::string_view{"LEADERBOARD_CLAIM"}, 28.0},
        std::pair{std::string_view{"LEADERBOARD_CONTROL"}, 43.0},
        std::pair{std::string_view{"LEADERBOARD_CONTEST"}, 41.0},
        std::pair{std::string_view{"LEADERBOARD_BOMB"}, 26.0},
        std::pair{std::string_view{"LEADERBOARD_CARRY"}, 27.0},
        std::pair{std::string_view{"LEADERBOARD_CAPTURE"}, 40.0},
        std::pair{std::string_view{"LEADERBOARD_UNCOVER"}, 43.0},
        std::pair{std::string_view{"LEADERBOARD_STEAL"}, 28.0},
        std::pair{std::string_view{"LEADERBOARD_LASTMANSTANDING"}, 94.0},
        std::pair{std::string_view{"LEADERBOARD_KILLSURVIVOR"}, 67.0},
        std::pair{std::string_view{"LEADERBOARD_KILLSASLASTMAN"}, 88.0},
        std::pair{std::string_view{"LEADERBOARD_DESTROY"}, 39.0},
        std::pair{std::string_view{"LEADERBOARD_REPAIR"}, 31.0},
        std::pair{std::string_view{"LEADERBOARD_FIRST"}, 23.0},
    };
    const auto found = std::ranges::find_if(widths, [key](const auto& item) {
        return item.first == key;
    });
    return found == widths.end() ? static_cast<double>(key.size()) * 5.5 : found->second;
}

[[nodiscard]] double header_width(const LeaderboardPresentationContext& context,
                                  std::string_view key) {
    if (context.measure_header_text) {
        const auto measured = context.measure_header_text(key, 11.0);
        if (std::isfinite(measured) && measured >= 0.0) {
            return measured;
        }
    }
    return fallback_header_width(key);
}

void append_square_button(ui::DrawList& list,
                          DrawRect bounds,
                          std::string_view icon,
                          bool enabled) {
    const auto tint = enabled ? white : disabled;
    list.push(sprite(leaderboard_presentation_assets::square_button,
                     bounds,
                     DrawSpace::design_pixels,
                     TextureFilter::nearest,
                     TextureAnchor::top_left,
                     1.0,
                     color(tint)));
    list.push(sprite(icon,
                     bounds,
                     DrawSpace::design_pixels,
                     TextureFilter::nearest,
                     TextureAnchor::center,
                     1.0,
                     color(tint)));
}

void append_dropdown(ui::DrawList& list,
                     DrawRect bounds,
                     std::string_view selected_key,
                     bool enabled) {
    // MenuOptionControl: 4-pixel border spacing, 2-pixel bar/button gap, and a
    // 12-pixel SquareButton for the recovered 20-pixel leaderboard controls.
    constexpr double spacing{4.0};
    constexpr double button_gap{2.0};
    const auto button_size = bounds.height - spacing * 2.0;
    const auto bar_width = bounds.width - spacing * 2.0 - button_size - button_gap;
    solid(list, bounds, black);
    solid(list,
          {bounds.x + spacing, bounds.y + spacing, bar_width, button_size},
          dropdown_grey);
    const auto button_bounds = DrawRect{bounds.x + bounds.width - spacing - button_size,
                                        bounds.y + spacing,
                                        button_size,
                                        button_size};
    append_square_button(list, button_bounds, leaderboard_presentation_assets::arrow_down, enabled);
    list.push(text(selected_key,
                   {bounds.x + 14.0,
                    bounds.y,
                    bounds.width - 28.0 - button_size - button_gap,
                    bounds.height},
                   14.0,
                   leaderboard_presentation_assets::dropdown_font,
                   cream,
                   HorizontalTextAlignment::left,
                   TextTransform::uppercase));
}

void append_red_header(ui::DrawList& list, DrawRect bounds) {
    // Both 40px cap images are loaded at global_images.global_scale=0.64,
    // truncated to 25px. Retail translates their centre to x+10 / right-12.
    constexpr double cap{25.0};
    list.push(sprite(leaderboard_presentation_assets::red_header_left,
                     {bounds.x - 2.0, bounds.y, cap, bounds.height},
                     DrawSpace::design_pixels,
                     TextureFilter::nearest,
                     TextureAnchor::center));
    list.push(sprite(leaderboard_presentation_assets::red_header_right,
                     {bounds.x + bounds.width - 24.0,
                      bounds.y,
                      cap,
                      bounds.height},
                     DrawSpace::design_pixels,
                     TextureFilter::nearest,
                     TextureAnchor::center));
}

void append_selection(ui::DrawList& list, DrawRect row) {
    list.push(sprite(leaderboard_presentation_assets::selection_line,
                     row,
                     DrawSpace::design_pixels,
                     TextureFilter::nearest,
                     TextureAnchor::top_left,
                     0.6));
    const auto glow_x = row.width * 0.05;
    const auto glow_y = row.height * 0.30;
    list.push(sprite(leaderboard_presentation_assets::selection_glow,
                     {row.x - glow_x * 0.5,
                      row.y - glow_y * 0.5,
                      row.width + glow_x,
                      row.height + glow_y},
                     DrawSpace::design_pixels,
                     TextureFilter::nearest,
                     TextureAnchor::top_left,
                     0.6));
}

void append_dropdown_options(ui::DrawList& list,
                             DrawRect title,
                             std::span<const std::string_view> keys,
                             std::size_t selected) {
    constexpr double option_height{20.0};
    const DrawRect area{title.x,
                        title.y + title.height + 1.0,
                        title.width,
                        option_height * static_cast<double>(keys.size())};
    solid(list, area, black);
    for (std::size_t index{}; index < keys.size(); ++index) {
        const DrawRect row{area.x,
                           area.y + static_cast<double>(index) * option_height,
                           area.width,
                           option_height};
        solid(list, row, index % 2U == 0U ? row_grey : row_dark);
        if (index == selected) append_selection(list, row);
        list.push(text(keys[index],
                       {row.x + 14.0, row.y, row.width - 28.0, row.height},
                       11.0,
                       leaderboard_presentation_assets::row_font,
                       cream,
                       HorizontalTextAlignment::left,
                       TextTransform::preserve));
    }
}

void append_horizontal_scrollbar(ui::DrawList& list,
                                 const LeaderboardGridLayout& grid,
                                 std::size_t stat_count,
                                 std::size_t first_stat) {
    if (!grid.shows_horizontal_scrollbar || stat_count == 0U) {
        return;
    }
    const auto bounds = grid.horizontal_scrollbar;
    solid(list, bounds, black);
    const auto track = DrawRect{bounds.x + scrollbar_button + 2.0,
                                bounds.y + 1.0,
                                bounds.width - scrollbar_button * 2.0 - 4.0,
                                scrollbar_button - 2.0};
    solid(list, track, scrollbar_track);
    const auto maximum = stat_count - grid.visible_stat_columns;
    append_square_button(list,
                         {bounds.x, bounds.y, scrollbar_button, scrollbar_button},
                         leaderboard_presentation_assets::arrow_left,
                         first_stat > 0U);
    append_square_button(list,
                         {bounds.x + bounds.width - scrollbar_button,
                          bounds.y,
                          scrollbar_button,
                          scrollbar_button},
                         leaderboard_presentation_assets::arrow_right,
                         first_stat < maximum);

    const auto thumb_width = std::max(
        7.0,
        std::floor(track.width * static_cast<double>(grid.visible_stat_columns) /
                   static_cast<double>(stat_count)));
    const auto travel = std::max(0.0, track.width - thumb_width);
    const auto progress = maximum == 0U
                              ? 0.0
                              : static_cast<double>(std::min(first_stat, maximum)) /
                                    static_cast<double>(maximum);
    // scrollbar_* is loaded at 0.6: the 3px truncated cap becomes 3.75px
    // when scaled from its 16px thickness to the runtime 20px bar.
    const auto thumb_x = bounds.x + scrollbar_button + 1.0 + travel * progress;
    constexpr double cap{3.75};
    list.push(sprite(leaderboard_presentation_assets::scrollbar_left,
                     {thumb_x, track.y, cap, track.height},
                     DrawSpace::design_pixels,
                     TextureFilter::nearest,
                     TextureAnchor::center));
    list.push(sprite(leaderboard_presentation_assets::scrollbar_hmid,
                     {thumb_x + cap, track.y, std::max(0.0, thumb_width - cap * 2.0), track.height},
                     DrawSpace::design_pixels,
                     TextureFilter::nearest,
                     TextureAnchor::center));
    list.push(sprite(leaderboard_presentation_assets::scrollbar_right,
                     {thumb_x + thumb_width - cap, track.y, cap, track.height},
                     DrawSpace::design_pixels,
                     TextureFilter::nearest,
                     TextureAnchor::center));
}

void append_vertical_scrollbar(ui::DrawList& list,
                               const LeaderboardGridLayout& grid,
                               std::size_t row_count,
                               std::size_t first_row) {
    if (!grid.shows_vertical_scrollbar || row_count == 0U) {
        return;
    }
    const auto bounds = grid.vertical_scrollbar;
    solid(list, bounds, black);
    const auto track = DrawRect{bounds.x + 1.0,
                                bounds.y + scrollbar_button + 2.0,
                                scrollbar_button - 2.0,
                                bounds.height - scrollbar_button * 2.0 - 4.0};
    solid(list, track, scrollbar_track);
    const auto maximum = row_count - grid.visible_rows;
    append_square_button(list,
                         {bounds.x, bounds.y, scrollbar_button, scrollbar_button},
                         leaderboard_presentation_assets::arrow_up,
                         first_row > 0U);
    append_square_button(list,
                         {bounds.x,
                          bounds.y + bounds.height - scrollbar_button,
                          scrollbar_button,
                          scrollbar_button},
                         leaderboard_presentation_assets::arrow_down,
                         first_row < maximum);

    const auto thumb_height = std::max(
        7.0,
        std::floor(track.height * static_cast<double>(grid.visible_rows) /
                   static_cast<double>(row_count)));
    const auto travel = std::max(0.0, track.height - thumb_height);
    const auto progress = maximum == 0U
                              ? 0.0
                              : static_cast<double>(std::min(first_row, maximum)) /
                                    static_cast<double>(maximum);
    const auto thumb_y = track.y + travel * progress;
    constexpr double cap{3.75};
    list.push(sprite(leaderboard_presentation_assets::scrollbar_top,
                     {track.x, thumb_y, track.width, cap},
                     DrawSpace::design_pixels,
                     TextureFilter::nearest,
                     TextureAnchor::center));
    if (thumb_height > cap * 2.0) {
        list.push(sprite(leaderboard_presentation_assets::scrollbar_mid,
                         {track.x, thumb_y + cap, track.width, thumb_height - cap * 2.0},
                         DrawSpace::design_pixels,
                         TextureFilter::nearest,
                         TextureAnchor::center));
    }
    list.push(sprite(leaderboard_presentation_assets::scrollbar_bottom,
                     {track.x, thumb_y + thumb_height - cap, track.width, cap},
                     DrawSpace::design_pixels,
                     TextureFilter::nearest,
                     TextureAnchor::center));
}

void append_navigation_back(ui::DrawList& list, DrawRect bounds) {
    // NavigationBar positions the 24px (40 * .6) icon around x+2.5 and uses
    // navigation_font = ALDO 24. Non-hovered retail modulation is 0.7.
    const auto faded_gold = ColorRgba8{162U, 144U, 54U, 255U};
    list.push(sprite(leaderboard_presentation_assets::back_icon,
                     {bounds.x - 9.5, bounds.y + 3.0, 24.0, 24.0},
                     DrawSpace::design_pixels,
                     TextureFilter::nearest,
                     TextureAnchor::center,
                     0.6,
                     color(ColorRgba8{179U, 179U, 179U, 255U})));
    list.push(text("BACK",
                   {bounds.x + 29.0, bounds.y, bounds.width - 29.0, bounds.height},
                   24.0,
                   leaderboard_presentation_assets::title_font,
                   faded_gold,
                   HorizontalTextAlignment::left,
                   TextTransform::uppercase));
}

} // namespace

LeaderboardClassicLayout leaderboard_classic_layout() noexcept {
    // Retail constants are bottom-left. These are their exact top-left 800x600
    // conversions after load_texture's 0.6 truncation for the outer frame.
    return LeaderboardClassicLayout{
        {2.0, 24.0, 796.0, 552.0},
        {250.0, 20.0, 300.0, 80.0},
        {30.0, 110.0, 300.0, 20.0},
        {340.0, 110.0, 200.0, 20.0},
        {30.0, header_top, 740.0, 350.0},
        {30.0, 525.0, 100.0, 30.0},
        {233.0, 484.0, 507.0, 22.0},
    };
}

LeaderboardGridLayout leaderboard_grid_layout(const LeaderboardMenuModel& model,
                                               const LeaderboardPresentationContext& context) {
    const auto& definition = leaderboard_definition(model.selected_type());
    const auto stat_count = definition.column_keys.size() > 2U
                                ? definition.column_keys.size() - 2U
                                : 0U;

    auto make_grid = [&](bool vertical_scrollbar) {
        LeaderboardGridLayout grid;
        const auto row_width = vertical_scrollbar ? 688.0 : 720.0;
        grid.header = {40.0, header_top, row_width, row_height};
        const auto rank_width =
            std::max(rank_width_minimum,
                     header_width(context, definition.column_keys[0]) + column_text_padding);
        const auto available_stats = std::max(0.0, row_width - rank_width - name_width);
        std::vector<double> required;
        required.reserve(stat_count);
        double added{};
        double widest{};
        for (std::size_t index = 0U; index < stat_count; ++index) {
            const auto width = header_width(context, definition.column_keys[index + 2U]) +
                               column_text_padding;
            required.push_back(width);
            added += width;
            widest = std::max(widest, width);
        }

        std::vector<double> stat_widths(stat_count);
        const auto remaining = available_stats - added;
        if (stat_count == 0U) {
            grid.visible_stat_columns = 0U;
        } else if (remaining >= 0.0) {
            grid.visible_stat_columns = stat_count;
            const auto extra = std::floor(remaining / static_cast<double>(stat_count));
            for (std::size_t index = 0U; index < stat_count; ++index) {
                stat_widths[index] = required[index] + extra;
            }
            stat_widths.back() +=
                std::max(0.0, available_stats - (added + extra * static_cast<double>(stat_count)));
        } else {
            grid.visible_stat_columns = std::min(
                stat_count,
                static_cast<std::size_t>(std::floor(available_stats / std::max(1.0, widest))));
            grid.visible_stat_columns = std::max<std::size_t>(grid.visible_stat_columns, 1U);
            const auto resized =
                std::floor(available_stats / static_cast<double>(grid.visible_stat_columns));
            std::ranges::fill(stat_widths, resized);
        }

        grid.shows_horizontal_scrollbar = grid.visible_stat_columns < stat_count;
        grid.visible_rows = grid.shows_horizontal_scrollbar ? 11U : 12U;
        grid.shows_vertical_scrollbar = vertical_scrollbar;
        grid.vertical_scrollbar = {738.0,
                                   145.0,
                                   scrollbar_button,
                                   vertical_scrollbar
                                       ? (grid.shows_horizontal_scrollbar ? 305.0 : 330.0)
                                       : 0.0};

        auto first_stat = std::min(model.first_visible_stat_column(),
                                   stat_count > grid.visible_stat_columns
                                       ? stat_count - grid.visible_stat_columns
                                       : 0U);
        auto x = grid.header.x;
        grid.columns.push_back({0U, {x, grid.header.y, rank_width, row_height}});
        x += rank_width;
        grid.columns.push_back({1U, {x, grid.header.y, name_width, row_height}});
        x += name_width;
        for (std::size_t visible = 0U; visible < grid.visible_stat_columns; ++visible) {
            const auto stat_index = first_stat + visible;
            auto width = stat_widths[stat_index];
            if (visible + 1U == grid.visible_stat_columns) {
                width = grid.header.x + grid.header.width - x;
            }
            grid.columns.push_back({stat_index + 2U, {x, grid.header.y, width, row_height}});
            x += width;
        }
        grid.horizontal_scrollbar = {grid.header.x + rank_width + name_width,
                                     484.0,
                                     available_stats,
                                     scrollbar_button};
        return grid;
    };

    auto grid = make_grid(model.rows().size() > LeaderboardMenuModel::visible_rows);
    if (grid.shows_horizontal_scrollbar && model.rows().size() > grid.visible_rows &&
        !grid.shows_vertical_scrollbar) {
        grid = make_grid(true);
    }
    return grid;
}

ui::DrawList LeaderboardPresentation::build(const LeaderboardMenuModel& model,
                                            const LeaderboardPresentationContext& context) const {
    if (context.window.width <= 0 || context.window.height <= 0 ||
        context.background_opacity_per_mille > 1'000U) {
        throw std::invalid_argument{"invalid leaderboard presentation context"};
    }

    const auto layout = leaderboard_classic_layout();
    const auto& definition = leaderboard_definition(model.selected_type());
    const auto grid = leaderboard_grid_layout(model, context);
    ui::DrawList list;
    list.reserve(24U + grid.visible_rows * grid.columns.size());
    list.push(sprite(leaderboard_presentation_assets::background,
                     background_cover(context.window),
                     DrawSpace::window_pixels,
                     TextureFilter::linear,
                     TextureAnchor::top_left,
                     0.6,
                     color(white, context.background_opacity_per_mille),
                     SpriteSizing::cover));
    list.push(sprite(leaderboard_presentation_assets::frame,
                     layout.frame,
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     TextureAnchor::center));

    list.push(text("LEADERBOARD",
                   layout.title,
                   46.0,
                   leaderboard_presentation_assets::title_font,
                   cream,
                   HorizontalTextAlignment::center,
                   TextTransform::uppercase));
    append_red_header(list, grid.header);
    for (std::size_t visible_index{}; visible_index < grid.columns.size(); ++visible_index) {
        const auto& column = grid.columns[visible_index];
        solid(list,
              {column.bounds.x,
               column.bounds.y,
               column.bounds.width -
                   (visible_index + 1U < grid.columns.size() ? 1.0 : 0.0),
               column.bounds.height},
              red_header);
        const auto key = definition.column_keys[column.source_index];
        list.push(text(key,
                       {column.bounds.x + column_side_padding,
                        column.bounds.y,
                        std::max(0.0,
                                 column.bounds.width - column_side_padding * 2.0 - 1.0),
                        column.bounds.height},
                       11.0,
                       leaderboard_presentation_assets::header_font));
        const auto sorting = model.sorted_column();
        const auto icon = sorting == column.source_index
                              ? (model.sort_direction() == LeaderboardSortDirection::ascending
                                     ? leaderboard_presentation_assets::filter_up
                                     : leaderboard_presentation_assets::filter_down)
                              : leaderboard_presentation_assets::filter_down_white;
        list.push(sprite(icon,
                         {column.bounds.x + column.bounds.width - 11.0,
                          column.bounds.y + 9.0,
                          8.0,
                          7.0},
                         DrawSpace::design_pixels,
                         TextureFilter::nearest,
                         TextureAnchor::center,
                         1.0));
    }

    if (model.state() == LeaderboardLoadState::loading) {
        auto connecting = text("CONNECTING_PLEASE_WAIT",
                               {30.0, 300.0, 740.0, 0.0},
                               18.0,
                               leaderboard_presentation_assets::header_font,
                               cream,
                               HorizontalTextAlignment::center,
                               TextTransform::preserve,
                               VerticalTextAlignment::baseline);
        // LeaderboardMenu.draw calls settings_font.draw directly at the
        // bottom-origin (400,300) baseline.  A zero-height fit rectangle used
        // to collapse this label to a one-pixel raster while data was loading.
        connecting.fit = TextFit::none;
        list.push(std::move(connecting));
    } else if (model.state() == LeaderboardLoadState::ready) {
        const auto rows = model.rows();
        const auto start = std::min(model.first_visible_row(), rows.size());
        const auto count = std::min(grid.visible_rows, rows.size() - start);
        for (std::size_t visible = 0U; visible < count; ++visible) {
            const auto& row = rows[start + visible];
            const auto source_row = start + visible;
            const auto y = grid.header.y + row_height * static_cast<double>(visible + 1U);

            // LeaderboardListItem draws the segmented row background first,
            // then the full-width hover quad, then its green selection line
            // and glow, and only afterward the per-column text.
            for (std::size_t column_index = 0U; column_index < grid.columns.size();
                 ++column_index) {
                const auto& column = grid.columns[column_index];
                const auto fill_width = column.bounds.width -
                                        (column_index + 1U < grid.columns.size() ? 1.0 : 0.0);
                solid(list,
                      {column.bounds.x, y, fill_width, row_height},
                      visible % 2U == 0U ? row_grey : row_dark);
            }
            const DrawRect row_bounds{grid.header.x, y, grid.header.width, row_height};
            if (model.hovered_row() == source_row) {
                solid(list, row_bounds, row_hovered);
            }
            if (model.selected_row() == source_row) {
                append_selection(list, row_bounds);
            }

            for (const auto& column : grid.columns) {
                std::string value;
                if (column.source_index == 0U) {
                    value = std::to_string(row.rank);
                } else if (column.source_index == 1U) {
                    value = retail_fitted_player_name(row.player_name, context);
                } else {
                    const auto value_index = column.source_index - 2U;
                    if (value_index < row.values.size()) {
                        value = row.values[value_index];
                    }
                }
                list.push(text(value,
                               {column.bounds.x + column_side_padding,
                                y,
                                std::max(0.0,
                                         column.bounds.width - column_side_padding * 2.0 - 1.0),
                                row_height},
                               11.0,
                               leaderboard_presentation_assets::row_font));
            }
        }
    }

    const auto stat_count = definition.column_keys.size() - 2U;
    append_horizontal_scrollbar(
        list, grid, stat_count, model.first_visible_stat_column());
    append_vertical_scrollbar(list, grid, model.rows().size(), model.first_visible_row());
    append_navigation_back(list, layout.back_button);

    // MenuScene draws the grid first and DropBoxControl instances afterward,
    // so an open list must cover the table rather than sit behind it.
    append_dropdown(list,
                    layout.type_dropdown,
                    definition.filter_key,
                    model.selectors_enabled());
    if (model.open_dropdown() == LeaderboardDropdown::type) {
        std::array<std::string_view, LeaderboardMenuModel::type_count> keys{};
        const auto definitions = leaderboard_definitions();
        for (std::size_t index{}; index < keys.size(); ++index) {
            keys[index] = definitions[index].filter_key;
        }
        append_dropdown_options(
            list, layout.type_dropdown, keys,
            static_cast<std::size_t>(model.selected_type()));
    }
    append_dropdown(list,
                    layout.scope_dropdown,
                    scope_key(model.selected_scope()),
                    model.selectors_enabled());
    if (model.open_dropdown() == LeaderboardDropdown::scope) {
        constexpr std::array scope_keys{std::string_view{"GLOBAL"},
                                        std::string_view{"LOCAL"},
                                        std::string_view{"FRIENDS"}};
        append_dropdown_options(
            list, layout.scope_dropdown, scope_keys,
            static_cast<std::size_t>(model.selected_scope()));
    }
    return list;
}

} // namespace battlespades::frontend
