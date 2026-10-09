#include "battlespades/frontend/main_menu_presentation.hpp"

#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using battlespades::frontend::MainMenuAction;
using battlespades::frontend::MainMenuModel;
using battlespades::frontend::MainMenuPresentation;
using battlespades::frontend::MainMenuPresentationContext;
using battlespades::ui::ColorRgba8;
using battlespades::ui::DrawCommand;
using battlespades::ui::DrawRect;
using battlespades::ui::DrawSpace;
using battlespades::ui::PlayerNamePlateDrawRequest;
using battlespades::ui::Point;
using battlespades::ui::SpriteDrawCommand;
using battlespades::ui::SpriteSizing;
using battlespades::ui::TextDrawCommand;
using battlespades::ui::TextFit;
using battlespades::ui::TextTransform;
using battlespades::ui::TextureAnchor;
using battlespades::ui::TextureFilter;
using battlespades::ui::VerticalTextAlignment;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

template <typename CommandType>
const CommandType& command_as(const std::vector<DrawCommand>& commands, std::size_t index) {
    if (index >= commands.size()) {
        throw std::runtime_error{"draw command index is out of range"};
    }
    const auto* command = std::get_if<CommandType>(&commands[index]);
    if (command == nullptr) {
        throw std::runtime_error{"draw command has an unexpected payload type"};
    }
    return *command;
}

[[nodiscard]] std::vector<DrawCommand> build(const MainMenuModel& menu,
                                             battlespades::ui::PixelExtent window = {800, 600}) {
    auto list =
        MainMenuPresentation{}.build(menu, MainMenuPresentationContext{window, "KikoTs", 1'000U});
    return {list.commands().begin(), list.commands().end()};
}

void command_order_matches_retail_composition() {
    const MainMenuModel menu;
    const auto commands = build(menu);
    expect(commands.size() == MainMenuPresentation::command_count,
           "Select Menu must emit its complete stable command count");

    expect(command_as<SpriteDrawCommand>(commands, 0U).asset_id == "png/ui/ugc_splash.png",
           "window background must draw first");
    expect(command_as<SpriteDrawCommand>(commands, 1U).asset_id ==
               "png/ui/main_menu/frame_main_menu.png",
           "main frame must follow the background");
    expect(command_as<PlayerNamePlateDrawRequest>(commands, 2U).requires_font_metrics,
           "dynamic name plate must retain its retail draw slot");

    expect(command_as<SpriteDrawCommand>(commands, 3U).asset_id.find("square_default") !=
               std::string::npos,
           "first control command must be Tutorial square state art");
    expect(command_as<SpriteDrawCommand>(commands, 4U).asset_id.find("tutorial") !=
               std::string::npos,
           "Tutorial icon must layer over its state art");
    expect(command_as<TextDrawCommand>(commands, 14U).localization_key ==
               "UGC_MAIN_MENU_UGC_BUTTON",
           "Map Creator text must follow its three slices");
    expect(command_as<TextDrawCommand>(commands, 27U).localization_key == "QUIT",
           "retail navigation draws Quit text before its icon");
    expect(command_as<SpriteDrawCommand>(commands, 28U).asset_id.find("quit_icon") !=
               std::string::npos,
           "Quit icon must follow Quit text");
    expect(command_as<TextDrawCommand>(commands, 29U).localization_key == "LOGOUT",
           "Logout must share the retail navigation bar");
    expect(command_as<SpriteDrawCommand>(commands, 30U).asset_id.find("back_icon") !=
               std::string::npos,
           "Logout icon must follow Logout text");
    expect(command_as<SpriteDrawCommand>(commands, 31U).asset_id == "png/ui/splash.png",
           "logo must draw last");
}

void retail_menu_text_centers_the_ftgl_metric_span_not_line_height() {
    const MainMenuModel menu;
    const auto commands = build(menu);
    std::size_t text_count{};
    for (const auto& command : commands) {
        const auto* text = std::get_if<TextDrawCommand>(&command);
        if (text == nullptr) continue;
        ++text_count;
        expect(text->vertical_alignment == VerticalTextAlignment::retail_center,
               "retail menu text must use text.py ascender/negative-descender centering");
    }
    expect(text_count == 7U,
           "the complete main menu text set must remain covered by metric-centering characterization");
}

void background_cover_and_center_anchors_are_resolved_to_top_left() {
    const MainMenuModel menu;
    const auto commands = build(menu, {1280, 720});

    const auto& background = command_as<SpriteDrawCommand>(commands, 0U);
    expect(background.destination == DrawRect{0.0, -120.0, 1280.0, 960.0},
           "4:3 background must cover a 16:9 window without stretching");
    expect(background.space == DrawSpace::window_pixels,
           "background must bypass the contained design viewport");
    expect(background.sizing == SpriteSizing::cover, "background must be tagged as cover");
    expect(background.sampling == TextureFilter::linear,
           "background must preserve retail linear sampling");
    expect(background.retail_source_scale == 0.6, "background must preserve its retail load scale");

    const auto& frame = command_as<SpriteDrawCommand>(commands, 1U);
    expect(frame.destination == DrawRect{230.0, 138.0, 340.0, 451.0},
           "main frame must resolve Python 2 integer center anchors exactly");
    expect(frame.retail_source_anchor == TextureAnchor::center,
           "frame must retain centered-loader evidence");

    const auto& logo = command_as<SpriteDrawCommand>(commands, 31U);
    expect(logo.destination == DrawRect{265.75, -6.75, 293.25, 193.5},
           "logo must retain its recovered 0.75 scene transform");
    expect(logo.retail_source_anchor == TextureAnchor::center,
           "logo must retain its centered-loader evidence");
}

void square_buttons_select_state_art_tint_and_pressed_offset() {
    MainMenuModel menu;

    auto commands = build(menu);
    auto square = command_as<SpriteDrawCommand>(commands, 3U);
    expect(square.asset_id.find("square_default") != std::string::npos,
           "accessibility focus must not invent a retail focused texture");
    expect(square.destination == DrawRect{267.0, 445.0, 63.75, 63.75},
           "fractional Tutorial square geometry must remain exact");
    expect(square.sampling == TextureFilter::nearest, "square state art must use nearest sampling");

    menu.pointer_press(Point{2'391, 3'815});
    commands = build(menu);
    square = command_as<SpriteDrawCommand>(commands, 3U);
    const auto icon = command_as<SpriteDrawCommand>(commands, 4U);
    expect(square.asset_id.find("square_press") != std::string::npos,
           "pressed square must select pressed art");
    expect(square.destination.y == 448.0 && icon.destination.y == 448.0,
           "pressed square art and icon must both move down exactly three pixels");
    expect(icon.retail_source_anchor == TextureAnchor::center,
           "square icon must retain its centered-loader evidence");

    static_cast<void>(menu.pointer_release(Point{2'391, 3'815}));
    expect(menu.set_enabled(MainMenuAction::tutorial, false),
           "Tutorial should support disabled characterization");
    commands = build(menu);
    square = command_as<SpriteDrawCommand>(commands, 3U);
    expect(square.asset_id.find("square_default") != std::string::npos,
           "disabled square must reuse normal art");
    expect(square.modulation.color == ColorRgba8{86U, 86U, 86U, 255U},
           "disabled square must use the recovered exact tint");
}

void text_buttons_keep_one_pixel_seams_and_move_only_pressed_text() {
    MainMenuModel menu;
    auto commands = build(menu);

    const auto& left = command_as<SpriteDrawCommand>(commands, 11U);
    const auto& middle = command_as<SpriteDrawCommand>(commands, 12U);
    const auto& right = command_as<SpriteDrawCommand>(commands, 13U);
    const auto& text = command_as<TextDrawCommand>(commands, 14U);
    expect(left.destination == DrawRect{269.0, 368.0, 37.0, 58.0},
           "left slice must include the retail seam overlap");
    expect(middle.destination == DrawRect{305.0, 368.0, 191.0, 58.0},
           "middle slice must overlap both neighbours by one pixel");
    expect(right.destination == DrawRect{495.0, 368.0, 37.0, 58.0},
           "right slice must include the retail seam overlap");
    expect(text.destination == DrawRect{283.0, 372.0, 234.0, 50.0},
           "normal label bounds must preserve retail content insets");
    expect(text.preferred_font_asset == "fonts/Spades.ttf" &&
               text.requested_font_size_pixels == 36.0,
           "large buttons must request the recovered display font");
    expect(text.transform == TextTransform::uppercase && text.fit == TextFit::shrink_to_fit &&
               text.maximum_lines == 2U,
           "localization must be uppercased and fitted by the text backend");

    menu.pointer_press(Point{3'200, 3'176});
    commands = build(menu);
    const auto& pressed_left = command_as<SpriteDrawCommand>(commands, 11U);
    const auto& pressed_text = command_as<TextDrawCommand>(commands, 14U);
    expect(pressed_left.asset_id.find("button_large_press_left") != std::string::npos,
           "pressed text button must select pressed slices");
    expect(pressed_left.destination.y == 368.0,
           "pressed text-button artwork must remain stationary");
    expect(pressed_text.destination.y == 374.0,
           "pressed text content must move down exactly two pixels");

    static_cast<void>(menu.pointer_release(Point{3'200, 3'176}));
    expect(menu.set_enabled(MainMenuAction::user_content, false),
           "Map Creator should support disabled characterization");
    commands = build(menu);
    const auto& disabled_left = command_as<SpriteDrawCommand>(commands, 11U);
    expect(disabled_left.asset_id.find("button_large_left.png") != std::string::npos,
           "disabled text button must reuse normal slices");
    expect(disabled_left.modulation.intensity_per_mille == 700U,
           "disabled text-button background must retain exact 0.7 intensity");
}

void navigation_item_preserves_font_metric_target_and_idle_dimming() {
    MainMenuModel menu;
    auto commands = build(menu);
    auto text = command_as<TextDrawCommand>(commands, 27U);
    auto icon = command_as<SpriteDrawCommand>(commands, 28U);

    expect(text.destination == DrawRect{391.0, 542.0, 73.0, 26.0},
           "Quit text must use its recovered font-metric target");
    expect(icon.destination == DrawRect{351.5, 542.0, 25.0, 25.0},
           "Quit icon must resolve its Python 2 center anchor");
    expect(text.modulation.intensity_per_mille == 700U &&
               icon.modulation.intensity_per_mille == 700U,
           "idle navigation text and icon must use retail 0.7 intensity");
    expect(icon.sampling == TextureFilter::linear && icon.retail_source_scale == 0.64,
           "Quit icon must preserve its distinct load recipe");

    menu.pointer_move(Point{3'200, 4'440});
    commands = build(menu);
    text = command_as<TextDrawCommand>(commands, 27U);
    icon = command_as<SpriteDrawCommand>(commands, 28U);
    expect(text.modulation.intensity_per_mille == 1'000U &&
               icon.modulation.intensity_per_mille == 1'000U,
           "hovered navigation item must restore full intensity");

    menu.pointer_press(Point{3'200, 4'440});
    commands = build(menu);
    expect(command_as<SpriteDrawCommand>(commands, 28U).destination == icon.destination,
           "middle Quit item has no recovered pressed offset");
}

void name_plate_is_an_explicit_font_metrics_request() {
    const MainMenuModel menu;
    const auto commands = build(menu, {2560, 1440});
    const auto& name_plate = command_as<PlayerNamePlateDrawRequest>(commands, 2U);

    expect(name_plate.player_name == "KikoTs", "presentation must forward the runtime name");
    expect(name_plate.welcome_localization_key == "WELCOME",
           "name plate must request localized welcome text");
    expect(name_plate.preferred_font_asset == "fonts/A750-Sans-Medium.ttf" &&
               name_plate.requested_font_size_pixels == 16.0,
           "name plate must request the recovered welcome font");
    expect(name_plate.window.width == 2560 && name_plate.window.height == 1440,
           "deferred safe-edge placement must receive the actual window aspect");
    expect(name_plate.maximum_player_name_code_points == 32U,
           "retail player-name truncation must remain part of the request");
}

void name_plate_geometry_scales_the_complete_retail_formula() {
    PlayerNamePlateDrawRequest request;
    request.window = {800, 600};
    const auto retail = battlespades::frontend::resolve_player_name_plate_geometry(request, 120.0);
    expect(retail.has_value(), "valid retail name-plate metrics must resolve");
    expect(retail->frame == DrawRect{636.0, 10.0, 154.0, 31.0} && retail->text_left == 651.0 &&
               retail->baseline_y == 30.5 && retail->scale == 1.0,
           "800x600 name-plate geometry must match the retail formula exactly");

    request.window = {1680, 1050};
    const auto widescreen =
        battlespades::frontend::resolve_player_name_plate_geometry(request, 120.0);
    expect(widescreen.has_value(), "valid widescreen name-plate metrics must resolve");
    expect(widescreen->frame == DrawRect{1393.0, 17.5, 269.5, 54.25} &&
               widescreen->text_left == 1419.25 && widescreen->baseline_y == 53.375 &&
               widescreen->scale == 1.75,
           "1680x1050 must scale the base, content delta, insets and baseline together");

    expect(!battlespades::frontend::resolve_player_name_plate_geometry(request, std::nan(""))
                .has_value(),
           "invalid font metrics must fail before renderer submission");
}

void invalid_presentation_context_fails_before_emitting_a_partial_frame() {
    const MainMenuModel menu;
    bool invalid_window_threw{};
    try {
        static_cast<void>(MainMenuPresentation{}.build(
            menu, MainMenuPresentationContext{{0, 600}, "Player", 1'000U}));
    } catch (const std::invalid_argument&) {
        invalid_window_threw = true;
    }
    expect(invalid_window_threw, "invalid drawable extent must throw");

    bool invalid_opacity_threw{};
    try {
        static_cast<void>(MainMenuPresentation{}.build(
            menu, MainMenuPresentationContext{{800, 600}, "Player", 1'001U}));
    } catch (const std::invalid_argument&) {
        invalid_opacity_threw = true;
    }
    expect(invalid_opacity_threw, "out-of-range opacity must throw");
}

struct TestCase final {
    std::string_view name;
    std::function<void()> body;
};

} // namespace

int main() {
    const std::vector<TestCase> tests{
        {"command_order_matches_retail_composition", command_order_matches_retail_composition},
        {"retail_menu_text_centers_the_ftgl_metric_span_not_line_height",
         retail_menu_text_centers_the_ftgl_metric_span_not_line_height},
        {"background_cover_and_center_anchors_are_resolved_to_top_left",
         background_cover_and_center_anchors_are_resolved_to_top_left},
        {"square_buttons_select_state_art_tint_and_pressed_offset",
         square_buttons_select_state_art_tint_and_pressed_offset},
        {"text_buttons_keep_one_pixel_seams_and_move_only_pressed_text",
         text_buttons_keep_one_pixel_seams_and_move_only_pressed_text},
        {"navigation_item_preserves_font_metric_target_and_idle_dimming",
         navigation_item_preserves_font_metric_target_and_idle_dimming},
        {"name_plate_is_an_explicit_font_metrics_request",
         name_plate_is_an_explicit_font_metrics_request},
        {"name_plate_geometry_scales_the_complete_retail_formula",
         name_plate_geometry_scales_the_complete_retail_formula},
        {"invalid_presentation_context_fails_before_emitting_a_partial_frame",
         invalid_presentation_context_fails_before_emitting_a_partial_frame},
    };

    std::size_t failures{};
    for (const auto& test : tests) {
        try {
            test.body();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
        }
    }

    std::cout << tests.size() - failures << '/' << tests.size() << " tests passed\n";
    return failures == 0U ? 0 : 1;
}
