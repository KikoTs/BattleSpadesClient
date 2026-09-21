#include "battlespades/frontend/localization_catalog.hpp"
#include "battlespades/frontend/ui_layout_editor.hpp"

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void expect(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error{std::string{message}};
}

struct TemporaryDirectory final {
    std::filesystem::path path;

    TemporaryDirectory() {
        path = std::filesystem::temp_directory_path() /
               ("battlespades-ui-customization-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

battlespades::ui::DrawList sample_draw_list() {
    using namespace battlespades::ui;
    DrawList list;
    list.push(SpriteDrawCommand{
        "panel.png", {10.0, 20.0, 100.0, 40.0}, DrawSpace::design_pixels,
        TextureFilter::nearest, TextureAnchor::top_left});
    list.push(TextDrawCommand{
        "JOIN_MATCH", "fonts/Tuffy_Bold.ttf", {20.0, 25.0, 80.0, 20.0},
        DrawSpace::design_pixels, 12.0});
    return list;
}

void layout_round_trips_and_applies_semantic_ids() {
    TemporaryDirectory directory;
    const auto path = directory.path / "ui-layout.json";
    battlespades::frontend::UiLayoutStore store{path};
    expect(store.load(), "missing layout must mean empty valid overrides");

    auto list = sample_draw_list();
    const auto elements = store.elements("main_menu", list);
    expect(elements.size() == 2U &&
               elements[0U].id == "main_menu/sprite.panel.png#0" &&
               elements[1U].id == "main_menu/text.JOIN_MATCH#0",
           "draw commands need deterministic semantic editor ids");
    store.set(elements[1U].id, {31.0, 32.0, 90.0, 24.0});
    expect(store.save(), "valid overrides must save");

    battlespades::frontend::UiLayoutStore reloaded{path};
    expect(reloaded.load() && reloaded.override_count() == 1U,
           "saved overrides must reload");
    auto rebuilt = sample_draw_list();
    reloaded.apply("main_menu", rebuilt);
    const auto* text = std::get_if<battlespades::ui::TextDrawCommand>(
        &rebuilt.commands()[1U]);
    expect(text != nullptr &&
               text->destination == battlespades::ui::DrawRect{31.0, 32.0, 90.0, 24.0},
           "reloaded rectangle must transform the real draw command");
}

void editor_drag_resize_and_reset_are_persistent_actions() {
    TemporaryDirectory directory;
    battlespades::frontend::UiLayoutStore store{directory.path / "ui-layout.json"};
    expect(store.load(), "empty layout must load");
    battlespades::frontend::UiLayoutEditor editor{store};
    editor.set_active(true);
    auto list = sample_draw_list();
    editor.observe("main_menu", list);

    expect(editor.pointer_press({25, 30}) && editor.pointer_move({35, 40}) &&
               editor.pointer_release({35, 40}),
           "drag input must be consumed by the offline editor");
    auto moved = sample_draw_list();
    store.apply("main_menu", moved);
    const auto* text = std::get_if<battlespades::ui::TextDrawCommand>(&moved.commands()[1U]);
    expect(text != nullptr && text->destination.x == 30.0 && text->destination.y == 35.0,
           "drag must move the topmost selected text in design pixels");
    expect(editor.reset_selected(), "delete/reset action must be accepted");
    auto reset = sample_draw_list();
    store.apply("main_menu", reset);
    text = std::get_if<battlespades::ui::TextDrawCommand>(&reset.commands()[1U]);
    expect(text != nullptr && text->destination.x == 20.0 && text->destination.y == 25.0,
           "reset must restore the source-backed retail rectangle");
}

void malformed_layout_keeps_last_valid_document() {
    TemporaryDirectory directory;
    const auto path = directory.path / "ui-layout.json";
    battlespades::frontend::UiLayoutStore store{path};
    expect(store.load(), "empty layout must load");
    store.set("main_menu/sprite.panel.png#0", {1.0, 2.0, 3.0, 4.0});
    expect(store.save(), "valid layout must save");
    {
        std::ofstream output{path, std::ios::binary | std::ios::trunc};
        output << "{broken";
    }
    expect(!store.load(), "malformed JSON must fail closed");
    auto list = sample_draw_list();
    store.apply("main_menu", list);
    const auto* sprite = std::get_if<battlespades::ui::SpriteDrawCommand>(&list.commands()[0U]);
    expect(sprite != nullptr && sprite->destination.x == 1.0,
           "failed reload must preserve the last valid in-memory layout");
}

void localization_supports_utf8_fallback_and_live_reload() {
    TemporaryDirectory directory;
    const auto path = directory.path / "localization.json";
    {
        std::ofstream output{path, std::ios::binary};
        output << R"({
          "schema_version": 1,
          "active_locale": "bg",
          "fallback_locale": "en",
          "locales": {
            "en": {"native_name":"English","font_asset":"","strings":{"JOIN_MATCH":"Join Match","QUIT":"Quit"}},
            "bg": {"native_name":"Български","font_asset":"fonts/Gen_Shin_Gothic_Monospace_Bold.ttf","strings":{"JOIN_MATCH":"Присъедини се"}},
            "ja": {"native_name":"日本語","font_asset":"fonts/NotoSansJP-SemiBold.ttf","strings":{}}
          }
        })";
    }
    battlespades::frontend::LocalizationCatalog catalog{path};
    expect(catalog.load(), "valid UTF-8 localization must load");
    expect(catalog.lookup("JOIN_MATCH") == "Присъедини се",
           "active locale must override English");
    expect(catalog.lookup("QUIT") == "Quit",
           "missing translated keys must fall back to English");
    expect(!catalog.lookup("UNKNOWN").has_value(),
           "unknown keys must defer to the source-backed native fallback");
    expect(catalog.active_font_asset() == "fonts/Gen_Shin_Gothic_Monospace_Bold.ttf" &&
               catalog.languages().size() == 3U,
           "locale metadata must expose Unicode font and available languages");
}

void localization_directory_loads_one_file_per_language() {
    TemporaryDirectory directory;
    const auto path = directory.path / "localization";
    std::filesystem::create_directories(path);
    {
        std::ofstream output{path / "en.json", std::ios::binary};
        output << R"({"schema_version":1,"locale":"en","native_name":"English","font_asset":"","strings":{"QUIT":"Quit"}})";
    }
    {
        std::ofstream output{path / "ru.json", std::ios::binary};
        output << R"({"schema_version":1,"locale":"ru","native_name":"Русский","font_asset":"fonts/Gen_Shin_Gothic_Monospace_Bold.ttf","strings":{"QUIT":"Выход"}})";
    }
    battlespades::frontend::LocalizationCatalog catalog{path};
    expect(catalog.load() && catalog.languages().size() == 2U,
           "localization directory must discover every JSON language pack");
    expect(catalog.set_active_locale("ru") && catalog.lookup("QUIT") == "Выход",
           "Settings must be able to select a loaded language immediately");
    expect(!catalog.set_active_locale("invalid/locale"),
           "unknown or malformed locale selections must fail closed");
}

struct TestCase final {
    std::string_view name;
    std::function<void()> body;
};

} // namespace

int main() {
    const std::vector<TestCase> tests{
        {"layout_round_trips_and_applies_semantic_ids",
         layout_round_trips_and_applies_semantic_ids},
        {"editor_drag_resize_and_reset_are_persistent_actions",
         editor_drag_resize_and_reset_are_persistent_actions},
        {"malformed_layout_keeps_last_valid_document",
         malformed_layout_keeps_last_valid_document},
        {"localization_supports_utf8_fallback_and_live_reload",
         localization_supports_utf8_fallback_and_live_reload},
        {"localization_directory_loads_one_file_per_language",
         localization_directory_loads_one_file_per_language},
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
