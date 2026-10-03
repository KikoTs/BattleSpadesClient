#include "battlespades/settings/client_settings.hpp"
#include "battlespades/settings/settings_session.hpp"
#include "battlespades/settings/settings_store.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using battlespades::settings::Antialiasing;
using battlespades::settings::BindingAssignmentStatus;
using battlespades::settings::BindingKind;
using battlespades::settings::ClientSettings;
using battlespades::settings::ControlAction;
using battlespades::settings::DrawDistance;
using battlespades::settings::GraphicsApi;
using battlespades::settings::InputBinding;
using battlespades::settings::QualityLevel;
using battlespades::settings::Resolution;
using battlespades::settings::SettingsSession;
using battlespades::settings::SettingsTab;
using battlespades::settings::ShaderQuality;
using battlespades::settings::TomlSettingsStore;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
                ("battlespades-settings-tests-" + std::to_string(stamp));
        std::filesystem::create_directories(root_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] std::filesystem::path path(std::string_view relative) const {
        return root_ / std::filesystem::path{relative};
    }

    [[nodiscard]] const std::filesystem::path& root() const noexcept {
        return root_;
    }

private:
    std::filesystem::path root_{};
};

void write_text(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    expect(static_cast<bool>(output), "test fixture could not be opened");
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    expect(static_cast<bool>(output), "test fixture could not be written");
}

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    expect(static_cast<bool>(input), "saved settings file could not be opened");
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void retail_defaults_cover_every_recovered_option() {
    const auto settings = battlespades::settings::retail_default_settings();
    expect(settings.schema_version == battlespades::settings::current_settings_schema_version,
           "defaults must use the current schema");
    expect(settings.main.master_volume == 1.0 && settings.main.music_volume == 1.0,
           "retail volume defaults must be exact");
    expect(!settings.main.invert_mouse, "retail Main toggles must be exact");
    expect(settings.graphics.window_mode == battlespades::settings::WindowMode::borderless,
           "retail fullscreen=true starts as native borderless fullscreen");
    expect(settings.graphics.resolution == Resolution{800U, 600U},
           "retail resolution default must be 800x600");
    expect(settings.graphics.antialiasing == Antialiasing::off &&
               settings.graphics.graphics_api == GraphicsApi::automatic &&
               settings.graphics.effect_quality == QualityLevel::medium &&
               settings.graphics.draw_distance == DrawDistance::high &&
               settings.graphics.shader_quality == ShaderQuality::medium &&
               settings.graphics.texture_quality == QualityLevel::medium &&
               settings.graphics.model_quality == QualityLevel::high && !settings.graphics.vsync,
           "every retail Graphics default must be represented");
    expect(settings.controls.mouse_sensitivity == 0.1, "retail mouse sensitivity must be exact");

    std::set<std::string_view> names;
    for (std::size_t index{}; index < battlespades::settings::control_action_count; ++index) {
        const auto action = static_cast<ControlAction>(index);
        const auto name = battlespades::settings::control_action_name(action);
        expect(!name.empty(), "every recovered Controls row must have a stable name");
        expect(names.emplace(name).second, "Controls row names must be unique");
        expect(battlespades::settings::control_action_from_name(name) == action,
               "Controls row names must round-trip");
    }
    expect(battlespades::settings::binding_to_string(
               settings.controls.binding(ControlAction::forward)) == "keyboard:w",
           "Forward must default to W");
    expect(battlespades::settings::binding_to_string(
               settings.controls.binding(ControlAction::crouch)) == "keyboard:left_ctrl",
           "Crouch must default to Left Ctrl");
    expect(battlespades::settings::binding_to_string(
               settings.controls.binding(ControlAction::aim)) == "mouse:right",
           "Aim must represent the retail RMB default");
    expect(settings.controls.binding(ControlAction::toggle_hud).is_unbound(),
           "Toggle HUD must preserve its unbound retail default");
    expect(static_cast<bool>(battlespades::settings::validate_settings(settings)),
           "retail defaults must validate");
}

void normalization_is_bounded_and_deterministic() {
    auto malformed = battlespades::settings::retail_default_settings();
    malformed.schema_version = 99U;
    malformed.main.master_volume = std::numeric_limits<double>::quiet_NaN();
    malformed.main.music_volume = -4.0;
    malformed.main.audio_device = "Speakers\n[graphics]";
    malformed.graphics.resolution = {0U, 99'999U};
    malformed.graphics.graphics_api = static_cast<GraphicsApi>(99U);
    malformed.graphics.antialiasing = static_cast<Antialiasing>(7U);
    malformed.graphics.effect_quality = static_cast<QualityLevel>(8U);
    malformed.graphics.draw_distance = static_cast<DrawDistance>(1U);
    malformed.graphics.shader_quality = static_cast<ShaderQuality>(12);
    malformed.graphics.texture_quality = static_cast<QualityLevel>(9U);
    malformed.controls.mouse_sensitivity = 4.0;
    static_cast<void>(
        malformed.controls.set_binding(ControlAction::forward, InputBinding::keyboard(900U)));
    static_cast<void>(
        malformed.controls.set_binding(ControlAction::backward, InputBinding::keyboard(26U)));
    static_cast<void>(
        malformed.controls.set_binding(ControlAction::jump, InputBinding::keyboard(30U)));

    const auto normalized = battlespades::settings::normalize_settings(malformed);
    expect(normalized.schema_version == battlespades::settings::current_settings_schema_version,
           "normalization must repair the schema version");
    expect(normalized.main.master_volume == 1.0 && normalized.main.music_volume == 0.0,
           "non-finite values must restore defaults and finite values must clamp");
    expect(normalized.main.audio_device.empty(),
           "invalid device labels must normalize to automatic selection");
    expect(normalized.graphics.resolution == Resolution{320U, 16'384U},
           "resolution must clamp to safe platform-independent bounds");
    expect(normalized.graphics.antialiasing == Antialiasing::off &&
               normalized.graphics.graphics_api == GraphicsApi::automatic &&
               normalized.graphics.effect_quality == QualityLevel::medium &&
               normalized.graphics.draw_distance == DrawDistance::high &&
               normalized.graphics.shader_quality == ShaderQuality::medium &&
               normalized.graphics.texture_quality == QualityLevel::medium,
           "invalid graphics enums must restore their retail defaults");
    expect(normalized.controls.mouse_sensitivity == 1.0, "mouse sensitivity must clamp to one");
    expect(battlespades::settings::binding_to_string(
               normalized.controls.binding(ControlAction::forward)) == "keyboard:w",
           "malformed bindings must restore their row default");
    expect(battlespades::settings::binding_to_string(
               normalized.controls.binding(ControlAction::backward)) == "keyboard:s",
           "duplicate bindings must deterministically restore their row default");
    expect(battlespades::settings::binding_to_string(
               normalized.controls.binding(ControlAction::jump)) == "keyboard:space",
           "reserved inventory bindings must restore their row default");
    expect(static_cast<bool>(battlespades::settings::validate_settings(normalized)),
           "normalized settings must always validate");
}

void edit_sessions_commit_cancel_and_reset_per_tab() {
    const auto defaults = battlespades::settings::retail_default_settings();
    SettingsSession session{defaults};
    expect(!session.dirty(), "a new session must start clean");

    auto main = session.draft().main;
    main.master_volume = 0.25;
    session.set_main(main);
    auto graphics = session.draft().graphics;
    graphics.vsync = true;
    graphics.model_quality = QualityLevel::low;
    session.set_graphics(graphics);
    expect(session.dirty(), "editing any tab must dirty the complete transaction");

    main = session.draft().main;
    main.language = "de";
    main.show_skins = false;
    main.ability_hints = true;
    main.invert_mouse = true;
    main.music_volume = 0.4;
    session.set_main(main);
    session.reset_tab(SettingsTab::main);
    expect(session.draft().main.master_volume == defaults.main.master_volume &&
               session.draft().main.music_volume == defaults.main.music_volume &&
               session.draft().main.invert_mouse == defaults.main.invert_mouse,
           "Main Defaults must restore retail MAIN_DEFAULT's keys");
    expect(session.draft().main.language == "de" && !session.draft().main.show_skins &&
               session.draft().main.ability_hints,
           "Main Defaults must keep the native-only language and cosmetic options");
    expect(session.draft().graphics.vsync &&
               session.draft().graphics.model_quality == QualityLevel::low,
           "per-tab Defaults must preserve edits on other tabs");

    session.cancel();
    expect(!session.dirty() && session.draft() == defaults,
           "Cancel must restore the complete committed snapshot");

    main = session.draft().main;
    main.invert_mouse = true;
    session.set_main(main);
    expect(session.commit(), "committing an edit must report a change");
    expect(!session.dirty() && session.committed().main.invert_mouse,
           "commit must atomically promote the complete draft");
    expect(!session.commit(), "committing an unchanged draft must report no change");
}

void binding_assignment_rejects_conflicts_and_inventory_keys() {
    SettingsSession session;
    const auto reserved =
        session.assign_binding(ControlAction::forward, InputBinding::keyboard(30U));
    expect(reserved.status == BindingAssignmentStatus::reserved_inventory_key,
           "1-9 inventory keys must not be assignable");

    const auto conflict =
        session.assign_binding(ControlAction::backward, InputBinding::keyboard(26U));
    expect(conflict.status == BindingAssignmentStatus::conflicts_with_existing_action &&
               conflict.conflicting_action == ControlAction::forward,
           "duplicate assignments must report the conflicting row");

    const auto invalid = session.assign_binding(ControlAction::jump,
                                                InputBinding{BindingKind::keyboard_scancode, 999U});
    expect(invalid.status == BindingAssignmentStatus::invalid_binding,
           "out-of-range physical bindings must fail closed");

    expect(session.assign_binding(ControlAction::forward, InputBinding::unbound()).accepted(),
           "a binding may be explicitly cleared");
    expect(session.assign_binding(ControlAction::backward, InputBinding::keyboard(26U)).accepted(),
           "a released physical key must become assignable");
    expect(session.draft().controls.binding(ControlAction::backward) == InputBinding::keyboard(26U),
           "accepted binding must update the draft");
}

void every_shader_tier_round_trips_on_disk() {
    // A tier missing from the store's name table used to serialise silently as
    // "medium", downgrading the player's choice at save time with no error, and
    // a tier missing from the parser failed the whole file at load. Cover every
    // enumerator so neither can regress unnoticed.
    constexpr std::array<ShaderQuality, 5U> tiers{ShaderQuality::compatibility,
                                                  ShaderQuality::low,
                                                  ShaderQuality::medium,
                                                  ShaderQuality::high,
                                                  ShaderQuality::ultra};
    constexpr std::array<std::string_view, 5U> tokens{
        "compatibility", "low", "medium", "high", "ultra"};
    for (std::size_t index{}; index < tiers.size(); ++index) {
        TemporaryDirectory temporary;
        const auto path = temporary.path("tier/client-settings.toml");
        TomlSettingsStore store{path};

        auto settings = battlespades::settings::retail_default_settings();
        settings.graphics.shader_quality = tiers[index];
        const auto saved = store.save(settings);
        expect(static_cast<bool>(saved), std::string{"tier save failed: "} + saved.error);

        const auto text = read_text(path);
        const std::string expected = "shader_quality = \"" + std::string{tokens[index]} + "\"";
        expect(text.find(expected) != std::string::npos,
               "tier must serialise as " + expected + ", not be silently downgraded");

        const auto loaded = store.load();
        expect(static_cast<bool>(loaded), std::string{"tier load failed: "} + loaded.error);
        expect(loaded.settings.graphics.shader_quality == tiers[index],
               "every shader tier must survive a save/load round trip");
    }
}

void every_graphics_api_round_trips_on_disk() {
    constexpr std::array<GraphicsApi, 6U> apis{
        GraphicsApi::automatic,
        GraphicsApi::direct3d11,
        GraphicsApi::direct3d12,
        GraphicsApi::vulkan,
        GraphicsApi::opengl,
        GraphicsApi::metal,
    };
    constexpr std::array<std::string_view, 6U> tokens{
        "auto", "direct3d11", "direct3d12", "vulkan", "opengl", "metal"};

    for (std::size_t index{}; index < apis.size(); ++index) {
        TemporaryDirectory temporary;
        const auto path = temporary.path("backend/client-settings.toml");
        TomlSettingsStore store{path};
        auto settings = battlespades::settings::retail_default_settings();
        settings.graphics.graphics_api = apis[index];

        const auto saved = store.save(settings);
        expect(static_cast<bool>(saved), std::string{"graphics API save failed: "} + saved.error);
        const auto text = read_text(path);
        const std::string expected = "graphics_api = \"" + std::string{tokens[index]} + "\"";
        expect(text.find(expected) != std::string::npos,
               "graphics API must use its stable portable token");

        const auto loaded = store.load();
        expect(static_cast<bool>(loaded), std::string{"graphics API load failed: "} + loaded.error);
        expect(loaded.settings.graphics.graphics_api == apis[index],
               "every graphics API must survive a save/load round trip");
    }
}

void toml_round_trip_is_human_readable_and_atomic() {
    TemporaryDirectory temporary;
    const auto path = temporary.path("nested/client-settings.toml");
    TomlSettingsStore store{path};

    auto settings = battlespades::settings::retail_default_settings();
    settings.main.language = "ru";
    settings.main.master_volume = 0.375;
    settings.main.music_volume = 0.1;
    settings.main.audio_device = "OpenAL Soft on Speakers (Player's \"Headset\")";
    settings.main.show_skins = false;
    settings.main.show_other_skins = false;
    settings.main.weapon_motion = false;
    settings.graphics.resolution = {1'680U, 1'050U};
    settings.graphics.graphics_api = GraphicsApi::vulkan;
    settings.graphics.antialiasing = Antialiasing::samples_4;
    settings.graphics.shader_quality = ShaderQuality::compatibility;
    settings.graphics.vsync = true;
    settings.graphics.window_mode = battlespades::settings::WindowMode::exclusive;
    settings.graphics.render_interpolation = false;
    // Every native Graphics addition away from its default.
    settings.graphics.field_of_view = 95.0;
    settings.graphics.frame_limit = battlespades::settings::FrameLimit::custom;
    settings.graphics.frame_rate_cap = 240U;
    settings.graphics.low_latency = false;
    settings.graphics.show_fps = true;
    settings.graphics.render_scale = 0.67;
    settings.graphics.upscale = battlespades::settings::UpscaleFilter::bilinear;
    settings.graphics.sharpness = 0.3;
    settings.graphics.anisotropic_filtering = false;
    settings.graphics.smooth_textures = false;
    settings.graphics.shadow_quality = battlespades::settings::ShadowQuality::ultra;
    settings.graphics.shadow_distance = battlespades::settings::ShadowDistance::far;
    settings.graphics.ambient_occlusion = battlespades::settings::EffectLevel::high;
    settings.graphics.bloom = battlespades::settings::EffectLevel::low;
    settings.graphics.motion_blur = battlespades::settings::EffectLevel::medium;
    settings.graphics.brightness = -0.1;
    settings.graphics.gamma = 1.3;
    settings.graphics.color_vision = battlespades::settings::ColorVision::tritanopia;
    settings.controls.mouse_sensitivity = 0.1;
    static_cast<void>(
        settings.controls.set_binding(ControlAction::toggle_hud, InputBinding::keyboard(53U)));

    const auto saved = store.save(settings);
    expect(static_cast<bool>(saved), std::string{"settings save failed: "} + saved.error);
    const auto text = read_text(path);
    expect(text.find("[main]") != std::string::npos &&
               text.find("[graphics]") != std::string::npos &&
               text.find("[controls.bindings]") != std::string::npos,
           "saved settings must use readable TOML sections");
    expect(text.find("language = \"ru\"") != std::string::npos &&
               text.find("resolution = \"1680x1050\"") != std::string::npos &&
               text.find("graphics_api = \"vulkan\"") != std::string::npos &&
               text.find("toggle_hud = \"keyboard:backquote\"") != std::string::npos,
           "saved settings must use readable option and binding values");
    expect(text.find("window_mode = \"exclusive\"") != std::string::npos &&
               text.find("fullscreen_mode = \"exclusive\"") != std::string::npos &&
               text.find("fullscreen = true") != std::string::npos &&
               text.find("render_interpolation = false") != std::string::npos,
           "native display options must be saved as readable TOML");
    {
        const auto defaults = battlespades::settings::retail_default_settings();
        expect(defaults.graphics.window_mode == battlespades::settings::WindowMode::borderless &&
                   defaults.graphics.render_interpolation,
               "fresh installs default to borderless fullscreen and render interpolation");
    }
    expect(text.find("music_volume = 0.1\n") != std::string::npos &&
               text.find("mouse_sensitivity = 0.1\n") != std::string::npos,
           "human-edited scalar values must use their shortest round-trip decimal");

    const auto loaded = store.load();
    expect(static_cast<bool>(loaded), std::string{"settings load failed: "} + loaded.error);
    expect(loaded.file_found, "round-trip load must report the existing file");
    expect(loaded.settings == settings, "saved settings must round-trip exactly");

    for (const auto& entry : std::filesystem::directory_iterator(path.parent_path())) {
        expect(entry.path() == path, "successful atomic save must leave no temporary sibling");
    }
}

void missing_and_unknown_data_fail_safely() {
    TemporaryDirectory temporary;
    const auto missing_path = temporary.path("missing.toml");
    const auto missing = TomlSettingsStore{missing_path}.load();
    expect(static_cast<bool>(missing) && !missing.file_found,
           "a missing file must safely produce defaults");
    auto fresh_install = battlespades::settings::retail_default_settings();
    fresh_install.graphics.shader_quality = ShaderQuality::compatibility;
    expect(missing.settings == fresh_install,
           "missing-file defaults must be exact, with the Retail (Legacy) tier for new installs");

    const auto future_path = temporary.path("future.toml");
    write_text(future_path,
               "schema_version = 1\n"
               "future_root = 7\n"
               "[main]\n"
               "master_volume = 0.5\n"
               "future_toggle = true\n"
               "[future.section]\n"
               "mystery = \"safe\"\n");
    const auto future = TomlSettingsStore{future_path}.load();
    expect(static_cast<bool>(future), "unknown future data must be ignored safely");
    expect(future.settings.main.master_volume == 0.5,
           "known values beside unknown data must still load");
    expect(future.ignored_keys.size() == 4U,
           "ignored sections and keys must remain observable diagnostics");
}

void legacy_fullscreen_keys_migrate_to_window_mode() {
    using battlespades::settings::WindowMode;
    TemporaryDirectory temporary;
    const auto load = [&](std::string_view name, std::string_view body) {
        const auto path = temporary.path(std::string{name});
        write_text(path, std::string{"schema_version = 1\n"} + std::string{body});
        const auto loaded = TomlSettingsStore{path}.load();
        expect(static_cast<bool>(loaded), std::string{name} + ": " + loaded.error);
        return loaded.settings.graphics.window_mode;
    };
    // 0.2.1 and older: retail [main] fullscreen + native [graphics] fullscreen_mode.
    expect(load("off.toml", "[main]\nfullscreen = false\n[graphics]\n"
                            "fullscreen_mode = \"exclusive\"\n") == WindowMode::windowed,
           "Fullscreen OFF migrates to Windowed whatever the kind said");
    expect(load("on.toml", "[main]\nfullscreen = true\n") == WindowMode::borderless,
           "Fullscreen ON without a kind was the native borderless default");
    expect(load("exclusive.toml", "[graphics]\nfullscreen_mode = \"exclusive\"\n[main]\n"
                                  "fullscreen = true\n") == WindowMode::exclusive,
           "an explicit exclusive kind survives, in either key order");
    expect(load("kind_only.toml", "[graphics]\nfullscreen_mode = \"exclusive\"\n") ==
               WindowMode::exclusive,
           "a kind without the toggle follows the retail fullscreen=true default");
    expect(load("none.toml", "[main]\nmaster_volume = 0.5\n") == WindowMode::borderless,
           "a file with neither key keeps the borderless default");
    // A new file carries legacy mirrors for older builds; window_mode wins.
    expect(load("new.toml", "[main]\nfullscreen = true\n[graphics]\nwindow_mode = \"windowed\"\n"
                            "fullscreen_mode = \"exclusive\"\n") == WindowMode::windowed,
           "window_mode wins over the legacy mirrors");
    // A file from before the native Graphics rows loads with their defaults.
    {
        const auto old_path = temporary.path("old.toml");
        write_text(old_path, "schema_version = 1\n[graphics]\nvsync = true\n");
        const auto old = TomlSettingsStore{old_path}.load();
        auto expected = battlespades::settings::retail_default_settings();
        expected.graphics.vsync = true;
        expect(static_cast<bool>(old) && old.settings == expected,
               "missing native Graphics keys must load as their defaults");
        const auto out_of_range = temporary.path("fov.toml");
        write_text(out_of_range, "schema_version = 1\n[graphics]\nfield_of_view = 140\n");
        expect(!static_cast<bool>(TomlSettingsStore{out_of_range}.load()),
               "an out-of-range field of view must fail the load");
    }
    const auto bad_path = temporary.path("bad.toml");
    write_text(bad_path, "schema_version = 1\n[graphics]\nwindow_mode = \"fullscreen\"\n");
    expect(!static_cast<bool>(TomlSettingsStore{bad_path}.load()),
           "an unknown window_mode must fail the load like any other bad enum");

    using battlespades::settings::alt_enter_window_mode;
    expect(alt_enter_window_mode(WindowMode::borderless, WindowMode::exclusive) ==
                   WindowMode::windowed &&
               alt_enter_window_mode(WindowMode::exclusive, WindowMode::borderless) ==
                   WindowMode::windowed,
           "Alt+Enter leaves either fullscreen mode for a window");
    expect(alt_enter_window_mode(WindowMode::windowed, WindowMode::exclusive) ==
                   WindowMode::exclusive &&
               alt_enter_window_mode(WindowMode::windowed, WindowMode::windowed) ==
                   WindowMode::borderless,
           "Alt+Enter returns to the last fullscreen mode, borderless when there was none");
}

void malformed_files_never_install_partial_state() {
    TemporaryDirectory temporary;
    const auto malformed_path = temporary.path("malformed.toml");
    write_text(malformed_path,
               "schema_version = 1\n"
               "[main]\n"
               "master_volume = 0.25\n"
               "music_volume = not-a-number\n");
    const auto malformed = TomlSettingsStore{malformed_path}.load();
    expect(!static_cast<bool>(malformed), "malformed recognized values must fail the load");
    expect(malformed.settings == battlespades::settings::retail_default_settings(),
           "failed loads must return a complete default snapshot, not partial state");

    const auto conflict_path = temporary.path("conflict.toml");
    write_text(conflict_path,
               "schema_version = 1\n"
               "[controls.bindings]\n"
               "forward = \"keyboard:w\"\n"
               "backward = \"keyboard:w\"\n");
    const auto conflict = TomlSettingsStore{conflict_path}.load();
    expect(!static_cast<bool>(conflict),
           "conflicting persisted bindings must fail rather than silently remap controls");

    const auto target_directory = temporary.path("cannot-replace-directory");
    std::filesystem::create_directories(target_directory);
    const auto failed_save =
        TomlSettingsStore{target_directory}.save(battlespades::settings::retail_default_settings());
    expect(!static_cast<bool>(failed_save), "atomic replacement of a directory must fail safely");
    expect(std::filesystem::is_directory(target_directory),
           "failed atomic save must preserve the previous destination");
}

void local_skin_visibility_preserves_independent_preferences() {
    battlespades::settings::MainSettings preferences;
    expect(preferences.skins_visible(true)&&preferences.skins_visible(false)&&preferences.weapon_motion,
           "existing installs retain all skins and movement");
    preferences.show_other_skins=false;
    expect(preferences.skins_visible(true)&&!preferences.skins_visible(false), "mine only hides remote skins");
    preferences.show_skins=false;
    expect(!preferences.skins_visible(true)&&!preferences.skins_visible(false), "master toggle hides every skin");
    preferences.show_skins=true;
    expect(preferences.skins_visible(true)&&!preferences.skins_visible(false), "master toggle preserves mine-only choice");
}

struct TestCase final {
    std::string_view name;
    std::function<void()> body;
};

} // namespace

int main() {
    const std::vector<TestCase> tests{
        {"local_skin_visibility_preserves_independent_preferences",local_skin_visibility_preserves_independent_preferences},
        {"retail_defaults_cover_every_recovered_option",
         retail_defaults_cover_every_recovered_option},
        {"normalization_is_bounded_and_deterministic", normalization_is_bounded_and_deterministic},
        {"edit_sessions_commit_cancel_and_reset_per_tab",
         edit_sessions_commit_cancel_and_reset_per_tab},
        {"binding_assignment_rejects_conflicts_and_inventory_keys",
         binding_assignment_rejects_conflicts_and_inventory_keys},
        {"toml_round_trip_is_human_readable_and_atomic",
         toml_round_trip_is_human_readable_and_atomic},
        {"every_shader_tier_round_trips_on_disk", every_shader_tier_round_trips_on_disk},
        {"every_graphics_api_round_trips_on_disk", every_graphics_api_round_trips_on_disk},
        {"missing_and_unknown_data_fail_safely", missing_and_unknown_data_fail_safely},
        {"malformed_files_never_install_partial_state",
         malformed_files_never_install_partial_state},
        {"legacy_fullscreen_keys_migrate_to_window_mode",
         legacy_fullscreen_keys_migrate_to_window_mode},
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
