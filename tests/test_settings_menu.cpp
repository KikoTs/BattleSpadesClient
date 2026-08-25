#include "battlespades/frontend/settings_menu.hpp"

#include <algorithm>
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

[[nodiscard]] SettingsMenuEnvironment full_environment() {
    SettingsMenuEnvironment environment;
    environment.display_modes = {
        {1'920U, 1'080U},
        {320U, 200U},
        {1'280U, 720U},
        {1'920U, 1'080U},
    };
    return environment;
}

void main_inventory_and_geometry_match_retail() {
    SettingsSession session;
    SettingsMenuModel menu{session, full_environment()};
    const auto view = menu.presentation();

    expect(view.active_tab == SettingsTab::main, "Main must be the initial tab");
    expect(view.rows.size() == 5U, "Main must expose exactly five recovered rows");
    const std::vector expected{
        SettingsRowId::master_volume,
        SettingsRowId::music_volume,
        SettingsRowId::fullscreen,
        SettingsRowId::invert_mouse,
        SettingsRowId::favorite_server,
    };
    for (std::size_t index{}; index < expected.size(); ++index) {
        expect(view.rows[index].id == expected[index], "Main row order changed");
        expect(view.rows[index].visible, "all five Main rows must fit without scrolling");
    }
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
    expect(view.rows.size() == 10U, "full graphics capabilities must expose ten rows");
    expect(view.maximum_scroll_index == 2U, "ten 32px rows must require two retail row scrolls");
    expect(row(view, SettingsRowId::resolution).choice_count == 3U,
           "display modes must be filtered, deduplicated, sorted, and include current mode");
    expect(row(view, SettingsRowId::graphics_api).choice_count == 6U,
           "the complete cross-platform backend vocabulary must be selectable in tests");
    expect(!row(view, SettingsRowId::compatibility_shader).visible,
           "the final graphics row must initially be below the viewport");

    expect(menu.mouse_wheel(Point{200, 200}, -1), "wheel inside viewport must be consumed");
    expect(menu.mouse_wheel(Point{200, 200}, -1), "second wheel step must be consumed");
    view = menu.presentation();
    expect(view.scroll_index == 2U, "wheel down must reach the final graphics row");
    expect(!row(view, SettingsRowId::resolution).visible &&
               !row(view, SettingsRowId::graphics_api).visible &&
               row(view, SettingsRowId::compatibility_shader).visible,
           "scrolling must replace the first two rows with the final rows");

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
    expect(limited_view.rows.size() == 8U,
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
        expect(row(view, SettingsRowId::shader_quality).value_text == "LEGACY",
               "a disabled tier row must read LEGACY, not a stale tier");
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
    SettingsSession session;
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

void in_game_graphics_are_disabled_without_disabling_other_tabs() {
    SettingsSession session;
    auto environment = full_environment();
    environment.context = SettingsMenuContext::in_game;
    SettingsMenuModel menu{session, environment};
    menu.set_active_tab(SettingsTab::graphics);
    const auto view = menu.presentation();

    expect(view.in_game && view.tooltip_key == "SETTINGS_GRAPHICS_DISABLED_MESSAGE",
           "in-game graphics tab must expose its recovered warning");
    expect(std::all_of(
               view.rows.begin(), view.rows.end(), [](const auto& item) { return !item.enabled; }),
           "every in-game graphics row must be disabled");
    expect(!view.buttons[0].enabled, "Defaults must be disabled with in-game graphics");
    expect(view.buttons[1].bounds == Rect{160, 480, 232, 41} &&
               view.buttons[2].bounds == Rect{403, 480, 232, 41},
           "in-game footer must use its compact retail geometry");
    expect(!menu.set_focus(SettingsMenuTarget::for_row(SettingsRowId::resolution)),
           "accessibility focus must reject disabled graphics rows");
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
               row(view, SettingsRowId::cycle_next_weapon).value_text == "MOUSE WHEEL" &&
               row(view, SettingsRowId::inventory_slots).value_text == "1-9",
           "non-configurable retail helper rows must remain visible");
    expect(row(view, SettingsRowId::mouse_sensitivity).value_text == "0.10",
           "mouse sensitivity must use retail's fixed two-decimal edit-box text");

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
    const Point quarter{volume.control_bounds.x + volume.control_bounds.width / 4, middle.y};
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
    SettingsSession session;
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
    expect(commit != nullptr && commit->changed && commit->resolution_changed &&
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

struct TestCase final {
    std::string_view name;
    std::function<void()> body;
};

} // namespace

int main() {
    const std::vector<TestCase> tests{
        {"main_inventory_and_geometry_match_retail", main_inventory_and_geometry_match_retail},
        {"graphics_capabilities_and_wheel_scrolling_are_deterministic",
         graphics_capabilities_and_wheel_scrolling_are_deterministic},
        {"resolution_dropdown_opens_scrolls_selects_and_closes_outside",
         resolution_dropdown_opens_scrolls_selects_and_closes_outside},
        {"scrollbar_arrows_track_and_thumb_drive_the_model",
         scrollbar_arrows_track_and_thumb_drive_the_model},
        {"in_game_graphics_are_disabled_without_disabling_other_tabs",
         in_game_graphics_are_disabled_without_disabling_other_tabs},
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
