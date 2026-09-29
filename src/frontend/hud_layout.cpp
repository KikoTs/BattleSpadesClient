#include "battlespades/frontend/hud_layout.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <cstdio>

namespace battlespades::frontend::hud_layout {

namespace {

[[nodiscard]] std::int32_t round_to_int(double value) noexcept {
    return static_cast<std::int32_t>(std::lround(value));
}

} // namespace

ui::ColorRgba8 team_color(Team team) noexcept {
    switch (team) {
    case Team::team1:
        return {44U, 117U, 179U, 255U};
    case Team::team2:
        return {137U, 179U, 44U, 255U};
    case Team::spectator:
        return {255U, 255U, 255U, 255U};
    case Team::neutral:
        break;
    }
    return {128U, 128U, 128U, 255U};
}

ui::ColorRgba8 block_count_color(std::int32_t block_count) noexcept {
    // hud.pyx:829 loads NOT_ENOUGH_AMMO_COLOR unconditionally, then :831
    // overwrites it with ENOUGH_AMMO_COLOR when block_count > 0 (Py_GT).
    return block_count > 0 ? enough_ammo_color : not_enough_ammo_color;
}

double to_top_left_y(double bottom_left_y, double height,
                     const ui::PixelExtent& window) noexcept {
    return static_cast<double>(window.height) - bottom_left_y - height;
}

ui::Rect to_layout_rect(const RectF& rect, const ui::PixelExtent& window) noexcept {
    return ui::Rect{round_to_int(rect.x),
                    round_to_int(to_top_left_y(rect.y, rect.height, window)),
                    round_to_int(rect.width), round_to_int(rect.height)};
}

RectF score_box(const ui::PixelExtent& window) noexcept {
    return RectF{message_left_margin,
                 static_cast<double>(window.height) - top_inset, score_frame_width,
                 score_frame_height};
}

double score_text_x(const ui::PixelExtent& window) noexcept {
    static_cast<void>(window);
    return message_left_margin + 10.0;
}

HeadCountMetrics head_count(const ui::PixelExtent& window,
                            double bg_width) noexcept {
    const auto half_width = static_cast<double>(window.width) * 0.5;
    HeadCountMetrics metrics;
    metrics.y = static_cast<double>(window.height) - top_inset;
    // The calculate_startx loop shadows HC_TEXT_WIDTH, but draw resolves the
    // untouched module global again. The label cells therefore stay 80 px.
    metrics.text_width = hc_text_width_small;
    metrics.start_x = half_width - bg_width * 0.5;
    metrics.left_text_x = half_width - hc_head_x_offset - hc_head_width -
                          metrics.text_width;
    metrics.right_text_x = half_width + hc_head_x_offset + hc_head_width -
                           hc_right_text_nudge;
    return metrics;
}

TeamProgressMetrics team_progress(const ui::PixelExtent& window, Team team,
                                  bool head_count_visible) noexcept {
    constexpr double frame_native_width{320.0};
    constexpr double frame_native_height{40.0};
    constexpr double base_font_size{20.0};
    constexpr double icon_size{32.0};
    constexpr double icon_anchor_compensation{4.0};

    const auto centre_x = static_cast<double>(window.width) * 0.5;
    auto local_y = static_cast<double>(window.height) - 40.0;
    if (head_count_visible) {
        local_y -= 40.0;
    }

    double direction{1.0};
    double offset{};
    if (team == Team::team1) {
        direction = -1.0;
    } else if (team == Team::neutral) {
        offset = -50.0;
    }

    TeamProgressMetrics metrics;
    // draw_image_resized(frame, x-W/2+font, y-10, W-font*2, H).
    metrics.background =
        RectF{centre_x - frame_native_width * 0.5 + base_font_size,
              local_y - 10.0,
              frame_native_width - base_font_size * 2.0,
              frame_native_height};

    auto icon_x = centre_x + direction * 48.0 + offset;
    if (direction < 0.0) {
        icon_x -= icon_size - icon_anchor_compensation;
    }
    metrics.icon = RectF{icon_x, local_y - 5.0, icon_size, icon_size};

    metrics.label_x = centre_x + direction * 84.0 + offset;
    if (direction == -1.0) {
        metrics.label_x -= 4.0;
    }
    metrics.label_y = local_y + 14.0;
    return metrics;
}

TerritoryBaseMetrics territory_base(
    const ui::PixelExtent& window, std::size_t base_count,
    std::uint8_t base_index, double capture_amount,
    bool contains_player) noexcept {
    const auto count = static_cast<double>(base_count);
    const auto index = static_cast<double>(base_index);
    const auto centre_x =
        static_cast<double>(window.width) - tc_hud_x_from_screen_left -
        tc_hud_base_x_interval * count * 0.5 +
        tc_hud_base_x_interval * index;
    const auto centre_y =
        static_cast<double>(window.height) - tc_hud_y_from_screen_top -
        tc_hud_base_y_interval * index;
    const auto icon_scale =
        tc_hud_icon_scale *
        (contains_player ? tc_hud_player_containing_scale : 1.0);

    // T(x,y) * S(icon) * T(-frame.width/2,-frame.height/2).
    const auto plate_x =
        centre_x + icon_scale * (-tc_frame_width * 0.5 +
                                 tc_hud_plate_x_offset);
    const auto plate_y =
        centre_y + icon_scale * (-tc_frame_height * 0.5 +
                                 tc_hud_plate_y_offset);
    const auto plate_width = tc_frame_width * icon_scale;
    const auto plate_height = tc_frame_height * icon_scale;
    const auto capture_fraction =
        std::clamp(capture_amount / 100.0, 0.0, 1.0);

    // Retail multiplies the current matrix by icon_scale*LETTER_SCALE before
    // blitting the letter. The current matrix already contains icon_scale.
    const auto nested_letter_scale = icon_scale * tc_hud_letter_scale;
    const auto final_letter_scale = icon_scale * nested_letter_scale;
    const auto letter_x =
        centre_x +
        icon_scale * (-tc_frame_width * 0.5 +
                      nested_letter_scale * tc_hud_letter_x_offset);
    const auto letter_y =
        centre_y +
        icon_scale * (-tc_frame_height * 0.5 +
                      nested_letter_scale * tc_hud_letter_y_offset);

    TerritoryBaseMetrics metrics;
    metrics.plate =
        RectF{plate_x, plate_y, plate_width, plate_height};
    metrics.attacked_overlay =
        RectF{plate_x, plate_y, plate_width * capture_fraction,
              plate_height};
    metrics.frame = metrics.plate;
    metrics.letter =
        RectF{letter_x, letter_y,
              tc_letter_width * final_letter_scale,
              tc_letter_height * final_letter_scale};
    metrics.centre_x = centre_x;
    metrics.centre_y = centre_y;
    metrics.icon_scale = icon_scale;
    return metrics;
}

TimerMetrics timer_metrics(const ui::PixelExtent& window,
                           TimerPlacement placement,
                           bool minimap_enabled) noexcept {
    const bool tutorial = placement == TimerPlacement::tutorial_corner;
    const auto caller_x = tutorial
                              ? static_cast<double>(window.width) - 78.0
                              : static_cast<double>(window.width) * 0.5 + 2.0;
    const auto caller_y = tutorial
                              ? static_cast<double>(window.height) -
                                    (minimap_enabled ? 190.0 : 40.0)
                              : static_cast<double>(window.height) - top_inset;

    // Python 2 evaluates 115 / 2 as the integer 57 before subtracting it.
    const auto x = caller_x - 57.0;
    TimerMetrics metrics;
    metrics.frame = RectF{x, caller_y, timer_frame_width, timer_frame_height};
    metrics.shadow_baseline_x = x + 42.0;
    metrics.shadow_baseline_y = caller_y - 12.0;
    metrics.foreground_baseline_x = x + 40.0;
    metrics.foreground_baseline_y = caller_y - 10.0;
    metrics.icon_center_x = x + 24.0;
    metrics.icon_center_y = caller_y;
    metrics.icon_width = timer_icon_width * timer_icon_scale;
    metrics.icon_height = timer_icon_height * timer_icon_scale;
    metrics.draw_background = tutorial;
    return metrics;
}

std::string format_clock(double remaining_seconds) {
    const auto clamped = std::max(0.0, remaining_seconds);
    // gmtime() truncates toward zero, so a partial second still shows.
    const auto total = static_cast<long long>(clamped);
    const auto minutes = total / 60;
    const auto seconds = total % 60;
    std::array<char, 32U> buffer{};
    // Retail's '%02d:%02d' does not wrap at an hour; gmtime's tm_min resets,
    // but every shipped countdown is under 60 minutes so the two agree.
    std::snprintf(buffer.data(), buffer.size(), "%02lld:%02lld", minutes % 60,
                  seconds);
    return std::string{buffer.data()};
}

RectF health_bar_frame(const ui::PixelExtent& window) noexcept {
    // Blitted at (window.width*0.5, 30) with center=True, so the 239x34 art
    // straddles the centre line and sits 30 px up from the bottom edge.
    const auto centre_x = static_cast<double>(window.width) * 0.5;
    return RectF{centre_x - health_bar_width * 0.5, 30.0 - health_bar_height * 0.5,
                 health_bar_width, health_bar_height};
}

RectF health_bar_fill(const ui::PixelExtent& window,
                      double health_fraction) noexcept {
    const auto fraction = std::clamp(health_fraction, 0.0, 1.0);
    const auto frame = health_bar_frame(window);
    // Retail translates to the 35 px anchor and scales the complete
    // center-loaded 239 px texture around it. At full health the fill still
    // spans the entire frame; cropping away the first 35 px was visibly wrong.
    const auto anchor = frame.x + health_bar_anchor_x;
    return RectF{anchor - health_bar_anchor_x * fraction, frame.y,
                   health_bar_width * fraction, health_bar_height};
}

HealthNumberDrawPasses health_number_draw_passes(
    const ui::PixelExtent& window) noexcept {
    // Windows hud.pyd 0x1009E70B..0x1009EC92 and macOS hud.so
    // 0x6CF9A/0x6D2DF/0x6D367 cross-confirm width*0.23, the explicit
    // images.py anchor_x=35, and the bottom-origin y=35 translation.
    constexpr double width_fraction{0.23};
    constexpr double outer_x{0.2};
    constexpr double outer_y{-0.2};
    constexpr double shadow_offset{2.0};
    const auto x = static_cast<double>(window.width) * 0.5 +
                   health_bar_width * width_fraction + health_bar_anchor_x;
    constexpr double y{35.0};
    return HealthNumberDrawPasses{
        RectF{x, y, 0.0, 0.0},
        RectF{x + outer_x + shadow_offset,
              y + outer_y - shadow_offset, 0.0, 0.0},
        RectF{x + outer_x, y + outer_y, 0.0, 0.0},
    };
}

double loaded_image_pixels(double source_pixels) noexcept {
    // image.py:100-101 -- int() truncation at load, before the anchor is taken.
    return std::trunc(source_pixels * image_global_scale);
}

RectF class_portrait(const ui::PixelExtent& window,
                     bool high_minimap_visibility) noexcept {
    const auto scale = high_minimap_visibility ? class_icon_visible_draw_scale
                                               : class_icon_draw_scale;
    const auto centre_x = static_cast<double>(window.width) * 0.5;
    // Retail computes these operands in the pre-scale space (dividing by the
    // draw scale) and glScalef multiplies them back, so the divide and the
    // multiply cancel on the base term and leave the +16 / +8 nudges scaled.
    const auto icon_centre_x =
        centre_x - health_bar_width * 0.5 + 16.0 * class_icon_draw_scale;
    const auto icon_centre_y =
        30.0 + 0.1 * health_bar_height + 8.0 * class_icon_draw_scale;
    const auto size = loaded_image_pixels(class_icon_source_pixels) * scale;
    return RectF{icon_centre_x - size * 0.5, icon_centre_y - size * 0.5, size, size};
}

double head_count_text_width(bool show_max_score, std::int32_t max_score) noexcept {
    if (!show_max_score) {
        return hc_text_width_small;
    }
    return std::to_string(max_score).size() >= 3U ? hc_text_width_big
                                                  : hc_text_width_medium;
}

double head_count_bar_width(bool show_max_score, std::int32_t max_score) noexcept {
    // headCount.py:68 adds the surviving width to ITSELF, then the timer column.
    // width_offset is identically 0 in retail, so it contributes nothing.
    const auto width = head_count_text_width(show_max_score, max_score);
    return width + width + hc_timer_width;
}

double ammo_anchor_x(const ui::PixelExtent& window) noexcept {
    return static_cast<double>(window.width) - 80.0;
}

RectF ammo_panel(const ui::PixelExtent& window) noexcept {
    const auto x = ammo_anchor_x(window) - ammo_frame_width * 0.5 - 19.0;
    return RectF{x, 60.0, ammo_frame_width * ammo_frame_scale,
                 ammo_frame_height};
}

RectF blocks_panel(const ui::PixelExtent& window) noexcept {
    auto panel = ammo_panel(window);
    panel.y = 12.0;
    return panel;
}

double ammo_icon_x(const ui::PixelExtent& window) noexcept {
    return ammo_panel(window).x - 50.0;
}

RectF jetpack_gauge(const ui::PixelExtent& window) noexcept {
    // x = window.width - 50, y = jetpack_fuel_frame.height*0.5 + 135 = 200.
    const auto x = static_cast<double>(window.width) - 50.0;
    const auto y = jetpack_frame_height * 0.5 + 135.0;
    return RectF{x - jetpack_frame_width * 0.5, y - 4.0, jetpack_frame_width,
                 jetpack_frame_height};
}

RectF jetpack_fill(const ui::PixelExtent& window, double fuel_fraction) noexcept {
    const auto fraction = std::clamp(fuel_fraction, 0.0, 1.0);
    auto gauge = jetpack_gauge(window);
    // anchor_y = 0 means the fill grows upward from the gauge's base.
    gauge.height *= fraction;
    return gauge;
}

double jetpack_tool_icon_y(const ui::PixelExtent& window) noexcept {
    static_cast<void>(window);
    return jetpack_frame_height * 0.5 + 135.0 - 70.0;
}

RectF active_equipment_icon(const ui::PixelExtent& window) noexcept {
    constexpr double tool_source_pixels{330.0};
    constexpr double tool_draw_scale{0.15};
    // TOOL_IMAGES load at scale 1.0 (no global 0.64 shrink), so
    // draw_parachute_hud's glScalef(0.15) blits a 49.5 px icon; the shrunk
    // 31 px one was about 60% of retail's (live A/B 2026-09-29).
    const auto size = tool_source_pixels * tool_draw_scale;
    const auto centre_x = static_cast<double>(window.width) - 50.0;
    constexpr double centre_y{135.0};
    return RectF{centre_x - size * 0.5, centre_y - size * 0.5, size, size};
}

} // namespace battlespades::frontend::hud_layout
