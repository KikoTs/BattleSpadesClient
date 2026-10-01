#include "battlespades/frontend/settings_presentation.hpp"

#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using battlespades::frontend::SettingsClassicLayout;
using battlespades::frontend::SettingsPresentation;
using battlespades::frontend::SettingsPresentationContext;
using battlespades::frontend::SettingsPresentationRow;
using battlespades::frontend::SettingsPresentationRowKind;
using battlespades::frontend::SettingsPresentationScreen;
using battlespades::frontend::SettingsPresentationSnapshot;
using battlespades::frontend::SettingsPresentationTab;
using battlespades::frontend::SettingsPresentationVisualState;
using battlespades::ui::DrawCommand;
using battlespades::ui::DrawRect;
using battlespades::ui::DrawSpace;
using battlespades::ui::HorizontalTextAlignment;
using battlespades::ui::SpriteDrawCommand;
using battlespades::ui::SpriteSizing;
using battlespades::ui::TextDrawCommand;
using battlespades::ui::TextureAnchor;
using battlespades::ui::TextureFilter;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] bool near(double left, double right) noexcept {
    return std::abs(left - right) < 0.000'001;
}

[[nodiscard]] bool same_rect(const DrawRect& left, const DrawRect& right) noexcept {
    return near(left.x, right.x) && near(left.y, right.y) &&
           near(left.width, right.width) && near(left.height, right.height);
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

[[nodiscard]] std::vector<DrawCommand>
build(const SettingsPresentationSnapshot& snapshot,
      SettingsPresentationContext context = SettingsPresentationContext{}) {
    auto list = SettingsPresentation{}.build(snapshot, context);
    return {list.commands().begin(), list.commands().end()};
}

[[nodiscard]] const SpriteDrawCommand&
find_sprite(const std::vector<DrawCommand>& commands,
            std::string_view asset_fragment,
            const DrawRect& destination) {
    for (const auto& command : commands) {
        if (const auto* sprite = std::get_if<SpriteDrawCommand>(&command);
            sprite != nullptr && sprite->asset_id.find(asset_fragment) != std::string::npos &&
            same_rect(sprite->destination, destination)) {
            return *sprite;
        }
    }
    throw std::runtime_error{"expected sprite command was not found"};
}

[[nodiscard]] const TextDrawCommand&
find_text(const std::vector<DrawCommand>& commands, std::string_view key) {
    for (const auto& command : commands) {
        if (const auto* text = std::get_if<TextDrawCommand>(&command);
            text != nullptr && text->localization_key == key) {
            return *text;
        }
    }
    throw std::runtime_error{"expected text command was not found"};
}

[[nodiscard]] std::size_t text_index(const std::vector<DrawCommand>& commands,
                                     std::string_view key) {
    for (std::size_t index = 0U; index < commands.size(); ++index) {
        if (const auto* text = std::get_if<TextDrawCommand>(&commands[index]);
            text != nullptr && text->localization_key == key) {
            return index;
        }
    }
    throw std::runtime_error{"expected text command index was not found"};
}

SettingsPresentationRow choice_row(std::string label, std::string value) {
    SettingsPresentationRow row;
    row.kind = SettingsPresentationRowKind::choice;
    row.label_key = std::move(label);
    row.value_key = std::move(value);
    return row;
}

void classic_layout_matches_retail_capture() {
    const SettingsClassicLayout frontend =
        battlespades::frontend::settings_classic_layout(false);
    expect(frontend.outer_frame == DrawRect{112.0, 10.0, 576.0, 579.0},
           "frontend Settings outer frame must match retail integer anchoring");
    expect(frontend.content_frame == DrawRect{112.0, 10.0, 552.0, 579.0},
           "tab content frame must retain its distinct 863-pixel source width");
    expect(frontend.title == DrawRect{250.0, 11.0, 300.0, 80.0},
           "frontend title box must match the 800x600 capture");
    expect(frontend.tab_hit_strip == DrawRect{152.0, 100.0, 494.0, 33.0},
           "tab strip must be expressed in top-left coordinates");
    expect(frontend.list_area == DrawRect{162.0, 143.0, 474.0, 273.0},
           "row viewport must preserve recovered retail insets");
    expect(frontend.defaults_button == DrawRect{151.0, 434.0, 80.0, 30.0} &&
               frontend.tooltip == DrawRect{238.0, 434.0, 407.0, 28.0},
           "defaults and tooltip strip must share the recovered baseline");
    expect(frontend.cancel_button == DrawRect{152.0, 492.0, 240.0, 60.0} &&
               frontend.done_button == DrawRect{405.0, 492.0, 240.0, 60.0},
           "frontend action buttons must match their retail hit rectangles");

    const auto in_game = battlespades::frontend::settings_classic_layout(true);
    expect(in_game.outer_frame == DrawRect{133.0, 28.0, 534.0, 524.0},
           "in-game Settings must use its shorter dedicated frame");
    expect(in_game.cancel_button == DrawRect{160.0, 480.0, 232.0, 41.0} &&
               in_game.done_button == DrawRect{403.0, 480.0, 232.0, 41.0},
           "in-game action geometry must remain separate from frontend geometry");
}

void single_choice_row_has_stable_retail_command_order_and_geometry() {
    SettingsPresentationSnapshot snapshot;
    snapshot.rows.push_back(choice_row("LANGUAGE", "ENGLISH"));
    const auto commands = build(snapshot);

    expect(commands.size() == 30U,
           "one choice row must emit a stable complete Settings command stream");
    const auto& background = command_as<SpriteDrawCommand>(commands, 0U);
    expect(background.asset_id == "png/ui/ugc_splash.png" &&
               background.space == DrawSpace::window_pixels &&
               background.sizing == SpriteSizing::cover,
           "frontend cover background must draw first");
    const auto& outer = command_as<SpriteDrawCommand>(commands, 1U);
    expect(outer.asset_id == "png/ui/common_elements/frames/ui_frame_small.png" &&
               outer.destination == DrawRect{112.0, 10.0, 576.0, 579.0} &&
               outer.retail_source_anchor == TextureAnchor::center,
           "outer frame must draw second with recovered centered-loader evidence");
    expect(command_as<TextDrawCommand>(commands, 2U).localization_key == "SETTINGS",
           "title must precede tab content");
    expect(command_as<SpriteDrawCommand>(commands, 3U).asset_id ==
               "png/ui/settings/settings_main/settings_main_content_frames.png",
           "selected Main content frame must follow title");

    const auto& row = command_as<SpriteDrawCommand>(commands, 4U);
    expect(row.asset_id.find("settings_matchsettings_frame") != std::string::npos &&
               row.destination == DrawRect{162.0, 143.0, 474.0, 32.0},
           "first row must retain exact full-width list geometry");
    const auto& label = command_as<TextDrawCommand>(commands, 5U);
    expect(label.localization_key == "LANGUAGE" &&
               label.destination == DrawRect{176.0, 145.0, 158.0, 28.0},
           "row name must occupy the recovered first-third column");
    const auto& control = command_as<SpriteDrawCommand>(commands, 6U);
    expect(control.asset_id == "png/high/white.png" &&
               control.destination == DrawRect{348.0, 147.0, 274.0, 24.0} &&
               control.sampling == TextureFilter::nearest,
           "choice control must begin at x=348 and use the white-pixel primitive");
    expect(command_as<SpriteDrawCommand>(commands, 7U).destination ==
               DrawRect{352.0, 151.0, 16.0, 16.0} &&
               command_as<SpriteDrawCommand>(commands, 9U).destination ==
                   DrawRect{602.0, 151.0, 16.0, 16.0},
           "choice arrows must bookend the exact control box");
    const auto& value = command_as<TextDrawCommand>(commands, 12U);
    expect(value.localization_key == "ENGLISH" &&
               value.horizontal_alignment == HorizontalTextAlignment::center,
           "choice value must layer after its state geometry");

    expect(command_as<TextDrawCommand>(commands, 13U).localization_key == "MAIN" &&
               command_as<TextDrawCommand>(commands, 14U).localization_key == "GRAPHICS" &&
               command_as<TextDrawCommand>(commands, 15U).localization_key == "CONTROLS",
           "tabs must draw after rows in fixed retail order");
    expect(command_as<SpriteDrawCommand>(commands, 16U).asset_id.find("tooltip") !=
               std::string::npos &&
               command_as<TextDrawCommand>(commands, 17U).localization_key ==
                   "SETTINGS_MESSAGE",
           "tooltip frame and text must follow tabs");
    expect(command_as<TextDrawCommand>(commands, 21U).localization_key == "DEFAULTS" &&
               command_as<TextDrawCommand>(commands, 25U).localization_key == "CANCEL" &&
               command_as<TextDrawCommand>(commands, 29U).localization_key == "DONE",
           "Defaults, Cancel and Done must be the final ordered controls");
}

void text_buttons_preserve_scaled_slices_and_pressed_content_offset() {
    SettingsPresentationSnapshot snapshot;
    snapshot.rows.push_back(choice_row("LANGUAGE", "ENGLISH"));
    snapshot.cancel_button.visual_state = SettingsPresentationVisualState::pressed;
    const auto commands = build(snapshot);

    const auto& default_left = command_as<SpriteDrawCommand>(commands, 18U);
    const auto& default_mid = command_as<SpriteDrawCommand>(commands, 19U);
    const auto& default_right = command_as<SpriteDrawCommand>(commands, 20U);
    expect(default_left.destination == DrawRect{151.0, 434.0, 19.0, 30.0} &&
               default_mid.destination == DrawRect{169.0, 434.0, 45.0, 30.0} &&
               default_right.destination == DrawRect{213.0, 434.0, 19.0, 30.0},
           "30-pixel Defaults button must scale the 36:58 cap ratio and retain seam overlaps");

    const auto& cancel_left = command_as<SpriteDrawCommand>(commands, 22U);
    const auto& cancel_text = command_as<TextDrawCommand>(commands, 25U);
    expect(cancel_left.asset_id.find("button_large_press_left") != std::string::npos &&
               cancel_left.destination == DrawRect{152.0, 492.0, 38.0, 60.0},
           "pressed Cancel must select press art without moving its slices");
    expect(near(cancel_text.destination.y, 498.068'965'517'241'4),
           "pressed Settings content must retain the retail height-scaled offset");
}

void graphics_rows_narrow_for_scrollbar_and_thumb_tracks_offset() {
    SettingsPresentationSnapshot snapshot;
    snapshot.selected_tab = SettingsPresentationTab::graphics;
    for (std::size_t index = 0U; index < 9U; ++index) {
        snapshot.rows.push_back(choice_row("GRAPHICS_ROW_" + std::to_string(index), "HIGH"));
    }

    auto commands = build(snapshot);
    const auto row = find_sprite(commands,
                                  "settings_matchsettings_frame",
                                  DrawRect{162.0, 143.0, 442.0, 32.0});
    expect(row.retail_source_scale == 0.64,
           "scrolling Graphics rows must preserve the retail 0.64 load recipe");
    static_cast<void>(find_sprite(
        commands, "white.png", DrawRect{337.333'333'333'333'3, 147.0, 252.666'666'666'666'7, 24.0}));
    static_cast<void>(find_sprite(commands, "white.png", DrawRect{614.0, 143.0, 22.0, 273.0}));
    const auto top_thumb =
        find_sprite(commands, "scroll_bar_top", DrawRect{615.0, 167.0, 20.0, 4.0});
    expect(top_thumb.sampling == TextureFilter::linear,
           "retail scrollbar bevels must use filtered sub-pixel sampling");
    expect(text_index(commands, "GRAPHICS") > text_index(commands, "HIGH"),
           "tab labels must remain above the complete row/scrollbar layer");

    snapshot.first_visible_row = 1U;
    commands = build(snapshot);
    static_cast<void>(find_sprite(commands, "scroll_bar_top", DrawRect{615.0, 192.0, 20.0, 4.0}));
    expect(find_text(commands, "GRAPHICS_ROW_1").destination.y == 145.0,
           "scrolling must replace the first logical row without moving its presentation slot");
}

void controls_categories_key_bindings_and_dropdown_overlay_stack_correctly() {
    SettingsPresentationSnapshot snapshot;
    snapshot.selected_tab = SettingsPresentationTab::controls;

    SettingsPresentationRow category;
    category.kind = SettingsPresentationRowKind::category;
    category.label_key = "MOVEMENT";
    category.expanded = true;
    snapshot.rows.push_back(category);

    SettingsPresentationRow binding;
    binding.kind = SettingsPresentationRowKind::key_binding;
    binding.label_key = "MOVE_FORWARD";
    binding.value_key = "W";
    snapshot.rows.push_back(binding);

    SettingsPresentationRow dropdown;
    dropdown.kind = SettingsPresentationRowKind::dropdown;
    dropdown.label_key = "GAMEPAD_PRESET";
    dropdown.value_key = "CLASSIC";
    dropdown.dropdown_open = true;
    dropdown.dropdown_options = {"CLASSIC", "MODERN"};
    dropdown.dropdown_selected_index = 1U;
    snapshot.rows.push_back(dropdown);

    const auto commands = build(snapshot);
    static_cast<void>(find_sprite(commands, "white.png", DrawRect{162.0, 143.0, 474.0, 26.0}));
    const auto minus = find_sprite(
        commands, "collapse_minus", DrawRect{604.0, 147.0, 18.0, 18.0});
    expect(minus.retail_source_scale == 1.0,
           "expanded category must place its collapse marker in the recovered right gutter");
    expect(find_text(commands, "MOVE_FORWARD").destination.y == 173.0 &&
               find_text(commands, "W").destination ==
                   DrawRect{352.0, 177.0, 266.0, 20.0},
           "key binding must follow the 26+2 category cadence and preserve control insets");
    expect(find_text(commands, "GAMEPAD_PRESET").destination.y == 207.0,
           "ordinary rows after bindings must continue at the 32+2 cadence");
    static_cast<void>(find_sprite(
        commands, "white.png", DrawRect{348.0, 233.0, 274.0, 40.0}));
    static_cast<void>(find_sprite(
        commands, "white.png", DrawRect{350.0, 255.0, 270.0, 18.0}));
    expect(text_index(commands, "MODERN") < text_index(commands, "MAIN"),
           "20-pixel retail dropdown options must overlay rows before the tab/tooltip layer");
}

void range_toggle_checkbox_and_scalar_controls_emit_retail_state_art() {
    SettingsPresentationSnapshot snapshot;

    SettingsPresentationRow range;
    range.kind = SettingsPresentationRowKind::range_bar;
    range.label_key = "MASTER_VOLUME";
    range.scalar_value = 0.5;
    snapshot.rows.push_back(range);

    SettingsPresentationRow toggle;
    toggle.kind = SettingsPresentationRowKind::toggle;
    toggle.label_key = "FULLSCREEN";
    toggle.checked = true;
    snapshot.rows.push_back(toggle);

    SettingsPresentationRow checkbox;
    checkbox.kind = SettingsPresentationRowKind::checkbox;
    checkbox.label_key = "FAVOURITE";
    checkbox.value_key = "SHOW_FAVOURITES";
    checkbox.checked = false;
    checkbox.visual_state = SettingsPresentationVisualState::disabled;
    snapshot.rows.push_back(checkbox);

    SettingsPresentationRow scalar;
    scalar.kind = SettingsPresentationRowKind::scalar_slider;
    scalar.label_key = "SENSITIVITY";
    scalar.value_key = "0.75";
    scalar.scalar_value = 0.75;
    snapshot.rows.push_back(scalar);

    const auto commands = build(snapshot);
    std::size_t volume_segments{};
    for (const auto& command : commands) {
        if (const auto* sprite = std::get_if<SpriteDrawCommand>(&command);
            sprite != nullptr && sprite->asset_id.find("volume_bar.png") != std::string::npos) {
            ++volume_segments;
        }
    }
    expect(volume_segments == 33U,
           "volume control must retain the retail 33-segment bar instead of a generic fill");
    expect(find_text(commands, "ON").destination.y == 185.0,
           "toggle labels must render inside the second retail row");
    const auto star = find_sprite(commands,
                                   "favourite_star_settings_off",
                                   DrawRect{602.0, 219.0, 16.0, 16.0});
    expect(star.modulation.color == battlespades::ui::ColorRgba8{83U, 83U, 83U, 255U},
           "disabled checkbox art must be dimmed without changing its semantic state");
    static_cast<void>(find_sprite(commands,
                                  "settings_bullet_slider",
                                  DrawRect{510.25, 260.0, 4.0, 10.0}));
}

void resolution_confirmation_uses_its_own_safe_command_surface() {
    SettingsPresentationSnapshot snapshot;
    snapshot.screen = SettingsPresentationScreen::resolution_confirmation;
    snapshot.resolution_countdown_seconds = 8.2;
    snapshot.done_button.visual_state = SettingsPresentationVisualState::hovered;
    const auto commands = build(snapshot);

    expect(commands.size() == 13U,
           "resolution confirmation must not retain hidden Settings rows or controls");
    expect(command_as<TextDrawCommand>(commands, 2U).localization_key == "CONFIRM" &&
               command_as<TextDrawCommand>(commands, 3U).localization_key ==
                   "KEEP_RESOLUTION_PROMPT" &&
               command_as<TextDrawCommand>(commands, 4U).localization_key ==
                   "Reverting to previous resolution in 8 seconds.",
           "confirmation copy must preserve retail int(timer) countdown semantics");
    expect(command_as<SpriteDrawCommand>(commands, 5U).destination ==
               DrawRect{161.0, 431.0, 38.0, 61.0} &&
               command_as<TextDrawCommand>(commands, 8U).localization_key == "KEEP_SETTING",
           "Keep Setting must use the recovered confirmation rectangle");
    expect(command_as<SpriteDrawCommand>(commands, 9U).destination ==
               DrawRect{402.0, 431.0, 38.0, 61.0} &&
               command_as<TextDrawCommand>(commands, 12U).localization_key == "REVERT",
           "Revert must be the final confirmation control");
}

void background_cover_and_validation_fail_before_partial_output() {
    SettingsPresentationSnapshot snapshot;
    snapshot.rows.push_back(choice_row("LANGUAGE", "ENGLISH"));
    auto commands = build(snapshot, SettingsPresentationContext{{1280, 720}, 850U});
    const auto& background = command_as<SpriteDrawCommand>(commands, 0U);
    expect(background.destination == DrawRect{0.0, -120.0, 1280.0, 960.0} &&
               background.modulation.opacity_per_mille == 850U,
           "Settings background must cover widescreen windows and preserve caller opacity");

    bool invalid_window_threw{};
    try {
        static_cast<void>(build(snapshot, SettingsPresentationContext{{0, 600}, 1'000U}));
    } catch (const std::invalid_argument&) {
        invalid_window_threw = true;
    }
    expect(invalid_window_threw, "invalid window must fail before command emission");

    snapshot.rows.front().scalar_value = std::nan("");
    bool invalid_scalar_threw{};
    try {
        static_cast<void>(build(snapshot));
    } catch (const std::invalid_argument&) {
        invalid_scalar_threw = true;
    }
    expect(invalid_scalar_threw, "non-finite control values must fail closed");

    snapshot.rows.front().scalar_value = 0.0;
    snapshot.first_visible_row = 2U;
    bool invalid_offset_threw{};
    try {
        static_cast<void>(build(snapshot));
    } catch (const std::invalid_argument&) {
        invalid_offset_threw = true;
    }
    expect(invalid_offset_threw, "out-of-range scroll offsets must fail closed");
}

struct TestCase final {
    std::string_view name;
    std::function<void()> body;
};

} // namespace

int main() {
    const std::vector<TestCase> tests{
        {"classic_layout_matches_retail_capture", classic_layout_matches_retail_capture},
        {"single_choice_row_has_stable_retail_command_order_and_geometry",
         single_choice_row_has_stable_retail_command_order_and_geometry},
        {"text_buttons_preserve_scaled_slices_and_pressed_content_offset",
         text_buttons_preserve_scaled_slices_and_pressed_content_offset},
        {"graphics_rows_narrow_for_scrollbar_and_thumb_tracks_offset",
         graphics_rows_narrow_for_scrollbar_and_thumb_tracks_offset},
        {"controls_categories_key_bindings_and_dropdown_overlay_stack_correctly",
         controls_categories_key_bindings_and_dropdown_overlay_stack_correctly},
        {"range_toggle_checkbox_and_scalar_controls_emit_retail_state_art",
         range_toggle_checkbox_and_scalar_controls_emit_retail_state_art},
        {"resolution_confirmation_uses_its_own_safe_command_surface",
         resolution_confirmation_uses_its_own_safe_command_surface},
        {"background_cover_and_validation_fail_before_partial_output",
         background_cover_and_validation_fail_before_partial_output},
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
