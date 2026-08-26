#include "battlespades/frontend/identity_presentation.hpp"

#include "battlespades/frontend/game_hud.hpp"

#include <algorithm>
#include <array>
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
using ui::TextDrawCommand;
using ui::TextFit;
using ui::TextTransform;
using ui::TextureAnchor;
using ui::TextureFilter;
using ui::VerticalTextAlignment;

constexpr double subpixels{
    static_cast<double>(IdentityMenuModel::subpixels_per_pixel)};
constexpr ColorRgba8 white{255U, 255U, 255U, 255U};
constexpr ColorRgba8 dark_text{20U, 20U, 20U, 255U};
constexpr ColorRgba8 warm_text{236U, 214U, 114U, 255U};
constexpr ColorRgba8 error_text{255U, 112U, 94U, 255U};
constexpr ColorRgba8 focus_color{238U, 217U, 122U, 255U};

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

[[nodiscard]] DrawRect draw_rect(ui::Rect value) noexcept {
    return DrawRect{
        static_cast<double>(value.x) / subpixels,
        static_cast<double>(value.y) / subpixels,
        static_cast<double>(value.width) / subpixels,
        static_cast<double>(value.height) / subpixels,
    };
}

[[nodiscard]] SpriteDrawCommand sprite(std::string_view asset,
                                       DrawRect destination,
                                       TextureFilter filter,
                                       ColorRgba8 color = white,
                                       std::uint16_t intensity = 1'000U,
                                       std::uint16_t opacity = 1'000U) {
    return SpriteDrawCommand{
        std::string{asset},
        destination,
        DrawSpace::design_pixels,
        filter,
        TextureAnchor::top_left,
        0.6,
        ui::SpriteSizing::stretch,
        ColorModulation{color, intensity, opacity},
    };
}

[[nodiscard]] TextDrawCommand text(std::string value,
                                   DrawRect destination,
                                   double size,
                                   ColorRgba8 color,
                                   HorizontalTextAlignment alignment =
                                       HorizontalTextAlignment::center,
                                   std::uint8_t lines = 1U) {
    return TextDrawCommand{
        std::move(value),
        std::string{main_menu_assets::button_font},
        destination,
        DrawSpace::design_pixels,
        size,
        2.0,
        lines,
        alignment,
        VerticalTextAlignment::retail_center,
        TextTransform::preserve,
        TextFit::shrink_to_fit,
        ColorModulation{color, 1'000U, 1'000U},
    };
}

[[nodiscard]] std::size_t state_index(WidgetVisualState state) noexcept {
    if (state == WidgetVisualState::pressed) return 2U;
    if (state == WidgetVisualState::hovered) return 1U;
    return 0U;
}

void append_button(ui::DrawList& list,
                   const IdentityMenuModel& model,
                   const IdentityControl& control) {
    const auto state = model.visual_state(control.widget.id);
    const auto bounds = draw_rect(control.widget.bounds);
    constexpr double slice{36.0};
    const auto middle = std::max(0.0, bounds.width - slice * 2.0);
    const std::array rectangles{
        DrawRect{bounds.x, bounds.y, slice + 1.0, bounds.height},
        DrawRect{bounds.x + slice, bounds.y, middle + 1.0, bounds.height},
        DrawRect{bounds.x + slice + middle, bounds.y, slice + 1.0, bounds.height},
    };
    const auto index = state_index(state);
    const auto intensity =
        state == WidgetVisualState::disabled ? std::uint16_t{600U}
                                             : std::uint16_t{1'000U};
    for (std::size_t part = 0U; part < rectangles.size(); ++part) {
        list.push(sprite(button_assets[index][part],
                         rectangles[part],
                         TextureFilter::nearest,
                         white,
                         intensity));
    }
    list.push(text(std::string{control.label},
                   DrawRect{bounds.x + 8.0, bounds.y + 3.0,
                            bounds.width - 16.0, bounds.height - 6.0},
                   bounds.width < 150.0 ? 23.0 : 28.0,
                   dark_text));
}

void append_field(ui::DrawList& list,
                  DrawRect bounds,
                  std::string_view label,
                  std::string value,
                  bool focused) {
    list.push(text(std::string{label},
                   DrawRect{bounds.x, bounds.y - 19.0, bounds.width, 17.0},
                   16.0,
                   warm_text,
                   HorizontalTextAlignment::left));
    list.push(sprite(game_hud_assets::white_pixel,
                     bounds,
                     TextureFilter::nearest,
                     focused ? focus_color : ColorRgba8{42U, 48U, 34U, 255U},
                     1'000U,
                     focused ? 900U : 780U));
    list.push(sprite(game_hud_assets::white_pixel,
                     DrawRect{bounds.x + 3.0, bounds.y + 3.0,
                              bounds.width - 6.0, bounds.height - 6.0},
                     TextureFilter::nearest,
                     ColorRgba8{17U, 20U, 15U, 255U},
                     1'000U,
                     940U));
    if (value.empty()) value = label == "USERNAME" ? "Player name" : "Password";
    list.push(text(std::move(value),
                   DrawRect{bounds.x + 10.0, bounds.y + 2.0,
                            bounds.width - 20.0, bounds.height - 4.0},
                   20.0,
                   ColorRgba8{240U, 235U, 198U, 255U},
                   HorizontalTextAlignment::left));
}

[[nodiscard]] DrawRect background_cover(ui::PixelExtent window) noexcept {
    constexpr double width{768.0};
    constexpr double height{576.0};
    const auto factor =
        std::max(static_cast<double>(window.width) / width,
                 static_cast<double>(window.height) / height);
    return DrawRect{
        (static_cast<double>(window.width) - width * factor) * 0.5,
        (static_cast<double>(window.height) - height * factor) * 0.5,
        width * factor,
        height * factor,
    };
}

} // namespace

ui::DrawList IdentityPresentation::build(
    const IdentityMenuModel& model,
    const IdentityPresentationContext& context) const {
    if (!context.window.is_valid() || context.opacity_per_mille > 1'000U) {
        throw std::invalid_argument{"identity presentation context is invalid"};
    }

    ui::DrawList list;
    list.reserve(30U);
    auto background = sprite(main_menu_assets::background,
                             background_cover(context.window),
                             TextureFilter::linear,
                             white,
                             1'000U,
                             context.opacity_per_mille);
    background.space = DrawSpace::window_pixels;
    background.sizing = ui::SpriteSizing::cover;
    list.push(std::move(background));
    list.push(sprite(main_menu_assets::frame,
                     DrawRect{230.0, 126.0, 340.0, 464.0},
                     TextureFilter::linear));
    list.push(sprite(main_menu_assets::splash,
                     DrawRect{266.0, 22.0, 293.0, 194.0},
                     TextureFilter::linear));
    list.push(text("PLAYER IDENTITY",
                   DrawRect{255.0, 222.0, 290.0, 32.0},
                   25.0,
                   warm_text));

    if (model.phase() == IdentityMenuPhase::recovery_code) {
        list.push(text("SAVE YOUR RECOVERY CODE",
                       DrawRect{258.0, 275.0, 284.0, 30.0},
                       22.0,
                       warm_text));
        list.push(text(std::string{model.recovery_code()},
                       DrawRect{252.0, 319.0, 296.0, 36.0},
                       19.0,
                       white));
        list.push(text("This is the only password-reset method. Store it outside the game folder.",
                       DrawRect{265.0, 365.0, 270.0, 64.0},
                       14.0,
                       white,
                       HorizontalTextAlignment::center,
                       3U));
    } else {
        append_field(list,
                     draw_rect(model.username_bounds()),
                     "USERNAME",
                     std::string{model.username()},
                     model.focused_field() == IdentityField::username);
        append_field(list,
                     draw_rect(model.password_bounds()),
                     "PASSWORD",
                     model.masked_password(),
                     model.focused_field() == IdentityField::password);
        const auto message = !model.error().empty() ? model.error() : model.status();
        if (!message.empty()) {
            list.push(text(std::string{message},
                           DrawRect{255.0, 542.0, 290.0, 36.0},
                           14.0,
                           model.error().empty() ? warm_text : error_text,
                           HorizontalTextAlignment::center,
                           2U));
        } else {
            list.push(text("SIGN IN, REGISTER, OR PLAY AS GUEST",
                           DrawRect{255.0, 548.0, 290.0, 24.0},
                           14.0,
                           warm_text));
        }
    }

    for (const auto& control : model.controls()) {
        if (control.widget.state.visible) append_button(list, model, control);
    }
    return list;
}

} // namespace battlespades::frontend
