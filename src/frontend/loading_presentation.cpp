#include "battlespades/frontend/loading_presentation.hpp"
#include "battlespades/frontend/menu_status.hpp"
#include "battlespades/frontend/settings_menu.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
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

[[nodiscard]] ColorModulation color(ColorRgba8 value = white,
                                    std::uint16_t opacity = 1'000U) noexcept {
    return {value, 1'000U, opacity};
}

[[nodiscard]] SpriteDrawCommand sprite(std::string_view asset,
                                       DrawRect destination,
                                       DrawSpace space = DrawSpace::design_pixels,
                                       TextureAnchor anchor = TextureAnchor::top_left,
                                       double scale = 0.6,
                                       ColorModulation modulation = color(),
                                       SpriteSizing sizing = SpriteSizing::stretch,
                                       TextureFilter filter = TextureFilter::linear) {
    return {std::string{asset}, destination, space, filter, anchor, scale, sizing, modulation};
}

[[nodiscard]] TextDrawCommand
text(std::string_view key,
     DrawRect destination,
     double size,
     HorizontalTextAlignment alignment = HorizontalTextAlignment::left,
     ColorRgba8 text_color = cream,
     std::string_view font = "fonts/A750-Sans-Medium.ttf",
     TextTransform transform = TextTransform::preserve) {
    return {std::string{key},
            std::string{font},
            destination,
            DrawSpace::design_pixels,
            size,
            1.0,
            2U,
            alignment,
            VerticalTextAlignment::retail_center,
            transform,
            TextFit::shrink_to_fit,
            color(text_color)};
}

[[nodiscard]] DrawRect cover(ui::PixelExtent window) noexcept {
    constexpr double source_width{768.0};
    constexpr double source_height{576.0};
    const auto width = static_cast<double>(window.width);
    const auto height = static_cast<double>(window.height);
    const auto factor = std::max(width / source_width, height / source_height);
    const auto drawn_width = source_width * factor;
    const auto drawn_height = source_height * factor;
    return {(width - drawn_width) * 0.5, (height - drawn_height) * 0.5, drawn_width, drawn_height};
}

void validate(const LoadingPresentationContext& context) {
    if (context.window.width <= 0 || context.window.height <= 0 ||
        context.background_opacity_per_mille > 1'000U) {
        throw std::invalid_argument{"invalid loading presentation context"};
    }
}

[[nodiscard]] std::string_view tab_key(LoadingTab tab) noexcept {
    switch (tab) {
    case LoadingTab::map:
        return "MAP";
    case LoadingTab::mode:
        return "MODE";
    case LoadingTab::scores:
        return "SCORES";
    }
    return "MAP";
}

/**
 * Retail TextButton: normal art when waiting, ready art with a pulsing glow
 * overlay when enabled; disabled state tints only the three images, never
 * the dark Spades label.
 */
void append_text_button(ui::DrawList& list, DrawRect bounds, std::string_view label,
                        bool enabled, bool glow_on) {
    constexpr ColorRgba8 button_text_color{20U, 20U, 20U, 255U};
    const std::string_view base = enabled ? "png/ui/common_elements/buttons/button_large_ready_"
                                          : "png/ui/common_elements/buttons/button_large_";
    const auto intensity = enabled ? std::uint16_t{1'000U} : std::uint16_t{700U};
    // Recovered cap width: 60/97 of the button height plus one pixel.
    const double cap = 60.0 / 97.0 * bounds.height + 1.0;
    list.push(sprite(std::string{base} + "left.png",
                     {bounds.x, bounds.y, cap, bounds.height},
                     DrawSpace::design_pixels,
                     TextureAnchor::top_left,
                     0.6,
                     ColorModulation{white, intensity, 1'000U}));
    list.push(sprite(std::string{base} + "mid.png",
                     {bounds.x + cap, bounds.y, bounds.width - cap * 2.0, bounds.height},
                     DrawSpace::design_pixels,
                     TextureAnchor::top_left,
                     0.6,
                     ColorModulation{white, intensity, 1'000U}));
    list.push(sprite(std::string{base} + "right.png",
                     {bounds.x + bounds.width - cap, bounds.y, cap, bounds.height},
                     DrawSpace::design_pixels,
                     TextureAnchor::top_left,
                     0.6,
                     ColorModulation{white, intensity, 1'000U}));
    if (enabled && glow_on) {
        // The glow washes OVER the button art (retail's rivets fade while it
        // is lit) and its bright ring lands on the button edges: the ring
        // peaks sit 10.6% / 30.8% into the 400x130 texture, so the texture
        // spans 1.25x the width and 2.6x the height (measured in retail).
        const double glow_width = bounds.width * 1.25;
        const double glow_height = bounds.height * 2.6;
        list.push(sprite("png/ui/common_elements/buttons/button_large_glow.png",
                         {bounds.x + bounds.width * 0.5 - glow_width * 0.5,
                          bounds.y + bounds.height * 0.5 - glow_height * 0.5,
                          glow_width, glow_height},
                         DrawSpace::design_pixels,
                         TextureAnchor::center,
                         0.6));
    }
    list.push(text(label,
                   bounds,
                   36.0,
                   HorizontalTextAlignment::center,
                   button_text_color,
                   "fonts/Spades.ttf",
                   TextTransform::uppercase));
}

void append_custom_rules(ui::DrawList& list, const MatchLoadingSnapshot& snapshot) {
    using namespace loading_layout;
    if (snapshot.custom_rules.empty()) return;
    // ExpandableListPanel with a header: black 150 alpha box, the
    // CUSTOM_GAME_RULES heading, then category rows (dark green) and rule rows
    // in two columns of 240 and 60 px. It grows with its rows up to 200 px.
    const auto rows = std::min<std::size_t>(
        snapshot.custom_rules.size(),
        static_cast<std::size_t>(custom_rules.height / custom_rule_row_height) - 1U);
    const auto height = custom_rule_row_height * static_cast<double>(rows + 1U);
    list.push(sprite("png/high/white.png", {custom_rules.x, custom_rules.y, custom_rules.width, height},
        DrawSpace::design_pixels, TextureAnchor::top_left, 1.0, color({0U, 0U, 0U, 150U})));
    list.push(text("CUSTOM_GAME_RULES",
        {custom_rules.x, custom_rules.y, custom_rules.width, custom_rule_row_height}, 14.0,
        HorizontalTextAlignment::center, cream, "fonts/Edo.ttf", TextTransform::uppercase));
    for (std::size_t index{}; index < rows; ++index) {
        const auto& row = snapshot.custom_rules[index];
        const DrawRect bounds{custom_rules.x,
            custom_rules.y + custom_rule_row_height * static_cast<double>(index + 1U),
            custom_rules.width, custom_rule_row_height};
        if (row.category) {
            list.push(sprite("png/high/white.png", bounds, DrawSpace::design_pixels,
                TextureAnchor::top_left, 1.0, color({59U, 68U, 25U, 255U})));
            list.push(text(row.label_key, {bounds.x + 8.0, bounds.y, bounds.width - 16.0, bounds.height},
                12.0, HorizontalTextAlignment::left, cream, "fonts/Edo.ttf",
                row.uppercase ? TextTransform::uppercase : TextTransform::preserve));
            continue;
        }
        list.push(sprite("png/high/white.png", bounds, DrawSpace::design_pixels,
            TextureAnchor::top_left, 1.0, color(index % 2U == 0U
                ? ColorRgba8{87U, 83U, 74U, 150U} : ColorRgba8{54U, 51U, 44U, 150U})));
        list.push(text(row.label_key, {bounds.x + 4.0, bounds.y, 236.0, bounds.height}, 11.0));
        list.push(text(row.value, {bounds.x + 244.0, bounds.y, 56.0, bounds.height}, 11.0));
    }
}

} // namespace

std::string loading_status_text(const MatchLoadingSnapshot& snapshot,
                                const std::function<std::string(std::string_view)>& localize) {
    const auto& key = snapshot.status_key;
    if (snapshot.start_enabled || key == "MAP_READY") {
        return {};
    }
    // english.py: CONNECTING_TO_SERVER, RECEIVING_SERVER_PACKS ("Connected,
    // receiving server packs..."), the five map stages with the map name as
    // {0}, and ERROR_TIMEOUT when nothing arrived for 30 seconds.
    constexpr std::array<std::string_view, 9U> catalogue{
        "CONNECTING_TO_SERVER", "RECEIVING_SERVER_PACKS", "CHECKING_MAP", "LOADING_MAP",
        "RECEIVING_MAP", "SYNCING_MAP", "INITIALISING_MAP", "ERROR_TIMEOUT",
        "ASSET_PRELOAD_FAILED"};
    if (std::ranges::find(catalogue, std::string_view{key}) == catalogue.end()) {
        // A host stage or server reason the caller already worded.
        return key == "LOAD_FAILED" ? std::string{"SERVER_CONNECTION_FAILED"} : key;
    }
    if (!localize) return key;
    auto value = localize(key);
    if (const auto slot = value.find("{0}"); slot != std::string::npos) {
        value.replace(slot, 3U, snapshot.map_name);
    }
    return std::string{literal_text_prefix} + value;
}

namespace {

void append_scores(ui::DrawList& list, const MatchLoadingSnapshot& snapshot) {
    using namespace loading_layout;
    // No dimming box: retail shows the map art at full brightness around
    // and above the rows (a black 125-alpha box halved it).
    const auto count = snapshot.score_rows.size();
    const auto maximum = count > MatchLoadingModel::visible_score_rows
        ? count - MatchLoadingModel::visible_score_rows : 0U;
    const auto first = std::min(snapshot.score_scroll, maximum);
    const auto end = std::min(count, first + MatchLoadingModel::visible_score_rows);
    for (auto index = first; index < end; ++index) {
        const auto& row = snapshot.score_rows[index];
        const DrawRect bounds{score_rows.x,
            score_rows.y + static_cast<double>(index - first) * score_row_height,
            score_rows.width, score_row_height};
        if (row.section) {
            // CategoryListItem.draw_background: red_header_left/right keep
            // their full 40 px source width (the torn brush ends), only the
            // centre stretches.
            constexpr double cap{40.0};
            list.push(sprite("png/ui/common_elements/header/red_header_left.png",
                {bounds.x, bounds.y, cap, bounds.height}));
            list.push(sprite("png/ui/common_elements/header/red_header_center.png",
                {bounds.x + cap, bounds.y, bounds.width - cap * 2.0, bounds.height}));
            list.push(sprite("png/ui/common_elements/header/red_header_right.png",
                {bounds.x + bounds.width - cap, bounds.y, cap, bounds.height}));
            list.push(text(row.label_key, {bounds.x + 13.0, bounds.y, bounds.width - 53.0, bounds.height},
                16.0, HorizontalTextAlignment::left, cream, "fonts/Edo.ttf", TextTransform::uppercase));
            const DrawRect button{bounds.x + bounds.width - 31.0, bounds.y + 4.0, 19.0, 19.0};
            list.push(sprite("png/ui/common_elements/buttons/button_square.png", button));
            list.push(sprite(row.expanded ? "png/ui/common_elements/collapse_minus.png"
                                         : "png/ui/common_elements/collapse_plus.png", button));
        } else {
            list.push(sprite("png/high/white.png", bounds, DrawSpace::design_pixels,
                TextureAnchor::top_left, 1.0, color(index % 2U == 0U
                    ? ColorRgba8{87U, 83U, 74U, 150U} : ColorRgba8{54U, 51U, 44U, 150U})));
            list.push(text(row.label_key, {bounds.x + 13.0, bounds.y, 322.0, bounds.height}, 12.0));
            list.push(text(row.value, {bounds.x + 343.0, bounds.y, bounds.width - 353.0, bounds.height}, 12.0));
        }
    }
    list.push(sprite("png/high/white.png", score_track, DrawSpace::design_pixels,
        TextureAnchor::top_left, 1.0, color({74U, 67U, 4U, 255U})));
    const auto arrow = [&](DrawRect bounds, std::string_view image, bool enabled) {
        const auto tint = enabled ? color() : ColorModulation{white, 600U, 1'000U};
        list.push(sprite("png/ui/common_elements/buttons/button_square.png", bounds,
            DrawSpace::design_pixels, TextureAnchor::top_left, 1.0, tint));
        list.push(sprite(image, bounds, DrawSpace::design_pixels, TextureAnchor::top_left, 1.0, tint));
    };
    arrow(score_up, "png/ui/common_elements/scroll_bar/scroll_bar_arrow_up.png", first > 0U);
    arrow(score_down, "png/ui/common_elements/scroll_bar/scroll_bar_arrow_down.png", first < maximum);
    const auto thumb = score_thumb(snapshot);
    list.push(sprite("png/ui/common_elements/scroll_bar/scroll_bar_top.png",
        {thumb.x, thumb.y, thumb.width, 16.0}));
    if (thumb.height > 32.0) list.push(sprite("png/ui/common_elements/scroll_bar/scroll_bar_mid.png",
        {thumb.x, thumb.y + 16.0, thumb.width, thumb.height - 32.0}));
    list.push(sprite("png/ui/common_elements/scroll_bar/scroll_bar_bottom.png",
        {thumb.x, thumb.y + thumb.height - 16.0, thumb.width, 16.0}));
}

} // namespace

bool MatchLoadingModel::handle_score_click(double x, double y) {
    using namespace loading_layout;
    if (tabs_[selected_tab_] != LoadingTab::scores) return false;
    if (contains(score_up, x, y)) { (void)scroll_scores(-1); return true; }
    if (contains(score_down, x, y)) { (void)scroll_scores(1); return true; }
    if (contains(score_track, x, y)) {
        const auto thumb = score_thumb(snapshot());
        if (y < thumb.y) (void)scroll_scores(-static_cast<int>(visible_score_rows));
        else if (y >= thumb.y + thumb.height) (void)scroll_scores(static_cast<int>(visible_score_rows));
        return true;
    }
    if (!contains(score_rows, x, y)) return false;
    const auto rows = snapshot().score_rows;
    const auto index = score_scroll_ + static_cast<std::size_t>((y - score_rows.y) / score_row_height);
    if (index >= rows.size() || !rows[index].section) return false;
    const auto group = *rows[index].section;
    score_expanded_[group] = !score_expanded_[group];
    tab_cycle_interrupted_ = true;
    (void)scroll_scores(0); // Collapsing the last page must clamp to the new content length.
    return true;
}

ui::DrawList BootLoadingPresentation::build(const BootLoadingSnapshot& snapshot,
                                            const LoadingPresentationContext& context) const {
    validate(context);
    if (snapshot.progress < 0.0 || snapshot.progress > 1.0 ||
        snapshot.filled_bullets > BootLoadingSnapshot::bullet_count) {
        throw std::invalid_argument{"invalid boot loading snapshot"};
    }
    ui::DrawList list;
    list.reserve(2U + BootLoadingSnapshot::bullet_count * 2U);
    list.push(sprite(loading_screen_assets::splash,
                     cover(context.window),
                     DrawSpace::window_pixels,
                     TextureAnchor::top_left,
                     1.0,
                     color(white, context.background_opacity_per_mille),
                     SpriteSizing::cover));
    list.push(sprite(loading_screen_assets::boot_title,
                     {0.0, 0.0, 800.0, 600.0},
                     DrawSpace::design_pixels,
                     TextureAnchor::top_left,
                     1.0));
    constexpr double x{197.5};
    constexpr double y{455.9375};
    constexpr double width{11.25};
    constexpr double height{40.0};
    for (std::size_t bullet = 0U; bullet < BootLoadingSnapshot::bullet_count; ++bullet) {
        list.push(sprite(loading_screen_assets::boot_bullet_dark,
                         {x + width * bullet, y, width, height},
                         DrawSpace::design_pixels,
                         TextureAnchor::top_left,
                         1.0));
    }
    for (std::size_t bullet = 0U; bullet < snapshot.filled_bullets; ++bullet) {
        list.push(sprite(loading_screen_assets::boot_bullet,
                         {x + width * bullet, y, width, height},
                         DrawSpace::design_pixels,
                         TextureAnchor::top_left,
                         1.0));
    }
    return list;
}

ui::DrawList MatchLoadingPresentation::build(const MatchLoadingSnapshot& snapshot,
                                             const LoadingPresentationContext& context) const {
    validate(context);
    if (snapshot.tabs.empty() || snapshot.selected_tab >= snapshot.tabs.size() ||
        snapshot.overall_progress < 0.0 || snapshot.overall_progress > 1.0) {
        throw std::invalid_argument{"invalid match loading snapshot"};
    }
    ui::DrawList list;
    list.reserve(40U);
    // MenuScene.draw blits its background splash scaled over the whole window
    // (calculate_scale_on_window_resize), never letterboxed, with
    // background_alpha. Once the map is shown that alpha falls to zero and
    // the live world drawn beneath shows through.
    if (context.background_opacity_per_mille > 0U) {
        list.push(sprite(loading_screen_assets::splash,
                         cover(context.window),
                         DrawSpace::window_pixels,
                         TextureAnchor::top_left,
                         1.0,
                         color(white, context.background_opacity_per_mille),
                         SpriteSizing::cover));
    }
    // ui_frame_large 1172x921 @0.64 centered at (400,300).
    list.push(sprite(loading_screen_assets::match_frame,
                     {24.96, 5.28, 750.08, 589.44},
                     DrawSpace::design_pixels,
                     TextureAnchor::center,
                     0.64));
    // game_loading_bar_bg 661x91 @0.64 bottom-left anchored at (60,95) BO.
    list.push(sprite(loading_screen_assets::loading_bar,
                     {60.0, 446.76, 423.04, 58.24},
                     DrawSpace::design_pixels,
                     TextureAnchor::top_left,
                     0.64));
    // Title: Spades 46 centered on x=400 with the baseline near TO 60.
    list.push(text("LOADING",
                   {202.0, 20.0, 400.0, 52.0},
                   46.0,
                   HorizontalTextAlignment::center,
                   cream,
                   "fonts/Spades.ttf",
                   TextTransform::uppercase));
    // game_loading_tab_bg and the map image share 1064x475 @0.64 centered at
    // (400,313) BO.
    list.push(sprite(loading_screen_assets::loading_tab_background,
                     loading_layout::content,
                     DrawSpace::design_pixels,
                     TextureAnchor::center,
                     0.64));
    if (!snapshot.textures.map_image_asset.empty()) {
        list.push(sprite(snapshot.textures.map_image_asset,
                         loading_layout::content,
                         DrawSpace::design_pixels,
                         TextureAnchor::center,
                         0.64,
                         color(),
                         SpriteSizing::stretch,
                         TextureFilter::linear));
    }
    if (snapshot.tabs[snapshot.selected_tab] == LoadingTab::mode &&
        !snapshot.textures.infographic_asset.empty()) {
        // The source is 1024x475. Keep its aspect ratio and every caption
        // plate inside the content frame, above the progress/start band.
        list.push(sprite(snapshot.textures.infographic_asset,
                         {75.0, 137.5, 650.0, 301.513671875},
                         DrawSpace::design_pixels,
                         TextureAnchor::center,
                         0.64,
                         color(),
                         SpriteSizing::stretch,
                         TextureFilter::linear));
        // get_resized_font_and_formatted_text_to_fit_boundaries(text, 180, 35,
        // map_tagline_font (Spades 20), 2) wraps and shrinks one pixel at a
        // time until the lines fit 35 px, so stock captions end up on one
        // smaller line. draw_text_lines then centres them in the retail boxes
        // (105,193,177,40), (315,186,177,39), (536,187,177,47) bottom-origin.
        // destination.y is the first baseline: box centre plus half a line.
        constexpr std::array<DrawRect, 3U> captions{{
            {105.0, 392.0, 177.0, 35.0}, {315.0, 399.5, 177.0, 35.0},
            {536.0, 394.5, 177.0, 35.0}}};
        for (std::size_t index{}; index < captions.size(); ++index) {
            auto caption = text(snapshot.infographic_captions[index], captions[index], 20.0,
                HorizontalTextAlignment::center, white, "fonts/Spades.ttf", TextTransform::uppercase);
            caption.vertical_alignment = VerticalTextAlignment::baseline;
            caption.fit = TextFit::retail_width_scale;
            caption.line_spacing_pixels = 2.0;
            caption.maximum_lines = 0U;
            caption.layout = ui::TextLayout::retail_wrapped_lines;
            list.push(std::move(caption));
        }
        if (!snapshot.mode_title_key.empty()) {
            // mode_text: Label(x=240, y=436, 320x50, centred, Spades 38,
            // white) drawn with black drop shadows at 2 and 3 px, then
            // draw_stroked(True, 2, black): outline under the white fill.
            // Measured against retail: the glyphs sit 9 px lower than a
            // retail_center of the (240, 436) bottom-origin label box.
            const DrawRect title{240.0, 148.0, 320.0, 50.0};
            const auto mode_title = [&](DrawRect bounds, ColorRgba8 tint, bool outline) {
                auto command = text(snapshot.mode_title_key, bounds, 38.0,
                    HorizontalTextAlignment::center, tint, "fonts/Spades.ttf",
                    TextTransform::uppercase);
                command.fit = TextFit::retail_width_scale;
                command.retail_outline_stroke = outline;
                return command;
            };
            constexpr ColorRgba8 black{0U, 0U, 0U, 255U};
            list.push(mode_title({title.x + 2.0, title.y + 2.0, title.width, title.height},
                                 black, false));
            list.push(mode_title({title.x + 3.0, title.y + 3.0, title.width, title.height},
                                 black, false));
            list.push(mode_title(title, black, true));
            list.push(mode_title(title, white, false));
        }
    }
    if (snapshot.tabs[snapshot.selected_tab] == LoadingTab::scores) append_scores(list, snapshot);

    // Resolve retail's texture anchor exactly once. Hit targets use these
    // same bounds; the Map tab must not escape the content frame's left edge.
    for (std::size_t index = 0U; index < snapshot.tabs.size(); ++index) {
        const auto bounds = loading_layout::tab(index);
        const auto selected = index == snapshot.selected_tab;
        list.push(sprite(selected ? "png/ui/settings/tab_frames/generic_tab_active.png"
                                  : "png/ui/settings/tab_frames/generic_tab_inactive.png",
                         bounds,
                         DrawSpace::design_pixels,
                         TextureAnchor::center));
        // Retail's tab labels sit 5.5 px left of and 3.5 px above the
        // frame centre (measured on every loader tab against retail).
        list.push(text(tab_key(snapshot.tabs[index]),
                       {bounds.x - 5.5, bounds.y - 3.5, bounds.width, bounds.height},
                       16.0,
                       HorizontalTextAlignment::center,
                       selected ? gold : cream,
                       "fonts/Edo.ttf"));
    }

    if (snapshot.tabs[snapshot.selected_tab] == LoadingTab::map) {
        // Training displays the recovered TUTORIAL_MODE_TITLE, white Spades
        // 48 (38 when a tagline follows), top-left aligned in {83,163,360,60}.
        const auto display_title = snapshot.map_name == "Training"
                                       ? std::string{"TUTORIAL_MODE_TITLE"}
                                       : snapshot.map_name;
        // draw_text_with_alignment_and_size_validation(..., 'left', 'top',
        // shadowed=True, stroked=True, shadow_offset=3, stroke_size=2): a
        // black drop shadow 3 px down-right, a black outline, the white fill.
        const auto map_title = [&](DrawRect bounds, ColorRgba8 tint, bool outline) {
            ui::TextDrawCommand command{
                display_title,
                "fonts/Spades.ttf",
                bounds,
                DrawSpace::design_pixels,
                snapshot.map_tagline_key.empty() ? 48.0 : 38.0,
                1.0,
                1U,
                HorizontalTextAlignment::left,
                VerticalTextAlignment::top,
                TextTransform::preserve,
                TextFit::shrink_to_fit,
                color(tint)};
            command.retail_outline_stroke = outline;
            return command;
        };
        constexpr ColorRgba8 title_shadow{0U, 0U, 0U, 255U};
        list.push(map_title({86.0, 166.0, 360.0, 60.0}, title_shadow, false));
        list.push(map_title({83.0, 163.0, 360.0, 60.0}, title_shadow, true));
        list.push(map_title({83.0, 163.0, 360.0, 60.0}, white, false));
        if (!snapshot.map_tagline_key.empty()) {
            // map_tagline_text: small_title_aldo_font (Spades 20) at (83,372).
            list.push(ui::TextDrawCommand{
                snapshot.map_tagline_key,
                "fonts/Spades.ttf",
                {83.0, 198.0, 320.0, 30.0},
                DrawSpace::design_pixels,
                20.0,
                1.0,
                1U,
                HorizontalTextAlignment::left,
                VerticalTextAlignment::top,
                TextTransform::preserve,
                TextFit::shrink_to_fit,
                color(white)});
        }
        if (!snapshot.map_preview_asset.empty()) {
            // draw_map_tab: loading_map_frame at 258x258, the preview at 238.
            list.push(sprite("png/ui/game_loading/minimap_bg.png", loading_layout::map_preview_frame,
                             DrawSpace::design_pixels, TextureAnchor::top_left, 0.64));
            auto preview = sprite(snapshot.map_preview_asset, loading_layout::map_preview,
                                  DrawSpace::design_pixels, TextureAnchor::top_left, 0.64);
            // images.load rotates every stock map_previews texture 90 degrees
            // clockwise (tex_coords[3:] + tex_coords[:3]); a Map Creator png
            // (create_ugc_preview_image) is drawn as authored.
            if (snapshot.map_preview_asset.find("game_loading/map_previews/") !=
                std::string::npos) {
                preview.rotation_degrees = 90.0;
            }
            list.push(std::move(preview));
        }
        append_custom_rules(list, snapshot);
    }
    append_menu_status(list, {220.0, 541.0, 516.0, 33.0}, {},
        loading_status_text(snapshot, context.localize),
        snapshot.state == MatchLoadingState::failed || snapshot.state == MatchLoadingState::timed_out
            ? ColorRgba8{220U, 112U, 81U, 255U} : gold);
    // Retail create_large_navbar(): back_icon arrow plus the yellow BACK
    // label, the same affordance SelectTeam/SelectClass draw.
    // NavigationBar.draw_item (not hovered): glColor 0.7 on the icon, the
    // label in MENU_FONT_COLOR2*0.7, navigation_font = Spades 24, the icon at
    // x+PAD/2 and the label PAD/2 after it (NavigationBar(54,27,695,32)).
    list.push(sprite("png/ui/common_elements/nav_bar/back_icon.png",
                     {56.5, 543.0, 26.0, 26.0}, DrawSpace::design_pixels,
                     TextureAnchor::top_left, 0.64,
                     // The arrow takes the label's glColor (MENU_FONT_COLOR2
                     // * 0.7): retail measures (149,115,16) = back_icon
                     // (235,204,77) * (162,145,55) / 255.
                     ColorModulation{{232U, 207U, 78U, 255U}, 700U, 1'000U}));
    list.push(text("BACK",
                   {84.0, 540.0, 110.0, 34.0},
                   24.0,
                   HorizontalTextAlignment::left,
                   {162U, 145U, 55U, 255U},
                   "fonts/Spades.ttf",
                   TextTransform::uppercase));
    append_text_button(list, loading_layout::start, "START", snapshot.start_enabled,
                       context.start_glow);
    // Progress bar draws last, over everything: the retail screenshot shows
    // a row of bullet sprites inside {66,451,414,50} — gold for the filled
    // fraction, darkened silhouettes for the remainder.
    constexpr int bullet_count{33};
    constexpr double bullet_width{11.2};
    constexpr double bullet_height{43.4};
    constexpr double bullet_stride{414.0 / bullet_count};
    const int filled =
        static_cast<int>(snapshot.overall_progress * bullet_count + 0.5);
    for (int bullet = 0; bullet < bullet_count; ++bullet) {
        const bool lit = bullet < filled;
        list.push(sprite(loading_screen_assets::loading_bullet,
                         {66.0 + bullet_stride * bullet +
                              (bullet_stride - bullet_width) * 0.5,
                          451.0 + (50.0 - bullet_height) * 0.5, bullet_width,
                          bullet_height},
                         DrawSpace::design_pixels,
                         TextureAnchor::top_left,
                         0.7,
                         lit ? color() : ColorModulation{white, 300U, 1'000U}));
    }
    return list;
}

} // namespace battlespades::frontend
