#include "battlespades/frontend/settings_menu.hpp"
#include "battlespades/settings/graphics_presets.hpp"

#include <algorithm>
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

using battlespades::frontend::SettingsBindingRejectedEffect;
using battlespades::frontend::SettingsCloseCommand;
using battlespades::frontend::SettingsCommitCommand;
using battlespades::frontend::SettingsDefaultsCommand;
using battlespades::frontend::SettingsFavoriteServerCommand;
using battlespades::frontend::SettingsMenuContext;
using battlespades::frontend::SettingsMenuEffect;
using battlespades::frontend::SettingsMenuEnvironment;
using battlespades::frontend::SettingsMenuModel;
using battlespades::frontend::SettingsMenuPresentation;
using battlespades::frontend::SettingsMenuTarget;
using battlespades::frontend::SettingsPreviewEffect;
using battlespades::frontend::SettingsRestoreCommand;
using battlespades::frontend::SettingsRowId;
using battlespades::frontend::SettingsTargetKind;
using battlespades::settings::BindingAssignmentStatus;
using battlespades::settings::ControlAction;
using battlespades::settings::GraphicsApi;
using battlespades::settings::InputBinding;
using battlespades::settings::Resolution;
using battlespades::settings::SettingsSession;
using battlespades::settings::SettingsTab;
using battlespades::ui::InputAction;
using battlespades::ui::InputEvent;
using battlespades::ui::InputPhase;
using battlespades::ui::Point;
using battlespades::ui::Rect;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] const battlespades::frontend::SettingsRowPresentation&
row(const SettingsMenuPresentation& presentation, SettingsRowId id) {
    const auto found = std::find_if(presentation.rows.begin(),
                                    presentation.rows.end(),
                                    [id](const auto& candidate) { return candidate.id == id; });
    if (found == presentation.rows.end()) {
        throw std::runtime_error{"settings row missing from presentation"};
    }
    return *found;
}

template <typename Effect>
[[nodiscard]] const Effect* find_effect(const std::vector<SettingsMenuEffect>& effects) {
    for (const auto& effect : effects) {
        if (const auto* value = std::get_if<Effect>(&effect); value != nullptr) {
            return value;
        }
    }
    return nullptr;
}

void click(SettingsMenuModel& menu, Point point);

[[nodiscard]] SettingsSession windowed_session() {
    auto settings = battlespades::settings::retail_default_settings();
    settings.graphics.window_mode = battlespades::settings::WindowMode::windowed;
    return SettingsSession{settings};
}

[[nodiscard]] SettingsMenuEnvironment full_environment() {
    SettingsMenuEnvironment environment;
    environment.display_modes = {
        {1'920U, 1'080U},
        {320U, 200U},
        {1'280U, 720U},
        {1'920U, 1'080U},
    };
    environment.languages = {{"en", "English"}, {"ru", "Русский"}};
    return environment;
}

void main_inventory_and_geometry_match_retail() {
    SettingsSession session;
    SettingsMenuModel menu{session, full_environment()};
    const auto view = menu.presentation();

    expect(view.active_tab == SettingsTab::main, "Main must be the initial tab");
    expect(view.rows.size() == 9U,
           "Main must expose the existing rows, local skin/movement preferences and ability hints");
    const std::vector expected{
        SettingsRowId::language,
        SettingsRowId::master_volume,
        SettingsRowId::music_volume,
        SettingsRowId::invert_mouse,
        SettingsRowId::favorite_server,
        SettingsRowId::show_skins,
        SettingsRowId::show_other_skins,
        SettingsRowId::weapon_motion,
        SettingsRowId::ability_hints,
    };
    expect(view.rows.size() >= expected.size() &&
               !row(view, SettingsRowId::ability_hints).value_text.empty(),
           "the non-retail ability hints option must exist");
    expect(!SettingsSession{}.draft().main.ability_hints,
           "non-retail ability hints must default to off (decision D4)");
    for (std::size_t index{}; index < expected.size(); ++index) {
        expect(view.rows[index].id == expected[index], "Main row order changed");
        if(index<6U)expect(view.rows[index].visible, "existing Main rows must remain visible");
    }
    expect(row(view, SettingsRowId::language).value_text == "English" &&
               menu.set_focus(SettingsMenuTarget::for_row(SettingsRowId::language)) &&
               menu.handle(InputEvent{InputAction::navigate_right, InputPhase::pressed}) &&
               session.draft().main.language == "ru",
           "language selector must stage the locale id while displaying its native name");
    expect(view.panel_bounds == Rect{152, 133, 494, 293},
           "content frame must retain its converted retail bounds");
    expect(view.viewport_bounds == Rect{162, 143, 474, 273},
           "list viewport must retain its converted retail bounds");
    expect(view.tabs[0].bounds == Rect{152, 100, 164, 33} &&
               view.tabs[2].bounds == Rect{480, 100, 166, 33},
           "tab hit strip must preserve retail integer boundaries");
    expect(view.buttons[0].bounds == Rect{151, 434, 80, 30} &&
               view.buttons[1].bounds == Rect{152, 492, 240, 60} &&
               view.buttons[2].bounds == Rect{405, 492, 240, 60},
           "frontend footer geometry must match retail");
    expect(!row(view, SettingsRowId::favorite_server).enabled,
           "Favourite Server must remain transient and unavailable in frontend settings");
}

void graphics_capabilities_and_wheel_scrolling_are_deterministic() {
    SettingsSession session;
    SettingsMenuModel menu{session, full_environment()};
    menu.set_active_tab(SettingsTab::graphics);
    static_cast<void>(menu.take_effects());

    auto view = menu.presentation();
    expect(view.rows.size() == 33U,
           "full graphics capabilities must expose four groups of thirty-three rows");
    expect(view.rows.front().id == SettingsRowId::graphics_display_category &&
               view.rows[1U].id == SettingsRowId::window_mode &&
               view.rows[2U].id == SettingsRowId::resolution,
           "the Display group must lead with Window Mode and Resolution");
    expect(row(view, SettingsRowId::resolution).choice_count == 3U,
           "display modes must be filtered, deduplicated, sorted, and include current mode");
    expect(row(view, SettingsRowId::graphics_api).choice_count == 6U,
           "the complete cross-platform backend vocabulary must be selectable in tests");
    expect(!row(view, SettingsRowId::color_vision).visible,
           "the final graphics row must initially be below the viewport");
    expect(view.maximum_scroll_index > 0U, "the Graphics list must scroll");

    for (std::size_t step{}; step < view.maximum_scroll_index; ++step) {
        expect(menu.mouse_wheel(Point{200, 200}, -1), "wheel inside viewport must be consumed");
    }
    view = menu.presentation();
    expect(view.scroll_index == view.maximum_scroll_index,
           "wheel down must reach the final graphics row");
    expect(!row(view, SettingsRowId::window_mode).visible &&
               row(view, SettingsRowId::color_vision).visible,
           "scrolling must replace the first rows with the final rows");

    // Collapsing a group removes its rows from the list and the scroll range.
    const auto before = view.maximum_scroll_index;
    for (std::size_t step{}; step < before; ++step) {
        static_cast<void>(menu.mouse_wheel(Point{200, 200}, 1));
    }
    view = menu.presentation();
    const auto header = row(view, SettingsRowId::graphics_display_category);
    click(menu, Point{header.bounds.x + 20, header.bounds.y + header.bounds.height / 2});
    view = menu.presentation();
    expect(!row(view, SettingsRowId::graphics_display_category).expanded &&
               !row(view, SettingsRowId::window_mode).visible &&
               view.maximum_scroll_index < before,
           "a collapsed Graphics group hides its rows and shortens the list");

    SettingsMenuEnvironment limited;
    limited.multisampling_supported = false;
    limited.glsl_shader_quality_supported = false;
    limited.graphics_apis = {
        GraphicsApi::automatic,
        GraphicsApi::vulkan,
        GraphicsApi::metal,
        GraphicsApi::vulkan,
    };
    SettingsMenuModel limited_menu{session, limited};
    limited_menu.set_active_tab(SettingsTab::graphics);
    auto limited_view = limited_menu.presentation();
    expect(limited_view.rows.size() == 31U,
           "unsupported antialiasing and GLSL rows must be omitted, not disabled placeholders");
    expect(row(limited_view, SettingsRowId::graphics_api).choices ==
               std::vector<std::string>{"AUTO", "VULKAN", "METAL"},
           "backend choices must be de-duplicated without inventing platform APIs");

    expect(limited_menu.set_focus(SettingsMenuTarget::for_row(SettingsRowId::graphics_api)),
           "graphics API row must accept focus");
    expect(limited_menu.handle(InputEvent{InputAction::navigate_right, InputPhase::pressed}),
           "graphics API row must move to the next supported backend");
    expect(session.draft().graphics.graphics_api == GraphicsApi::vulkan,
           "graphics API selection must stage the exact backend");
    limited_view = limited_menu.presentation();
    expect(row(limited_view, SettingsRowId::graphics_api).value_text == "VULKAN",
           "graphics API row must display the staged backend");
}

void native_graphics_rows_edit_presets_and_report_why_they_are_unavailable() {
    using namespace battlespades::settings;
    SettingsSession session;
    SettingsMenuModel menu{session, full_environment()};
    menu.set_active_tab(SettingsTab::graphics);
    static_cast<void>(menu.take_effects());

    auto view = menu.presentation();
    expect(row(view, SettingsRowId::graphics_preset).value_text == "MEDIUM",
           "the shipped defaults read as the Medium preset");
    expect(row(view, SettingsRowId::field_of_view).value_text == "75" &&
               row(view, SettingsRowId::render_scale).value_text == "100%" &&
               row(view, SettingsRowId::frame_limit).value_text == "FRAME_LIMIT_DISPLAY" &&
               row(view, SettingsRowId::motion_blur).value_text == "OFF" &&
               row(view, SettingsRowId::gamma).value_text == "1.0" &&
               row(view, SettingsRowId::brightness).value_text == "0%",
           "every native row must open at the value that reproduces the old renderer");
    expect(!row(view, SettingsRowId::upscale).enabled &&
               row(view, SettingsRowId::upscale).description == "UPSCALE_FULL_RESOLUTION",
           "upscaling is unavailable at 100% render scale and says why");
    expect(row(view, SettingsRowId::low_latency).description == "RESTART_REQUIRED",
           "frame latency is fixed when bgfx starts");

    const auto step = [&menu](SettingsRowId target, InputAction action) {
        expect(menu.set_focus(SettingsMenuTarget::for_row(target)),
               std::string{settings_row_name(target)} + " must accept focus");
        expect(menu.handle(InputEvent{action, InputPhase::pressed}),
               std::string{settings_row_name(target)} + " must step");
    };
    step(SettingsRowId::field_of_view, InputAction::navigate_right);
    expect(session.draft().graphics.field_of_view == 80.0, "FOV steps by five degrees");
    step(SettingsRowId::render_scale, InputAction::navigate_left);
    expect(session.draft().graphics.render_scale == 0.85, "render scale steps down");
    expect(menu.presentation().rows.size() > 0U &&
               row(menu.presentation(), SettingsRowId::upscale).enabled,
           "upscaling becomes available below 100%");
    step(SettingsRowId::frame_limit, InputAction::navigate_right);
    expect(session.draft().graphics.frame_limit == FrameLimit::custom &&
               session.draft().graphics.frame_rate_cap == 60U,
           "the limiter's first custom step is 60 fps");
    step(SettingsRowId::color_vision, InputAction::navigate_right);
    expect(session.draft().graphics.color_vision == ColorVision::protanopia,
           "colour vision steps to protanopia");
    expect(find_effect<SettingsPreviewEffect>(menu.take_effects()) != nullptr,
           "native rows emit live previews");

    step(SettingsRowId::graphics_preset, InputAction::navigate_right);
    expect(session.draft().graphics.shader_quality == ShaderQuality::high &&
               session.draft().graphics.ambient_occlusion == EffectLevel::medium &&
               session.draft().graphics.bloom == EffectLevel::low,
           "Medium -> High writes the High preset");
    expect(session.draft().graphics.field_of_view == 80.0 &&
               session.draft().graphics.render_scale == 0.85,
           "a preset never touches personal display choices");
    step(SettingsRowId::ambient_occlusion, InputAction::navigate_right);
    expect(row(menu.presentation(), SettingsRowId::graphics_preset).value_text == "PRESET_CUSTOM",
           "a hand-edited quality field reads Custom");
    step(SettingsRowId::graphics_preset, InputAction::navigate_left);
    expect(matching_graphics_preset(session.draft().graphics) == GraphicsPreset::medium,
           "leaving Custom lands on Medium");

    // The Retail tier keeps its look: Enhanced effects grey out with a reason.
    step(SettingsRowId::graphics_preset, InputAction::navigate_left);
    step(SettingsRowId::graphics_preset, InputAction::navigate_left);
    expect(session.draft().graphics.compatibility_shader(), "the first preset is Retail");
    view = menu.presentation();
    for (const auto id : {SettingsRowId::ambient_occlusion, SettingsRowId::bloom,
                          SettingsRowId::motion_blur, SettingsRowId::shadow_quality}) {
        expect(!row(view, id).enabled && row(view, id).description == "ENHANCED_ONLY",
               std::string{settings_row_name(id)} + " is Enhanced-only in the Retail tier");
    }
    expect(row(view, SettingsRowId::brightness).enabled &&
               row(view, SettingsRowId::color_vision).enabled,
           "accessibility rows stay available in the Retail tier");

    // A backend without the post chain disables its rows with a reason.
    auto environment = full_environment();
    environment.post_chain_supported = false;
    SettingsMenuModel unsupported{session, environment};
    unsupported.set_active_tab(SettingsTab::graphics);
    view = unsupported.presentation();
    for (const auto id : {SettingsRowId::render_scale, SettingsRowId::sharpness,
                          SettingsRowId::gamma, SettingsRowId::color_vision}) {
        expect(!row(view, id).enabled && row(view, id).description == "NOT_SUPPORTED_BACKEND",
               std::string{settings_row_name(id)} + " explains an unsupported backend");
    }
}

void window_mode_row_cycles_and_greys_out_resolution_when_borderless() {
    using battlespades::settings::WindowMode;
    SettingsSession session;
    SettingsMenuModel menu{session, full_environment()};
    menu.set_active_tab(SettingsTab::graphics);
    static_cast<void>(menu.take_effects());
    auto view = menu.presentation();
    expect(row(view, SettingsRowId::window_mode).value_text == "WINDOW_MODE_BORDERLESS" &&
               !row(view, SettingsRowId::resolution).enabled &&
               row(view, SettingsRowId::resolution).value_text == "WINDOW_MODE_DESKTOP",
           "borderless covers the desktop, so Resolution reads Desktop and is disabled");
    expect(menu.set_focus(SettingsMenuTarget::for_row(SettingsRowId::window_mode)),
           "Window Mode must accept focus");
    expect(menu.handle(InputEvent{InputAction::activate, InputPhase::pressed}),
           "activating Window Mode cycles it");
    expect(session.draft().graphics.window_mode == WindowMode::exclusive,
           "Borderless -> Fullscreen");
    expect(row(menu.presentation(), SettingsRowId::resolution).enabled,
           "exclusive fullscreen uses the chosen resolution");
    expect(menu.handle(InputEvent{InputAction::activate, InputPhase::pressed}) &&
               session.draft().graphics.window_mode == WindowMode::windowed,
           "Fullscreen -> Windowed");
    static_cast<void>(menu.take_effects());
    menu.activate_done();
    const auto* commit = find_effect<SettingsCommitCommand>(menu.take_effects());
    expect(commit != nullptr && commit->display_changed,
           "a window mode change goes through the keep/revert prompt");
}

void every_renderer_tier_is_reachable_and_legacy_stays_deliberate() {
    using battlespades::settings::ShaderQuality;
    SettingsSession session;
    SettingsMenuModel menu{session, full_environment()};
    menu.set_active_tab(SettingsTab::graphics);
    static_cast<void>(menu.take_effects());

    const auto tier = [&session] { return session.draft().graphics.shader_quality; };
    const auto shader_row = SettingsMenuTarget::for_row(SettingsRowId::shader_quality);
    const auto toggle_row = SettingsMenuTarget::for_row(SettingsRowId::compatibility_shader);

    // Drive the rows the way a player does: focus, then left/right.
    const auto step_tier = [&menu](InputAction action) {
        return menu.handle(InputEvent{action, InputPhase::pressed});
    };
    expect(menu.set_focus(shader_row), "the tier row must accept focus");

    expect(tier() == ShaderQuality::medium, "the default tier must be medium");
    static_cast<void>(step_tier(InputAction::navigate_left));
    expect(tier() == ShaderQuality::low, "left from medium must reach low");
    static_cast<void>(step_tier(InputAction::navigate_right));
    static_cast<void>(step_tier(InputAction::navigate_right));
    expect(tier() == ShaderQuality::high, "two rights from low must reach high");
    static_cast<void>(step_tier(InputAction::navigate_right));
    expect(tier() == ShaderQuality::ultra, "ultra must be reachable from the row");
    expect(row(menu.presentation(), SettingsRowId::shader_quality).value_text == "ULTRA",
           "the row must display the tier it selected");

    // shifted_index saturates rather than wrapping, so the top dead-ends
    // instead of cycling back around to low.
    static_cast<void>(step_tier(InputAction::navigate_right));
    expect(tier() == ShaderQuality::ultra, "the tier row must saturate, not wrap");

    // Legacy is reachable only through the recovered toggle. Cycling the tier
    // row must never produce it, or the parity path becomes an accident.
    for (int index{}; index < 8; ++index) {
        static_cast<void>(step_tier(InputAction::navigate_left));
        expect(tier() != ShaderQuality::compatibility,
               "cycling the tier row must never select the legacy parity path");
    }

    // Back to the top, then hand the field to the toggle.
    for (int index{}; index < 4; ++index) {
        static_cast<void>(step_tier(InputAction::navigate_right));
    }
    expect(tier() == ShaderQuality::ultra, "precondition: back at ultra");

    expect(menu.set_focus(toggle_row), "the toggle row must accept focus");
    static_cast<void>(step_tier(InputAction::navigate_right));
    expect(tier() == ShaderQuality::compatibility, "the toggle must select the legacy parity tier");
    {
        const auto view = menu.presentation();
        expect(!row(view, SettingsRowId::shader_quality).enabled,
               "the tier row must grey out while the toggle owns the field");
        expect(row(view, SettingsRowId::shader_quality).value_text == "RETAIL",
               "a disabled tier row must read RETAIL, not a stale tier");
        expect(row(view, SettingsRowId::compatibility_shader).enabled,
               "the toggle itself must stay live");
    }

    // Turning it back off must restore what the player chose, not snap to high.
    static_cast<void>(step_tier(InputAction::navigate_left));
    expect(tier() == ShaderQuality::ultra,
           "switching the toggle off must restore the selected tier, not force high");
    expect(row(menu.presentation(), SettingsRowId::shader_quality).enabled,
           "the tier row must come back live");
}

void resolution_dropdown_opens_scrolls_selects_and_closes_outside() {
    auto session = windowed_session();
    SettingsMenuEnvironment environment;
    environment.display_modes = {
        {640U, 480U},
        {800U, 600U},
        {900U, 600U},
        {1'000U, 700U},
        {1'100U, 700U},
        {1'200U, 800U},
        {1'280U, 720U},
        {1'366U, 768U},
        {1'440U, 900U},
        {1'600U, 900U},
        {1'920U, 1'080U},
        {2'560U, 1'440U},
    };
    SettingsMenuModel menu{session, environment};
    menu.set_active_tab(SettingsTab::graphics);
    static_cast<void>(menu.take_effects());

    auto resolution = row(menu.presentation(), SettingsRowId::resolution);
    const Point title{resolution.control_bounds.x + resolution.control_bounds.width / 2,
                      resolution.control_bounds.y + resolution.control_bounds.height / 2};
    menu.pointer_press(title);
    menu.pointer_release(title);
    resolution = row(menu.presentation(), SettingsRowId::resolution);
    expect(resolution.dropdown_open && resolution.dropdown_visible_count == 10U,
           "Resolution must open the retail ten-row DropBoxControl, not cycle as a range bar");
    expect(resolution.dropdown_first_index == 1U,
           "opening a DropBoxControl must reveal its selected resolution");

    const Point option_panel{resolution.control_bounds.x + 10,
                             resolution.control_bounds.y + resolution.control_bounds.height + 10};
    expect(menu.mouse_wheel(option_panel, -1), "an open resolution list must own wheel scrolling");
    resolution = row(menu.presentation(), SettingsRowId::resolution);
    expect(resolution.dropdown_first_index == 2U,
           "resolution wheel scrolling must advance the bounded ten-row window");

    constexpr std::size_t local_option{2U};
    const Point option{resolution.control_bounds.x + 10,
                       resolution.control_bounds.y + resolution.control_bounds.height +
                           static_cast<std::int32_t>(local_option * 20U + 10U)};
    menu.pointer_press(option);
    menu.pointer_release(option);
    expect(!row(menu.presentation(), SettingsRowId::resolution).dropdown_open,
           "selecting a resolution must close its dropdown");
    expect(session.draft().graphics.resolution == environment.display_modes[4U],
           "the selected dropdown row must stage the exact display mode");

    resolution = row(menu.presentation(), SettingsRowId::resolution);
    menu.pointer_press(title);
    menu.pointer_release(title);
    expect(row(menu.presentation(), SettingsRowId::resolution).dropdown_open,
           "resolution dropdown must remain reusable after a selection");
    menu.pointer_press(Point{100, 200});
    menu.pointer_release(Point{100, 200});
    expect(!row(menu.presentation(), SettingsRowId::resolution).dropdown_open,
           "pressing outside an open DropBoxControl must close without changing a setting");
}

void scrollbar_arrows_track_and_thumb_drive_the_model() {
    SettingsSession session;
    SettingsMenuModel menu{session};
    menu.set_active_tab(SettingsTab::controls);
    static_cast<void>(menu.take_effects());
    const auto maximum = menu.presentation().maximum_scroll_index;
    expect(maximum > 4U, "Controls precondition requires a meaningful scroll range");

    menu.pointer_press(Point{625, 405});
    menu.pointer_release(Point{625, 405});
    expect(menu.presentation().scroll_index == 1U,
           "the rendered down arrow must advance one logical row");

    menu.pointer_press(Point{625, 380});
    menu.pointer_release(Point{625, 380});
    expect(menu.presentation().scroll_index > 1U,
           "clicking the rendered scrollbar track must move toward that position");

    static_cast<void>(menu.mouse_wheel(Point{200, 200}, 100));
    expect(menu.presentation().scroll_index == 0U, "wheel reset precondition failed");
    menu.pointer_press(Point{625, 180});
    menu.pointer_drag(Point{625, 390});
    menu.pointer_release(Point{625, 390});
    expect(menu.presentation().scroll_index == maximum,
           "dragging the rendered thumb must reach the final legal row offset");

    menu.pointer_press(Point{625, 154});
    menu.pointer_release(Point{625, 154});
    expect(menu.presentation().scroll_index + 1U == maximum,
           "the rendered up arrow must retreat one logical row");
}

void in_game_graphics_apply_live() {
    // Native: retail locked this tab in a match. Every row now applies live
    // or is marked RESTART_REQUIRED (settings/graphics_apply.hpp).
    auto session = windowed_session();
    auto environment = full_environment();
    environment.context = SettingsMenuContext::in_game;
    SettingsMenuModel menu{session, environment};
    menu.set_active_tab(SettingsTab::graphics);
    const auto view = menu.presentation();

    expect(view.in_game && view.tooltip_key == "SETTINGS_MESSAGE",
           "in-game graphics must no longer claim to be unchangeable");
    expect(row(view, SettingsRowId::resolution).enabled &&
               row(view, SettingsRowId::vsync).enabled &&
               row(view, SettingsRowId::antialiasing).enabled &&
               row(view, SettingsRowId::draw_distance).enabled,
           "in-game graphics rows must be editable");
    expect(view.buttons[0].enabled, "Defaults must be available with in-game graphics");
    expect(view.buttons[1].bounds == Rect{160, 480, 232, 41} &&
               view.buttons[2].bounds == Rect{403, 480, 232, 41},
           "in-game footer must use its compact retail geometry");
    expect(menu.set_focus(SettingsMenuTarget::for_row(SettingsRowId::resolution)),
           "accessibility focus must reach in-game graphics rows");
}

void direct3d_antialiasing_reads_restart_required() {
    SettingsSession session;
    auto environment = full_environment();
    environment.multisampling_live = false;
    SettingsMenuModel menu{session, environment};
    menu.set_active_tab(SettingsTab::graphics);
    expect(row(menu.presentation(), SettingsRowId::antialiasing).description == "RESTART_REQUIRED",
           "Antialiasing must say it applies after a restart where it cannot apply live");

    SettingsSession live_session;
    SettingsMenuModel live{live_session, full_environment()};
    live.set_active_tab(SettingsTab::graphics);
    expect(row(live.presentation(), SettingsRowId::antialiasing).description.empty(),
           "a backend that resets MSAA in place needs no restart label");

    // Done must not claim a restart for a change that applies live; the
    // frontend adds a deferred MSAA notice from the renderer itself.
    auto draft = session.draft();
    draft.graphics.antialiasing = battlespades::settings::Antialiasing::samples_4;
    draft.graphics.vsync = !draft.graphics.vsync;
    session.set_graphics(draft.graphics);
    static_cast<void>(menu.take_effects());
    menu.activate_done();
    const auto effects = menu.take_effects();
    const auto* commit = find_effect<SettingsCommitCommand>(effects);
    expect(commit != nullptr && commit->changed && !commit->restart_required,
           "MSAA/VSync commit must not flag a startup-resource restart");

    draft = session.draft();
    draft.graphics.texture_quality = battlespades::settings::QualityLevel::low;
    session.set_graphics(draft.graphics);
    menu.activate_done();
    const auto texture_effects = menu.take_effects();
    const auto* texture_commit = find_effect<SettingsCommitCommand>(texture_effects);
    expect(texture_commit != nullptr && texture_commit->restart_required,
           "texture quality still requires a restart");
}

void controls_inventory_collapse_and_scroll_preserve_order() {
    SettingsSession session;
    SettingsMenuModel menu{session};
    menu.set_active_tab(SettingsTab::controls);
    static_cast<void>(menu.take_effects());
    auto view = menu.presentation();

    expect(view.rows.size() == 39U, "Controls must retain both categories and all 37 rows");
    expect(view.rows.front().id == SettingsRowId::ugc_controls_category &&
               view.rows[1].id == SettingsRowId::ugc_settings &&
               view.rows[10].id == SettingsRowId::quick_save &&
               view.rows[11].id == SettingsRowId::main_controls_category &&
               view.rows[12].id == SettingsRowId::mouse_sensitivity &&
               view.rows[21].id == SettingsRowId::fire_use &&
               view.rows.back().id == SettingsRowId::toggle_hud,
           "Controls row order must match the shipped Python 2 retail ordering");
    expect(row(view, SettingsRowId::fire_use).value_text == "LMB" &&
               row(view, SettingsRowId::cycle_next_weapon).value_text == "MOUSE_WHEEL" &&
               row(view, SettingsRowId::inventory_slots).value_text == "1-9",
           "non-configurable retail helper rows must remain visible");
    expect(row(view, SettingsRowId::mouse_sensitivity).value_text == "0.1",
           "mouse sensitivity must use retail's str(round(value, 2)) edit-box text");

    expect(menu.set_category_expanded(SettingsRowId::ugc_controls_category, false),
           "Map Creator category must collapse");
    view = menu.presentation();
    expect(!row(view, SettingsRowId::ugc_settings).visible,
           "collapsed children must leave layout without leaving inventory");
    expect(row(view, SettingsRowId::main_controls_category).visible,
           "the next category must move into the visible viewport");
    expect(!menu.category_expanded(SettingsRowId::ugc_controls_category),
           "collapsed state must be queryable");
}

void semantic_navigation_adjusts_values_and_reveals_focus() {
    SettingsSession session;
    SettingsMenuModel menu{session};
    expect(menu.set_focus(SettingsMenuTarget::for_row(SettingsRowId::master_volume)),
           "visible enabled row must accept focus");
    expect(menu.handle(InputEvent{InputAction::navigate_left, InputPhase::pressed}),
           "left must be consumed by the focused range bar");
    expect(session.draft().main.master_volume == 0.8,
           "keyboard range-bar adjustment must use retail 0.2 steps");
    expect(menu.handle(InputEvent{InputAction::navigate_right, InputPhase::repeated}),
           "repeated right must adjust the row");
    expect(session.draft().main.master_volume == 1.0, "range bar must clamp and return to one");

    menu.set_active_tab(SettingsTab::controls);
    expect(menu.set_focus(SettingsMenuTarget::for_row(SettingsRowId::quick_save)),
           "offscreen control must accept accessibility focus");
    const auto view = menu.presentation();
    expect(row(view, SettingsRowId::quick_save).visible && view.scroll_index > 0U,
           "keyboard focus must automatically reveal an offscreen row");
    expect(menu.handle(InputEvent{InputAction::focus_next, InputPhase::pressed}),
           "Tab navigation must remain cyclic and semantic");
}

void pointer_hit_testing_tabs_and_slider_drag_edit_the_draft() {
    SettingsSession session;
    SettingsMenuModel menu{session};
    menu.pointer_press(Point{320, 110});
    menu.pointer_release(Point{320, 110});
    expect(menu.active_tab() == SettingsTab::graphics,
           "retail tab hit strip must switch tabs on pointer press");

    menu.set_active_tab(SettingsTab::main);
    static_cast<void>(menu.take_effects());
    const auto volume = row(menu.presentation(), SettingsRowId::master_volume);
    const Point middle{volume.control_bounds.x + volume.control_bounds.width / 2,
                       volume.control_bounds.y + volume.control_bounds.height / 2};
    menu.pointer_press(middle);
    // RangeBarControl maps only the bar between its arrows: x1 = x + 4 +
    // arrow + 2 and x2 mirrors it, with arrow = height - 8.
    const auto arrow = volume.control_bounds.height - 8;
    const auto bar_left = volume.control_bounds.x + 4 + arrow + 2;
    const auto bar_right =
        volume.control_bounds.x + volume.control_bounds.width - (4 + arrow + 2);
    const Point quarter{bar_left + (bar_right - bar_left) / 4, middle.y};
    menu.pointer_drag(quarter);
    menu.pointer_release(quarter);
    expect(session.draft().main.master_volume > 0.24 && session.draft().main.master_volume < 0.26,
           "range bar pointer capture must continuously update the normalized value");
    const auto invert = row(menu.presentation(), SettingsRowId::invert_mouse);
    const Point right_arrow{invert.control_bounds.x + invert.control_bounds.width - 1,
                            invert.control_bounds.y + invert.control_bounds.height / 2};
    menu.pointer_press(right_arrow);
    menu.pointer_release(right_arrow);
    expect(session.draft().main.invert_mouse,
           "pointer choice controls must route their right arrow to the next option");
    expect(find_effect<SettingsPreviewEffect>(menu.take_effects()) != nullptr,
           "value edits must emit a typed live-preview effect");
}

void raw_binding_capture_rejects_reserved_and_duplicate_scancodes() {
    SettingsSession session;
    SettingsMenuModel menu{session};
    menu.set_active_tab(SettingsTab::controls);
    expect(menu.set_focus(SettingsMenuTarget::for_row(SettingsRowId::forward)),
           "Forward binding must accept focus");
    expect(menu.handle(InputEvent{InputAction::activate, InputPhase::pressed}),
           "activating a binding must enter capture");
    expect(menu.presentation().binding_capture.has_value(),
           "capture state must be visible to presentation/accessibility");

    const auto reserved = menu.capture_scancode(30U);
    expect(reserved.status == BindingAssignmentStatus::reserved_inventory_key,
           "SDL number-row scancodes must be reserved for inventory");
    expect(menu.presentation().binding_capture->rejection.has_value(),
           "rejection must remain visible while capture stays active");

    const auto duplicate = menu.capture_scancode(22U);
    expect(duplicate.status == BindingAssignmentStatus::conflicts_with_existing_action &&
               duplicate.conflicting_action == ControlAction::backward,
           "duplicate binding must identify the conflicting action");
    const auto accepted = menu.capture_scancode(40U);
    expect(accepted.accepted() && session.draft().controls.binding(ControlAction::forward) ==
                                      InputBinding::keyboard(40U),
           "an unused raw SDL scancode must update the session draft");
    expect(!menu.presentation().binding_capture.has_value(),
           "successful assignment must end capture");

    expect(menu.set_focus(SettingsMenuTarget::for_row(SettingsRowId::backward)),
           "a second binding row must accept focus");
    expect(menu.handle(InputEvent{InputAction::activate, InputPhase::pressed}),
           "mouse-binding precondition must enter raw capture");
    const auto mouse_accepted = menu.capture_mouse_button(5U);
    expect(mouse_accepted.accepted() &&
               session.draft().controls.binding(ControlAction::backward) == InputBinding::mouse(5U),
           "raw mouse buttons must be accepted through the same conflict gate");

    const auto effects = menu.take_effects();
    expect(find_effect<SettingsBindingRejectedEffect>(effects) != nullptr,
           "binding rejection must be emitted as a typed effect");
    expect(find_effect<SettingsPreviewEffect>(effects) != nullptr,
           "successful binding must emit a preview/value-changed effect");
}

void defaults_done_and_cancel_are_transactional_typed_commands() {
    auto session = windowed_session();
    SettingsMenuModel menu{session, full_environment()};
    expect(menu.set_focus(SettingsMenuTarget::for_row(SettingsRowId::invert_mouse)),
           "Invert Mouse must accept focus");
    expect(menu.handle(InputEvent{InputAction::activate, InputPhase::pressed}),
           "activation must toggle Invert Mouse");
    expect(session.draft().main.invert_mouse, "toggle must edit draft before apply");
    static_cast<void>(menu.take_effects());
    menu.activate_defaults();
    expect(!session.draft().main.invert_mouse,
           "Defaults must reset only the active tab to retail values");
    expect(find_effect<SettingsDefaultsCommand>(menu.take_effects()) != nullptr,
           "Defaults must emit an explicit typed command");

    menu.set_active_tab(SettingsTab::graphics);
    static_cast<void>(menu.take_effects());
    expect(menu.set_focus(SettingsMenuTarget::for_row(SettingsRowId::resolution)),
           "Resolution must accept focus");
    expect(menu.handle(InputEvent{InputAction::navigate_right, InputPhase::pressed}),
           "resolution choice must move right");
    expect(menu.set_focus(SettingsMenuTarget::for_row(SettingsRowId::antialiasing)),
           "Antialiasing must accept focus");
    expect(menu.handle(InputEvent{InputAction::navigate_right, InputPhase::pressed}),
           "antialiasing choice must move right");
    expect(menu.set_focus(SettingsMenuTarget::for_row(SettingsRowId::model_quality)),
           "Model Quality must accept focus");
    expect(menu.handle(InputEvent{InputAction::navigate_left, InputPhase::pressed}),
           "model quality must move to a restart-sensitive tier");
    static_cast<void>(menu.take_effects());
    menu.activate_done();
    const auto committed_effects = menu.take_effects();
    const auto* commit = find_effect<SettingsCommitCommand>(committed_effects);
    expect(commit != nullptr && commit->changed && commit->display_changed &&
               commit->restart_required,
           "Done must describe resolution preview and restart-sensitive changes");
    const auto* committed_close = find_effect<SettingsCloseCommand>(committed_effects);
    expect(committed_close != nullptr && committed_close->committed,
           "Done must close with committed=true");
    expect(session.committed() == session.draft(), "Done must commit the complete draft");

    expect(menu.set_focus(SettingsMenuTarget::for_row(SettingsRowId::antialiasing)),
           "committed model remains reusable until controller closes it");
    expect(menu.handle(InputEvent{InputAction::navigate_right, InputPhase::pressed}),
           "a second draft edit must be possible");
    static_cast<void>(menu.take_effects());
    menu.activate_cancel();
    const auto cancelled_effects = menu.take_effects();
    expect(session.draft() == session.committed(), "Cancel must discard every draft edit");
    expect(find_effect<SettingsRestoreCommand>(cancelled_effects) != nullptr,
           "Cancel must request runtime rollback of live previews");
    const auto* close = find_effect<SettingsCloseCommand>(cancelled_effects);
    expect(close != nullptr && !close->committed, "Cancel must close with committed=false");
}

void live_graphics_rows_do_not_claim_a_restart() {
    SettingsSession session;
    SettingsMenuModel menu{session, full_environment()};
    menu.set_active_tab(SettingsTab::graphics);
    static_cast<void>(menu.take_effects());

    expect(menu.set_focus(SettingsMenuTarget::for_row(SettingsRowId::antialiasing)) &&
               menu.handle(InputEvent{InputAction::navigate_right, InputPhase::pressed}),
           "MSAA must be editable");
    expect(menu.set_focus(SettingsMenuTarget::for_row(SettingsRowId::shader_quality)) &&
               menu.handle(InputEvent{InputAction::navigate_right, InputPhase::pressed}),
           "Shader Quality must be editable");
    static_cast<void>(menu.take_effects());
    menu.activate_done();
    const auto effects = menu.take_effects();
    const auto* commit = find_effect<SettingsCommitCommand>(effects);
    expect(commit != nullptr && commit->changed && !commit->restart_required,
           "live MSAA and shader changes must not show a false restart warning");
}

void favorite_server_is_transient_and_commits_only_on_done() {
    SettingsSession session;
    SettingsMenuEnvironment environment;
    environment.context = SettingsMenuContext::in_game;
    environment.favorite_server_available = true;
    environment.favorite_server = false;
    environment.favorite_server_description = "Test Server";
    SettingsMenuModel menu{session, environment};
    expect(menu.set_focus(SettingsMenuTarget::for_row(SettingsRowId::favorite_server)),
           "available live server favourite must accept focus");
    expect(menu.handle(InputEvent{InputAction::activate, InputPhase::pressed}),
           "favourite row must toggle");
    expect(row(menu.presentation(), SettingsRowId::favorite_server).value_text == "ON",
           "favourite draft must update independently from ClientSettings");
    menu.activate_done();
    const auto effects = menu.take_effects();
    const auto* favorite = find_effect<SettingsFavoriteServerCommand>(effects);
    expect(favorite != nullptr && favorite->favorite,
           "Done must emit the separate server-browser favourite command");
}

void skin_preferences_are_reachable_live_and_cancelable() {
    auto environment=full_environment();
    environment.context=SettingsMenuContext::in_game;
    SettingsSession session;
    SettingsMenuModel menu{session,environment};
    for(const auto id:{SettingsRowId::show_other_skins,SettingsRowId::show_skins,SettingsRowId::weapon_motion}){
        expect(menu.set_focus(SettingsMenuTarget::for_row(id)),"skin preference must be focusable in game");
        expect(row(menu.presentation(),id).visible,"focusing lower Main settings must scroll them into view");
        expect(menu.handle(InputEvent{InputAction::activate,InputPhase::pressed}),"skin toggle must activate");
        const auto effects=menu.take_effects();
        const auto* preview=find_effect<SettingsPreviewEffect>(effects);
        expect(preview&&preview->source==id,"skin toggle must emit a live preview");
    }
    expect(!session.draft().main.show_skins&&!session.draft().main.show_other_skins&&!session.draft().main.weapon_motion,
           "all three preferences must stage independently");
    menu.activate_cancel();
    expect(session.draft().main.show_skins&&session.draft().main.show_other_skins&&session.draft().main.weapon_motion,
           "Cancel must restore all three preferences");
    expect(find_effect<SettingsRestoreCommand>(menu.take_effects())!=nullptr,"Cancel must restore live appearance");
    expect(menu.set_focus(SettingsMenuTarget::for_row(SettingsRowId::show_other_skins)),"mine-only control must remain reachable");
    static_cast<void>(menu.handle(InputEvent{InputAction::activate,InputPhase::pressed}));
    static_cast<void>(menu.take_effects());
    menu.activate_done();
    const auto effects=menu.take_effects();
    const auto* commit=find_effect<SettingsCommitCommand>(effects);
    expect(commit&&commit->changed&&!commit->restart_required&&!commit->display_changed,
           "skin preferences must apply without restarting the renderer");
    expect(session.committed().main.show_skins&&!session.committed().main.show_other_skins,
           "Done must persist mine-only without switching off own skin");
}

[[nodiscard]] Point control_point(const battlespades::frontend::SettingsRowPresentation& item,
                                  int x) {
    return {x, item.control_bounds.y + item.control_bounds.height / 2};
}

void click(SettingsMenuModel& menu, Point point) {
    menu.pointer_press(point);
    menu.pointer_release(point);
}

[[nodiscard]] bool has_sound(const std::vector<SettingsMenuEffect>& effects,
                             battlespades::frontend::SettingsMenuSound sound) {
    return std::ranges::any_of(effects, [sound](const SettingsMenuEffect& effect) {
        const auto* value = std::get_if<battlespades::frontend::SettingsSoundEffect>(&effect);
        return value != nullptr && value->sound == sound;
    });
}

void range_bar_arrows_step_without_rounding_and_grey_out_at_the_ends() {
    SettingsSession session;
    auto main = session.draft().main;
    main.master_volume = 0.37;
    session.set_main(main);
    SettingsMenuModel menu{session};
    const auto volume = row(menu.presentation(), SettingsRowId::master_volume);
    const auto left_arrow = control_point(volume, volume.control_bounds.x + 2);
    const auto right_arrow =
        control_point(volume, volume.control_bounds.x + volume.control_bounds.width - 2);

    click(menu, left_arrow);
    expect(std::abs(session.draft().main.master_volume - 0.17) < 1.0e-9,
           "the left arrow must step 0.2 down without rounding to a fifth");
    click(menu, left_arrow);
    expect(session.draft().main.master_volume == 0.0,
           "a step below 0.01 must snap to silence");
    click(menu, left_arrow);
    expect(session.draft().main.master_volume == 0.0, "the left arrow is disabled at zero");
    click(menu, right_arrow);
    expect(std::abs(session.draft().main.master_volume - 0.2) < 1.0e-9,
           "the right arrow must step 0.2 up");

    // Pressing an arrow and releasing elsewhere does nothing (SquareButton).
    menu.pointer_press(right_arrow);
    menu.pointer_release(control_point(volume, volume.control_bounds.x + 2));
    expect(std::abs(session.draft().main.master_volume - 0.2) < 1.0e-9,
           "an arrow fires only when released over itself");

    const auto arrow = volume.control_bounds.height - 8;
    const auto bar_left = volume.control_bounds.x + 4 + arrow + 2;
    const auto bar_right =
        volume.control_bounds.x + volume.control_bounds.width - (4 + arrow + 2);
    click(menu, control_point(volume, bar_right));
    expect(session.draft().main.master_volume == 1.0, "the bar's right end must be full volume");
    click(menu, control_point(volume, bar_left));
    expect(session.draft().main.master_volume == 0.0, "the bar's left end must be silence");
}

void sensitivity_track_maps_and_its_box_takes_typed_values() {
    SettingsSession session;
    SettingsMenuModel menu{session};
    menu.set_active_tab(SettingsTab::controls);
    expect(menu.set_focus(SettingsMenuTarget::for_row(SettingsRowId::mouse_sensitivity)),
           "sensitivity must be reachable");
    auto slider = row(menu.presentation(), SettingsRowId::mouse_sensitivity);
    expect(slider.value_text == "0.1", "the box shows str(round(0.1, 2)), not 0.10");

    // SliderControl: track from x + 8, width w - w/6 - 20.
    const auto width = static_cast<double>(slider.control_bounds.width);
    const auto track_x = static_cast<double>(slider.control_bounds.x) + 8.0;
    const auto track_width = width - width / 6.0 - 20.0;
    click(menu, control_point(slider, static_cast<int>(std::ceil(track_x + track_width))));
    expect(session.draft().controls.mouse_sensitivity == 1.0,
           "the end of the track, not of the control, is 1.0");
    click(menu, control_point(slider, static_cast<int>(track_x)));
    expect(session.draft().controls.mouse_sensitivity == 0.0, "the track start is 0.0");

    // The right sixth is the edit box: a click there types instead of sliding.
    const auto box_x = static_cast<double>(slider.control_bounds.x) + width - 4.0 - width / 6.0;
    click(menu, control_point(slider, static_cast<int>(box_x + width / 12.0)));
    expect(menu.text_editing(), "clicking the box must focus it for typing");
    expect(session.draft().controls.mouse_sensitivity == 0.0, "focusing the box keeps the value");
    for (int erase{}; erase < 4; ++erase) static_cast<void>(menu.text_erase(false));
    expect(menu.text_input("0.4x56"), "typed text must reach the box");
    slider = row(menu.presentation(), SettingsRowId::mouse_sensitivity);
    expect(slider.text_editing && slider.value_text.find("0.456") != std::string::npos,
           "only float characters are kept");
    expect(menu.handle(InputEvent{InputAction::activate, InputPhase::pressed}),
           "Enter commits the box");
    expect(!menu.text_editing() && session.draft().controls.mouse_sensitivity == 0.46,
           "on_return rounds to two decimals");
    expect(row(menu.presentation(), SettingsRowId::mouse_sensitivity).value_text == "0.46",
           "the committed value is shown");

    click(menu, control_point(slider, static_cast<int>(box_x + width / 12.0)));
    static_cast<void>(menu.text_erase(false));
    static_cast<void>(menu.text_erase(false));
    static_cast<void>(menu.text_erase(false));
    static_cast<void>(menu.text_erase(false));
    static_cast<void>(menu.text_input("7"));
    click(menu, Point{200, 110});
    expect(!menu.text_editing() && session.draft().controls.mouse_sensitivity == 1.0,
           "losing focus commits and clamps to the 0..1 range");
}

void toggle_rows_set_the_clicked_half() {
    SettingsSession session;
    SettingsMenuModel menu{session};
    auto toggle = row(menu.presentation(), SettingsRowId::show_skins);
    const auto on_half =
        control_point(toggle, toggle.control_bounds.x + toggle.control_bounds.width - 5);
    const auto off_half = control_point(toggle, toggle.control_bounds.x + 5);
    const bool initial = session.draft().main.show_skins;
    click(menu, initial ? on_half : off_half);
    expect(session.draft().main.show_skins == initial,
           "clicking the selected half must not flip the toggle");
    click(menu, off_half);
    expect(!session.draft().main.show_skins, "clicking OFF selects OFF");
    click(menu, off_half);
    expect(!session.draft().main.show_skins, "clicking OFF again keeps OFF");
    menu.pointer_move(on_half);
    toggle = row(menu.presentation(), SettingsRowId::show_skins);
    expect(toggle.unselected_half_hovered, "hovering the unselected half highlights it");
    click(menu, on_half);
    expect(session.draft().main.show_skins, "clicking ON selects ON");
}

void choice_rows_react_only_to_their_arrows() {
    SettingsSession session;
    SettingsMenuModel menu{session};
    const auto invert = row(menu.presentation(), SettingsRowId::invert_mouse);
    click(menu, control_point(invert, invert.control_bounds.x + invert.control_bounds.width / 2));
    expect(!session.draft().main.invert_mouse, "the value text between the arrows is inert");
    click(menu, control_point(invert, invert.control_bounds.x + 2));
    expect(!session.draft().main.invert_mouse, "the left arrow at the first option is inert");
}

void in_game_done_and_menu_key_return_to_the_game_cancel_to_the_escape_menu() {
    using battlespades::frontend::SettingsMenuSound;
    SettingsMenuEnvironment environment;
    environment.context = SettingsMenuContext::in_game;
    {
        SettingsSession session;
        SettingsMenuModel menu{session, environment};
        menu.activate_done();
        const auto* close = find_effect<SettingsCloseCommand>(menu.take_effects());
        expect(close != nullptr && close->committed && close->return_to_game,
               "save_pressed returns to the game");
    }
    {
        SettingsSession session;
        SettingsMenuModel menu{session, environment};
        menu.activate_cancel();
        const auto effects = menu.take_effects();
        const auto* close = find_effect<SettingsCloseCommand>(effects);
        expect(close != nullptr && !close->return_to_game,
               "back_pressed reopens the Escape menu");
        expect(!has_sound(effects, SettingsMenuSound::back), "in-game Cancel is silent");
    }
    {
        SettingsSession session;
        SettingsMenuModel menu{session, environment};
        expect(menu.handle(InputEvent{InputAction::cancel, InputPhase::pressed}),
               "the Menu key is consumed");
        const auto effects = menu.take_effects();
        const auto* close = find_effect<SettingsCloseCommand>(effects);
        expect(close != nullptr && close->return_to_game && !close->committed,
               "the Menu key restores and returns to the game");
        expect(has_sound(effects, SettingsMenuSound::back) &&
                   find_effect<SettingsRestoreCommand>(effects) != nullptr,
               "the Menu key plays menu_backA and restores the config");
    }
    {
        SettingsSession session;
        SettingsMenuModel menu{session};
        expect(menu.handle(InputEvent{InputAction::cancel, InputPhase::pressed}),
               "the frontend Menu key is Cancel");
        const auto effects = menu.take_effects();
        const auto* close = find_effect<SettingsCloseCommand>(effects);
        expect(close != nullptr && !close->return_to_game &&
                   has_sound(effects, SettingsMenuSound::back),
               "frontend Cancel plays the back cue and leaves Settings");
    }
}

struct TestCase final {
    std::string_view name;
    std::function<void()> body;
};

} // namespace

int main() {
    const std::vector<TestCase> tests{
        {"skin_preferences_are_reachable_live_and_cancelable",skin_preferences_are_reachable_live_and_cancelable},
        {"main_inventory_and_geometry_match_retail", main_inventory_and_geometry_match_retail},
        {"graphics_capabilities_and_wheel_scrolling_are_deterministic",
         graphics_capabilities_and_wheel_scrolling_are_deterministic},
        {"native_graphics_rows_edit_presets_and_report_why_they_are_unavailable",
         native_graphics_rows_edit_presets_and_report_why_they_are_unavailable},
        {"window_mode_row_cycles_and_greys_out_resolution_when_borderless",
         window_mode_row_cycles_and_greys_out_resolution_when_borderless},
        {"resolution_dropdown_opens_scrolls_selects_and_closes_outside",
         resolution_dropdown_opens_scrolls_selects_and_closes_outside},
        {"scrollbar_arrows_track_and_thumb_drive_the_model",
         scrollbar_arrows_track_and_thumb_drive_the_model},
        {"in_game_graphics_apply_live", in_game_graphics_apply_live},
        {"direct3d_antialiasing_reads_restart_required",
         direct3d_antialiasing_reads_restart_required},
        {"controls_inventory_collapse_and_scroll_preserve_order",
         controls_inventory_collapse_and_scroll_preserve_order},
        {"semantic_navigation_adjusts_values_and_reveals_focus",
         semantic_navigation_adjusts_values_and_reveals_focus},
        {"pointer_hit_testing_tabs_and_slider_drag_edit_the_draft",
         pointer_hit_testing_tabs_and_slider_drag_edit_the_draft},
        {"raw_binding_capture_rejects_reserved_and_duplicate_scancodes",
         raw_binding_capture_rejects_reserved_and_duplicate_scancodes},
        {"defaults_done_and_cancel_are_transactional_typed_commands",
         defaults_done_and_cancel_are_transactional_typed_commands},
        {"live_graphics_rows_do_not_claim_a_restart",
         live_graphics_rows_do_not_claim_a_restart},
        {"favorite_server_is_transient_and_commits_only_on_done",
         favorite_server_is_transient_and_commits_only_on_done},
        {"every_renderer_tier_is_reachable_and_legacy_stays_deliberate",
         every_renderer_tier_is_reachable_and_legacy_stays_deliberate},
        {"range_bar_arrows_step_without_rounding_and_grey_out_at_the_ends",
         range_bar_arrows_step_without_rounding_and_grey_out_at_the_ends},
        {"sensitivity_track_maps_and_its_box_takes_typed_values",
         sensitivity_track_maps_and_its_box_takes_typed_values},
        {"toggle_rows_set_the_clicked_half", toggle_rows_set_the_clicked_half},
        {"choice_rows_react_only_to_their_arrows", choice_rows_react_only_to_their_arrows},
        {"in_game_done_and_menu_key_return_to_the_game_cancel_to_the_escape_menu",
         in_game_done_and_menu_key_return_to_the_game_cancel_to_the_escape_menu},
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
