#include "battlespades/frontend/loading_presentation.hpp"
#include "battlespades/frontend/menu_status.hpp"

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
    if (enabled && glow_on) {
        const double glow_width = bounds.width * 1.2;
        const double glow_height = bounds.height * 1.75;
        list.push(sprite("png/ui/common_elements/buttons/button_large_glow.png",
                         {bounds.x + bounds.width * 0.5 - glow_width * 0.5,
                          bounds.y + bounds.height * 0.5 - glow_height * 0.5,
                          glow_width, glow_height},
                         DrawSpace::design_pixels,
                         TextureAnchor::center,
                         0.6));
    }
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
    list.push(text(label,
                   bounds,
                   36.0,
                   HorizontalTextAlignment::center,
                   button_text_color,
                   "fonts/Spades.ttf",
                   TextTransform::uppercase));
}

/** Retail status wording with map-name formatting; empty once ready. */
[[nodiscard]] std::string retail_status(const MatchLoadingSnapshot& snapshot) {
    const auto& key = snapshot.status_key;
    const auto& map = snapshot.map_name;
    if (snapshot.start_enabled || key == "MAP_READY") {
        return {};
    }
    if (key == "CONNECTING_TO_SERVER") {
        return "Connecting to server...";
    }
    if (key == "RECEIVING_SERVER_PACKS") {
        return "Receiving server packs...";
    }
    if (key == "CHECKING_MAP") {
        return "Checking map " + map + "...";
    }
    if (key == "LOADING_MAP") {
        return "Loading map " + map + "...";
    }
    if (key == "RECEIVING_MAP") {
        return "Receiving map " + map + "...";
    }
    if (key == "SYNCING_MAP") {
        return "Syncing map " + map + "...";
    }
    if (key == "INITIALISING_MAP") {
        return "Initializing map " + map + "...";
    }
    if (key == "ERROR_TIMEOUT") return "The server stopped responding. Go back to retry.";
    if (key == "LOAD_FAILED" || key == "ASSET_PRELOAD_FAILED")
        return "Unable to load this match. Go back to retry.";
    return std::string{key};
}

void append_scores(ui::DrawList& list, const MatchLoadingSnapshot& snapshot) {
    using namespace loading_layout;
    list.push(sprite("png/high/white.png", content, DrawSpace::design_pixels,
        TextureAnchor::top_left, 1.0, color({0U, 0U, 0U, 125U})));
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
            constexpr double cap{8.0};
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
    // Retail draws ugc_splash (1280x960 @0.6 = 768x576) on the letterboxed
    // 800x600 canvas underneath every menu, centered.
    list.push(sprite(loading_screen_assets::splash,
                     {16.0, 12.0, 768.0, 576.0},
                     DrawSpace::design_pixels,
                     TextureAnchor::top_left,
                     0.6,
                     color(white, context.background_opacity_per_mille)));
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
                   {200.0, 14.0, 400.0, 52.0},
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
        constexpr std::array<DrawRect, 3U> captions{{
            {110.0, 373.0, 168.0, 34.0}, {320.0, 380.0, 168.0, 34.0},
            {538.0, 374.0, 168.0, 38.0}}};
        for (std::size_t index{}; index < captions.size(); ++index) {
            auto caption = text(snapshot.infographic_captions[index], captions[index], 16.0,
                HorizontalTextAlignment::center, white, "fonts/Spades.ttf", TextTransform::uppercase);
            caption.layout = ui::TextLayout::bounded_wrapped_lines;
            caption.maximum_lines = 2U;
            list.push(std::move(caption));
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
        list.push(text(tab_key(snapshot.tabs[index]),
                       bounds,
                       16.0,
                       HorizontalTextAlignment::center,
                       selected ? gold : cream,
                       "fonts/Edo.ttf"));
    }

    if (snapshot.tabs[snapshot.selected_tab] == LoadingTab::map) {
        // Training displays the recovered TUTORIAL_MODE_TITLE, white Spades
        // 48, top-left aligned inside box {83,163,360,60}.
        const std::string_view display_title =
            snapshot.map_name == "Training" ? std::string_view{"Tutorial"}
                                            : std::string_view{snapshot.map_name};
        list.push(ui::TextDrawCommand{
            std::string{display_title},
            "fonts/Spades.ttf",
            {83.0, 163.0, 360.0, 60.0},
            DrawSpace::design_pixels,
            48.0,
            1.0,
            1U,
            HorizontalTextAlignment::left,
            VerticalTextAlignment::top,
            TextTransform::preserve,
            TextFit::shrink_to_fit,
            color(white)});
    }
    append_menu_status(list, {220.0, 541.0, 516.0, 33.0}, {}, retail_status(snapshot),
        snapshot.state == MatchLoadingState::failed || snapshot.state == MatchLoadingState::timed_out
            ? ColorRgba8{220U, 112U, 81U, 255U} : gold);
    // Retail large navbar BACK affordance, bottom-left strip.
    list.push(text("BACK",
                   loading_layout::back,
                   20.0,
                   HorizontalTextAlignment::left,
                   cream,
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
