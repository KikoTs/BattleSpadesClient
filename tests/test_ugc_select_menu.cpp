#include "battlespades/frontend/ugc_select_menu.hpp"
#include "battlespades/frontend/ugc_select_presentation.hpp"

#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using namespace battlespades::frontend;
using battlespades::ui::InputAction;
using battlespades::ui::InputEvent;
using battlespades::ui::InputPhase;
using battlespades::ui::Point;
using battlespades::ui::Rect;
using battlespades::ui::SpriteDrawCommand;
using battlespades::ui::TextDrawCommand;

constexpr std::int32_t scale{MainMenuModel::subpixels_per_pixel};

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] InputEvent pressed(InputAction action) noexcept {
    return InputEvent{action, InputPhase::pressed};
}

void recovered_controls_have_exact_labels_actions_and_geometry() {
    const UgcSelectMenuModel menu;
    const auto controls = menu.controls();
    expect(controls.size() == 4U, "UGC Select must expose three actions and Back");
    expect(controls[0].action == UgcSelectAction::create_map &&
               controls[0].localization_key == "UGC_MENU_MAP_EDITOR" &&
               controls[0].widget.bounds == Rect{269 * scale, 166 * scale, 262 * scale, 58 * scale},
           "Create Map must occupy the recovered upper button bounds");
    expect(controls[1].action == UgcSelectAction::publish_map &&
               controls[1].localization_key == "UGC_MENU_PUBLISH_MAP" &&
               controls[1].widget.bounds == Rect{269 * scale, 229 * scale, 262 * scale, 58 * scale},
           "Publish Map must occupy the recovered middle button bounds");
    expect(controls[2].action == UgcSelectAction::subscribe_workshop &&
               controls[2].localization_key == "UGC_MENU_SUBSCRIBE" &&
               controls[2].widget.bounds == Rect{269 * scale, 292 * scale, 262 * scale, 58 * scale},
           "Subscribe must occupy the recovered lower button bounds");
    expect(controls[3].action == UgcSelectAction::back &&
               controls[3].localization_key == "BACK" &&
               controls[3].widget.bounds == Rect{248 * scale, 542 * scale, 78 * scale, 26 * scale},
           "Back hit bounds must preserve the NavigationBar text metrics");
}

void invalid_data_disables_only_ugc_operations() {
    UgcSelectMenuModel menu;
    menu.set_invalid_data_error(true);
    expect(menu.invalid_data_error(), "invalid-data state must be observable");
    const auto controls = menu.controls();
    expect(!controls[0].widget.state.enabled && !controls[1].widget.state.enabled &&
               !controls[2].widget.state.enabled && controls[3].widget.state.enabled,
           "invalid retail data must disable only the three UGC actions");
    expect(menu.focused() == controls[3].widget.id,
           "focus must repair to Back when every content route is disabled");

    menu.pointer_press(Point{300 * scale, 180 * scale});
    expect(!menu.pointer_release(Point{300 * scale, 180 * scale}).has_value(),
           "a disabled Create Map button must never route");
    const auto back = menu.handle(pressed(InputAction::cancel));
    expect(back.has_value() && back->action == UgcSelectAction::back &&
               back->sound_asset == main_menu_assets::back_sound &&
               !back->external_url.has_value(),
           "Back must remain a safe recovery route with the retail back sound");
}

void semantic_navigation_emits_typed_routes_and_audio() {
    UgcSelectMenuModel menu;
    const auto create = menu.handle(pressed(InputAction::activate));
    expect(create.has_value() && create->action == UgcSelectAction::create_map &&
               create->sound_asset == main_menu_assets::confirmation_sound,
           "initial focus must activate Create Map with menu_confirmA");

    static_cast<void>(menu.handle(pressed(InputAction::navigate_down)));
    const auto publish = menu.handle(pressed(InputAction::activate));
    expect(publish.has_value() && publish->action == UgcSelectAction::publish_map &&
               !publish->external_url.has_value(),
           "one Down action must select Publish Map without manufacturing a web URL");

    static_cast<void>(menu.handle(pressed(InputAction::navigate_down)));
    const auto subscribe = menu.handle(pressed(InputAction::activate));
    expect(subscribe.has_value() && subscribe->action == UgcSelectAction::subscribe_workshop &&
               subscribe->external_url ==
                   "http://steamcommunity.com/workshop/browse/?appid=224540",
           "Subscribe must emit the original Ace of Spades Workshop URL");
}

void workshop_app_id_is_configurable_but_never_zero() {
    UgcSelectMenuModel menu;
    expect(!menu.set_steam_app_id(0U) &&
               menu.steam_app_id() == UgcSelectMenuModel::retail_steam_app_id,
           "app id zero must fail closed without changing the retail default");
    expect(menu.set_steam_app_id(480U) &&
               menu.workshop_url() ==
                   "http://steamcommunity.com/workshop/browse/?appid=480",
           "a compatibility runtime may explicitly substitute its Steam app id");
}

void pointer_activation_preserves_retail_text_button_quirk() {
    UgcSelectMenuModel menu;
    menu.pointer_press(std::nullopt);
    const auto result = menu.pointer_release(Point{300 * scale, 180 * scale});
    expect(result.has_value() && result->action == UgcSelectAction::create_map,
           "retail TextButton arms at screen press and activates on an inside release");

    menu.pointer_press(std::nullopt);
    expect(!menu.pointer_release(Point{260 * scale, 550 * scale}).has_value(),
           "NavigationBar must still require an inside press before an inside release");
    menu.pointer_press(Point{260 * scale, 550 * scale});
    const auto back = menu.pointer_release(Point{260 * scale, 550 * scale});
    expect(back.has_value() && back->action == UgcSelectAction::back,
           "an armed Back navigation item must route to Select Menu");
}

void presentation_builds_exact_shared_scene_layers() {
    UgcSelectMenuModel menu;
    UgcSelectPresentation presentation;
    const auto layer = presentation.build_layer(menu);
    expect(layer.size() == UgcSelectPresentation::layer_command_count,
           "UGC foreground must have a stable 17-command composition");
    const auto* frame = std::get_if<SpriteDrawCommand>(&layer.commands()[0]);
    const auto* navigation_frame = std::get_if<SpriteDrawCommand>(&layer.commands()[1]);
    const auto* create_label = std::get_if<TextDrawCommand>(&layer.commands()[5]);
    const auto* splash = std::get_if<SpriteDrawCommand>(&layer.commands()[16]);
    expect(frame != nullptr && frame->asset_id == ugc_select_assets::three_button_frame &&
               frame->destination == battlespades::ui::DrawRect{231.0, 129.5, 339.0, 253.0},
           "three-button frame must retain integer-truncated Pyglet geometry");
    expect(navigation_frame != nullptr &&
               navigation_frame->asset_id == ugc_select_assets::small_navigation_frame,
           "UGC scene must include the small navigation frame");
    expect(create_label != nullptr && create_label->localization_key == "UGC_MENU_MAP_EDITOR",
           "first TextButton label must remain a localization request");
    expect(splash != nullptr && splash->asset_id == main_menu_assets::splash &&
               splash->destination ==
                   battlespades::ui::DrawRect{265.75, -6.75, 293.25, 193.5},
           "splash must preserve the recovered translate-and-scale transform");

    const auto complete = presentation.build(menu, UgcSelectPresentationContext{});
    expect(complete.size() == UgcSelectPresentation::complete_command_count,
           "standalone UGC frame must prepend exactly one shared background");
    const auto* background = std::get_if<SpriteDrawCommand>(&complete.commands().front());
    expect(background != nullptr && background->asset_id == main_menu_assets::background &&
               background->sizing == battlespades::ui::SpriteSizing::cover,
           "standalone UGC composition must cover the window with the frontend background");

    menu.set_invalid_data_error(true);
    const auto disabled = presentation.build_layer(menu);
    const auto* disabled_button = std::get_if<SpriteDrawCommand>(&disabled.commands()[2]);
    const auto* disabled_label = std::get_if<TextDrawCommand>(&disabled.commands()[5]);
    expect(disabled_button != nullptr &&
               disabled_button->modulation.intensity_per_mille == 700U,
           "disabled UGC actions must retain retail background-art dimming");
    expect(disabled_label != nullptr &&
               disabled_label->modulation.intensity_per_mille == 1'000U,
           "retail TextButton must leave its configured label color undimmed");
}

void presentation_rejects_invalid_context_before_drawing() {
    const UgcSelectMenuModel menu;
    const UgcSelectPresentation presentation;
    bool invalid_extent{};
    try {
        static_cast<void>(presentation.build(menu, UgcSelectPresentationContext{{0, 600}, 1'000U}));
    } catch (const std::invalid_argument&) {
        invalid_extent = true;
    }
    expect(invalid_extent, "zero-width presentation must fail closed");

    bool invalid_opacity{};
    try {
        static_cast<void>(presentation.build(menu, UgcSelectPresentationContext{{800, 600}, 1'001U}));
    } catch (const std::invalid_argument&) {
        invalid_opacity = true;
    }
    expect(invalid_opacity, "opacity above one must fail before producing a partial DrawList");
}

struct TestCase final {
    std::string_view name;
    std::function<void()> body;
};

} // namespace

int main() {
    const std::vector<TestCase> tests{
        {"recovered_controls_have_exact_labels_actions_and_geometry",
         recovered_controls_have_exact_labels_actions_and_geometry},
        {"invalid_data_disables_only_ugc_operations",
         invalid_data_disables_only_ugc_operations},
        {"semantic_navigation_emits_typed_routes_and_audio",
         semantic_navigation_emits_typed_routes_and_audio},
        {"workshop_app_id_is_configurable_but_never_zero",
         workshop_app_id_is_configurable_but_never_zero},
        {"pointer_activation_preserves_retail_text_button_quirk",
         pointer_activation_preserves_retail_text_button_quirk},
        {"presentation_builds_exact_shared_scene_layers",
         presentation_builds_exact_shared_scene_layers},
        {"presentation_rejects_invalid_context_before_drawing",
         presentation_rejects_invalid_context_before_drawing},
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
