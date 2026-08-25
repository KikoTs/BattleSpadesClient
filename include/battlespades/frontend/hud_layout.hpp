#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "battlespades/ui/design_canvas.hpp"
#include "battlespades/ui/draw_list.hpp"

namespace battlespades::frontend {

/**
 * Recovered retail in-game HUD geometry (hud.pyd).
 *
 * Two conventions are load bearing and easy to get wrong:
 *
 * 1. The in-game HUD is laid out in RAW WINDOW PIXELS and is never scaled by
 *    resolution. GameScene.draw sets glOrtho(0, window.width, 0, window.height,
 *    -1, 1) (gameScene.pyd 0x1013F83F, args 0x1013F86D / 0x1013F898) and never
 *    calls glScalef. The 800x600 design canvas belongs to the frontend menus
 *    only, which apply get_aspect(800, 600) + glScalef
 *    (aoslib/scenes/frontend/menuScene.py:137-140). The only multipliers in the
 *    HUD are fixed authored constants (1.3 ammo frame, 0.4 class icon, 0.5 head
 *    icon, 0.15 jetpack tool icon, 0.8 timer icon).
 * 2. pyglet's origin is BOTTOM-LEFT, so every recovered `window.height - N` is
 *    N pixels down from the top. Retail anchors are kept verbatim below in that
 *    bottom-left space; `to_top_left_y` converts to ui::Rect's top-left space.
 *
 * Positions stay in double because retail computes them in float and several
 * land on halves (the ammo panel's x is W - 156.5). Rounding only at the
 * renderer boundary keeps the arithmetic comparable to the decompile.
 */
namespace hud_layout {

/** Rectangle in retail's bottom-left pixel space. */
struct RectF final {
    double x{};
    double y{};
    double width{};
    double height{};

    [[nodiscard]] friend constexpr bool operator==(const RectF&, const RectF&) = default;
};

// hud.pyd inithud: 0x100c5920, 0x100c595a, 0x100c598c, 0x100c59be, 0x100c59f0.
inline constexpr double message_left_margin{12.0};
inline constexpr double message_top_margin{12.0};
inline constexpr double message_bottom_margin{60.0};
inline constexpr double message_pad{5.0};
inline constexpr int max_chat_entries{5};

/** Shared top inset for the SCORE box and the HeadCount bar (window.height-48). */
inline constexpr double top_inset{48.0};

// HeadCount widths, hud.pyd inithud 0x100b34d2-0x100b35d3.
inline constexpr double hc_text_width_small{80.0};
inline constexpr double hc_text_width_medium{90.0};
inline constexpr double hc_text_width_big{110.0};
inline constexpr double hc_timer_width{120.0};
/** Shipped png/ui/head_count/hc_frame_long.png height. */
inline constexpr double hc_frame_height{40.0};
/** HeadCount draws the int-truncated 46x44 loaded images at scale 0.5. */
inline constexpr double hc_head_width{23.0};
inline constexpr double hc_head_height{22.0};
inline constexpr double hc_head_x_offset{60.0};
inline constexpr double hc_head_y_offset{10.0};
inline constexpr double hc_right_text_nudge{4.0};

// Shipped asset dimensions (png/ui), all measured from the files themselves.
inline constexpr double score_frame_width{200.0};
inline constexpr double score_frame_height{40.0};
inline constexpr double health_bar_width{239.0};
inline constexpr double health_bar_height{34.0};
inline constexpr double ammo_frame_width{115.0};
inline constexpr double ammo_frame_height{40.0};
inline constexpr double timer_frame_width{115.0};
inline constexpr double timer_frame_height{40.0};
inline constexpr double timer_icon_width{32.0};
inline constexpr double timer_icon_height{32.0};
inline constexpr double timer_icon_scale{0.8};
inline constexpr double jetpack_frame_width{33.0};
inline constexpr double jetpack_frame_height{130.0};

/** images.py:439 pins health_bar.anchor_x; draw_healthbar scales about it. */
inline constexpr double health_bar_anchor_x{35.0};
/** draw_ammo_hud glScalef(1.3, 1.0, 0.0), hud.pyx:747-751. */
inline constexpr double ammo_frame_scale{1.3};

// Font sizes, all "Spades" (ALDO_FONT), aoslib/text.py.
inline constexpr double score_font_pixels{26.0};   // text.py:315
inline constexpr double head_count_font_pixels{26.0}; // text.py:320
inline constexpr double health_font_pixels{25.0};  // text.py HEALTH_FONT_SIZE
inline constexpr double ammo_font_pixels{26.0};    // text.py:299
inline constexpr double reserve_font_pixels{18.0}; // text.py:301

/** Retail team identifiers; TEAM1 is blue and TEAM2 is green. */
enum class Team : std::uint8_t { team1, team2, spectator, neutral };

/** shared/constants.py:229-242. TEAM_COLOURS and UI_TEAM_COLOURS are equal. */
[[nodiscard]] ui::ColorRgba8 team_color(Team team) noexcept;

/**
 * ENOUGH_AMMO_COLOR / NOT_ENOUGH_AMMO_COLOR.
 *
 * ALIAS TRAP: the human-annotated shared/constants.py mis-assigns A47-A52. The
 * raw decompiler output at shared/backup/constants-copy.py:81-91 is the
 * authority: A47 = (255,228,0,255) and A48 = (204,28,24,255).
 */
inline constexpr ui::ColorRgba8 enough_ammo_color{255U, 228U, 0U, 255U};
inline constexpr ui::ColorRgba8 not_enough_ammo_color{204U, 28U, 24U, 255U};
inline constexpr ui::ColorRgba8 low_health_text_color{255U, 0U, 0U, 255U};

/** draw_tools_hud hud.pyx:829/831 -- yellow above zero, red at zero. */
[[nodiscard]] ui::ColorRgba8 block_count_color(std::int32_t block_count) noexcept;

/** Converts a retail bottom-left y to ui::Rect's top-left y. */
[[nodiscard]] double to_top_left_y(double bottom_left_y, double height,
                                   const ui::PixelExtent& window) noexcept;

/** Rounds a bottom-left RectF into our top-left integer layout space. */
[[nodiscard]] ui::Rect to_layout_rect(const RectF& rect,
                                      const ui::PixelExtent& window) noexcept;

/**
 * SCORE box, top-left. HUD.draw calls draw_player_score(MSG_LEFT_MARGIN,
 * window.height - 48) at hud.pyd 0x100a35e2-0x100a366c, and score_frame is the
 * only HUD frame loaded center=False, so it is bottom-left anchored.
 */
[[nodiscard]] RectF score_box(const ui::PixelExtent& window) noexcept;

/** SCORE label origin: draw_player_score offsets the text by x+10. */
[[nodiscard]] double score_text_x(const ui::PixelExtent& window) noexcept;

/** Per-team readout metrics for the top-centre HeadCount bar. */
struct HeadCountMetrics final {
    double start_x{};
    double left_text_x{};
    double right_text_x{};
    double y{};
    double text_width{};

    [[nodiscard]] friend constexpr bool operator==(const HeadCountMetrics&,
                                                   const HeadCountMetrics&) = default;
};

/**
 * HeadCount.calculate_startx (hud.pyd 0x10031F70, hud.pyx:59-60) and
 * HeadCount.draw (0x100328E0, hud.pyx:79-85). Dynamic text widths affect only
 * the resized background. Both actual labels retain the module-level
 * HC_TEXT_WIDTH=80: the left label is right-aligned before its head and the
 * right label is left-aligned after its head with retail's four-pixel nudge.
 */
[[nodiscard]] HeadCountMetrics head_count(const ui::PixelExtent& window,
                                          double bg_width) noexcept;

/** Exact window-pixel geometry for one entry in retail TeamProgressBar. */
struct TeamProgressMetrics final {
    RectF background{};
    RectF icon{};
    /** Centre-anchored label position in retail's bottom-left coordinates. */
    double label_x{};
    double label_y{};

    [[nodiscard]] friend constexpr bool operator==(const TeamProgressMetrics&,
                                                   const TeamProgressMetrics&) = default;
};

/**
 * TeamProgressBar.draw (hud.pyd 0x100409A0).
 *
 * The widget is centred at `(window.width / 2, window.height - 30)`, lowers
 * itself by 40 pixels whenever HeadCount is visible, stretches
 * `hc_frame_long` to 280x40, temporarily resizes each 16x16 icon to 32x32,
 * and positions its Spades-20 label independently of the icon.
 */
[[nodiscard]] TeamProgressMetrics
team_progress(const ui::PixelExtent& window, Team team,
              bool head_count_visible) noexcept;

// TerritoryBasesHud constants recovered from hud.pyd inithud
// 0x100B648E-0x100B68BF. These are module-owned Python values in retail,
// which is why they are absent from the surviving .py sources.
inline constexpr double tc_hud_x_from_screen_left{70.0};
inline constexpr double tc_hud_y_from_screen_top{214.0};
inline constexpr double tc_hud_base_x_interval{0.0};
inline constexpr double tc_hud_base_y_interval{32.0};
inline constexpr double tc_hud_base_size{64.0};
inline constexpr double tc_hud_icon_scale{0.8};
inline constexpr double tc_hud_player_containing_scale{1.2};
inline constexpr double tc_hud_plate_x_offset{0.0};
inline constexpr double tc_hud_plate_y_offset{0.0};
inline constexpr double tc_hud_letter_scale{0.5};
inline constexpr double tc_hud_letter_x_offset{-100.0};
inline constexpr double tc_hud_letter_y_offset{-6.0};

inline constexpr double tc_frame_width{128.0};
inline constexpr double tc_frame_height{32.0};
inline constexpr double tc_letter_width{65.0};
inline constexpr double tc_letter_height{79.0};

/** Exact transformed rectangles for one retail TerritoryBaseInfo draw. */
struct TerritoryBaseMetrics final {
    RectF plate{};
    RectF attacked_overlay{};
    RectF frame{};
    RectF letter{};
    double centre_x{};
    double centre_y{};
    double icon_scale{};

    [[nodiscard]] friend constexpr bool operator==(
        const TerritoryBaseMetrics&, const TerritoryBaseMetrics&) = default;
};

/**
 * TerritoryBasesHud.arrange_bases + TerritoryBaseInfo.draw
 * (hud.pyd 0x10048BC0 / 0x10045760).
 *
 * Retail computes an x interval for every base even though the shipped value
 * is exactly zero, leaving the strip vertical at W-70. The letter is drawn
 * after a second `glScalef(icon_scale * 0.5)`, so its total scale is
 * `icon_scale² * 0.5`; collapsing the two scales loses the retail placement.
 */
[[nodiscard]] TerritoryBaseMetrics territory_base(
    const ui::PixelExtent& window, std::size_t base_count,
    std::uint8_t base_index, double capture_amount,
    bool contains_player) noexcept;

/** Retail draw_timer call-site selected by the active protocol mode. */
enum class TimerPlacement : std::uint8_t {
    /** Ordinary HUD.draw: (window.width*0.5 + 2, window.height - 48). */
    head_count,
    /** Tutorial HUD.draw: (window.width - 78, window.height - C, True). */
    tutorial_corner,
};

/** Exact draw_timer outputs in retail's bottom-left pixel space. */
struct TimerMetrics final {
    RectF frame{};
    double shadow_baseline_x{};
    double shadow_baseline_y{};
    double foreground_baseline_x{};
    double foreground_baseline_y{};
    double icon_center_x{};
    double icon_center_y{};
    double icon_width{};
    double icon_height{};
    bool draw_background{};

    [[nodiscard]] friend constexpr bool operator==(const TimerMetrics&,
                                                   const TimerMetrics&) = default;
};

/**
 * HUD.draw_timer recovered from hud.so 0x80800 and its two callers.
 *
 * The function first subtracts ``timer_frame.width / 2``. Python 2 integer
 * division makes that exactly 57 for the shipped 115px frame. Ordinary modes
 * omit the frame. Tutorial draws it and changes its top offset from 190px to
 * 40px when the minimap is disabled.
 */
[[nodiscard]] TimerMetrics timer_metrics(
    const ui::PixelExtent& window, TimerPlacement placement,
    bool minimap_enabled) noexcept;

/** draw_timer's '%02d:%02d' over gmtime of the remaining countdown. */
[[nodiscard]] std::string format_clock(double remaining_seconds);

/**
 * Health bar frame, bottom-centre. draw_healthbar blits the frame at
 * (window.width*0.5, 30) with both health_bar.png and health_bar_frame.png
 * loaded center=True at 239x34.
 */
[[nodiscard]] RectF health_bar_frame(const ui::PixelExtent& window) noexcept;

/**
 * The complete 239px fill is SCALED, not clipped:
 * glScalef(clamp(hp/100), 1, 1) about the 35px anchor
 * (hud.pyx:1057-1059). Returns the transformed texture rectangle.
 */
[[nodiscard]] RectF health_bar_fill(const ui::PixelExtent& window,
                                    double health_fraction) noexcept;

/**
 * Three exact Label draws used for the numeric HP readout.
 *
 * HUD.draw_healthbar first translates to
 *
 *   (window.width*0.5 + health_bar.width*0.23 + health_bar.anchor_x, 35)
 *
 * and calls ``health_text.draw()``. It then calls
 * ``draw_offset(health_text.draw_shadowed, 0.2, -0.2)``. draw_offset is a
 * push/translate/call/pop wrapper (draw.pyd 0x1000DC70), while
 * Label.draw_shadowed adds its own (2,-2) translation before drawing the
 * 64-grey shadow and then redraws the foreground at the outer offset.
 *
 * The returned zero-size anchors remain in retail's bottom-left coordinate
 * system and preserve that draw order: foreground, shadow, offset foreground.
 * images.py:439 makes health_bar.anchor_x exactly 35, not its original centred
 * texture anchor.
 */
struct HealthNumberDrawPasses final {
    RectF foreground{};
    RectF shadow{};
    RectF offset_foreground{};

    [[nodiscard]] friend constexpr bool operator==(
        const HealthNumberDrawPasses&,
        const HealthNumberDrawPasses&) = default;
};

[[nodiscard]] HealthNumberDrawPasses health_number_draw_passes(
    const ui::PixelExtent& window) noexcept;

/** Class icons ship at 230x230 and load through images.py's global_scale. */
inline constexpr double class_icon_source_pixels{230.0};
/** images.py:60 -- an art downsample applied to texture dims at load time. */
inline constexpr double image_global_scale{0.64};
/** draw_healthbar hud.pyx:1034 glScalef(0.4, 0.4, 0.4). */
inline constexpr double class_icon_draw_scale{0.4};
/**
 * hud.pyx:1044 glScalef(0.546, ...), gated at :1040 by
 * `if player.high_minimap_visibility:` -- NOT by a VIP class id. The two
 * happen to coincide in the gangster modes, which is exactly why assuming
 * "VIP" here would look right and be wrong.
 */
inline constexpr double class_icon_visible_draw_scale{0.546};

/**
 * image.py:100-101 truncates to int BEFORE taking the anchor:
 * `tex.width = int(tex.width * scale)`. int(230 * 0.64) = 147, not 147.2, so
 * the drawn head is 147 * 0.4 = 58.8 px and its anchor is the Python-2 integer
 * division 147 / 2 = 73. Rounding late instead of early puts every scaled UI
 * image a fraction off.
 */
[[nodiscard]] double loaded_image_pixels(double source_pixels) noexcept;

/**
 * The per-class head beside the health bar (draw_healthbar hud.pyx:1032-1038).
 *
 * Retail computes the blit position INSIDE the 0.4 scale, so the pre-scale
 * operands divide by 0.4 and the effective screen position multiplies back:
 *
 *   icon_x = (x - health_bar_frame.width*0.5) / 0.4 + 16
 *   icon_y = (30 + 0.1*health_bar_frame.height) / 0.4 + 8
 *
 * with x = window.width*0.5. The +16 and +8 are therefore *scaled* nudges worth
 * 6.4 and 3.2 screen pixels, which is what lands the head overlapping the bar's
 * left end rather than clear of it. The icon is loaded center=True, so the
 * returned rect is centred on that point.
 *
 * The whole block is gated on `scene.manager.enable_player_score`
 * (hud.pyx:1031) -- the same switch as the SCORE box, so the head and the score
 * appear and disappear together.
 */
[[nodiscard]] RectF class_portrait(const ui::PixelExtent& window,
                                   bool high_minimap_visibility) noexcept;

/**
 * HeadCount.calculate_startx (headCount.py:51-69).
 *
 * Per-team width is chosen by the score text: 3+ digits of max_score take
 * HC_TEXT_WIDTH_BIG, 1-2 digits take MEDIUM, and a team with no max_score takes
 * SMALL.
 *
 * RETAIL QUIRK, load bearing for parity: the per-team widths are NOT summed.
 * The loop body assigns a local that shadows the module-level HC_TEXT_WIDTH, so
 * only the LAST dict iteration survives, and line 68 then adds that one value
 * to itself (`PyNumber_Add(v, v)` on the same object). Both halves of the bar
 * therefore get the same width. HC_TIMER_WIDTH is added unconditionally -- there
 * is no timer or gamemode test in calculate_startx. `width_offset` is seeded to
 * 0 in __init__ and never reassigned anywhere in the module.
 */
[[nodiscard]] double head_count_text_width(bool show_max_score,
                                           std::int32_t max_score) noexcept;
[[nodiscard]] double head_count_bar_width(bool show_max_score,
                                          std::int32_t max_score) noexcept;

/**
 * Bottom-right ammo panel. draw_tools_hud sets ammo_x = window.width - 80 and
 * x = ammo_x - ammo_frame.width*0.5 - 19; the frame then renders under
 * glScalef(1.3). The weapon panel sits at y = 60 and the blocks panel at
 * y = 12, so blocks read directly beneath ammo.
 *
 * NOTE: which local binds to `x` versus `ammo_x` is an INFERENCE, not a
 * decompile -- the stack slots shift between assignment and call. The chosen
 * assignment is the one that keeps the scaled frame on screen.
 */
[[nodiscard]] double ammo_anchor_x(const ui::PixelExtent& window) noexcept;
[[nodiscard]] RectF ammo_panel(const ui::PixelExtent& window) noexcept;
[[nodiscard]] RectF blocks_panel(const ui::PixelExtent& window) noexcept;

/** draw_ammo_hud blits the tool icon at x - 50, vertically centred. */
[[nodiscard]] double ammo_icon_x(const ui::PixelExtent& window) noexcept;

/**
 * The bottom-right vertical yellow-to-red gauge is the JETPACK FUEL bar, not
 * health and not weapon heat: draw_jetpack_hud (hud.pyd 0x10096C10) returns
 * immediately when player.jetpack == NO_JETPACK. jetpack_fuel_bar.png has
 * anchor_y = 0 (images.py:441-443) so the fill grows upward from its base.
 */
[[nodiscard]] RectF jetpack_gauge(const ui::PixelExtent& window) noexcept;
[[nodiscard]] RectF jetpack_fill(const ui::PixelExtent& window,
                                 double fuel_fraction) noexcept;
/** The jetpack tool icon sits 70 px below the gauge origin at 0.15 scale. */
[[nodiscard]] double jetpack_tool_icon_y(const ui::PixelExtent& window) noexcept;

/**
 * Active disguise/parachute status icon.
 *
 * Both retail functions load their 330x330 TOOL_IMAGES entry through the
 * global 0.64 image scale, center it, translate to `(window.width - 50, 135)`
 * in bottom-left coordinates, then apply a 0.15 draw scale. The two icons
 * deliberately share the same rectangle; draw order makes parachute cover
 * disguise if malformed state ever advertises both.
 */
[[nodiscard]] RectF active_equipment_icon(const ui::PixelExtent& window) noexcept;

} // namespace hud_layout

} // namespace battlespades::frontend
