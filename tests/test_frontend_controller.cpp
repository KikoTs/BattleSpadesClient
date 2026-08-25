#include "battlespades/frontend/frontend_controller.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using battlespades::frontend::FrontendController;
using battlespades::frontend::FrontendControllerConfig;
using battlespades::frontend::FrontendRoute;
using battlespades::frontend::MainMenuAction;
using battlespades::frontend::RuntimeAudioEffect;
using battlespades::frontend::RuntimeAudioEffectKind;
using battlespades::frontend::RuntimeDisplayEffect;
using battlespades::frontend::RuntimeDisplayEffectKind;
using battlespades::frontend::RuntimeInputEffect;
using battlespades::frontend::RuntimeSettingsEffect;
using battlespades::frontend::RuntimeSettingsNoticeEffect;
using battlespades::frontend::RuntimeSettingsNoticeKind;
using battlespades::frontend::SettingsMenuPresentation;
using battlespades::frontend::SettingsRowId;
using battlespades::frontend::SettingsTargetKind;
using battlespades::settings::Resolution;
using battlespades::settings::SettingsTab;
using battlespades::settings::TomlSettingsStore;
using battlespades::ui::Point;
using battlespades::ui::Rect;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("battlespades-frontend-controller-" + std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_{};
};

[[nodiscard]] Point center(Rect bounds) noexcept {
    return {bounds.x + bounds.width / 2, bounds.y + bounds.height / 2};
}

[[nodiscard]] Point right_arrow(Rect bounds) noexcept {
    return {bounds.x + bounds.width - 2, bounds.y + bounds.height / 2};
}

void settle(FrontendController& controller) {
    for (std::size_t index{}; index < 180U && controller.shell().transitioning(); ++index) {
        controller.tick(std::chrono::nanoseconds::zero());
    }
    expect(!controller.shell().transitioning(), "frontend transition did not settle");
    expect(controller.shell().accepts_input(), "settled frontend must accept input");
}

void click(FrontendController& controller, Point point) {
    controller.pointer_press(point);
    controller.pointer_release(point);
}

void open_settings(FrontendController& controller) {
    // Fourth retail square button: Settings.
    click(controller, Point{502, 477});
    expect(controller.route() == FrontendRoute::settings,
           "Settings action must push the Settings route");
    settle(controller);
}

[[nodiscard]] const battlespades::frontend::SettingsRowPresentation&
row(const SettingsMenuPresentation& presentation, SettingsRowId id);

void select_resolution_option(FrontendController& controller, std::size_t option_index) {
    auto resolution = row(controller.settings_menu().presentation(), SettingsRowId::resolution);
    click(controller, center(resolution.control_bounds));
    resolution = row(controller.settings_menu().presentation(), SettingsRowId::resolution);
    expect(resolution.dropdown_open, "Resolution control must open its retail dropdown");
    expect(option_index < resolution.dropdown_visible_count,
           "test resolution option must be visible in the dropdown");
    click(controller,
          Point{resolution.control_bounds.x + 10,
                resolution.control_bounds.y + resolution.control_bounds.height +
                    static_cast<std::int32_t>(option_index * 20U + 10U)});
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

[[nodiscard]] Rect button(const SettingsMenuPresentation& presentation, SettingsTargetKind id) {
    const auto found = std::find_if(presentation.buttons.begin(),
                                    presentation.buttons.end(),
                                    [id](const auto& candidate) { return candidate.id == id; });
    if (found == presentation.buttons.end()) {
        throw std::runtime_error{"settings button missing from presentation"};
    }
    return found->bounds;
}

template <typename Effect, typename Predicate>
[[nodiscard]] bool has_effect(const std::vector<RuntimeSettingsEffect>& effects,
                              Predicate predicate) {
    return std::any_of(effects.begin(), effects.end(), [&](const auto& effect) {
        const auto* value = std::get_if<Effect>(&effect.payload);
        return value != nullptr && predicate(*value);
    });
}

template <typename Effect>
[[nodiscard]] bool has_effect(const std::vector<RuntimeSettingsEffect>& effects) {
    return has_effect<Effect>(effects, [](const auto&) { return true; });
}

[[nodiscard]] FrontendControllerConfig config_for(const std::filesystem::path& path) {
    FrontendControllerConfig config;
    config.settings_path = path;
    config.settings_environment.display_modes = {{800U, 600U}, {1'024U, 768U}};
    return config;
}

void load_failure_is_non_fatal_and_main_actions_survive_routing() {
    TemporaryDirectory temporary;
    const auto settings_path = temporary.path() / "settings.toml";
    {
        std::ofstream malformed{settings_path};
        malformed << "[main\nmaster_volume = nope\n";
    }

    FrontendController controller{config_for(settings_path)};
    expect(controller.start(), "malformed settings must fall back without bricking frontend");
    expect(controller.route() == FrontendRoute::select_menu, "frontend must start on Select Menu");
    expect(!controller.last_error().empty(), "malformed settings warning must remain inspectable");
    expect(controller.settings_session().draft() ==
               battlespades::settings::retail_default_settings(),
           "failed load must install one complete retail-default snapshot");

    click(controller, Point{299, 477});
    const auto actions = controller.take_unhandled_main_actions();
    expect(actions == std::vector{MainMenuAction::tutorial},
           "non-Settings Select-menu actions must be preserved in order");
    const auto effects = controller.take_effects();
    expect(has_effect<RuntimeAudioEffect>(effects,
                                          [](const RuntimeAudioEffect& effect) {
                                              return effect.kind ==
                                                     RuntimeAudioEffectKind::menu_confirm;
                                          }),
           "Select-menu activation must publish its confirmation sound");
}

void cancel_rolls_back_live_previews_without_writing() {
    TemporaryDirectory temporary;
    const auto settings_path = temporary.path() / "settings.toml";
    FrontendController controller{config_for(settings_path)};
    expect(controller.start(), "controller must start with an absent settings file");
    open_settings(controller);
    static_cast<void>(controller.take_effects());

    const auto presentation = controller.settings_menu().presentation();
    const auto volume = row(presentation, SettingsRowId::master_volume).control_bounds;
    const Point quarter{volume.x + volume.width / 4, volume.y + volume.height / 2};
    click(controller, quarter);
    auto effects = controller.take_effects();
    expect(has_effect<RuntimeAudioEffect>(
               effects,
               [](const RuntimeAudioEffect& effect) {
                   return effect.kind == RuntimeAudioEffectKind::set_master_volume &&
                          effect.value > 0.20 && effect.value < 0.30;
               }),
           "volume edit must publish a renderer-independent live preview");

    click(controller,
          center(button(controller.settings_menu().presentation(),
                        SettingsTargetKind::cancel_button)));
    expect(controller.route() == FrontendRoute::select_menu, "Cancel must return to Select Menu");
    effects = controller.take_effects();
    expect(has_effect<RuntimeAudioEffect>(
               effects,
               [](const RuntimeAudioEffect& effect) {
                   return effect.kind == RuntimeAudioEffectKind::set_master_volume &&
                          effect.value == 1.0;
               }),
           "Cancel must restore every live preview to the persisted snapshot");
    expect(!std::filesystem::exists(settings_path), "Cancel must never persist its draft");
}

void defaults_restore_the_active_tab_and_publish_live_effects() {
    TemporaryDirectory temporary;
    FrontendController controller{config_for(temporary.path() / "settings.toml")};
    expect(controller.start(), "controller must start");
    open_settings(controller);
    static_cast<void>(controller.take_effects());

    auto presentation = controller.settings_menu().presentation();
    const auto volume = row(presentation, SettingsRowId::master_volume).control_bounds;
    click(controller, Point{volume.x + volume.width / 4, volume.y + volume.height / 2});
    static_cast<void>(controller.take_effects());
    expect(controller.settings_session().draft().main.master_volume < 0.30,
           "precondition: Main draft volume must differ from retail default");

    presentation = controller.settings_menu().presentation();
    click(controller, center(button(presentation, SettingsTargetKind::defaults_button)));
    expect(controller.settings_session().draft().main.master_volume == 1.0,
           "Defaults must restore the active tab's retail values");
    const auto effects = controller.take_effects();
    expect(has_effect<RuntimeAudioEffect>(
               effects,
               [](const RuntimeAudioEffect& effect) {
                   return effect.kind == RuntimeAudioEffectKind::set_master_volume &&
                          effect.value == 1.0;
               }),
           "Main Defaults must publish the aggregate live-volume restoration");
}

void graphics_defaults_restore_the_live_vsync_preview() {
    TemporaryDirectory temporary;
    FrontendController controller{config_for(temporary.path() / "settings.toml")};
    expect(controller.start(), "controller must start");
    open_settings(controller);
    static_cast<void>(controller.take_effects());
    click(controller, Point{350, 115});

    expect(controller.mouse_wheel(Point{200, 200}, -1),
           "graphics list must scroll to reveal VSync after adding Graphics API");
    const auto vsync = row(controller.settings_menu().presentation(), SettingsRowId::vsync);
    click(controller, right_arrow(vsync.control_bounds));
    auto effects = controller.take_effects();
    expect(has_effect<RuntimeDisplayEffect>(effects,
                                            [](const RuntimeDisplayEffect& effect) {
                                                return effect.kind ==
                                                           RuntimeDisplayEffectKind::set_vsync &&
                                                       effect.enabled;
                                            }),
           "VSync edit must publish its recovered live preview");

    click(controller,
          center(button(controller.settings_menu().presentation(),
                        SettingsTargetKind::defaults_button)));
    expect(!controller.settings_session().draft().graphics.vsync,
           "Graphics Defaults must restore retail VSync in the draft");
    effects = controller.take_effects();
    expect(has_effect<RuntimeDisplayEffect>(effects,
                                            [](const RuntimeDisplayEffect& effect) {
                                                return effect.kind ==
                                                           RuntimeDisplayEffectKind::set_vsync &&
                                                       !effect.enabled;
                                            }),
           "Graphics Defaults must also reverse the active VSync preview");
}

void done_persists_an_atomic_non_resolution_transaction() {
    TemporaryDirectory temporary;
    const auto settings_path = temporary.path() / "settings.toml";
    FrontendController controller{config_for(settings_path)};
    expect(controller.start(), "controller must start");
    open_settings(controller);
    static_cast<void>(controller.take_effects());

    const auto invert = row(controller.settings_menu().presentation(), SettingsRowId::invert_mouse);
    click(controller, right_arrow(invert.control_bounds));
    click(
        controller,
        center(button(controller.settings_menu().presentation(), SettingsTargetKind::done_button)));
    expect(controller.route() == FrontendRoute::select_menu,
           "successful Done must return to Select Menu");

    const TomlSettingsStore store{settings_path};
    const auto loaded = store.load();
    expect(loaded && loaded.settings.main.invert_mouse,
           "Done must atomically persist the complete approved snapshot");
    const auto effects = controller.take_effects();
    expect(has_effect<RuntimeInputEffect>(
               effects, [](const RuntimeInputEffect& effect) { return effect.invert_mouse; }),
           "Done must publish committed inversion and control preferences");
}

void resolution_timeout_restores_persisted_display_and_session() {
    TemporaryDirectory temporary;
    const auto settings_path = temporary.path() / "settings.toml";
    FrontendController controller{config_for(settings_path)};
    expect(controller.start(), "controller must start");
    open_settings(controller);
    static_cast<void>(controller.take_effects());

    click(controller, Point{350, 115});
    expect(controller.settings_menu().active_tab() == SettingsTab::graphics,
           "Graphics tab must be reachable through controller pointer dispatch");
    select_resolution_option(controller, 1U);
    expect(controller.settings_session().draft().graphics.resolution == Resolution{1'024U, 768U},
           "resolution choice must edit the staged transaction");
    click(
        controller,
        center(button(controller.settings_menu().presentation(), SettingsTargetKind::done_button)));
    expect(controller.route() == FrontendRoute::resolution_confirmation,
           "resolution edits must enter the 15-second safety route");
    auto effects = controller.take_effects();
    expect(has_effect<RuntimeDisplayEffect>(
               effects,
               [](const RuntimeDisplayEffect& effect) {
                   return effect.kind == RuntimeDisplayEffectKind::set_resolution &&
                          effect.resolution == Resolution{1'024U, 768U};
               }),
           "Done must request the temporary display mode before confirmation");

    controller.tick(std::chrono::seconds{15});
    expect(controller.route() == FrontendRoute::settings,
           "timeout must fail safe back to Graphics Settings");
    expect(controller.settings_session().committed().graphics.resolution == Resolution{800U, 600U},
           "timeout must restore the last successfully persisted transaction");
    effects = controller.take_effects();
    expect(has_effect<RuntimeDisplayEffect>(
               effects,
               [](const RuntimeDisplayEffect& effect) {
                   return effect.kind == RuntimeDisplayEffectKind::set_resolution &&
                          effect.resolution == Resolution{800U, 600U};
               }),
           "timeout must explicitly request the old display mode");
    expect(!std::filesystem::exists(settings_path),
           "unconfirmed resolution must never reach persistent storage");
}

void keep_persists_the_confirmed_resolution() {
    TemporaryDirectory temporary;
    const auto settings_path = temporary.path() / "settings.toml";
    FrontendController controller{config_for(settings_path)};
    expect(controller.start(), "controller must start");
    open_settings(controller);
    static_cast<void>(controller.take_effects());
    click(controller, Point{350, 115});
    select_resolution_option(controller, 1U);
    click(
        controller,
        center(button(controller.settings_menu().presentation(), SettingsTargetKind::done_button)));
    settle(controller);
    click(controller, Point{200, 460});
    expect(controller.route() == FrontendRoute::select_menu,
           "Keep must finalize the transaction and return to Select Menu");

    const auto loaded = TomlSettingsStore{settings_path}.load();
    expect(loaded && loaded.settings.graphics.resolution == Resolution{1'024U, 768U},
           "Keep must persist the confirmed resolution");
}

void persistence_failure_keeps_settings_open_and_restores_runtime() {
    TemporaryDirectory temporary;
    const auto directory_as_file = temporary.path() / "settings-directory";
    std::filesystem::create_directory(directory_as_file);
    FrontendController controller{config_for(directory_as_file)};
    expect(controller.start(), "directory load failure must remain non-fatal");
    open_settings(controller);
    static_cast<void>(controller.take_effects());

    const auto fullscreen =
        row(controller.settings_menu().presentation(), SettingsRowId::fullscreen);
    click(controller, center(fullscreen.control_bounds));
    static_cast<void>(controller.take_effects());
    click(
        controller,
        center(button(controller.settings_menu().presentation(), SettingsTargetKind::done_button)));
    expect(controller.route() == FrontendRoute::settings,
           "failed save must keep Settings open for recovery");
    expect(controller.settings_session().draft().main.fullscreen,
           "failed save must restore the last persisted/default session snapshot");
    const auto effects = controller.take_effects();
    expect(has_effect<RuntimeSettingsNoticeEffect>(
               effects,
               [](const RuntimeSettingsNoticeEffect& effect) {
                   return effect.kind == RuntimeSettingsNoticeKind::persistence_failed;
               }),
           "failed save must surface a typed non-fatal persistence notice");
    expect(has_effect<RuntimeDisplayEffect>(
               effects,
               [](const RuntimeDisplayEffect& effect) {
                   return effect.kind == RuntimeDisplayEffectKind::set_fullscreen && effect.enabled;
               }),
           "failed save must reverse any already-applied fullscreen preview");
}

void resolution_keep_persistence_failure_reverts_safely() {
    TemporaryDirectory temporary;
    const auto directory_as_file = temporary.path() / "settings-directory";
    std::filesystem::create_directory(directory_as_file);
    FrontendController controller{config_for(directory_as_file)};
    expect(controller.start(), "directory load failure must remain non-fatal");
    open_settings(controller);
    static_cast<void>(controller.take_effects());
    click(controller, Point{350, 115});
    select_resolution_option(controller, 1U);
    click(
        controller,
        center(button(controller.settings_menu().presentation(), SettingsTargetKind::done_button)));
    settle(controller);
    static_cast<void>(controller.take_effects());

    click(controller, Point{200, 460});
    expect(controller.route() == FrontendRoute::settings,
           "failed resolution persistence must return to Graphics Settings");
    expect(controller.settings_session().committed().graphics.resolution == Resolution{800U, 600U},
           "failed Keep must restore the last persisted settings transaction");
    const auto effects = controller.take_effects();
    expect(has_effect<RuntimeSettingsNoticeEffect>(
               effects,
               [](const RuntimeSettingsNoticeEffect& effect) {
                   return effect.kind == RuntimeSettingsNoticeKind::persistence_failed;
               }),
           "failed Keep must surface a persistence notice");
    expect(has_effect<RuntimeDisplayEffect>(
               effects,
               [](const RuntimeDisplayEffect& effect) {
                   return effect.kind == RuntimeDisplayEffectKind::set_resolution &&
                          effect.resolution == Resolution{800U, 600U};
               }),
           "failed Keep must request the previously persisted display mode");
}

void binding_rejections_are_forwarded_as_typed_notices() {
    TemporaryDirectory temporary;
    FrontendController controller{config_for(temporary.path() / "settings.toml")};
    expect(controller.start(), "controller must start");
    open_settings(controller);
    static_cast<void>(controller.take_effects());

    click(controller, Point{550, 115});
    expect(controller.settings_menu().active_tab() == SettingsTab::controls,
           "Controls tab must be reachable through controller dispatch");
    const auto ugc_category =
        row(controller.settings_menu().presentation(), SettingsRowId::ugc_controls_category);
    expect(ugc_category.visible, "retail-first Map Creator category must be visible");
    click(controller, center(ugc_category.bounds));
    const auto forward = row(controller.settings_menu().presentation(), SettingsRowId::forward);
    click(controller, center(forward.control_bounds));
    expect(controller.settings_menu().presentation().binding_capture.has_value(),
           "binding row must enter raw-input capture");

    const auto rejected = controller.capture_scancode(30U);
    expect(!rejected.accepted(), "reserved inventory scancode must be rejected");
    const auto effects = controller.take_effects();
    expect(has_effect<RuntimeSettingsNoticeEffect>(
               effects,
               [](const RuntimeSettingsNoticeEffect& effect) {
                   return effect.kind == RuntimeSettingsNoticeKind::binding_rejected &&
                          effect.control_action.has_value();
               }),
           "binding validation must cross the controller as a typed runtime notice");
}

struct TestCase final {
    std::string_view name;
    std::function<void()> body;
};

} // namespace

int main() {
    const std::vector<TestCase> tests{
        {"load_failure_is_non_fatal_and_main_actions_survive_routing",
         load_failure_is_non_fatal_and_main_actions_survive_routing},
        {"cancel_rolls_back_live_previews_without_writing",
         cancel_rolls_back_live_previews_without_writing},
        {"defaults_restore_the_active_tab_and_publish_live_effects",
         defaults_restore_the_active_tab_and_publish_live_effects},
        {"graphics_defaults_restore_the_live_vsync_preview",
         graphics_defaults_restore_the_live_vsync_preview},
        {"done_persists_an_atomic_non_resolution_transaction",
         done_persists_an_atomic_non_resolution_transaction},
        {"resolution_timeout_restores_persisted_display_and_session",
         resolution_timeout_restores_persisted_display_and_session},
        {"keep_persists_the_confirmed_resolution", keep_persists_the_confirmed_resolution},
        {"persistence_failure_keeps_settings_open_and_restores_runtime",
         persistence_failure_keeps_settings_open_and_restores_runtime},
        {"resolution_keep_persistence_failure_reverts_safely",
         resolution_keep_persistence_failure_reverts_safely},
        {"binding_rejections_are_forwarded_as_typed_notices",
         binding_rejections_are_forwarded_as_typed_notices},
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
