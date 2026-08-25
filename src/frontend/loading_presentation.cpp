#include "battlespades/frontend/loading_presentation.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
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
    return std::string{key};
}

} // namespace

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
                     {59.52, 135.0, 680.96, 304.0},
                     DrawSpace::design_pixels,
                     TextureAnchor::center,
                     0.64));
    if (!snapshot.textures.map_image_asset.empty()) {
        list.push(sprite(snapshot.textures.map_image_asset,
                         {59.52, 135.0, 680.96, 304.0},
                         DrawSpace::design_pixels,
                         TextureAnchor::center,
                         0.64,
                         color(),
                         SpriteSizing::stretch,
                         TextureFilter::linear));
    }
    if (snapshot.tabs[snapshot.selected_tab] == LoadingTab::mode &&
        !snapshot.textures.infographic_asset.empty()) {
        // Retail resizes the loaded 1024x530 infographic to 650x305 keeping
        // its original center anchor.
        list.push(sprite(snapshot.textures.infographic_asset,
                         {72.3, 151.6, 650.0, 305.0},
                         DrawSpace::design_pixels,
                         TextureAnchor::center,
                         0.64,
                         color(),
                         SpriteSizing::stretch,
                         TextureFilter::linear));
        list.push(text(snapshot.mode_key,
                       {80.0, 139.0, 320.0, 50.0},
                       38.0,
                       HorizontalTextAlignment::center,
                       white,
                       "fonts/Spades.ttf",
                       TextTransform::uppercase));
    }

    // Tab frames 224x42, stride 224 + UI_CONTROL_SPACING(4); the Edo 16pt
    // label centers 48px right of the frame center.
    constexpr double tab_width{224.0};
    for (std::size_t index = 0U; index < snapshot.tabs.size(); ++index) {
        const auto x = 7.0 + 228.0 * static_cast<double>(index);
        const auto selected = index == snapshot.selected_tab;
        list.push(sprite(selected ? "png/ui/settings/tab_frames/generic_tab_active.png"
                                  : "png/ui/settings/tab_frames/generic_tab_inactive.png",
                         {x, 99.0, tab_width, 42.0},
                         DrawSpace::design_pixels,
                         TextureAnchor::center));
        list.push(text(tab_key(snapshot.tabs[index]),
                       {x + 48.0, 99.0, tab_width, 42.0},
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
    // Status: Spades 24 centered in the recovered {405..720} band.
    list.push(text(retail_status(snapshot),
                   {405.0, 550.0, 315.0, 30.0},
                   24.0,
                   HorizontalTextAlignment::center,
                   cream,
                   "fonts/Spades.ttf"));
    // Retail large navbar BACK affordance, bottom-left strip.
    list.push(text("BACK",
                   {54.0, 541.0, 135.0, 32.0},
                   20.0,
                   HorizontalTextAlignment::left,
                   cream,
                   "fonts/Spades.ttf",
                   TextTransform::uppercase));
    append_text_button(list, {492.0, 449.0, 246.0, 58.0}, "START", snapshot.start_enabled,
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
