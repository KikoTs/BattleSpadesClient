#include "battlespades/frontend/game_hud.hpp"
#include "battlespades/frontend/hud_layout.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

namespace layout = battlespades::frontend::hud_layout;
using battlespades::ui::PixelExtent;

void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

void expect_near(double actual, double expected, const char* message) {
    if (std::fabs(actual - expected) > 1.0e-9) {
        throw std::runtime_error{std::string{message} + ": expected " +
                                 std::to_string(expected) + " got " +
                                 std::to_string(actual)};
    }
}

void test_minimap_entity_assets_are_exact() {
    using battlespades::frontend::minimap_entity_style;
    using battlespades::frontend::minimap_zone_icon_style;
    const auto base = minimap_entity_style(25U);
    const auto grave = minimap_entity_style(11U);
    const auto intel = minimap_entity_style(16U);
    const auto turret = minimap_entity_style(8U);
    expect(base.has_value() &&
               base->icon_asset == "png/ui/map_base_16.png" &&
               base->supports_height_indicator,
           "capture points use the recovered base art and height arrow");
    expect(grave.has_value() &&
               grave->icon_asset == "png/ui/map_grave_16.png" &&
               !grave->supports_height_indicator,
           "graves use their dedicated 16px art");
    expect(intel.has_value() &&
               intel->icon_asset == "png/ui/minimap_intel.png" &&
               intel->supports_height_indicator,
           "ground intel uses the tool-family icon and height arrow");
    expect(turret.has_value() &&
               turret->icon_asset == "png/ui/marker_turret_16.png" &&
               turret->friendly_only,
           "retail hides rocket-turret markers from the opposing team");
    expect(!minimap_entity_style(3U).has_value(),
           "ordinary ammo crates explicitly have no retail minimap icon");
    expect(!minimap_entity_style(255U).has_value(),
           "unknown entity ids must not invent minimap art");

    const auto ctf = minimap_zone_icon_style(6U);
    const auto vip = minimap_zone_icon_style(5U);
    const auto territory_j = minimap_zone_icon_style(16U);
    expect(ctf.has_value() &&
               ctf->icon_asset == "png/ui/minimap_base.png" &&
               ctf->authored_size_pixels == 16.0,
           "CTF zones use the 16px base icon");
    expect(vip.has_value() &&
               vip->icon_asset == "png/ui/vip_icon_256x256.png" &&
               vip->authored_size_pixels == 256.0,
           "VIP zones retain their authored 256px icon");
    expect(territory_j.has_value() &&
               territory_j->icon_asset == "png/ui/tc_minimap_g.png" &&
               territory_j->authored_size_pixels == 64.0,
           "territory H/I/J intentionally reuse the shipped G art");
    expect(!minimap_zone_icon_style(0U).has_value() &&
               !minimap_zone_icon_style(18U).has_value(),
           "NONE and unknown zone ordinals must not invent art");
}

void test_minimap_orientation_matches_world_axes() {
    using battlespades::frontend::minimap_marker_rotation_degrees;

    // The map texture is north-up in top-left screen coordinates: +X points
    // right and +Y points down. Both the player pin and view cone are authored
    // pointing up before this clockwise rotation is applied.
    expect_near(minimap_marker_rotation_degrees(0.0, -1.0), 0.0,
                "world -Y must point toward the top of the minimap");
    expect_near(minimap_marker_rotation_degrees(1.0, 0.0), 90.0,
                "world +X must point right on the minimap");
    expect_near(std::fabs(minimap_marker_rotation_degrees(0.0, 1.0)), 180.0,
                "world +Y must point toward the bottom of the minimap");
    expect_near(minimap_marker_rotation_degrees(-1.0, 0.0), -90.0,
                "world -X must point left on the minimap");
    expect_near(minimap_marker_rotation_degrees(0.0, 0.0), 0.0,
                "invalid horizontal orientation must fail upright");
}

/**
 * The retail HUD is authored in raw window pixels and is never scaled by
 * resolution, so a widget's distance from its own anchoring edge must be
 * identical at every window size. That invariant is the whole point of the
 * convention, and it is what a stray design-canvas scale would break.
 */
void test_anchors_are_resolution_independent() {
    const PixelExtent small{1280, 720};
    const PixelExtent large{2560, 1440};

    // Top-left SCORE box: fixed inset from the top-left corner.
    const auto score_small = layout::score_box(small);
    const auto score_large = layout::score_box(large);
    expect_near(score_small.x, layout::message_left_margin, "score x small");
    expect_near(score_large.x, layout::message_left_margin, "score x large");
    expect_near(static_cast<double>(small.height) - score_small.y, layout::top_inset,
                "score top inset small");
    expect_near(static_cast<double>(large.height) - score_large.y, layout::top_inset,
                "score top inset large");

    // Bottom-right ammo panel: fixed inset from the right edge.
    const auto ammo_small = layout::ammo_panel(small);
    const auto ammo_large = layout::ammo_panel(large);
    expect_near(static_cast<double>(small.width) - ammo_small.x, 156.5,
                "ammo right inset small");
    expect_near(static_cast<double>(large.width) - ammo_large.x, 156.5,
                "ammo right inset large");
    expect_near(ammo_small.y, 60.0, "ammo y");
    expect_near(ammo_small.width, 115.0 * 1.3,
                "ammo frame carries the recovered horizontal scale");
    expect_near(ammo_small.height, 40.0,
                "ammo frame does not invent a vertical 1.3 scale");

    // The blocks panel sits directly beneath ammo at the same x.
    const auto blocks = layout::blocks_panel(small);
    expect_near(blocks.x, ammo_small.x, "blocks share the ammo x");
    expect_near(blocks.y, 12.0, "blocks y");
    expect(blocks.y < ammo_small.y, "blocks read below ammo");

    // Bottom-centre health bar stays centred at any width.
    for (const auto& window : {small, large}) {
        const auto frame = layout::health_bar_frame(window);
        expect_near(frame.x + frame.width * 0.5,
                    static_cast<double>(window.width) * 0.5, "health bar centred");
    }

    // Jetpack gauge: fixed inset from the right edge, fixed height above bottom.
    expect_near(static_cast<double>(small.width) - layout::jetpack_gauge(small).x,
                50.0 + layout::jetpack_frame_width * 0.5, "jetpack right inset");
    expect_near(layout::jetpack_gauge(small).y, 196.0, "jetpack y");
}

/** The scaled ammo frame must stay on screen; this is what pins the x/ammo_x
 *  binding that the decompile could not settle. */
void test_ammo_panel_stays_on_screen() {
    const PixelExtent window{1280, 720};
    const auto panel = layout::ammo_panel(window);
    expect(panel.x > 0.0, "ammo panel left edge on screen");
    expect(panel.x + panel.width < static_cast<double>(window.width),
           "ammo panel right edge on screen");
    // Its centre lands within 2 px of ammo_x, which is the reason this binding
    // was chosen over the reverse one.
    expect(std::fabs(panel.x + panel.width * 0.5 - layout::ammo_anchor_x(window)) <
               2.0,
           "scaled frame centres on ammo_x");
}

void test_team_colours() {
    const auto blue = layout::team_color(layout::Team::team1);
    const auto green = layout::team_color(layout::Team::team2);
    expect(blue.red == 44U && blue.green == 117U && blue.blue == 179U,
           "TEAM1 is blue (44,117,179)");
    expect(green.red == 137U && green.green == 179U && green.blue == 44U,
           "TEAM2 is green (137,179,44)");
    const auto spectator = layout::team_color(layout::Team::spectator);
    expect(spectator.red == 255U && spectator.green == 255U && spectator.blue == 255U,
           "spectator is white");
    const auto neutral = layout::team_color(layout::Team::neutral);
    expect(neutral.red == 128U && neutral.green == 128U && neutral.blue == 128U,
           "neutral is grey");
}

/** Guards the A47/A48 alias trap: the annotated constants.py has these
 *  swapped against the raw decompiler output, which is the authority. */
void test_block_count_colour_threshold() {
    const auto empty = layout::block_count_color(0);
    expect(empty == layout::not_enough_ammo_color, "zero blocks is red");
    expect(empty.red == 204U && empty.green == 28U && empty.blue == 24U,
           "NOT_ENOUGH_AMMO_COLOR is (204,28,24)");
    const auto stocked = layout::block_count_color(1);
    expect(stocked == layout::enough_ammo_color, "one block is yellow");
    expect(stocked.red == 255U && stocked.green == 228U && stocked.blue == 0U,
           "ENOUGH_AMMO_COLOR is (255,228,0)");
    expect(layout::block_count_color(-5) == layout::not_enough_ammo_color,
           "negative blocks stay red");
}

/** The complete texture is scaled about the 35 px anchor, not cropped. */
void test_health_bar_fill_scales_about_its_anchor() {
    const PixelExtent window{1920, 1080};
    const auto frame = layout::health_bar_frame(window);
    const auto anchor = frame.x + layout::health_bar_anchor_x;

    const auto full = layout::health_bar_fill(window, 1.0);
    expect_near(full.x, frame.x, "full fill spans from the authored left edge");
    expect_near(full.width, layout::health_bar_width,
                "full fill retains the complete authored texture");

    const auto half = layout::health_bar_fill(window, 0.5);
    expect_near(half.x, anchor - layout::health_bar_anchor_x * 0.5,
                "half fill scales its left extent around the anchor");
    expect_near(half.width, full.width * 0.5, "half fill is half as wide");

    const auto empty = layout::health_bar_fill(window, 0.0);
    expect_near(empty.width, 0.0, "empty fill has no width");

    // Out-of-range health must clamp rather than overdraw the frame.
    expect_near(layout::health_bar_fill(window, 2.0).width, full.width,
                "over-full health clamps");
    expect_near(layout::health_bar_fill(window, -1.0).width, 0.0,
                "negative health clamps");
}

/** Numeric HP uses three centred Label passes at exact retail transforms. */
void test_health_number_draw_passes_match_retail() {
    const PixelExtent hd{1600, 900};
    const auto passes = layout::health_number_draw_passes(hd);

    // W/2 + 239*0.23 + images.py's explicit anchor_x=35.
    expect_near(passes.foreground.x, 889.97,
                "health number keeps the recovered x expression");
    expect_near(passes.foreground.y, 35.0,
                "health number keeps the bottom-origin 35px anchor");
    expect_near(passes.foreground.width, 0.0,
                "health number foreground is a centred point anchor");
    expect_near(passes.shadow.x, 892.17,
                "health shadow combines outer and Label offsets on x");
    expect_near(passes.shadow.y, 32.8,
                "health shadow combines outer and Label offsets on y");
    expect_near(passes.offset_foreground.x, 890.17,
                "health duplicate foreground keeps draw_offset x");
    expect_near(passes.offset_foreground.y, 34.8,
                "health duplicate foreground keeps draw_offset y");

    expect_near(layout::to_top_left_y(passes.foreground.y, 0.0, hd), 865.0,
                "primary HP anchor converts to native top-left y");
    expect_near(layout::to_top_left_y(passes.shadow.y, 0.0, hd), 867.2,
                "shadow's negative retail y offset becomes positive native y");
    expect_near(layout::to_top_left_y(passes.offset_foreground.y, 0.0, hd), 865.2,
                "offset foreground converts without losing its fraction");

    const PixelExtent full_hd{1920, 1080};
    const auto wide = layout::health_number_draw_passes(full_hd);
    expect_near(wide.foreground.x, 1049.97,
                "health number follows the live window centre at 1080p");
    expect_near(layout::to_top_left_y(wide.foreground.y, 0.0, full_hd), 1045.0,
                "health number stays 35px from the bottom at 1080p");
    expect_near(layout::health_font_pixels, 25.0,
                "health label keeps retail HEALTH_FONT_SIZE");
}

/**
 * The reference screenshot shows the class head overlapping the health bar's
 * left end, roughly as tall as the bar is plus a little, vertically centred on
 * it. That is the observable the arithmetic has to reproduce.
 */
void test_class_portrait_sits_on_the_bar_left_end() {
    const PixelExtent window{1600, 900};
    const auto bar = layout::health_bar_frame(window);
    const auto head = layout::class_portrait(window, false);

    const auto head_centre_x = head.x + head.width * 0.5;
    const auto head_centre_y = head.y + head.height * 0.5;

    // Centre lands 6.4 px right of the bar's left edge -- the +16 nudge taken
    // through the 0.4 draw scale -- so the head straddles that end.
    expect_near(head_centre_x - bar.x, 6.4, "head centres just inside the bar's left end");
    expect(head_centre_x > bar.x && head_centre_x < bar.x + bar.width * 0.5,
           "head overlaps the bar's left half, not floating clear of it");

    // Vertically it tracks the bar's centre line, nudged up slightly.
    expect_near(head_centre_y, 30.0 + 3.4 + 3.2, "head y follows the bar centre");
    expect(head_centre_y > bar.y && head_centre_y < bar.y + bar.height,
           "head centre stays within the bar's height");

    // 230 px art through global_scale then the 0.4 draw scale.
    expect_near(head.width, 147.0 * 0.4, "portrait is 58.8 px square");
    // image.py truncates to int at load: int(230*0.64) = 147, NOT 147.2.
    expect_near(layout::loaded_image_pixels(230.0), 147.0, "load truncates, not rounds");
    expect_near(head.width, head.height, "portrait is square");

    // The VIP variant is the same anchor at a larger scale.
    const auto vip = layout::class_portrait(window, true);  // high_minimap_visibility
    expect(vip.width > head.width, "the VIP head is drawn larger");
    expect_near(vip.x + vip.width * 0.5, head_centre_x, "VIP shares the anchor x");
    expect_near(vip.y + vip.height * 0.5, head_centre_y, "VIP shares the anchor y");

    // Resolution independence, same as every other widget.
    const auto wide = layout::class_portrait(PixelExtent{2560, 1440}, false);
    expect_near(wide.width, head.width, "portrait size ignores resolution");
}

void test_clock_formatting() {
    expect(layout::format_clock(741.0) == "12:21", "12:21 matches the reference shot");
    expect(layout::format_clock(694.0) == "11:34", "11:34 matches the reference shot");
    expect(layout::format_clock(0.0) == "00:00", "zero is padded");
    expect(layout::format_clock(-30.0) == "00:00", "expired countdown clamps");
    expect(layout::format_clock(59.9) == "00:59", "partial seconds truncate");
    expect(layout::format_clock(60.0) == "01:00", "minute rollover");
}

/**
 * Pins both recovered draw_timer callers and every authored internal offset.
 * The 115/2 subtraction deliberately remains Python-2 integer division.
 */
void test_timer_layout_matches_retail_call_sites() {
    const PixelExtent window{1600, 900};

    const auto ordinary = layout::timer_metrics(
        window, layout::TimerPlacement::head_count, true);
    expect(!ordinary.draw_background,
           "ordinary HeadCount timer does not draw timer_frame");
    expect_near(ordinary.frame.x, 745.0,
                "ordinary caller subtracts integer half frame width");
    expect_near(ordinary.frame.y, 852.0,
                "ordinary caller keeps window.height-48 baseline");
    expect_near(ordinary.shadow_baseline_x, 787.0,
                "ordinary timer shadow uses x+42");
    expect_near(ordinary.shadow_baseline_y, 840.0,
                "ordinary timer shadow uses y-12");
    expect_near(ordinary.foreground_baseline_x, 785.0,
                "ordinary timer foreground uses x+40");
    expect_near(ordinary.foreground_baseline_y, 842.0,
                "ordinary timer foreground uses y-10");
    expect_near(ordinary.icon_center_x, 769.0,
                "ordinary timer icon uses x+24");
    expect_near(ordinary.icon_center_y, 852.0,
                "ordinary timer icon stays centred on caller y");
    expect_near(ordinary.icon_width, 25.6,
                "timer icon applies recovered 0.8 scale");

    const auto tutorial = layout::timer_metrics(
        window, layout::TimerPlacement::tutorial_corner, true);
    expect(tutorial.draw_background,
           "tutorial caller passes draw_bg=True");
    expect_near(tutorial.frame.x, 1465.0,
                "tutorial timer uses window.width-78 caller x");
    expect_near(tutorial.frame.y, 710.0,
                "tutorial minimap timer uses 190px top caller inset");
    expect_near(static_cast<double>(window.height) -
                    (tutorial.frame.y + tutorial.frame.height),
                150.0,
                "tutorial timer frame starts 150px from top");
    expect_near(tutorial.foreground_baseline_x, 1505.0,
                "tutorial timer foreground uses exact x offset");
    expect_near(static_cast<double>(window.height) -
                    tutorial.foreground_baseline_y,
                200.0,
                "tutorial timer foreground converts to 200px top baseline");

    const auto tutorial_without_minimap = layout::timer_metrics(
        window, layout::TimerPlacement::tutorial_corner, false);
    expect_near(tutorial_without_minimap.frame.y, 860.0,
                "tutorial without minimap uses 40px top caller inset");
    expect_near(static_cast<double>(window.height) -
                    (tutorial_without_minimap.frame.y +
                     tutorial_without_minimap.frame.height),
                0.0,
                "tutorial timer frame becomes flush with top without minimap");
}

void test_tutorial_timer_reaches_exact_draw_positions() {
    using battlespades::frontend::GameHudModel;
    using battlespades::frontend::GameHudPresentation;
    using battlespades::frontend::GameHudPresentationContext;

    GameHudModel model;
    model.set_match_clock(741.0, true,
                          layout::TimerPlacement::tutorial_corner, true);
    GameHudPresentationContext context;
    context.window = PixelExtent{1600, 900};
    const GameHudPresentation presentation;
    const auto list = presentation.build(model, context);

    const battlespades::ui::SpriteDrawCommand* frame{};
    const battlespades::ui::SpriteDrawCommand* icon{};
    std::vector<const battlespades::ui::TextDrawCommand*> timer_texts;
    for (const auto& command : list.commands()) {
        if (const auto* sprite =
                std::get_if<battlespades::ui::SpriteDrawCommand>(&command);
            sprite != nullptr) {
            if (sprite->asset_id == "png/ui/timer/timer_frame.png") frame = sprite;
            if (sprite->asset_id == "png/ui/timer/timer.png") icon = sprite;
        } else if (const auto* text =
                       std::get_if<battlespades::ui::TextDrawCommand>(&command);
                   text != nullptr && text->localization_key == "12:21") {
            timer_texts.push_back(text);
        }
    }

    expect(frame != nullptr && icon != nullptr,
           "tutorial timer emits its frame and centred clock icon");
    expect(frame->destination ==
               battlespades::ui::DrawRect{1465.0, 150.0, 115.0, 40.0},
           "tutorial frame matches retail top-left geometry");
    expect_near(icon->destination.x, 1476.2,
                "tutorial icon converts its centred anchor exactly");
    expect_near(icon->destination.y, 177.2,
                "tutorial icon converts bottom-left y exactly");
    expect_near(icon->destination.width, 25.6,
                "tutorial icon retains 0.8 scale");
    expect(timer_texts.size() == 2U,
           "timer emits black shadow then white foreground");
    expect(timer_texts[0U]->destination ==
               battlespades::ui::DrawRect{1507.0, 202.0, 0.0, 0.0} &&
               timer_texts[0U]->horizontal_alignment ==
                   battlespades::ui::HorizontalTextAlignment::left &&
               timer_texts[0U]->vertical_alignment ==
                   battlespades::ui::VerticalTextAlignment::baseline &&
               timer_texts[0U]->modulation.color ==
                   battlespades::ui::ColorRgba8{0U, 0U, 0U, 255U},
           "tutorial shadow uses recovered left baseline and black tuple");
    expect(timer_texts[1U]->destination ==
               battlespades::ui::DrawRect{1505.0, 200.0, 0.0, 0.0} &&
               timer_texts[1U]->modulation.color ==
                   battlespades::ui::ColorRgba8{255U, 255U, 255U, 255U},
           "tutorial foreground uses recovered left baseline and white tuple");
}

/** pyglet measures y up from the bottom; ui::Rect measures down from the top. */
void test_bottom_left_to_top_left_conversion() {
    const PixelExtent window{1280, 720};
    const auto score = layout::to_layout_rect(layout::score_box(window), window);
    expect(score.x == 12, "score box keeps its left margin");
    // y = height - 48 in retail space, and the 40 px frame is bottom-left
    // anchored, so its top edge is 48 - 40 = 8 px down from the window top.
    expect(score.y == 8, "score box top edge is 8 px from the top");
    expect(score.width == 200 && score.height == 40, "score frame is 200x40");

    const auto health = layout::to_layout_rect(layout::health_bar_frame(window), window);
    expect(health.y + health.height / 2 == 720 - 30,
           "health bar centre is 30 px up from the bottom");
}

/**
 * calculate_startx does NOT sum the two teams' widths. Retail's loop assigns a
 * local that shadows the module-level HC_TEXT_WIDTH, so only the last team
 * survives, and line 68 adds that value to itself. Both halves therefore match
 * even when the two teams' scores differ in digit count.
 */
void test_head_count_width_follows_the_retail_quirk() {
    // "4/200" -- 3-digit max_score takes HC_TEXT_WIDTH_BIG.
    expect_near(layout::head_count_text_width(true, 200), layout::hc_text_width_big,
                "3-digit max score takes the big width");
    expect_near(layout::head_count_bar_width(true, 200), 110.0 + 110.0 + 120.0,
                "bar is width + width + timer column");
    // 1-2 digits take MEDIUM.
    expect_near(layout::head_count_text_width(true, 99), layout::hc_text_width_medium,
                "2-digit max score takes the medium width");
    // No max score at all falls back to SMALL.
    expect_near(layout::head_count_text_width(false, 200), layout::hc_text_width_small,
                "a team with no max score takes the small width");
    // The timer column is unconditional -- there is no gamemode test.
    expect_near(layout::head_count_bar_width(false, 0),
                80.0 + 80.0 + layout::hc_timer_width,
                "the timer column is always included");
    // hc_frame_long is natively 320 but is always resized, never blitted 1:1.
    expect(layout::head_count_bar_width(true, 200) != 320.0,
           "the native frame width is not the resize target");
}

void test_head_count_uses_retail_score_cells_and_head_offsets() {
    const PixelExtent window{1600, 900};
    const auto metrics = layout::head_count(window, 400.0);
    expect_near(metrics.start_x, 800.0 - 200.0, "bar is centred on the window");
    expect_near(metrics.y, 900.0 - layout::top_inset, "bar shares the score inset");
    expect_near(metrics.text_width, 80.0,
                "draw keeps module HC_TEXT_WIDTH despite dynamic bar width");
    expect_near(metrics.left_text_x, 637.0,
                "left score precedes the 23px head at W/2-60");
    expect_near(metrics.right_text_x, 879.0,
                "right score follows the 23px head with retail minus-four nudge");
    const auto timer = layout::timer_metrics(
        window, layout::TimerPlacement::head_count, false);
    expect_near(timer.frame.x + 57.0, 802.0,
                "clock caller sits at W/2 + 2 before integer half-frame subtraction");
}

void test_team_progress_layout_matches_retail_constants() {
    const PixelExtent window{1600, 900};
    const auto team1 =
        layout::team_progress(window, layout::Team::team1, false);
    expect_near(team1.background.x, 660.0,
                "progress frame starts at centre minus 140");
    expect_near(team1.background.y, 850.0,
                "progress frame is ten pixels from the top");
    expect_near(team1.background.width, 280.0,
                "progress frame trims forty pixels from the native art");
    expect_near(team1.icon.x, 724.0,
                "left icon compensates its resized width by twenty-eight");
    expect_near(team1.icon.y, 855.0,
                "progress icon keeps retail bottom-left y");
    expect_near(team1.label_x, 712.0,
                "left label includes retail's extra minus-four nudge");
    expect_near(team1.label_y, 874.0,
                "label uses the recovered local-y plus fourteen");

    const auto team2 =
        layout::team_progress(window, layout::Team::team2, true);
    expect_near(team2.background.y, 810.0,
                "visible HeadCount lowers TeamProgress by forty");
    expect_near(team2.icon.x, 848.0, "right icon starts at centre plus forty-eight");
    expect_near(team2.icon.y, 815.0, "lowered icon keeps its five-pixel lift");
    expect_near(team2.label_x, 884.0,
                "right label starts at centre plus eighty-four");
    expect_near(team2.label_y, 834.0,
                "lowered label preserves its fourteen-pixel offset");

    const auto neutral =
        layout::team_progress(window, layout::Team::neutral, true);
    expect_near(neutral.icon.x, 798.0,
                "neutral icon applies the recovered minus-fifty offset");
    expect_near(neutral.label_x, 834.0,
                "neutral label shares the same minus-fifty offset");
}

void test_team_progress_retains_retail_packet_state() {
    using battlespades::frontend::GameHudModel;
    using battlespades::frontend::GameHudTeamProgressIcon;
    using battlespades::frontend::GameHudTeamProgressUpdate;
    using battlespades::frontend::team_progress_icon_from_wire;

    expect(team_progress_icon_from_wire(0U) ==
               GameHudTeamProgressIcon::base,
           "icon zero maps to minimap_base");
    expect(team_progress_icon_from_wire(1U) ==
               GameHudTeamProgressIcon::diamond,
           "icon one maps to minimap_diamond");
    expect(!team_progress_icon_from_wire(2U).has_value(),
           "unknown icon ids retain prior state");

    GameHudModel model;
    model.update_team_progress(GameHudTeamProgressUpdate{
        layout::Team::team1, true, false, true, true, 25.0, 100.0,
        GameHudTeamProgressIcon::diamond});
    auto state = model.team_progress();
    expect_near(state.entries[0U].current, 75.0,
                "percent packets display maximum minus the wire value");
    expect_near(state.entries[0U].previous_damage, 100.0,
                "show_previous retains the row's prior current value");
    expect(state.entries[0U].icon == GameHudTeamProgressIcon::diamond,
           "known icon updates replace the row icon");

    model.update_team_progress(GameHudTeamProgressUpdate{
        layout::Team::team2, true, false, false, false, 10.0, 50.0,
        GameHudTeamProgressIcon::base});
    state = model.team_progress();
    expect_near(state.maximum, 50.0,
                "fraction form replaces the widget-wide denominator");
    expect(!state.show_as_percent,
           "show mode is shared across all rows in retail");
    expect_near(state.entries[1U].current, 40.0,
                "fraction numerator is inverted against its denominator");

    model.update_team_progress(GameHudTeamProgressUpdate{
        layout::Team::team1, true, false, false, true, 0.0, 999.0,
        std::nullopt});
    state = model.team_progress();
    expect_near(state.maximum, 50.0,
                "percent form does not replace the prior shared maximum");
    expect_near(state.entries[0U].current, 50.0,
                "wire zero resets current to the retained maximum");
    expect_near(state.entries[0U].previous_damage, 0.0,
                "wire zero also resets previous damage");
    expect(state.entries[0U].icon == GameHudTeamProgressIcon::diamond,
           "unknown icon leaves the previous icon installed");

    model.update_team_progress(GameHudTeamProgressUpdate{
        layout::Team::spectator, true, true, true, true, 1.0, 100.0,
        GameHudTeamProgressIcon::base});
    expect(model.team_progress().entries[0U].current == 50.0,
           "unsupported teams fail closed without changing valid rows");
}

void test_team_progress_reaches_the_exact_draw_positions() {
    using battlespades::frontend::GameHudModel;
    using battlespades::frontend::GameHudPresentation;
    using battlespades::frontend::GameHudPresentationContext;
    using battlespades::frontend::GameHudTeamProgressIcon;
    using battlespades::frontend::GameHudTeamProgressUpdate;
    using battlespades::frontend::GameHudTeamScore;

    GameHudModel model;
    model.set_team_scores(
        GameHudTeamScore{layout::Team::team1, 2, 10, true,
                         battlespades::ui::ColorRgba8{1U, 2U, 3U, 255U}},
        GameHudTeamScore{layout::Team::team2, 3, 10, true,
                         battlespades::ui::ColorRgba8{4U, 5U, 6U, 255U}},
        true);
    model.update_team_progress(GameHudTeamProgressUpdate{
        layout::Team::team1, true, false, false, true, 25.0, 100.0,
        GameHudTeamProgressIcon::base});
    model.update_team_progress(GameHudTeamProgressUpdate{
        layout::Team::team2, true, false, false, true, 0.0, 100.0,
        GameHudTeamProgressIcon::base});

    const GameHudPresentation presentation;
    const auto draw = presentation.build(
        model, GameHudPresentationContext{PixelExtent{1600, 900}, 1'000U});

    std::vector<const battlespades::ui::SpriteDrawCommand*> icons;
    const battlespades::ui::SpriteDrawCommand* progress_frame{};
    std::vector<const battlespades::ui::TextDrawCommand*> team1_labels;
    for (const auto& command : draw.commands()) {
        if (const auto* sprite =
                std::get_if<battlespades::ui::SpriteDrawCommand>(&command);
            sprite != nullptr) {
            if (sprite->asset_id == "png/ui/minimap_base.png") {
                icons.push_back(sprite);
            }
            if (sprite->asset_id == "png/ui/head_count/hc_frame_long.png" &&
                sprite->destination.width == 280.0) {
                progress_frame = sprite;
            }
        } else if (const auto* text =
                       std::get_if<battlespades::ui::TextDrawCommand>(&command);
                   text != nullptr && text->localization_key == "75%") {
            team1_labels.push_back(text);
        }
    }

    expect(progress_frame != nullptr,
           "two visible teams draw the separate progress backing frame");
    expect_near(progress_frame->destination.x, 660.0,
                "progress frame is horizontally centred");
    expect_near(progress_frame->destination.y, 50.0,
                "HeadCount shifts the progress frame down to top y fifty");
    expect(icons.size() == 2U,
           "each visible team emits one temporarily resized progress icon");
    expect_near(icons[0U]->destination.x, 724.0,
                "team one progress icon keeps exact left x");
    expect_near(icons[0U]->destination.y, 53.0,
                "team one progress icon converts to exact top-left y");
    expect_near(icons[1U]->destination.x, 848.0,
                "team two progress icon keeps exact right x");
    expect(icons[0U]->modulation.color ==
               battlespades::ui::ColorRgba8{1U, 2U, 3U, 255U},
           "StateData custom team color tints the progress icon");
    expect(team1_labels.size() == 3U,
           "retail submits foreground, translated shadow, and foreground again");
    expect_near(team1_labels[0U]->destination.x, 632.0,
                "left label rectangle centres on x 712");
    expect_near(team1_labels[0U]->destination.y, 77.0,
                "left label baseline applies the centred Spades-20 line span");
    expect(team1_labels[0U]->destination.height == 0.0 &&
               team1_labels[0U]->vertical_alignment ==
                   battlespades::ui::VerticalTextAlignment::baseline,
           "progress text uses Label's baseline-centre transform");
    expect_near(team1_labels[1U]->destination.x,
                team1_labels[0U]->destination.x + 2.2,
                "shadow includes outer and inner x translations");
    expect_near(team1_labels[1U]->destination.y,
                team1_labels[0U]->destination.y + 2.2,
                "shadow includes outer and inner negative-y translations");
}

void test_team_progress_particles_match_retail_manager() {
    using battlespades::frontend::GameHudModel;
    using battlespades::frontend::GameHudPresentation;
    using battlespades::frontend::GameHudPresentationContext;
    using battlespades::frontend::GameHudTeamProgressIcon;
    using battlespades::frontend::GameHudTeamProgressUpdate;
    using battlespades::frontend::GameHudTeamScore;

    constexpr battlespades::ui::ColorRgba8 blue{7U, 61U, 143U, 255U};
    constexpr battlespades::ui::ColorRgba8 green{31U, 177U, 47U, 255U};
    GameHudModel model;
    model.set_team_scores(
        GameHudTeamScore{layout::Team::team1, 0, 10, true, blue},
        GameHudTeamScore{layout::Team::team2, 0, 10, true, green}, true);

    model.update_team_progress(GameHudTeamProgressUpdate{
        layout::Team::team1, true, false, true, true, 20.0, 100.0,
        GameHudTeamProgressIcon::base});
    model.update_team_progress(GameHudTeamProgressUpdate{
        layout::Team::team1, true, true, true, true, 25.0, 100.0,
        GameHudTeamProgressIcon::base});
    expect(model.team_progress_particles().size() == 1U,
           "a visible team-one loss starts one pooled HUD particle");
    const auto initial = model.team_progress_particles().front();
    expect_near(initial.offset_x, -77.0,
                "team one starts at direction times sixty-seven minus ten");
    expect_near(initial.offset_y, 0.0,
                "TeamProgress particles start on the widget baseline");
    expect_near(initial.direction_x, -1.0,
                "team one particles travel left");
    expect_near(initial.direction_y, -1.0,
                "TeamProgress particles travel down");
    expect_near(initial.remaining_seconds, 0.5,
                "TeamProgress particle lifetime is half a second");
    expect_near(initial.speed_pixels_per_second, 200.0,
                "TeamProgress particle speed is two hundred pixels per second");
    expect_near(initial.scale, 0.5,
                "ordinary progress loss starts at half scale");
    expect_near(initial.scale_rate_per_second, 0.8,
                "TeamProgress particles grow at the recovered rate");
    expect_near(initial.alpha_rate_per_second, -2.0,
                "TeamProgress particles fade at the recovered rate");
    expect(initial.color == blue,
           "particles use the live custom team color");

    model.update_team_progress(GameHudTeamProgressUpdate{
        layout::Team::team2, true, false, true, true, 10.0, 100.0,
        GameHudTeamProgressIcon::base});
    model.update_team_progress(GameHudTeamProgressUpdate{
        layout::Team::team2, true, true, true, true, 10.05, 100.0,
        GameHudTeamProgressIcon::base});
    expect(model.team_progress_particles().size() == 2U,
           "team two can start a second pooled particle");
    const auto small = model.team_progress_particles()[1U];
    expect_near(small.offset_x, 57.0,
                "team two starts at plus sixty-seven minus ten");
    expect_near(small.direction_x, 1.0,
                "team two particles travel right");
    expect_near(small.scale, 0.02,
                "sub-ten threshold uses delta times point four scale");
    expect(small.color == green,
           "team-two particles use its custom color");

    model.update_team_progress(GameHudTeamProgressUpdate{
        layout::Team::team1, true, true, true, true, 20.0, 100.0,
        GameHudTeamProgressIcon::base});
    expect(model.team_progress_particles().size() == 2U,
           "retail suppresses particles while progress heals");
    model.update_team_progress(GameHudTeamProgressUpdate{
        layout::Team::team1, true, true, true, true, 0.0, 100.0,
        GameHudTeamProgressIcon::base});
    expect(model.team_progress_particles().size() == 2U,
           "wire zero resets the row without spawning a particle");

    model.tick();
    const auto advanced = model.team_progress_particles().front();
    expect_near(advanced.offset_x, -77.0 - 200.0 / 60.0,
                "HudParticle advances x once per fixed tick");
    expect_near(advanced.offset_y, -200.0 / 60.0,
                "HudParticle advances y once per fixed tick");
    expect_near(advanced.scale, 0.5 + 0.8 / 60.0,
                "HudParticle applies scale_rate once per fixed tick");
    expect_near(advanced.alpha, 1.0 - 2.0 / 60.0,
                "HudParticle applies alpha_rate once per fixed tick");
    expect_near(advanced.remaining_seconds, 0.5 - 1.0 / 60.0,
                "HudParticle consumes its half-second lifetime");

    for (int tick = 0; tick < 30; ++tick) {
        model.tick();
    }
    expect(model.team_progress_particles().empty(),
           "expired HudParticles return to the recovered manager pool");

    GameHudModel rendered;
    rendered.set_team_scores(
        GameHudTeamScore{layout::Team::team1, 0, 10, true, blue},
        GameHudTeamScore{layout::Team::team2, 0, 10, true, green}, true);
    rendered.update_team_progress(GameHudTeamProgressUpdate{
        layout::Team::team1, true, false, true, true, 20.0, 100.0,
        GameHudTeamProgressIcon::base});
    rendered.update_team_progress(GameHudTeamProgressUpdate{
        layout::Team::team1, true, true, true, true, 25.0, 100.0,
        GameHudTeamProgressIcon::base});
    const auto draw = GameHudPresentation{}.build(
        rendered, GameHudPresentationContext{PixelExtent{1600, 900}, 1'000U});
    const battlespades::ui::SpriteDrawCommand* particle{};
    for (const auto& command : draw.commands()) {
        const auto* sprite =
            std::get_if<battlespades::ui::SpriteDrawCommand>(&command);
        if (sprite != nullptr &&
            sprite->asset_id == "png/high/block128.png") {
            particle = sprite;
            break;
        }
    }
    expect(particle != nullptr,
           "TeamProgress loss draws the retail block128 particle asset");
    expect_near(particle->destination.x, 723.0,
                "particle blit uses its uncentred bottom-left x");
    expect_near(particle->destination.y, 16.0,
                "particle baseline converts exactly into top-left draw space");
    expect_near(particle->destination.width, 64.0,
                "128px source at half scale draws sixty-four pixels wide");
    expect_near(particle->destination.height, 64.0,
                "TeamProgress particle remains square");
    expect(particle->modulation.color == blue &&
               particle->modulation.opacity_per_mille == 1'000U,
           "particle draw preserves retail team tint and initial alpha");

    rendered.reset_team_progress();
    expect(rendered.team_progress_particles().empty(),
           "map/session reset clears active TeamProgress particles");

    GameHudModel bounded;
    for (int value = 1; value <= 101; ++value) {
        bounded.update_team_progress(GameHudTeamProgressUpdate{
            layout::Team::neutral, true, true, true, true,
            static_cast<double>(value), 100.0,
            GameHudTeamProgressIcon::base});
    }
    expect(bounded.team_progress_particles().size() == 100U,
           "HUD uses the recovered one-hundred-particle pool and drops overflow");
}

void test_head_count_value_type_is_server_owned() {
    using battlespades::frontend::resolve_game_hud_head_count;

    const auto players = resolve_game_hud_head_count(0U, 91, 7);
    expect(players.visible && players.value == 7,
           "HeadCount type zero must display the live roster count");
    const auto score = resolve_game_hud_head_count(1U, 91, 7);
    expect(score.visible && score.value == 91,
           "HeadCount score mode must not substitute the roster count");
    const auto inactive = resolve_game_hud_head_count(3U, 91, 7);
    expect(!inactive.visible,
           "HeadCount inactive mode must suppress the complete top-centre widget");
    const auto unknown = resolve_game_hud_head_count(6U, 91, 7);
    expect(unknown.visible && unknown.value == 91,
           "unknown HeadCount values must follow retail's visible score fallback");
}

void test_territory_layout_matches_retail_transforms() {
    const PixelExtent window{1600, 900};
    const auto base =
        layout::territory_base(window, 5U, 2U, 50.0, false);
    expect_near(base.centre_x, 1530.0,
                "territory strip stays seventy pixels from the right");
    expect_near(base.centre_y, 622.0,
                "base index advances down by thirty-two pixels");
    expect_near(base.icon_scale, 0.8, "normal base uses icon scale 0.8");
    expect_near(base.plate.x, 1478.8,
                "plate is centred after the initial icon transform");
    expect_near(base.plate.y, 609.2, "plate bottom matches retail transform");
    expect_near(base.plate.width, 102.4,
                "128px plate is drawn through the 0.8 icon scale");
    expect_near(base.attacked_overlay.width, 51.2,
                "capture overlay scales the full plate to fifty percent");
    expect_near(base.letter.x, 1446.8,
                "letter keeps the nested negative one-hundred x offset");
    expect_near(base.letter.y, 607.28,
                "letter keeps the nested negative-six y offset");
    expect_near(base.letter.width, 20.8,
                "letter receives icon scale twice plus letter scale");
    expect_near(base.letter.height, 25.28,
                "79px letter height preserves the nested transform");

    const auto emphasized =
        layout::territory_base(window, 9U, 2U, 50.0, true);
    expect_near(emphasized.centre_x, base.centre_x,
                "zero x interval makes base count irrelevant");
    expect_near(emphasized.icon_scale, 0.96,
                "containing player multiplies icon scale by 1.2");
    expect_near(emphasized.plate.width, 122.88,
                "containing-player plate visibly grows");
    expect_near(emphasized.letter.width, 29.952,
                "nested letter transform grows quadratically with icon scale");
}

void test_territory_state_matches_retail_actions_and_pulse() {
    using battlespades::frontend::GameHudModel;
    using battlespades::frontend::GameHudTerritoryBaseAction;
    using battlespades::frontend::GameHudTerritoryBaseUpdate;

    GameHudModel model;
    model.update_territory_base(GameHudTerritoryBaseUpdate{
        2U, GameHudTerritoryBaseAction::initial_info,
        layout::Team::team1, layout::Team::team2, 25.0});
    auto base = model.territory_bases().bases[2U];
    expect(base.has_value() && base->visible,
           "initial info creates a visible retained base");
    expect(base->controlled_by == layout::Team::team1 &&
               base->attacked_by == layout::Team::team2 &&
               base->capture_amount == 25.0,
           "initial info commits all detailed fields");

    model.update_territory_base(GameHudTerritoryBaseUpdate{
        2U, GameHudTerritoryBaseAction::entering,
        layout::Team::neutral, layout::Team::neutral, 99.0});
    base = model.territory_bases().bases[2U];
    expect(base->contains_player && base->capture_amount == 25.0,
           "entering changes only local containment and retains details");

    model.update_territory_base(GameHudTerritoryBaseUpdate{
        2U, GameHudTerritoryBaseAction::contended,
        layout::Team::neutral, layout::Team::neutral, 99.0});
    model.tick();
    base = model.territory_bases().bases[2U];
    expect(base->contended && base->contend_time > 0.0 &&
               base->contend_alpha > 0.9,
           "contended bases begin the recovered orange pulse");

    model.update_territory_base(GameHudTerritoryBaseUpdate{
        2U, GameHudTerritoryBaseAction::capture_update,
        layout::Team::team2, layout::Team::team1, 75.0});
    base = model.territory_bases().bases[2U];
    expect(base->controlled_by == layout::Team::team2 &&
               base->attacked_by == layout::Team::team1 &&
               base->capture_amount == 75.0,
           "capture update replaces all detailed fields");

    model.update_territory_base(GameHudTerritoryBaseUpdate{
        2U, GameHudTerritoryBaseAction::deactivate,
        layout::Team::team2, layout::Team::team1, 75.0});
    expect(!model.territory_bases().bases[2U]->visible,
           "deactivate hides without deleting retained state");
    model.update_territory_base(GameHudTerritoryBaseUpdate{
        2U, GameHudTerritoryBaseAction::activate,
        layout::Team::team2, layout::Team::team1, 75.0});
    expect(model.territory_bases().bases[2U]->visible,
           "activate reveals the retained base");

    model.reset_territory_bases();
    expect(!model.territory_bases().bases[2U].has_value(),
           "map/session reset clears every territory slot");
}

void test_territory_widget_reaches_the_draw_list() {
    using battlespades::frontend::GameHudModel;
    using battlespades::frontend::GameHudPresentation;
    using battlespades::frontend::GameHudPresentationContext;
    using battlespades::frontend::GameHudTerritoryBaseAction;
    using battlespades::frontend::GameHudTerritoryBaseUpdate;
    using battlespades::ui::SpriteDrawCommand;

    GameHudModel model;
    model.update_territory_base(GameHudTerritoryBaseUpdate{
        0U, GameHudTerritoryBaseAction::initial_info,
        layout::Team::team1, layout::Team::team2, 50.0});
    model.update_territory_base(GameHudTerritoryBaseUpdate{
        0U, GameHudTerritoryBaseAction::contended,
        layout::Team::neutral, layout::Team::neutral, 0.0});
    model.tick();

    const auto list = GameHudPresentation{}.build(
        model, GameHudPresentationContext{{1600, 900}, 1'000U});
    std::vector<const SpriteDrawCommand*> plates;
    const SpriteDrawCommand* frame{};
    const SpriteDrawCommand* letter{};
    for (const auto& command : list.commands()) {
        const auto* sprite = std::get_if<SpriteDrawCommand>(&command);
        if (sprite == nullptr) {
            continue;
        }
        if (sprite->asset_id == "png/ui/modes/tc_backplate.png") {
            plates.push_back(sprite);
        } else if (sprite->asset_id == "png/ui/modes/tc_frame.png") {
            frame = sprite;
        } else if (sprite->asset_id == "png/ui/modes/tc_text_a.png") {
            letter = sprite;
        }
    }
    expect(plates.size() == 2U && frame != nullptr && letter != nullptr,
           "one active base emits controlled plate, capture overlay, frame, and letter");
    expect_near(plates[0U]->destination.x, 1478.8,
                "base zero plate keeps exact right-edge x");
    expect_near(plates[0U]->destination.y, 201.2,
                "base zero converts its bottom-origin plate to top-left");
    expect_near(plates[1U]->destination.width, 51.2,
                "attacked plate is stretched to the capture fraction");
    expect(plates[0U]->modulation.color ==
               battlespades::ui::ColorRgba8{44U, 117U, 179U, 255U} &&
               plates[1U]->modulation.color ==
                   battlespades::ui::ColorRgba8{137U, 179U, 44U, 255U},
           "controlled and attacked plates use retail TEAM_COLOURS");
    expect(frame->modulation.color ==
               battlespades::ui::ColorRgba8{251U, 100U, 4U, 255U},
           "first contested tick blends frame almost fully to orange");
    expect(letter->modulation.color == frame->modulation.color,
           "letter inherits the contested frame tint");
    expect(battlespades::frontend::game_hud_assets::territory_letters[7U] ==
               "png/ui/modes/tc_text_g.png" &&
               battlespades::frontend::game_hud_assets::territory_letters[9U] ==
                   "png/ui/modes/tc_text_g.png",
           "H through J intentionally reuse retail's G sprite");
}

void test_damage_indicator_uses_retail_relative_angle() {
    using battlespades::frontend::damage_indicator_angle_degrees;

    // Facing east, damage from the east is straight ahead and damage from
    // north is a quarter-turn counter to the view vector.
    expect_near(damage_indicator_angle_degrees(1.0, 0.0, 1.0, 0.0),
                0.0, "front hit has zero rotation");
    expect_near(damage_indicator_angle_degrees(0.0, 1.0, 1.0, 0.0),
                90.0, "north hit is positive ninety degrees");
    expect_near(damage_indicator_angle_degrees(-1.0, 0.0, 0.0, 1.0),
                90.0, "relative angle subtracts the current view");
}

/**
 * End-to-end: the three widgets added from the recovery must actually reach
 * the draw list, and the block counter must carry the ammo threshold colour
 * rather than plain white. That colour is the A47/A48 alias trap, so it is
 * worth asserting on the real emitted command and not just the pure helper.
 */
void test_new_widgets_reach_the_draw_list() {
    using battlespades::frontend::GameHudModel;
    using battlespades::frontend::GameHudPresentation;
    using battlespades::frontend::GameHudPresentationContext;
    using battlespades::frontend::GameHudTeamScore;

    GameHudModel model;
    model.set_team(layout::Team::team2);
    model.set_block_state("png/ui/mini_map/minimap_blockcrate.png", 0, 1500, true);
    model.set_jetpack_fuel(0.5, true);
    model.set_disguise_active(true);
    model.set_parachute_active(true);
    model.set_team_color({10U, 20U, 30U, 255U});
    model.set_team_scores(GameHudTeamScore{layout::Team::team1, 4, 200, true,
                                          battlespades::ui::ColorRgba8{
                                              1U, 2U, 3U, 255U}},
                          GameHudTeamScore{layout::Team::team2, 2, 200, true}, true);
    model.set_match_clock(741.0, true);
    model.add_kill(std::string{"OldKiller"},
                   battlespades::ui::ColorRgba8{10U, 20U, 30U, 255U},
                   "png/ui/kill_types/headshot.png", 330.0,
                   "OldVictim",
                   battlespades::ui::ColorRgba8{40U, 50U, 60U, 255U});
    model.add_kill(std::string{"NewKiller"},
                   battlespades::ui::ColorRgba8{70U, 80U, 90U, 255U},
                   "png/ui/kill_types/fall.png", 330.0,
                   "NewVictim",
                   battlespades::ui::ColorRgba8{100U, 110U, 120U, 255U});
    model.set_big_message("Blue took the intel");
    model.set_respawn_time(5U);
    model.add_damage_indicator(90.0);
    model.set_intel_carrier(3U, true);
    battlespades::frontend::GameHudMinimapState minimap;
    minimap.visible = true;
    minimap.focus_x = 20.0;
    minimap.focus_y = 500.0;
    minimap.zones.push_back(
        battlespades::frontend::GameHudMinimapZone{
            10.0, 490.0, 30.0, 510.0,
            battlespades::ui::ColorRgba8{1U, 2U, 3U, 255U},
            "png/ui/minimap_base.png", 16.0, 1.0});
    minimap.view_cone = battlespades::frontend::GameHudMinimapMarker{
        "png/ui/map_view_cone.png", 20.0, 500.0, 256.0 * 0.2, -180.0,
        battlespades::ui::ColorRgba8{44U, 117U, 179U, 255U}, false};
    minimap.markers.push_back(battlespades::frontend::GameHudMinimapMarker{
        "png/ui/map_player_16.png", 20.0, 500.0, 16.0, 0.0,
        battlespades::ui::ColorRgba8{66U, 175U, 255U, 255U}, true});
    model.set_minimap(std::move(minimap));

    GameHudPresentationContext context;
    context.window = PixelExtent{1600, 900};
    context.measure_big_text = [](std::string_view text, double) {
        return static_cast<double>(text.size()) * 10.0;
    };
    const GameHudPresentation presentation;
    const auto list = presentation.build(model, context);

    const auto find_sprite = [&list](std::string_view asset)
        -> const battlespades::ui::SpriteDrawCommand* {
        for (const auto& command : list.commands()) {
            const auto* sprite =
                std::get_if<battlespades::ui::SpriteDrawCommand>(&command);
            if (sprite != nullptr && sprite->asset_id == asset) {
                return sprite;
            }
        }
        return nullptr;
    };
    const auto find_text = [&list](std::string_view text)
        -> const battlespades::ui::TextDrawCommand* {
        for (const auto& command : list.commands()) {
            const auto* draw = std::get_if<battlespades::ui::TextDrawCommand>(&command);
            if (draw != nullptr && draw->localization_key == text) {
                return draw;
            }
        }
        return nullptr;
    };

    expect(find_sprite("png/ui/jetpack_fuel/jetpack_fuel_frame.png") != nullptr,
           "jetpack gauge frame is drawn");
    const auto* disguise = find_sprite("png/ui/weapons/disguise.png");
    const auto* parachute = find_sprite("png/ui/weapons/parachute.png");
    expect(disguise != nullptr && parachute != nullptr,
           "active disguise and parachute status icons reach the draw list");
    // TOOL_IMAGES load at scale 1.0: glScalef(0.15) on the 330 px art
    // (live A/B 2026-09-29: the 0.64-shrunk 31 px chute was ~60% of retail).
    expect_near(disguise->destination.width, 330.0 * 0.15,
                "status tool draws the unshrunk 330 px TOOL_IMAGES art at 0.15");
    expect(disguise->destination == parachute->destination,
           "the two active-equipment icons intentionally share retail geometry");
    const auto* fuel = find_sprite("png/ui/jetpack_fuel/jetpack_fuel_bar.png");
    expect(fuel != nullptr, "jetpack fill is drawn");
    expect_near(fuel->destination.height, layout::jetpack_frame_height * 0.5,
                "half fuel draws half the bar");
    expect(find_sprite("png/ui/head_count/hc_frame_long.png") != nullptr,
           "head count bar is drawn");
    const auto* team1_head =
        find_sprite("png/ui/icons/deuce_head_colour_2.png");
    const auto* team2_head =
        find_sprite("png/ui/icons/deuce_head_colour_1.png");
    expect(team1_head != nullptr && team2_head != nullptr,
           "HeadCount draws both retail face tint layers");
    expect_near(team1_head->destination.x, 728.5,
                 "TEAM1 head is centred on the left timer edge");
    expect_near(team2_head->destination.x, 848.5,
                 "TEAM2 head is centred on the right timer edge");
    expect_near(team1_head->destination.y, 17.0,
                "head is vertically centred in the HeadCount backing");
    expect_near(team1_head->destination.width, 23.0,
                 "head width truncates at load before the 0.5 draw scale");
    expect_near(team1_head->destination.height, 22.0,
                 "head height truncates at load before the 0.5 draw scale");
    expect(team1_head->modulation.color ==
               battlespades::ui::ColorRgba8{1U, 2U, 3U, 255U},
           "StateData custom TEAM1 colour tints the head overlay");
    const auto* team1_score = find_text("4/200");
    const auto* team2_score = find_text("2/200");
    expect(team1_score != nullptr && team2_score != nullptr,
           "HeadCount draws both team score strings");
    expect(team1_score->destination ==
               battlespades::ui::DrawRect{637.0, 8.0, 80.0, 40.0} &&
               team2_score->destination ==
                   battlespades::ui::DrawRect{879.0, 8.0, 80.0, 40.0},
           "HeadCount score cells span the complete backing height");
    expect(team1_score->horizontal_alignment ==
               battlespades::ui::HorizontalTextAlignment::right &&
               team2_score->horizontal_alignment ==
                   battlespades::ui::HorizontalTextAlignment::left &&
               team1_score->vertical_alignment ==
                   battlespades::ui::VerticalTextAlignment::retail_center &&
               team2_score->vertical_alignment ==
                   battlespades::ui::VerticalTextAlignment::retail_center,
           "HeadCount preserves retail x alignment and centres both score spans");
    const auto* timer_icon = find_sprite("png/ui/timer/timer.png");
    expect(timer_icon != nullptr, "timer icon is drawn");
    expect_near(timer_icon->destination.x, 756.2,
                "timer icon converts its recovered centre anchor");
    expect_near(timer_icon->destination.y, 15.2,
                "timer icon is vertically centred in the HeadCount backing");
    expect(find_sprite("png/ui/timer/timer_frame.png") == nullptr,
           "ordinary timer omits the tutorial-only frame");
    std::vector<const battlespades::ui::TextDrawCommand*> timer_draws;
    for (const auto& command : list.commands()) {
        const auto* draw =
            std::get_if<battlespades::ui::TextDrawCommand>(&command);
        if (draw != nullptr && draw->localization_key == "12:21") {
            timer_draws.push_back(draw);
        }
    }
    expect(timer_draws.size() == 2U,
           "ordinary timer emits retail shadow and foreground draws");
    expect(timer_draws[0U]->destination ==
               battlespades::ui::DrawRect{787.0, 10.0, 0.0, 40.0} &&
               timer_draws[1U]->destination ==
                   battlespades::ui::DrawRect{785.0, 8.0, 0.0, 40.0} &&
               timer_draws[0U]->vertical_alignment ==
                   battlespades::ui::VerticalTextAlignment::retail_center &&
               timer_draws[1U]->vertical_alignment ==
                   battlespades::ui::VerticalTextAlignment::retail_center,
           "ordinary timer shares the centred HeadCount metric span");
    const auto* old_kill = find_sprite("png/ui/kill_types/headshot.png");
    const auto* new_kill = find_sprite("png/ui/kill_types/fall.png");
    expect(old_kill != nullptr && new_kill != nullptr,
           "kill causes render as icons between names");
    expect(old_kill->destination.y < new_kill->destination.y,
           "oldest surviving kill is above the newest, matching reversed(feed)");
    expect_near(old_kill->destination.width, 33.0,
                "330px kill icon is drawn at retail 0.1 scale");
    std::vector<const battlespades::ui::TextDrawCommand*> old_killer_draws;
    for (const auto& command : list.commands()) {
        const auto* draw =
            std::get_if<battlespades::ui::TextDrawCommand>(&command);
        if (draw != nullptr && draw->localization_key == "OldKiller") {
            old_killer_draws.push_back(draw);
        }
    }
    expect(old_killer_draws.size() == 2U,
           "kill names use one FTGL outline bitmap followed by one fill bitmap");
    expect(old_killer_draws.front()->retail_outline_stroke &&
               !old_killer_draws.back()->retail_outline_stroke &&
               old_killer_draws.front()->modulation.color ==
                   battlespades::ui::ColorRgba8{0U, 0U, 0U, 255U},
           "kill-name pass order is recovered FTGL outside stroke then normal fill");
    expect(old_killer_draws.front()->destination.y == 75.0 &&
               old_killer_draws.front()->vertical_alignment ==
                   battlespades::ui::VerticalTextAlignment::baseline,
           "outline and fill share retail's exact baseline rather than translated copies");
    expect(old_killer_draws.back()->destination.y == 75.0 &&
               old_killer_draws.back()->vertical_alignment ==
                   battlespades::ui::VerticalTextAlignment::baseline,
           "oldest kill name uses retail's exact 75px top-origin baseline");
    const auto* big_frame =
        find_sprite("png/ui/in_game_menus/big_text_frame.png");
    expect(big_frame != nullptr, "CHAT_BIG renders the retail backing frame");
    expect_near(big_frame->destination.width,
                40.0 + static_cast<double>(std::string_view{"Blue took the intel"}.size()) * 10.0,
                "big-text frame follows shaped content width plus forty");
    expect_near(big_frame->destination.height, 58.0 * 0.64,
                "one big-text line keeps the frame's global_scale load height");
    std::vector<const battlespades::ui::TextDrawCommand*> big_text_draws;
    for (const auto& command : list.commands()) {
        const auto* draw =
            std::get_if<battlespades::ui::TextDrawCommand>(&command);
        if (draw != nullptr &&
            draw->localization_key == "Blue took the intel") {
            big_text_draws.push_back(draw);
        }
    }
    expect(big_text_draws.size() == 2U,
           "CHAT_BIG emits Label.draw_shadowed before its foreground");
    expect(big_text_draws[0U]->destination ==
               battlespades::ui::DrawRect{802.0, 200.5, 0.0, 0.0} &&
               big_text_draws[1U]->destination ==
                   battlespades::ui::DrawRect{800.0, 198.5, 0.0, 0.0} &&
               big_text_draws[0U]->vertical_alignment ==
                   battlespades::ui::VerticalTextAlignment::baseline,
           "CHAT_BIG uses the centered Edo-30 baseline and converted shadow offset");
    expect(big_text_draws[0U]->modulation.color ==
               battlespades::ui::ColorRgba8{64U, 64U, 64U, 255U} &&
               big_text_draws[1U]->modulation.color ==
                   battlespades::ui::ColorRgba8{231U, 74U, 25U, 255U},
           "CHAT_BIG keeps retail shadow gray and BIG_TEXT_COLOR foreground");
    const auto* damage_indicator = find_sprite("png/ui/indicator.png");
    expect(damage_indicator != nullptr,
           "SetHP direction state reaches the retail indicator sprite");
    expect_near(damage_indicator->destination.width, 658.0 * 0.5,
                "damage indicator uses recovered half scale");
    expect_near(damage_indicator->destination.height, 952.0 * 0.5,
                "damage indicator keeps the authored transparent canvas");
    expect_near(damage_indicator->destination.x +
                    damage_indicator->destination.width * 0.5,
                800.0, "damage indicator is centred horizontally");
    expect_near(damage_indicator->destination.y +
                    damage_indicator->destination.height * 0.5,
                450.0, "damage indicator is centred vertically");
    expect_near(damage_indicator->rotation_degrees, 90.0,
                "damage indicator preserves pyglet clockwise degrees");
    const auto* map = find_sprite("runtime/minimap");
    const auto* map_frame =
        find_sprite("png/ui/mini_map/minimap_frame.png");
    expect(map != nullptr && map_frame != nullptr,
           "north-up minimap texture and retail frame reach the draw list");
    expect_near(map->destination.x, 1600.0 - 128.0 - 15.0,
                "minimap has the recovered right inset");
    expect_near(map->destination.y, 15.0,
                "bottom-origin top-right map converts to a 15px top inset");
    expect(map->source_pixels.has_value(),
           "small minimap is a texture crop, not a scaled whole map");
    expect_near(map->source_pixels->x, 0.0,
                "edge focus clamps to crop centre x=64");
    expect_near(map->source_pixels->y, 384.0,
                "edge focus clamps to crop centre y=448");
    expect_near(map_frame->destination.x, 1600.0 - 150.0,
                "142px frame starts seven pixels before content");
    expect_near(map_frame->destination.y, 8.0,
                "142px frame is inset eight pixels from the top");
    const auto* cone = find_sprite("png/ui/map_view_cone.png");
    const auto* local_marker = find_sprite("png/ui/map_player_16.png");
    const auto* zone_icon = find_sprite("png/ui/minimap_base.png");
    const auto* intel_corner =
        find_sprite("png/ui/icons/intel_blue_90.png");
    expect(cone != nullptr && local_marker != nullptr,
           "focus cone and local marker overlay the map");
    const battlespades::ui::DrawRect expected_minimap_clip{
        1600.0 - 128.0 - 15.0, 15.0, 128.0, 128.0};
    expect(cone->clip_pixels == expected_minimap_clip &&
               local_marker->clip_pixels == expected_minimap_clip,
           "rotated minimap overlays retain the retail 128px raster clip");
    expect(zone_icon != nullptr,
           "server-authored objective zone icon reaches the minimap");
    // Live retail captures (Diamond Mine drop-off, TC letters, 2026-09-27)
    // show zone icons spilling over the minimap frame: they are not clipped.
    expect(!zone_icon->clip_pixels.has_value(),
           "objective zone icons draw unclipped, like retail");
    // MinimapZone.draw keeps icon.scale and pulses icon.opacity:
    // int((sin(phase) * 0.4 + 0.6) * 255), 0.6 at phase 0.
    expect_near(zone_icon->destination.width, 16.0,
                "zone icon keeps its packet icon_scale (no size pulse)");
    expect(zone_icon->modulation.opacity_per_mille == 600U,
           "zone icon opacity starts at the recovered sin phase 0.6");
    expect(intel_corner != nullptr,
           "green carrier receives the opposing blue intel corner icon");
    expect_near(intel_corner->destination.x, 1600.0 - 80.0 - 45.0,
                "centre-anchored intel icon keeps the recovered right inset");
    expect_near(intel_corner->destination.y, 250.0 - 45.0,
                "enabled minimap moves the intel icon 250px below the top");
    expect_near(cone->rotation_degrees, -180.0,
                "view cone preserves retail yaw minus 180");
    expect_near(cone->destination.width, 256.0 * 0.2,
                "view cone applies Player.player_cone_view_icon_scale");
    expect(big_text_draws[1U]->preferred_font_asset == "fonts/Edo.ttf" &&
               big_text_draws[1U]->requested_font_size_pixels == 30.0,
           "CHAT_BIG uses the recovered Edo 30 label");
    std::vector<const battlespades::ui::TextDrawCommand*> respawn_draws;
    for (const auto& command : list.commands()) {
        const auto* draw =
            std::get_if<battlespades::ui::TextDrawCommand>(&command);
        if (draw != nullptr && draw->localization_key == "Respawning in 5") {
            respawn_draws.push_back(draw);
        }
    }
    expect(respawn_draws.size() == 2U,
           "respawn label emits its shadow and foreground");
    expect(respawn_draws[0U]->preferred_font_asset == "fonts/Spades.ttf" &&
               respawn_draws[0U]->requested_font_size_pixels == 40.0,
           "respawn label uses recovered HUD_FONT at 40 pixels");
    expect_near(respawn_draws[0U]->destination.x, 2.0,
                "respawn shadow keeps Label.draw_shadowed x offset");
    expect_near(respawn_draws[0U]->destination.y, 474.0,
                "respawn shadow applies the centered Spades-40 baseline plus two");
    expect(respawn_draws[0U]->vertical_alignment ==
               battlespades::ui::VerticalTextAlignment::baseline,
           "respawn text must not use generic line-box centering");

    expect(find_text("12:21") != nullptr, "the countdown renders as MM:SS");
    expect(find_text("4/200") != nullptr, "left team shows score over max");
    expect(find_text("2/200") != nullptr, "right team shows score over max");
    const auto* left_score = find_text("4/200");
    expect(left_score != nullptr &&
               left_score->modulation.color ==
                   battlespades::ui::ColorRgba8{1U, 2U, 3U, 255U},
           "custom StateData team color overrides the stock HeadCount tint");

    const auto* blocks = find_text("0");
    expect(blocks != nullptr, "block count is drawn");
    expect(blocks->modulation.color == layout::not_enough_ammo_color,
           "an empty block wallet renders in NOT_ENOUGH_AMMO_COLOR, not white");

    // Hiding a widget must remove it entirely rather than draw it empty.
    GameHudModel hidden;
    hidden.set_jetpack_fuel(1.0, false);
    const auto bare = presentation.build(hidden, context);
    bool has_jetpack{};
    for (const auto& command : bare.commands()) {
        const auto* sprite = std::get_if<battlespades::ui::SpriteDrawCommand>(&command);
        if (sprite != nullptr &&
            sprite->asset_id == "png/ui/jetpack_fuel/jetpack_fuel_frame.png") {
            has_jetpack = true;
        }
    }
    expect(!has_jetpack, "a class without a jetpack draws no gauge at all");

    model.tick();
    expect_near(model.match_clock_seconds(), 741.0 - GameHudModel::fixed_dt,
                "the countdown advances locally between server refresh packets");
    expect(model.respawn().remaining_seconds < 5.0,
           "finite respawn countdown advances locally");
    expect(model.damage_indicators().size() == 1U &&
               model.damage_indicators()[0U].remaining_seconds < 1.2,
           "damage flash fades on the fixed HUD tick");

    GameHudModel capped_feed;
    for (int index{}; index < 6; ++index) {
        capped_feed.add_kill(std::nullopt, {}, {}, 330.0,
                             "Victim" + std::to_string(index), {});
    }
    expect(capped_feed.kill_feed().size() == 5U &&
               capped_feed.kill_feed().back().victim_name == "Victim1",
           "kill feed inserts newest first and discards the oldest beyond five");

    GameHudModel iconless_feed;
    iconless_feed.add_kill(std::nullopt, {}, {}, 330.0, "OldWorldKill", {});
    iconless_feed.add_kill(std::nullopt, {}, {}, 330.0, "NewWorldKill", {});
    const auto iconless_draw = presentation.build(iconless_feed, context);
    const auto foreground_baseline = [&iconless_draw](std::string_view name) {
        double baseline{-1.0};
        for (const auto& command : iconless_draw.commands()) {
            const auto* draw =
                std::get_if<battlespades::ui::TextDrawCommand>(&command);
            if (draw != nullptr && draw->localization_key == name &&
                draw->modulation.color !=
                    battlespades::ui::ColorRgba8{0U, 0U, 0U, 255U}) {
                baseline = draw->destination.y;
            }
        }
        return baseline;
    };
    expect_near(foreground_baseline("OldWorldKill"), 75.0,
                "iconless oldest kill preserves the retail baseline");
    expect_near(foreground_baseline("NewWorldKill"), 98.0,
                "iconless kill rows advance by measured ChatLine 13 plus padding 10");

    GameHudModel queued_messages;
    queued_messages.set_big_message("first");
    queued_messages.set_big_message("second");
    expect(queued_messages.big_message().text == "first" &&
               queued_messages.big_message().pending.size() == 1U &&
               queued_messages.big_message().pending.front().text == "second",
           "a burst preserves the current big message and queues the next one");
    for (int tick{}; tick < 89; ++tick) queued_messages.tick();
    expect(queued_messages.big_message().text == "first",
           "a queued CHAT_BIG line keeps BIG_TEXT_MIN_DURATION (1.5 s) of dwell");
    queued_messages.tick();
    expect(queued_messages.big_message().text == "second",
           "HUD.update swaps to the queued line once big_text_time > 1.5 s");
    for (int tick{}; tick < 239; ++tick) queued_messages.tick();
    expect(queued_messages.big_message().text == "second",
           "with nothing queued a line runs its full four-second duration");
    queued_messages.tick();
    expect(queued_messages.big_message().text.empty(),
           "the big text clears when its own duration ends");

    GameHudModel short_message;
    short_message.set_big_message("short", false, 1.0);
    short_message.set_big_message("next");
    for (int tick{}; tick < 59; ++tick) short_message.tick();
    expect(short_message.big_message().text == "short",
           "min dwell is min(duration, 1.5)");
    short_message.tick();
    expect(short_message.big_message().text == "next",
           "a one-second line yields after one second");

    GameHudModel bounded_messages;
    bounded_messages.set_big_message("active");
    for (int index{}; index < 8; ++index) {
        bounded_messages.set_big_message("queued" + std::to_string(index));
    }
    expect(bounded_messages.big_message().pending.size() == 6U &&
               bounded_messages.big_message().pending.front().text == "queued0" &&
               bounded_messages.big_message().pending.back().text == "queued7",
           "CHAT_BIG overflow pops the newest queued row (list.pop()) before appending");
    for (int tick{}; tick < 90; ++tick) bounded_messages.tick();
    expect(bounded_messages.big_message().text == "queued7",
           "retail list.pop() shows the newest queued row next");

    bounded_messages.set_big_message("override", true);
    expect(bounded_messages.big_message().text == "override" &&
               bounded_messages.big_message().pending.empty(),
           "packet-50 override must clear both recovered pending lists");

    GameHudModel no_respawns;
    no_respawns.set_respawn_time(0xFFU);
    const auto no_respawn_draw =
        presentation.build(no_respawns, context);
    bool found_no_respawn{};
    for (const auto& command : no_respawn_draw.commands()) {
        const auto* text =
            std::get_if<battlespades::ui::TextDrawCommand>(&command);
        found_no_respawn |= text != nullptr &&
                            text->localization_key == "No respawns!";
    }
    expect(found_no_respawn,
           "NEVER_RESPAWN_TIME renders the retail no-respawns message");
    no_respawns.clear_respawn();
    expect(!no_respawns.respawn().visible,
           "authoritative CreatePlayer can clear the death overlay");
    no_respawns.set_respawn_time(0U);
    expect(!no_respawns.respawn().visible &&
               !no_respawns.respawn().never_respawn,
           "zero-second Zombie/VIP transitions must not fabricate a generic respawn countdown");
    const auto death_camera_draw = presentation.build(no_respawns, context);
    bool found_fabricated_camera_text{};
    for (const auto& command : death_camera_draw.commands()) {
        const auto* text =
            std::get_if<battlespades::ui::TextDrawCommand>(&command);
        found_fabricated_camera_text |=
            text != nullptr &&
            (text->localization_key.find("DEATH CAMERA") != std::string::npos ||
             text->localization_key.find("SPECTATING") != std::string::npos ||
             text->localization_key.find("CHANGE PLAYER") != std::string::npos ||
             text->localization_key.find("CLICK TO CHASE") != std::string::npos);
    }
    expect(!found_fabricated_camera_text,
           "retail death/chase cameras must not synthesize a spectator title or input hint");

    model.set_full_map_visible(true);
    const auto full_map_draw = presentation.build(model, context);
    bool found_full_frame{};
    bool found_full_texture{};
    std::vector<battlespades::ui::DrawRect> vertical_grid_lines;
    std::vector<battlespades::ui::DrawRect> horizontal_grid_lines;
    for (const auto& command : full_map_draw.commands()) {
        const auto* sprite =
            std::get_if<battlespades::ui::SpriteDrawCommand>(&command);
        if (sprite == nullptr) continue;
        if (sprite->asset_id == "png/ui/map/map_frame.png") {
            found_full_frame =
                sprite->destination.width == 552.0 &&
                sprite->destination.x == (1600.0 - 552.0) * 0.5;
        }
        if (sprite->asset_id == "runtime/minimap") {
            found_full_texture =
                sprite->destination.width == 512.0 &&
                sprite->source_pixels.has_value() &&
                sprite->source_pixels->width == 512.0;
        }
        if (sprite->asset_id == "png/high/white.png" &&
            sprite->modulation.color ==
                battlespades::ui::ColorRgba8{255U, 255U, 255U, 255U} &&
            sprite->modulation.intensity_per_mille == 1'000U &&
            sprite->modulation.opacity_per_mille == 500U) {
            if (sprite->destination.width == 1.0 &&
                sprite->destination.height == 512.0) {
                vertical_grid_lines.push_back(sprite->destination);
            } else if (sprite->destination.width == 512.0 &&
                       sprite->destination.height == 1.0) {
                horizontal_grid_lines.push_back(sprite->destination);
            }
        }
    }
    expect(found_full_frame && found_full_texture,
            "held VIEW_MAP switches to the centred 552/512 retail full map");
    expect(vertical_grid_lines.size() == 9U &&
               horizontal_grid_lines.size() == 9U,
           "full map emits both recovered nine-line 64px grid passes");
    const double full_map_left = (1600.0 - 512.0) * 0.5;
    const double full_map_top = (900.0 - 512.0) * 0.5;
    for (std::size_t index{}; index < 9U; ++index) {
        const double offset = static_cast<double>(index) * 64.0;
        expect(vertical_grid_lines[index] ==
                   battlespades::ui::DrawRect{
                       full_map_left + offset, full_map_top, 1.0, 512.0},
               "vertical full-map grid follows xrange(0, 576, 64)");
        expect(horizontal_grid_lines[index] ==
                   battlespades::ui::DrawRect{
                       full_map_left, full_map_top + offset, 512.0, 1.0},
               "horizontal full-map grid follows converted xrange(512, -64, -64)");
    }
}

void test_vip_banner_uses_retail_big_text_independently_from_score() {
    using battlespades::frontend::GameHudModel;
    using battlespades::frontend::GameHudPresentation;
    using battlespades::frontend::GameHudPresentationContext;

    constexpr std::string_view vip_text{"You are a V.I.P! Stay safe!"};
    constexpr std::string_view portrait_asset{
        "png/ui/classes/soldier_blue.png"};

    GameHudModel model;
    model.set_team_color({10U, 20U, 30U, 255U});
    model.set_class_portrait(std::string{portrait_asset}, true);
    // HUD.draw_healthbar checks high_minimap_visibility after the independent
    // enable_player_score branch. Disabling SCORE must not suppress VIP text.
    model.set_player_score(0, false);

    GameHudPresentationContext context;
    context.window = PixelExtent{800, 600};
    context.measure_big_text = [](std::string_view text, double) {
        return static_cast<double>(text.size()) * 10.0;
    };
    const GameHudPresentation presentation;
    const auto list = presentation.build(model, context);

    const battlespades::ui::SpriteDrawCommand* frame{};
    bool score_frame_visible{};
    bool portrait_visible{};
    std::vector<const battlespades::ui::TextDrawCommand*> text_draws;
    for (const auto& command : list.commands()) {
        if (const auto* sprite =
                std::get_if<battlespades::ui::SpriteDrawCommand>(&command)) {
            if (sprite->asset_id ==
                "png/ui/in_game_menus/big_text_frame.png") {
                frame = sprite;
            } else if (sprite->asset_id == "png/ui/score/score_frame.png") {
                score_frame_visible = true;
            } else if (sprite->asset_id == portrait_asset) {
                portrait_visible = true;
            }
        } else if (const auto* text =
                       std::get_if<battlespades::ui::TextDrawCommand>(&command);
                   text != nullptr && text->localization_key == vip_text) {
            text_draws.push_back(text);
        }
    }

    expect(!score_frame_visible && !portrait_visible,
           "disabled player score hides SCORE and portrait while VIP remains");
    expect(frame != nullptr, "high_minimap_visibility draws VIP backing frame");
    expect(frame->destination == battlespades::ui::DrawRect{
                                     400.0 -
                                         (40.0 + vip_text.size() * 10.0) * 0.5,
                                     519.5 - 58.0 * 0.64 * 0.5,
                                     40.0 + vip_text.size() * 10.0, 58.0 * 0.64},
           "VIP frame uses draw_big_text at bottom-origin W/2, 90");
    expect(text_draws.size() == 2U,
           "VIP label emits source Label.draw_shadowed and foreground draws");
    expect(text_draws[0U]->destination ==
               battlespades::ui::DrawRect{402.0, 530.5, 0.0, 0.0} &&
               text_draws[1U]->destination ==
                   battlespades::ui::DrawRect{400.0, 528.5, 0.0, 0.0} &&
               text_draws[1U]->vertical_alignment ==
                   battlespades::ui::VerticalTextAlignment::baseline,
           "VIP text retains the source centered baseline and shadow offset");
    expect(text_draws[0U]->modulation.color ==
               battlespades::ui::ColorRgba8{64U, 64U, 64U, 255U} &&
               text_draws[1U]->modulation.color ==
                   battlespades::ui::ColorRgba8{10U, 20U, 30U, 255U},
           "VIP foreground uses the active StateData team colour");
    expect(text_draws[1U]->preferred_font_asset == "fonts/Edo.ttf" &&
               text_draws[1U]->requested_font_size_pixels == 30.0,
           "VIP label uses retail BIG_TEXT_FONT and BIG_TEXT_FONT_SIZE");
}

} // namespace

int main() {
    try {
        test_minimap_entity_assets_are_exact();
        test_minimap_orientation_matches_world_axes();
        test_new_widgets_reach_the_draw_list();
        test_vip_banner_uses_retail_big_text_independently_from_score();
        test_anchors_are_resolution_independent();
        test_ammo_panel_stays_on_screen();
        test_team_colours();
        test_block_count_colour_threshold();
        test_health_bar_fill_scales_about_its_anchor();
        test_health_number_draw_passes_match_retail();
        test_class_portrait_sits_on_the_bar_left_end();
        test_clock_formatting();
        test_timer_layout_matches_retail_call_sites();
        test_tutorial_timer_reaches_exact_draw_positions();
        test_bottom_left_to_top_left_conversion();
        test_head_count_width_follows_the_retail_quirk();
        test_head_count_uses_retail_score_cells_and_head_offsets();
        test_team_progress_layout_matches_retail_constants();
        test_team_progress_retains_retail_packet_state();
        test_team_progress_reaches_the_exact_draw_positions();
        test_team_progress_particles_match_retail_manager();
        test_head_count_value_type_is_server_owned();
        test_territory_layout_matches_retail_transforms();
        test_territory_state_matches_retail_actions_and_pulse();
        test_territory_widget_reaches_the_draw_list();
        test_damage_indicator_uses_retail_relative_angle();
    } catch (const std::exception& error) {
        std::cerr << "hud layout test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "hud layout tests passed\n";
    return 0;
}
