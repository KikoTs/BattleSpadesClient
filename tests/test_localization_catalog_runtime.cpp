#include "battlespades/frontend/localization_catalog.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using battlespades::frontend::LocalizationCatalog;

void expect(bool condition, std::string_view message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

std::filesystem::path localization_root() {
    return std::filesystem::path{AOS_TEST_LOCALIZATION_ROOT};
}

std::string translated(LocalizationCatalog& catalog, std::string_view key) {
    const auto value = catalog.lookup(key);
    expect(value.has_value(), "expected localization key is missing");
    return std::string{*value};
}

void shipped_catalog_loads_exact_utf8_and_declared_fonts() {
    LocalizationCatalog catalog{localization_root()};
    expect(catalog.load(), catalog.last_error());
    expect(catalog.languages().size() >= 14U,
           "all shipped language files must load through the runtime parser");

    expect(catalog.set_active_locale("ru"), "Russian locale must be selectable");
    expect(catalog.active_font_asset() == "fonts/Tuffy_Bold.ttf",
           "Russian must retain its recovered Cyrillic face");
    expect(translated(catalog, "SETTINGS") == "Настройки",
           "Russian UTF-8 must survive the real runtime catalogue");
    expect(translated(catalog, "ADD FRIEND") == "Добавить друга",
           "revival-only Russian controls must be translated");
    expect(translated(catalog, "SERVER_CONNECTION_FAILED") ==
               "Не удалось подключиться к серверу",
           "native Russian connection failures must not fall back to English");

    expect(catalog.set_active_locale("ja"), "Japanese locale must be selectable");
    expect(catalog.active_font_asset() == "fonts/NotoSansJP-SemiBold.ttf",
           "Japanese must request the bundled cross-platform font");
    expect(translated(catalog, "SETTINGS") == "設定",
           "Japanese UTF-8 must survive the real runtime catalogue");
    expect(translated(catalog, "UGC_MENU_SUBSCRIBE") == "購読",
           "known broken Japanese depot glyphs must remain repaired");
    expect(translated(catalog, "MATCHMAKING_UNAVAILABLE") ==
               "マッチメイキングを利用できません",
           "native Japanese matchmaking states must not fall back to English");
}

void platform_and_retail_locale_names_resolve_without_english_fallback() {
    LocalizationCatalog catalog{localization_root()};
    expect(catalog.load(), catalog.last_error());

    expect(catalog.set_active_locale("russian") && catalog.active_locale() == "ru",
           "retail Russian setting must migrate to BCP-47 ru");
    expect(catalog.set_active_locale("ja_JP") && catalog.active_locale() == "ja",
           "OS-style Japanese locale must resolve through the base pack");
    expect(catalog.set_active_locale("es-mx") && catalog.active_locale() == "es-MX",
           "locale matching must be case-insensitive and canonical");
    expect(catalog.set_active_locale("fr-CA") && catalog.active_locale() == "fr",
           "a missing regional pack must use its installed base language");

    const auto generation = catalog.generation();
    expect(!catalog.set_active_locale("not/a/locale"),
           "malformed locale names must be rejected");
    expect(catalog.active_locale() == "fr" && catalog.generation() == generation,
           "a rejected locale must not corrupt the active language");
}

void partial_community_pack_falls_back_to_utf8_english() {
    LocalizationCatalog catalog{localization_root()};
    expect(catalog.load(), catalog.last_error());
    expect(catalog.set_active_locale("bg_BG"),
           "Bulgarian OS locale must resolve to the community pack");
    expect(translated(catalog, "SETTINGS") == "Настройки",
           "translated Bulgarian keys must win over fallback");
    expect(translated(catalog, "A301") == "Block",
           "missing Bulgarian retail keys must fall back to English, not become blank");
}

} // namespace

int main() {
    shipped_catalog_loads_exact_utf8_and_declared_fonts();
    platform_and_retail_locale_names_resolve_without_english_fallback();
    partial_community_pack_falls_back_to_utf8_english();
    std::cout << "localization runtime tests passed\n";
    return EXIT_SUCCESS;
}
