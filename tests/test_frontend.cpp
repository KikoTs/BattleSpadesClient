#include "battlespades/frontend/frontend_shell.hpp"
#include "battlespades/frontend/frontend_navigation.hpp"
#include "battlespades/frontend/main_menu.hpp"
#include "battlespades/network/live_protocol168_connection.hpp"
#include "battlespades/ui/design_canvas.hpp"
#include "battlespades/ui/draw_list_transform.hpp"

#include <array>
#include <cmath>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using battlespades::frontend::FrontendShellModel;
using battlespades::frontend::FrontendNavigationModel;
using battlespades::frontend::FrontendScreen;
using battlespades::frontend::MainMenuAction;
using battlespades::frontend::MainMenuModel;
using battlespades::frontend::NavigationDirection;
using battlespades::frontend::WidgetVisualState;
using battlespades::ui::DesignCanvas;
using battlespades::ui::InputAction;
using battlespades::ui::InputEvent;
using battlespades::ui::InputPhase;
using battlespades::ui::PixelExtent;
using battlespades::ui::Point;
using battlespades::ui::Rect;
using battlespades::ui::ScreenId;
using battlespades::ui::WidgetId;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

void live_packet_budget_bounds_authority_age_without_reordering() {
    using battlespades::network::protocol168_inbound_apply_budget;

    expect(protocol168_inbound_apply_budget(0U, true) == 16U,
           "ordinary playable frames must retain the small fixed tranche");
    expect(protocol168_inbound_apply_budget(17U, true) == 17U &&
               protocol168_inbound_apply_budget(64U, true) == 64U,
           "a short jitter burst must be drained completely");
    expect(protocol168_inbound_apply_budget(65U, true) == 64U &&
               protocol168_inbound_apply_budget(512U, true) == 64U,
           "ordinary live catch-up must remain bounded");
    expect(protocol168_inbound_apply_budget(513U, true) == 128U,
           "a severe backlog must converge faster without an unbounded frame");
    expect(protocol168_inbound_apply_budget(8'192U, false) == 128U,
           "hidden loading may use the bounded aggressive drain");
}

void canvas_preserves_aspect_and_rejects_letterbox_input() {
    const DesignCanvas canvas{
        MainMenuModel::reference_width_pixels,
        MainMenuModel::reference_height_pixels,
        MainMenuModel::subpixels_per_pixel,
    };
    const auto viewport = canvas.viewport(PixelExtent{1280, 720});
    expect(viewport.has_value(), "valid window must produce a viewport");
    expect(viewport->x == 160.0, "16:9 window must pillarbox the 4:3 canvas");
    expect(viewport->y == 0.0, "matching height must not letterbox vertically");
    expect(viewport->width == 960.0, "viewport width must remain 4:3");
    expect(!canvas.to_canvas(PixelExtent{1280, 720}, Point{159, 100}).has_value(),
           "left pillarbox must not map to UI");
    expect(canvas.to_canvas(PixelExtent{1280, 720}, Point{160, 0}) == Point{0, 0},
           "viewport origin must map exactly");
    expect(!canvas.to_canvas(PixelExtent{1280, 720}, Point{1120, 0}).has_value(),
           "right viewport edge must be half-open");
}

void canvas_uses_recovered_truncation_for_render_and_pointer_mapping() {
    const DesignCanvas canvas{
        MainMenuModel::reference_width_pixels,
        MainMenuModel::reference_height_pixels,
        MainMenuModel::subpixels_per_pixel,
    };

    const auto retail = canvas.viewport(PixelExtent{800, 600});
    expect(retail == battlespades::ui::CanvasViewport{0, 0, 800, 600, 1.0},
           "the 800x600 golden viewport must remain unchanged");
    const auto widescreen = canvas.viewport(PixelExtent{1680, 1050});
    expect(widescreen == battlespades::ui::CanvasViewport{140, 0, 1400, 1050, 1.75},
           "the 1680x1050 golden viewport must remain unchanged");

    const auto odd = canvas.viewport(PixelExtent{801, 600});
    expect(odd == battlespades::ui::CanvasViewport{0, 0, 800, 600, 1.0},
           "retail truncation must put an odd spare pixel on the far edge");
    expect(canvas.to_canvas(PixelExtent{801, 600}, 0.0, 0.0) == Point{0, 0},
           "pointer inverse must use the same truncated origin as rendering");
    expect(!canvas.to_canvas(PixelExtent{801, 600}, 800.0, 0.0).has_value(),
           "the odd far-edge pixel must remain outside the logical canvas");

    const auto fractional = canvas.viewport(PixelExtent{1000, 602});
    expect(fractional.has_value(), "fractional-scale window must map");
    expect(fractional->x == 98 && fractional->y == 0 && fractional->width == 802 &&
               fractional->height == 602,
           "origin and reported extent must truncate exactly like get_aspect");
    expect(std::abs(fractional->scale - (602.0 / 600.0)) < 1.0e-12,
           "the uniform retail scale must remain unrounded");

    const auto exact_far_x = static_cast<double>(fractional->x) + 800.0 * fractional->scale;
    expect(canvas.to_canvas(PixelExtent{1000, 602}, std::nextafter(exact_far_x, 0.0), 100.0)
               .has_value(),
           "the last point rendered by the floating scale must be hittable");
    expect(!canvas.to_canvas(PixelExtent{1000, 602}, std::nextafter(exact_far_x, 1000.0), 100.0)
                .has_value(),
           "the first point beyond the rendered canvas must fail closed");
    expect(!canvas.to_canvas(PixelExtent{1000, 602}, std::nan(""), 0.0).has_value(),
           "non-finite SDL coordinates must fail closed");
}

void canvas_text_rasterization_preserves_layout_and_uses_drawable_resolution() {
    const auto retail = battlespades::ui::resolve_canvas_text_rasterization(
        36.0, battlespades::ui::CanvasViewport{0, 0, 800, 600, 1.0});
    expect(retail == battlespades::ui::CanvasTextRasterization{36U, 1.0},
           "800x600 text must keep its recovered raster size");

    const auto high_resolution = battlespades::ui::resolve_canvas_text_rasterization(
        36.0, battlespades::ui::CanvasViewport{160, 0, 960, 720, 1.2});
    expect(high_resolution.has_value() && high_resolution->pixel_height == 43U,
           "larger canvases must request a correspondingly sharper glyph bitmap");
    expect(std::abs(high_resolution->design_pixels_per_bitmap_pixel - 36.0 / 43.0) < 1.0e-12,
           "high-resolution glyphs must map back to unchanged design geometry");

    const auto capped = battlespades::ui::resolve_canvas_text_rasterization(
        72.0, battlespades::ui::CanvasViewport{0, 0, 6400, 4800, 8.0});
    expect(capped.has_value() && capped->pixel_height == 256U,
           "extreme DPI must remain inside the text rasterizer resource bound");
    expect(!battlespades::ui::resolve_canvas_text_rasterization(
                0.0, battlespades::ui::CanvasViewport{0, 0, 800, 600, 1.0})
                .has_value(),
           "invalid font sizes must fail closed");
}

void frontend_shell_reproduces_slide_direction_and_input_gate() {
    FrontendShellModel shell;
    expect(shell.start(ScreenId{1U}), "root screen should start");
    expect(shell.active() == ScreenId{1U}, "root should become active");
    expect(shell.accepts_input(), "initial screen should accept input immediately");

    expect(shell.navigate(ScreenId{2U}), "forward navigation should start");
    expect(shell.active() == ScreenId{2U}, "destination should become active");
    expect(shell.previous() == ScreenId{1U}, "outgoing screen should be retained");
    expect(shell.active_offset() == 1.0, "forward screen enters from the right");
    expect(shell.previous_offset() == 0.0,
           "forward transition begins with the outgoing screen in place");
    expect(!shell.accepts_input(), "early transition input must be gated");

    shell.tick();
    expect(std::abs(shell.active_offset() - 0.9) < 0.000001,
           "transition must use the recovered divide-by-ten interpolation");
    expect(std::abs(shell.previous_offset() + 0.1) < 0.000001,
           "outgoing screen must travel left beside the incoming screen");

    for (int tick = 1; tick < 7; ++tick) {
        shell.tick();
    }
    expect(shell.accepts_input(), "input opens once retail offset is below 0.5");

    for (int tick = 0; tick < 60; ++tick) {
        shell.tick();
    }
    expect(!shell.transitioning(), "settled transition must release prior screen");
    expect(shell.active_offset() != 0.0 && std::abs(shell.active_offset()) < 0.005,
           "releasing the old screen must not snap the incoming screen to zero");
    for (int tick = 0; tick < 80; ++tick) {
        shell.tick();
    }
    expect(shell.active_offset() == 0.0,
           "sub-pixel residual must eventually settle exactly at zero");

    expect(shell.navigate(ScreenId{1U}, NavigationDirection::back), "back navigation should start");
    expect(shell.active_offset() == -1.0, "back screen enters from the left");
    expect(shell.previous_offset() == 0.0,
           "back transition begins with the outgoing screen in place");
    shell.tick();
    expect(std::abs(shell.previous_offset() - 0.1) < 0.000001,
           "back outgoing screen must travel right beside the returning screen");
}

void frontend_navigation_pushes_and_pops_nested_routes_atomically() {
    FrontendNavigationModel navigation{4U};
    expect(navigation.start(FrontendScreen::select_menu), "root navigation should start");
    expect(navigation.push(FrontendScreen::join_match), "Join Match should push");
    expect(navigation.depth() == 2U, "child route must remain on a real stack");
    expect(!navigation.push(FrontendScreen::server_browser),
           "early transition input must not mutate nested routes");

    for (std::size_t tick{}; tick < 7U; ++tick) {
        navigation.tick();
    }
    expect(navigation.push(FrontendScreen::server_browser),
           "retail input threshold must permit a chained route before visual settling");
    expect(!navigation.pop(), "the new transition must immediately restore its early gate");
    for (std::size_t tick{}; tick < 7U; ++tick) {
        navigation.tick();
    }
    expect(navigation.pop(), "Back should pop Server Browser");
    expect(navigation.active() == FrontendScreen::join_match,
           "Back must reveal the immediate parent");
    expect(navigation.shell().active_offset() == -1.0,
           "popped parent must enter from the left");
}

void gameplay_overlay_navigation_is_immediate() {
    FrontendNavigationModel navigation{4U};
    expect(navigation.start(FrontendScreen::tutorial_world), "world route should start");
    expect(navigation.push_instant(FrontendScreen::pause_menu),
           "Pause should push without a frontend slide");
    expect(navigation.active() == FrontendScreen::pause_menu &&
               !navigation.previous().has_value() &&
               navigation.shell().active_offset() == 0.0 &&
               navigation.shell().accepts_input(),
           "Pause must be usable immediately while gameplay remains stationary");
    expect(navigation.pop_instant(), "Resume should remove Pause immediately");
    expect(navigation.active() == FrontendScreen::tutorial_world &&
               !navigation.previous().has_value() &&
               navigation.shell().active_offset() == 0.0,
           "Resume must restore the world without translating the HUD");
}

void friends_returns_to_the_existing_lobby_without_duplicate_routes() {
    FrontendNavigationModel navigation;
    expect(navigation.start(FrontendScreen::select_menu), "start menu");
    expect(navigation.push_instant(FrontendScreen::create_match), "open lobby");
    expect(navigation.push(FrontendScreen::friends_lobby), "invite friends");
    expect(!navigation.return_to(FrontendScreen::create_match) && navigation.depth() == 3U,
           "an early asynchronous response must wait without mutating the route stack");
    for (int tick{}; tick < 8; ++tick) navigation.tick();
    expect(navigation.return_to(FrontendScreen::create_match), "reveal existing lobby");
    expect(navigation.depth() == 2U && navigation.active() == FrontendScreen::create_match &&
           navigation.previous() == FrontendScreen::friends_lobby,
           "return should slide from Friends to exactly one lobby");
    for (int tick{}; tick < 8; ++tick) navigation.tick();
    expect(navigation.pop() && navigation.active() == FrontendScreen::select_menu,
           "leaving the lobby must reveal the menu, not a duplicate lobby");
}

void match_departure_unwinds_overlays_without_transport_state() {
    FrontendNavigationModel navigation;
    expect(navigation.start(FrontendScreen::select_menu), "start menu");
    expect(navigation.push_instant(FrontendScreen::create_match), "open lobby");
    expect(navigation.push_instant(FrontendScreen::friends_lobby), "open friends");
    expect(!navigation.leave_match_instant() && navigation.depth() == 3U,
           "unrelated menu routes must survive a stale match departure");
    expect(navigation.pop_instant(), "close friends");
    expect(navigation.push_instant(FrontendScreen::game_loading), "loading route");
    expect(navigation.push_instant(FrontendScreen::tutorial_world), "world route");
    expect(navigation.push_instant(FrontendScreen::pause_menu), "pause overlay");
    expect(navigation.push(FrontendScreen::settings), "settings starts sliding");
    expect(navigation.leave_match_instant(), "forced departure during transition");
    expect(navigation.depth() == 2U && navigation.active() == FrontendScreen::create_match &&
               !navigation.previous() && navigation.shell().accepts_input(),
           "all departed match screens must unwind to a usable lobby immediately");
    expect(!navigation.leave_match_instant(), "duplicate departure is harmless");

    FrontendNavigationModel standalone;
    expect(standalone.start(FrontendScreen::tutorial_world), "standalone world");
    expect(standalone.push_instant(FrontendScreen::class_selection), "class overlay");
    expect(standalone.leave_match_instant() && standalone.depth() == 1U &&
               standalone.active() == FrontendScreen::select_menu,
           "a standalone match must return to a valid root menu");
}

void translated_draw_lists_preserve_spaces_and_deferred_geometry() {
    battlespades::ui::DrawList source;
    battlespades::ui::SpriteDrawCommand sprite{
        "design", {10.0, 20.0, 30.0, 40.0}};
    sprite.source_pixels =
        battlespades::ui::DrawRect{1.0, 2.0, 3.0, 4.0};
    sprite.clip_pixels =
        battlespades::ui::DrawRect{5.0, 6.0, 7.0, 8.0};
    source.push(sprite);
    battlespades::ui::TextDrawCommand text;
    text.localization_key = "window";
    text.destination = {50.0, 60.0, 70.0, 80.0};
    text.space = battlespades::ui::DrawSpace::window_pixels;
    source.push(text);
    battlespades::ui::PlayerNamePlateDrawRequest name_plate;
    name_plate.player_name = "Player";
    source.push(name_plate);

    battlespades::ui::DrawList translated;
    battlespades::ui::append_translated(translated, source, 800.0, 1'280.0);
    expect(translated.size() == 3U, "translation must retain every command");
    const auto commands = translated.commands();
    const auto& translated_sprite =
        std::get<battlespades::ui::SpriteDrawCommand>(commands[0]);
    expect(translated_sprite.destination.x == 810.0,
           "design commands must use the design-width offset");
    expect(translated_sprite.source_pixels == sprite.source_pixels,
           "source texture rectangles must never move with screen transitions");
    expect(translated_sprite.clip_pixels ==
               battlespades::ui::DrawRect{805.0, 6.0, 7.0, 8.0},
           "raster clips must travel with their translated destination");
    expect(std::get<battlespades::ui::TextDrawCommand>(commands[1]).destination.x == 1'330.0,
           "window commands must use the physical-width offset");
    expect(std::get<battlespades::ui::PlayerNamePlateDrawRequest>(commands[2])
               .window_offset_x == 1'280.0,
           "deferred name-plate geometry must retain the window translation");
}

void retail_main_menu_bounds_are_preserved_exactly() {
    const MainMenuModel menu;
    const auto controls = menu.controls();
    expect(controls.size() == MainMenuModel::control_count,
           "Select Menu must expose every retail control");

    expect(controls[0].widget.bounds == Rect{2136, 3560, 510, 510},
           "tutorial square must preserve fractional retail bounds");
    expect(controls[4].widget.bounds == Rect{2152, 2944, 2096, 464},
           "UGC text button must preserve retail coordinates");
    expect(controls[7].widget.bounds == Rect{2152, 1432, 2096, 464},
           "Join Match must preserve retail coordinates");
    expect(controls[8].widget.bounds == Rect{2908, 4336, 584, 208},
           "Quit must use the retail icon, text-width, and padding hit target");
    expect(controls[9].widget.bounds == Rect{5440, 4336, 880, 208},
           "Logout must align to the Quit navigation bar below the name plate");

    MainMenuModel hit_test_menu;
    hit_test_menu.pointer_move(Point{2152, 1432});
    expect(!hit_test_menu.hovered().has_value(),
           "recovered retail collision excludes the exact button edge");
    hit_test_menu.pointer_move(Point{2153, 1433});
    expect(hit_test_menu.hovered() == WidgetId{8U}, "first subpixel inside the button must hit");
}

void pointer_states_and_retail_release_activation_are_deterministic() {
    MainMenuModel menu;
    const Point join_center{3200, 1664};
    menu.pointer_move(join_center);
    expect(menu.hovered() == WidgetId{8U}, "Join Match should hover");
    expect(menu.visual_state(WidgetId{8U}) == WidgetVisualState::hovered,
           "hover should be externally renderable");

    menu.pointer_press(std::nullopt);
    menu.pointer_move(join_center);
    expect(menu.visual_state(WidgetId{8U}) == WidgetVisualState::pressed,
           "retail drag-in gesture should show pressed state");
    expect(menu.pointer_release(join_center) == MainMenuAction::join_match,
           "retail release-inside gesture should activate Join Match");
    expect(menu.focused() == WidgetId{8U}, "pointer activation should synchronize focus");

    menu.pointer_press(join_center);
    expect(!menu.pointer_release(std::nullopt).has_value(),
           "release outside the canvas must fail closed");
    expect(menu.visual_state(WidgetId{8U}) != WidgetVisualState::pressed,
           "cancelled pointer capture must not leave a control visually pressed");
    expect(!menu.hovered().has_value(),
           "cancelled pointer capture outside the canvas must clear hover");

    const Point quit_center{3200, 4440};
    menu.pointer_press(std::nullopt);
    expect(!menu.pointer_release(quit_center).has_value(),
           "navigation items require press and release inside");
    menu.pointer_press(quit_center);
    expect(menu.pointer_release(quit_center) == MainMenuAction::quit,
           "navigation item should activate after a captured press");

    const Point logout_center{5'800, 4'440};
    menu.pointer_press(logout_center);
    expect(menu.pointer_release(logout_center) == MainMenuAction::logout,
           "Logout must emit its own typed main-menu action");
}

void disabled_controls_cannot_activate_and_repair_focus() {
    MainMenuModel menu;
    const Point tutorial_center{2391, 3815};
    expect(menu.focused() == WidgetId{1U}, "source registration order starts at Tutorial");
    expect(menu.set_enabled(MainMenuAction::tutorial, false), "known action should update");
    expect(menu.focused() == WidgetId{2U}, "disabled focus should move predictably");
    expect(menu.visual_state(WidgetId{1U}) == WidgetVisualState::disabled,
           "disabled state must dominate hover and focus");

    menu.pointer_press(tutorial_center);
    expect(!menu.pointer_release(tutorial_center).has_value(),
           "disabled Tutorial must reject pointer activation");
}

void semantic_navigation_emits_typed_actions() {
    MainMenuModel menu;
    const auto next = menu.handle(InputEvent{
        InputAction::focus_next,
        InputPhase::pressed,
    });
    expect(!next.has_value(), "navigation should move focus without activating");
    expect(menu.focused() == WidgetId{2U}, "focus-next should follow source order");

    expect(menu.handle(InputEvent{
               InputAction::activate,
               InputPhase::pressed,
           }) == MainMenuAction::friends,
           "activation must emit a typed action");
    expect(!menu.handle(InputEvent{
                            InputAction::activate,
                            InputPhase::released,
                        })
                .has_value(),
           "release event must not double-activate");
}

void every_required_main_menu_asset_exists() {
#ifndef AOS_TEST_ASSET_ROOT
    throw std::runtime_error{"AOS_TEST_ASSET_ROOT is not defined"};
#else
    const std::filesystem::path root{AOS_TEST_ASSET_ROOT};
    const auto client_root = root.parent_path() / "client";
    const auto assets = battlespades::frontend::main_menu_assets::required();
    expect(assets.size() == 26U, "interactive English Select Menu needs 26 assets");
    for (const auto& asset : assets) {
        const auto path = root / std::filesystem::path{asset.path};
        const auto client_path = client_root / std::filesystem::path{asset.path};
        expect(std::filesystem::is_regular_file(path) ||
                   std::filesystem::is_regular_file(client_path),
               std::string{"missing required frontend asset: "} + path.string() +
                   " (also checked " + client_path.string() + ")");
    }
#endif
}

struct TestCase final {
    std::string_view name;
    std::function<void()> body;
};

} // namespace

int main() {
    const std::vector<TestCase> tests{
        {"live_packet_budget_bounds_authority_age_without_reordering",
         live_packet_budget_bounds_authority_age_without_reordering},
        {"canvas_preserves_aspect_and_rejects_letterbox_input",
         canvas_preserves_aspect_and_rejects_letterbox_input},
        {"canvas_uses_recovered_truncation_for_render_and_pointer_mapping",
         canvas_uses_recovered_truncation_for_render_and_pointer_mapping},
        {"canvas_text_rasterization_preserves_layout_and_uses_drawable_resolution",
         canvas_text_rasterization_preserves_layout_and_uses_drawable_resolution},
        {"frontend_shell_reproduces_slide_direction_and_input_gate",
         frontend_shell_reproduces_slide_direction_and_input_gate},
        {"frontend_navigation_pushes_and_pops_nested_routes_atomically",
         frontend_navigation_pushes_and_pops_nested_routes_atomically},
        {"gameplay_overlay_navigation_is_immediate",
         gameplay_overlay_navigation_is_immediate},
        {"friends_returns_to_the_existing_lobby_without_duplicate_routes",
         friends_returns_to_the_existing_lobby_without_duplicate_routes},
        {"match_departure_unwinds_overlays_without_transport_state",
         match_departure_unwinds_overlays_without_transport_state},
        {"translated_draw_lists_preserve_spaces_and_deferred_geometry",
         translated_draw_lists_preserve_spaces_and_deferred_geometry},
        {"retail_main_menu_bounds_are_preserved_exactly",
         retail_main_menu_bounds_are_preserved_exactly},
        {"pointer_states_and_retail_release_activation_are_deterministic",
         pointer_states_and_retail_release_activation_are_deterministic},
        {"disabled_controls_cannot_activate_and_repair_focus",
         disabled_controls_cannot_activate_and_repair_focus},
        {"semantic_navigation_emits_typed_actions", semantic_navigation_emits_typed_actions},
        {"every_required_main_menu_asset_exists", every_required_main_menu_asset_exists},
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
