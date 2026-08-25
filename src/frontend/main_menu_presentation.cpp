#include "battlespades/frontend/main_menu_presentation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string_view>

namespace battlespades::frontend {
namespace {

using ui::ColorModulation;
using ui::ColorRgba8;
using ui::DrawRect;
using ui::DrawSpace;
using ui::HorizontalTextAlignment;
using ui::PlayerNamePlateDrawRequest;
using ui::SpriteDrawCommand;
using ui::SpriteSizing;
using ui::TextDrawCommand;
using ui::TextFit;
using ui::TextTransform;
using ui::TextureFilter;
using ui::VerticalTextAlignment;
using UiTextureAnchor = ui::TextureAnchor;

constexpr double source_scale{0.6};
constexpr double square_source_scale{1.0};
constexpr double navigation_source_scale{0.64};
constexpr double design_height{600.0};
constexpr double subpixels_per_pixel{static_cast<double>(MainMenuModel::subpixels_per_pixel)};

constexpr ColorRgba8 white{255U, 255U, 255U, 255U};
constexpr ColorRgba8 square_disabled{86U, 86U, 86U, 255U};
constexpr ColorRgba8 button_text_color{20U, 20U, 20U, 255U};
constexpr ColorRgba8 navigation_text_color{232U, 207U, 78U, 255U};

constexpr std::array<std::array<std::string_view, 3U>, 3U> text_button_assets{{
    {
        "png/ui/common_elements/buttons/button_large_left.png",
        "png/ui/common_elements/buttons/button_large_mid.png",
        "png/ui/common_elements/buttons/button_large_right.png",
    },
    {
        "png/ui/common_elements/buttons/button_large_hover_left.png",
        "png/ui/common_elements/buttons/button_large_hover_mid.png",
        "png/ui/common_elements/buttons/button_large_hover_right.png",
    },
    {
        "png/ui/common_elements/buttons/button_large_press_left.png",
        "png/ui/common_elements/buttons/button_large_press_mid.png",
        "png/ui/common_elements/buttons/button_large_press_right.png",
    },
}};

constexpr std::array<std::string_view, 3U> square_button_assets{
    "png/ui/common_elements/buttons/mm_button_square_default.png",
    "png/ui/common_elements/buttons/mm_button_square_hover.png",
    "png/ui/common_elements/buttons/mm_button_square_press.png",
};

[[nodiscard]] DrawRect to_design_rect(const ui::Rect& rect) noexcept {
    return DrawRect{
        static_cast<double>(rect.x) / subpixels_per_pixel,
        static_cast<double>(rect.y) / subpixels_per_pixel,
        static_cast<double>(rect.width) / subpixels_per_pixel,
        static_cast<double>(rect.height) / subpixels_per_pixel,
    };
}

[[nodiscard]] DrawRect
retail_bottom_left_rect(double x, double y, double width, double height) noexcept {
    return DrawRect{x, design_height - y - height, width, height};
}

[[nodiscard]] ColorModulation full_color(ColorRgba8 color = white) noexcept {
    return ColorModulation{color, 1'000U, 1'000U};
}

[[nodiscard]] SpriteDrawCommand sprite(std::string_view asset,
                                       DrawRect destination,
                                       DrawSpace space,
                                       TextureFilter sampling,
                                       UiTextureAnchor retail_anchor,
                                       double retail_scale,
                                       ColorModulation modulation = full_color(),
                                       SpriteSizing sizing = SpriteSizing::stretch) {
    return SpriteDrawCommand{
        std::string{asset},
        destination,
        space,
        sampling,
        retail_anchor,
        retail_scale,
        sizing,
        modulation,
    };
}

[[nodiscard]] std::size_t texture_state_index(WidgetVisualState state) noexcept {
    switch (state) {
    case WidgetVisualState::hovered:
        return 1U;
    case WidgetVisualState::pressed:
        return 2U;
    case WidgetVisualState::normal:
    case WidgetVisualState::focused:
    case WidgetVisualState::disabled:
        return 0U;
    }
    return 0U;
}

void append_square_button(ui::DrawList& list,
                          const MainMenuModel& menu,
                          const MainMenuControl& control) {
    const auto state = menu.visual_state(control.widget.id);
    auto destination = to_design_rect(control.widget.bounds);

    // Retail uses floor(-4 * 63.75 / 97) = -3 in bottom-left coordinates.
    if (state == WidgetVisualState::pressed) {
        destination.y += 3.0;
    }

    const auto modulation =
        state == WidgetVisualState::disabled ? full_color(square_disabled) : full_color();
    const auto state_index = texture_state_index(state);
    list.push(sprite(square_button_assets[state_index],
                     destination,
                     DrawSpace::design_pixels,
                     TextureFilter::nearest,
                     UiTextureAnchor::top_left,
                     square_source_scale,
                     modulation));
    list.push(sprite(control.icon_asset,
                     destination,
                     DrawSpace::design_pixels,
                     TextureFilter::nearest,
                     UiTextureAnchor::center,
                     square_source_scale,
                     modulation));
}

void append_text_button(ui::DrawList& list,
                        const MainMenuModel& menu,
                        const MainMenuControl& control) {
    constexpr double retail_slice_width{36.0};
    constexpr double seam_width{1.0};
    constexpr double horizontal_text_inset{14.0};
    constexpr double vertical_text_inset{4.0};

    const auto state = menu.visual_state(control.widget.id);
    const auto button = to_design_rect(control.widget.bounds);
    const auto middle_width = button.width - retail_slice_width * 2.0;
    const auto state_index = texture_state_index(state);
    const auto background_modulation = ColorModulation{
        white,
        state == WidgetVisualState::disabled ? std::uint16_t{700U} : std::uint16_t{1'000U},
        1'000U,
    };

    // The one-pixel overlaps are present in TextButton.draw() and hide seams.
    const std::array destinations{
        DrawRect{button.x, button.y, retail_slice_width + seam_width, button.height},
        DrawRect{button.x + retail_slice_width, button.y, middle_width + seam_width, button.height},
        DrawRect{button.x + retail_slice_width + middle_width,
                 button.y,
                 retail_slice_width + seam_width,
                 button.height},
    };

    for (std::size_t slice = 0U; slice < destinations.size(); ++slice) {
        list.push(sprite(text_button_assets[state_index][slice],
                         destinations[slice],
                         DrawSpace::design_pixels,
                         TextureFilter::nearest,
                         UiTextureAnchor::top_left,
                         source_scale,
                         background_modulation));
    }

    // Pressed TextButton artwork does not move; only its content drops 2 px.
    const auto pressed_offset = state == WidgetVisualState::pressed ? 2.0 : 0.0;
    list.push(TextDrawCommand{
        std::string{control.localization_key},
        std::string{main_menu_assets::button_font},
        DrawRect{
            button.x + horizontal_text_inset,
            button.y + vertical_text_inset + pressed_offset,
            button.width - horizontal_text_inset * 2.0,
            button.height - vertical_text_inset * 2.0,
        },
        DrawSpace::design_pixels,
        36.0,
        2.0,
        2U,
        HorizontalTextAlignment::center,
        VerticalTextAlignment::retail_center,
        TextTransform::uppercase,
        TextFit::shrink_to_fit,
        full_color(button_text_color),
    });
}

void append_navigation_item(ui::DrawList& list,
                            const MainMenuModel& menu,
                            const MainMenuControl& control) {
    constexpr double navigation_padding{5.0};
    constexpr double icon_width{25.0};
    constexpr double icon_height{25.0};
    constexpr double icon_integer_anchor{12.0};

    const auto state = menu.visual_state(control.widget.id);
    const auto highlighted =
        state == WidgetVisualState::hovered || state == WidgetVisualState::pressed;
    const auto intensity = highlighted ? std::uint16_t{1'000U} : std::uint16_t{700U};
    const auto target = to_design_rect(control.widget.bounds);
    const auto text_x = target.x + icon_width + navigation_padding * 0.5;

    // Retail draw_item emits text first and the centered icon second.
    list.push(TextDrawCommand{
        std::string{control.localization_key},
        std::string{main_menu_assets::button_font},
        DrawRect{text_x, target.y, target.width, target.height},
        DrawSpace::design_pixels,
        24.0,
        0.0,
        1U,
        HorizontalTextAlignment::left,
        VerticalTextAlignment::retail_center,
        TextTransform::uppercase,
        TextFit::shrink_to_fit,
        ColorModulation{navigation_text_color, intensity, 1'000U},
    });

    // The loader's centered 25 px texture uses Python 2 integer anchor 12.
    const auto icon_left = target.x - icon_integer_anchor;
    list.push(sprite(control.icon_asset,
                     DrawRect{icon_left,
                              target.y +
                                  std::floor((target.height - icon_height) * 0.5),
                              icon_width,
                              icon_height},
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     UiTextureAnchor::center,
                     navigation_source_scale,
                     ColorModulation{white, intensity, 1'000U}));
}

[[nodiscard]] DrawRect background_cover(ui::PixelExtent window) noexcept {
    constexpr double loaded_width{768.0};
    constexpr double loaded_height{576.0};
    const auto window_width = static_cast<double>(window.width);
    const auto window_height = static_cast<double>(window.height);
    const auto scale = std::max(window_width / loaded_width, window_height / loaded_height);
    const auto width = loaded_width * scale;
    const auto height = loaded_height * scale;
    return DrawRect{
        (window_width - width) * 0.5,
        (window_height - height) * 0.5,
        width,
        height,
    };
}

} // namespace

std::optional<PlayerNamePlateGeometry>
resolve_player_name_plate_geometry(const PlayerNamePlateDrawRequest& request,
                                   double content_width_pixels) noexcept {
    if (!request.window.is_valid() || !std::isfinite(content_width_pixels) ||
        !std::isfinite(request.window_offset_x) ||
        content_width_pixels < 0.0) {
        return std::nullopt;
    }

    const std::array constants{
        request.frame_base_width_pixels,
        request.frame_height_pixels,
        request.minimum_text_width_pixels,
        request.safe_edge_inset_pixels,
        request.text_center_x_padding_pixels,
        request.text_center_y_adjustment_pixels,
    };
    if (!std::all_of(constants.begin(), constants.end(), [](double value) {
            return std::isfinite(value);
        })) {
        return std::nullopt;
    }

    constexpr double retail_width{800.0};
    constexpr double retail_height{600.0};
    const auto scale = std::min(static_cast<double>(request.window.width) / retail_width,
                                static_cast<double>(request.window.height) / retail_height);
    const auto design_frame_width =
        request.frame_base_width_pixels + content_width_pixels - request.minimum_text_width_pixels;
    const auto frame_width = design_frame_width * scale;
    const auto frame_height = request.frame_height_pixels * scale;
    if (!(scale > 0.0) || !(frame_width > 0.0) || !(frame_height > 0.0)) {
        return std::nullopt;
    }

    const auto frame_x = static_cast<double>(request.window.width) - frame_width -
                         request.safe_edge_inset_pixels * scale + request.window_offset_x;
    const auto frame_y = request.safe_edge_inset_pixels * scale;
    return PlayerNamePlateGeometry{
        DrawRect{frame_x, frame_y, frame_width, frame_height},
        frame_x + request.text_center_x_padding_pixels * scale,
        frame_y + frame_height * 0.5 - request.text_center_y_adjustment_pixels * scale,
        scale,
    };
}

ui::DrawList MainMenuPresentation::build(const MainMenuModel& menu,
                                         const MainMenuPresentationContext& context) const {
    if (!context.window.is_valid()) {
        throw std::invalid_argument{"main-menu presentation requires a valid window extent"};
    }
    if (context.background_opacity_per_mille > 1'000U) {
        throw std::invalid_argument{"main-menu background opacity exceeds one"};
    }

    ui::DrawList list;
    list.reserve(command_count);

    list.push(sprite(main_menu_assets::background,
                     background_cover(context.window),
                     DrawSpace::window_pixels,
                     TextureFilter::linear,
                     ui::TextureAnchor::top_left,
                     source_scale,
                     ColorModulation{white, 1'000U, context.background_opacity_per_mille},
                     SpriteSizing::cover));

    // 340x451 is int(source * 0.6); Python 2 center anchors are (170, 225).
    list.push(sprite(main_menu_assets::frame,
                     retail_bottom_left_rect(230.0, 11.0, 340.0, 451.0),
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     ui::TextureAnchor::center,
                     source_scale));

    list.push(PlayerNamePlateDrawRequest{
        std::string{main_menu_assets::player_name_frame},
        "WELCOME",
        context.player_name,
        std::string{main_menu_assets::welcome_font},
        context.window,
    });

    const auto controls = menu.controls();
    for (std::size_t index = 0U; index < 4U; ++index) {
        append_square_button(list, menu, controls[index]);
    }
    for (std::size_t index = 4U; index < 8U; ++index) {
        append_text_button(list, menu, controls[index]);
    }
    append_navigation_item(list, menu, controls[8U]);
    append_navigation_item(list, menu, controls[9U]);

    // Loaded 391x258 splash has integer center anchor (195,129), then a 0.75
    // scene transform about (412,510). Resolve that transform exactly here.
    list.push(sprite(main_menu_assets::splash,
                     retail_bottom_left_rect(265.75, 413.25, 293.25, 193.5),
                     DrawSpace::design_pixels,
                     TextureFilter::linear,
                     ui::TextureAnchor::center,
                     source_scale));

    return list;
}

} // namespace battlespades::frontend
