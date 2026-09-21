#include "battlespades/text/text_rasterizer.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
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

using battlespades::text::TextCase;
using battlespades::text::TextErrorCode;
using battlespades::text::TextRasterizer;
using battlespades::text::TextRasterizerConfig;
using battlespades::text::TextRasterRequest;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] std::filesystem::path asset_root() {
#ifndef AOS_TEST_ASSET_ROOT
    throw std::runtime_error{"AOS_TEST_ASSET_ROOT is not defined"};
#else
    return std::filesystem::path{AOS_TEST_ASSET_ROOT};
#endif
}

[[nodiscard]] std::filesystem::path client_asset_root() {
#ifndef AOS_TEST_CLIENT_ASSET_ROOT
    throw std::runtime_error{"AOS_TEST_CLIENT_ASSET_ROOT is not defined"};
#else
    return std::filesystem::path{AOS_TEST_CLIENT_ASSET_ROOT};
#endif
}

[[nodiscard]] std::filesystem::path localization_root() {
#ifndef AOS_TEST_LOCALIZATION_ROOT
    throw std::runtime_error{"AOS_TEST_LOCALIZATION_ROOT is not defined"};
#else
    return std::filesystem::path{AOS_TEST_LOCALIZATION_ROOT};
#endif
}

[[nodiscard]] TextRasterizerConfig spades_config(std::uint32_t pixel_height = 36U) {
    return TextRasterizerConfig{asset_root(), "fonts/Spades.ttf", pixel_height, {}};
}

void initialization_is_scoped_to_the_asset_root_and_fails_closed() {
    TextRasterizer invalid{TextRasterizerConfig{
        asset_root(),
        "../README.md",
        16U,
        {},
    }};
    expect(!invalid.ready(), "font traversal must not initialize a rasterizer");
    expect(invalid.initialization_error_code() == TextErrorCode::invalid_configuration,
           "font traversal must be reported as invalid configuration");
    expect(invalid.initialization_error().find("outside") != std::string_view::npos,
           "initialization failure must explain the containment violation");

    const auto result = invalid.rasterize(
        TextRasterRequest{"QUIT", TextCase::preserve, std::nullopt, false});
    expect(!result && result.error_code == TextErrorCode::not_ready,
           "an unready rasterizer must not emit partial output");
    expect(!result.error.empty(), "failed rendering must retain useful initialization text");
}

void deterministic_keys_include_font_content_size_case_and_normalized_text() {
    TextRasterizer first{spades_config()};
    TextRasterizer second{spades_config()};
    expect(first.ready() && second.ready(), "retail Spades font must initialize");
    expect(first.font_asset_id() == "fonts/Spades.ttf",
           "font identity must remain asset-root relative");
    expect(first.font_fingerprint() == second.font_fingerprint(),
           "font fingerprint must be stable across independent services");

    const TextRasterRequest lower{"join match", TextCase::ascii_uppercase, 36U};
    const TextRasterRequest upper{"JOIN MATCH", TextCase::ascii_uppercase, 36U};
    const auto lower_key = first.cache_key(lower);
    const auto upper_key = second.cache_key(upper);
    expect(lower_key && upper_key && *lower_key.key == *upper_key.key,
           "uppercase-equivalent English labels must share a deterministic key");

    const auto different_size =
        first.cache_key(TextRasterRequest{"JOIN MATCH", TextCase::ascii_uppercase, 24U});
    const auto preserved =
        first.cache_key(TextRasterRequest{"JOIN MATCH", TextCase::preserve, 36U});
    expect(different_size && different_size.key->stable_hash != lower_key.key->stable_hash,
           "pixel height must participate in the key");
    expect(preserved && preserved.key->stable_hash != lower_key.key->stable_hash,
           "case policy must participate in the key even when bytes already match");
}

void english_menu_text_shapes_measures_and_rasterizes_to_transparent_rgba8() {
    TextRasterizer rasterizer{spades_config()};
    expect(rasterizer.ready(), std::string{rasterizer.initialization_error()});

    const auto first =
        rasterizer.rasterize(TextRasterRequest{"Join Match", TextCase::ascii_uppercase, 36U});
    expect(static_cast<bool>(first), first.error);
    expect(!first.from_cache, "first render must execute native shaping");
    expect(first.output->transformed_utf8 == "JOIN MATCH",
           "English uppercase mapping must occur before shaping");
    expect(first.output->metrics.glyph_count > 0U && first.output->metrics.advance_width_pixels > 0,
           "shaping must expose glyph count and measured advance");
    expect(first.output->metrics.advance_width_pixels == 163 &&
               first.output->metrics.content_width_pixels == 160U,
           "Spades 36 metrics must match the recovered FTGL JOIN MATCH bounds");
    expect(first.output->metrics.ascender_pixels > 0 &&
               first.output->metrics.line_height_pixels > 0,
           "font-wide vertical metrics must be present for name-plate layout");
    expect(first.output->bitmap.width > 0U && first.output->bitmap.height > 0U,
           "visible menu text must produce ink bounds");
    expect(first.output->bitmap.row_stride_bytes == first.output->bitmap.width * 4U,
           "RGBA rows must be tightly packed");
    expect(first.output->bitmap.pixels.size() ==
               static_cast<std::size_t>(first.output->bitmap.row_stride_bytes) *
                   first.output->bitmap.height,
           "RGBA allocation must exactly match its dimensions");

    bool found_coverage{};
    bool found_transparency{};
    for (std::size_t index = 0U; index < first.output->bitmap.pixels.size(); index += 4U) {
        const auto alpha = first.output->bitmap.pixels[index + 3U];
        found_coverage = found_coverage || alpha > 0U;
        found_transparency = found_transparency || alpha == 0U;
        if (alpha > 0U) {
            expect(first.output->bitmap.pixels[index] == 255U &&
                       first.output->bitmap.pixels[index + 1U] == 255U &&
                       first.output->bitmap.pixels[index + 2U] == 255U,
                   "covered glyph pixels must remain white for renderer tinting");
        }
    }
    expect(found_coverage && found_transparency,
           "glyph bitmap must contain both coverage and transparent background");

    const auto second =
        rasterizer.rasterize(TextRasterRequest{"join match", TextCase::ascii_uppercase, 36U});
    expect(second && second.from_cache, "normalized repeated label must hit the cache");
    expect(second.output == first.output, "cache hit must reuse immutable output ownership");
    const auto stats = rasterizer.cache_stats();
    expect(stats.hits == 1U && stats.misses == 1U && stats.entries == 1U,
           "cache accounting must be deterministic");
}

void valid_utf8_is_preserved_for_player_names_and_invalid_utf8_is_rejected() {
    TextRasterizer rasterizer{TextRasterizerConfig{
        asset_root(),
        "fonts/A750-Sans-Medium.ttf",
        16U,
        {},
    }};
    expect(rasterizer.ready(), std::string{rasterizer.initialization_error()});

    // Use the Unicode fallback face that the frontend selects for player-provided
    // names. A750 intentionally has retail-era coverage gaps and now fails closed
    // instead of silently shaping those gaps as question-mark/tofu glyphs.
    TextRasterizer unicode_names{TextRasterizerConfig{
        client_asset_root(),
        "fonts/NotoSansJP-SemiBold.ttf",
        16U,
        {},
    }};
    expect(unicode_names.ready(), std::string{unicode_names.initialization_error()});
    const std::string player_name{"Kiko \xC5\xBD"}; // U+017D LATIN CAPITAL LETTER Z WITH CARON.
    const auto player = unicode_names.rasterize(
        TextRasterRequest{player_name, TextCase::preserve, std::nullopt});
    expect(player && player.output->transformed_utf8 == player_name,
           "valid UTF-8 player names must remain byte-for-byte intact");
    const auto name_plate = rasterizer.rasterize(
        TextRasterRequest{"Welcome Player", TextCase::preserve, std::nullopt});
    expect(name_plate && name_plate.output->metrics.advance_width_pixels == 120 &&
               name_plate.output->metrics.content_width_pixels == 120U,
           "A750 16 metrics must match the recovered FTGL name-plate bounds");

    const std::string truncated_sequence{"bad\xE2\x82"};
    const auto invalid = rasterizer.rasterize(
        TextRasterRequest{truncated_sequence, TextCase::preserve, std::nullopt});
    expect(!invalid && invalid.error_code == TextErrorCode::invalid_utf8,
           "truncated UTF-8 must fail before HarfBuzz receives it");
    expect(invalid.error.find("UTF-8") != std::string::npos,
           "invalid encoding failure must identify UTF-8");

    const auto multiline =
        rasterizer.rasterize(TextRasterRequest{"Player\nTwo", TextCase::preserve, std::nullopt});
    expect(!multiline && multiline.error_code == TextErrorCode::invalid_request,
           "the bounded one-line service must reject line breaks explicitly");
}

void every_retail_ui_font_face_initializes_and_rasterizes() {
    constexpr std::array<std::string_view, 5U> font_assets{
        "fonts/Spades.ttf",
        "fonts/Edo.ttf",
        "fonts/A750-Sans-Medium.ttf",
        "fonts/A750-Sans-Bold.ttf",
        "fonts/Tuffy_Bold.ttf",
    };

    for (const auto font_asset : font_assets) {
        TextRasterizer rasterizer{TextRasterizerConfig{
            asset_root(),
            std::string{font_asset},
            20U,
            {},
        }};
        expect(rasterizer.ready(),
               std::string{font_asset} + ": " +
                   std::string{rasterizer.initialization_error()});

        const auto output =
            rasterizer.rasterize(TextRasterRequest{"BATTLE SPADES", TextCase::preserve, 20U});
        expect(output && output.output->metrics.glyph_count > 0U &&
                   output.output->bitmap.width > 0U && output.output->bitmap.height > 0U,
               std::string{font_asset} + " must emit visible UI glyphs");
    }
}

void unicode_coverage_is_detected_before_missing_glyphs_are_shaped() {
    TextRasterizer display{spades_config()};
    TextRasterizer unicode{TextRasterizerConfig{
        asset_root(),
        "fonts/Tuffy_Bold.ttf",
        20U,
        {},
    }};
    expect(display.ready() && unicode.ready(), "coverage fixture fonts must initialize");
    expect(display.supports_text("SETTINGS") && unicode.supports_text("SETTINGS"),
           "both retail faces must report their shared Latin coverage");
    constexpr std::string_view cyrillic_with_yo{"Ёлка"};
    expect(!display.supports_text(cyrillic_with_yo),
           "the Spades display face must reject unsupported Cyrillic glyphs");
    expect(unicode.supports_text(cyrillic_with_yo),
           "Tuffy must expose complete Cyrillic coverage before shaping");
    expect(!unicode.supports_text(std::string{"broken\xE2\x82"}),
           "coverage checks must fail closed for malformed UTF-8");
}

void legacy_player_symbols_use_missing_glyphs_without_stopping_the_ui() {
    // Captured from the live 192.248.177.80:32887 roster: U+1F1F7 has no
    // glyph in any bundled face. This used to abort the entire frontend.
    const std::string name{"beta keks\xF0\x9F\x87\xB7"};
    TextRasterizerConfig config{asset_root(), "fonts/Tuffy_Bold.ttf", 20U, {}};
    TextRasterizer strict{config};
    config.allow_missing_glyphs = true;
    TextRasterizer live{config};
    TextRasterizer display{spades_config()};
    TextRasterizer japanese{{client_asset_root(), "fonts/NotoSansJP-SemiBold.ttf", 20U, {}}};
    expect(strict.ready() && live.ready() && display.ready() && japanese.ready(),
           "legacy name fixture fonts must load");
    expect(!live.supports_text(name) && !japanese.supports_text(name),
           "rendering fallback must not falsely claim actual symbol coverage");
    std::array<TextRasterizer*, 5U> candidates{nullptr, &live, &live, &display, &japanese};
    auto* selected = select_text_font(candidates, name);
    expect(selected == &live, "an uncovered name must retain a ready UI face");
    const TextRasterRequest request{name, TextCase::preserve, 20U};
    const auto result = selected->rasterize(request);
    expect(result && result.output->transformed_utf8 == name &&
               result.output->metrics.glyph_count == 10U &&
               result.output->bitmap.width > 0U && result.output->bitmap.height > 0U,
           "legacy names must render with a placeholder without changing identity text");
    const auto repeat = selected->rasterize(request);
    expect(repeat && repeat.from_cache && repeat.output == result.output,
           "unsupported symbols must reuse the bounded raster cache");
    expect(strict.cache_key(request).key != live.cache_key(request).key,
           "strict and live glyph policies must have distinct texture identities");
    const auto strict_result = strict.rasterize(request);
    expect(!strict_result && strict_result.error_code == TextErrorCode::font_error,
           "strict font/translation validation must still detect absent glyphs");
    const auto outlined = live.rasterize({name, TextCase::unicode_uppercase, 20U, true});
    expect(outlined && outlined.output->transformed_utf8 == "BETA KEKS\xF0\x9F\x87\xB7",
           "outlined and uppercase HUD paths must also tolerate unsupported symbols");
    const auto symbol = live.rasterize({"\xF0\x9F\x87\xB7", TextCase::preserve, 20U});
    expect(symbol && symbol.output->bitmap.width > 0U &&
               symbol.output->metrics.advance_width_pixels > 0,
           "an entirely unsupported name must still have a visible placeholder");
    expect(select_text_font(candidates, "Settings") == &live &&
               select_text_font(candidates, "日本語") == &japanese,
           "complete fallback coverage must still win over missing-glyph rendering");
    const std::array<TextRasterizer*, 2U> absent{nullptr, nullptr};
    expect(select_text_font(absent, name) == nullptr, "missing fonts remain an initialization error");
    const auto invalid = live.rasterize({"bad\xF0\x9F\x87", TextCase::preserve, 20U});
    expect(!invalid && invalid.error_code == TextErrorCode::invalid_utf8,
           "glyph fallback must not bypass UTF-8 validation");
    const auto oversized = live.rasterize(
        {std::string(config.limits.maximum_utf8_bytes + 1U, 'A'), TextCase::preserve, 20U});
    expect(!oversized && oversized.error_code == TextErrorCode::resource_limit,
           "live glyph fallback must retain the resource bounds");
}

void major_language_runs_rasterize_as_unicode_not_question_marks() {
    TextRasterizer cyrillic{TextRasterizerConfig{
        asset_root(),
        "fonts/Tuffy_Bold.ttf",
        24U,
        {},
    }};
    const std::string russian{"Настройки — Ёлка"};
    expect(cyrillic.ready() && cyrillic.supports_text(russian),
           "Tuffy must cover the recovered Russian UI run");
    const auto russian_output = cyrillic.rasterize(
        TextRasterRequest{russian, TextCase::preserve, 24U});
    expect(russian_output && russian_output.output->transformed_utf8 == russian &&
               russian_output.output->metrics.glyph_count > 10U &&
               russian_output.output->bitmap.width > 0U,
           "Russian must survive UTF-8 shaping and produce visible glyphs");
    const std::string russian_mixed_case{"Настройки — ёлка"};
    const std::string russian_uppercase{"НАСТРОЙКИ — ЁЛКА"};
    expect(cyrillic.supports_text(russian_mixed_case, TextCase::unicode_uppercase),
           "font selection must validate the transformed Cyrillic run");
    const auto uppercase_output = cyrillic.rasterize(
        TextRasterRequest{russian_mixed_case, TextCase::unicode_uppercase, 24U});
    expect(uppercase_output &&
               uppercase_output.output->transformed_utf8 == russian_uppercase,
           "localized uppercase labels must case Cyrillic instead of only ASCII");

    TextRasterizer japanese{TextRasterizerConfig{
        client_asset_root(),
        "fonts/NotoSansJP-SemiBold.ttf",
        24U,
        {},
    }};
    const std::string label{"設定・プレイヤープロフィール"};
    expect(japanese.ready() && japanese.supports_text(label),
           "the bundled Noto face must cover recovered Japanese UI text");
    const auto japanese_output = japanese.rasterize(
        TextRasterRequest{label, TextCase::preserve, 24U});
    expect(japanese_output && japanese_output.output->transformed_utf8 == label &&
               japanese_output.output->metrics.glyph_count > 5U &&
               japanese_output.output->bitmap.width > 0U,
           "Japanese must survive UTF-8 shaping and produce visible glyphs");

    const std::string recovered_ugc_label{"購読"};
    TextRasterizer display{spades_config()};
    expect(!display.supports_text(recovered_ugc_label),
           "the Latin display face must not claim Japanese UGC glyph coverage");
    expect(japanese.supports_text(recovered_ugc_label),
           "the bundled Noto face must cover the recovered Japanese UGC label");
    const auto ugc_output = japanese.rasterize(
        TextRasterRequest{recovered_ugc_label, TextCase::preserve, 43U});
    expect(ugc_output && ugc_output.output->transformed_utf8 == recovered_ugc_label &&
               ugc_output.output->bitmap.width > 0U,
           std::string{"recovered Japanese UGC label failed: "} + ugc_output.error);

    const std::string polish_no_break_space{"Pobierz pełną\xC2\xA0wersję"};
    const auto polish_output = cyrillic.rasterize(
        TextRasterRequest{polish_no_break_space, TextCase::preserve, 24U});
    expect(polish_output &&
               polish_output.output->transformed_utf8 == "Pobierz pełną wersję",
           "unsupported Unicode no-break spacing must normalize to a visible-font space");

}

void bundled_faces_cover_every_shipped_translation() {
    const auto verify_pack = [](std::string_view locale,
                                std::initializer_list<TextRasterizer*> faces) {
        const auto path = localization_root() / (std::string{locale} + ".json");
        std::ifstream input{path, std::ios::binary};
        expect(static_cast<bool>(input), "language pack fixture must open");
        const auto document = nlohmann::json::parse(input);
        const auto verify = [&](std::string_view key, const std::string& value) {
            const auto covered = std::ranges::any_of(faces, [&](const auto* face) {
                return face != nullptr && face->supports_text(value);
            });
            expect(covered,
                   std::string{locale} + " font is missing glyphs for " + std::string{key});
        };
        verify("native_name", document.at("native_name").get_ref<const std::string&>());
        for (const auto& [key, value] : document.at("strings").items()) {
            verify(key, value.get_ref<const std::string&>());
        }
    };

    TextRasterizer cyrillic{TextRasterizerConfig{
        asset_root(), "fonts/Tuffy_Bold.ttf", 24U, {}}};
    TextRasterizer japanese{TextRasterizerConfig{
        client_asset_root(), "fonts/NotoSansJP-SemiBold.ttf", 24U, {}}};
    expect(cyrillic.ready() && japanese.ready(),
           "bundled Unicode coverage faces must initialize");
    // The runtime resolves a whole run through this same fallback family.
    // Checking every shipped pack catches code-page damage and obscure glyph
    // gaps in long UGC/tutorial strings, not merely the handful of labels a
    // screenshot happens to exercise.
    constexpr std::array<std::string_view, 14U> locales{
        "bg", "cs", "de", "en", "es", "es-MX", "fr",
        "it", "ja", "pl", "pt-BR", "ru", "tr", "uk",
    };
    for (const auto locale : locales) {
        verify_pack(locale, {&cyrillic, &japanese});
    }
}

void retail_ftgl_outline_uses_a_distinct_cache_entry_and_expands_ink() {
    TextRasterizer rasterizer{TextRasterizerConfig{
        asset_root(),
        "fonts/A750-Sans-Medium.ttf",
        14U,
        {},
    }};
    expect(rasterizer.ready(), std::string{rasterizer.initialization_error()});

    const TextRasterRequest normal_request{
        "OldKiller", TextCase::preserve, 14U, false};
    const TextRasterRequest outline_request{
        "OldKiller", TextCase::preserve, 14U, true};
    const auto normal_key = rasterizer.cache_key(normal_request);
    const auto outline_key = rasterizer.cache_key(outline_request);
    expect(normal_key && outline_key &&
               normal_key.key->stable_hash != outline_key.key->stable_hash &&
               normal_key.key->canonical != outline_key.key->canonical,
           "retail outline and normal glyph bitmaps must never alias in cache");

    const auto normal = rasterizer.rasterize(normal_request);
    const auto outline = rasterizer.rasterize(outline_request);
    expect(normal && outline, outline ? normal.error : outline.error);
    expect(!normal.from_cache && !outline.from_cache,
           "normal and outlined glyphs must each rasterize once");
    expect(outline.output->metrics.advance_width_pixels ==
               normal.output->metrics.advance_width_pixels &&
               outline.output->metrics.content_width_pixels ==
                   normal.output->metrics.content_width_pixels,
           "FTTextureGlyph's outline must not alter shaped advance/content metrics");
    expect(outline.output->bitmap.width > normal.output->bitmap.width &&
               outline.output->bitmap.height > normal.output->bitmap.height,
           "the recovered 140/64px outside stroke must expand glyph ink bounds");
    expect(outline.output->metrics.ink_left_pixels <
               normal.output->metrics.ink_left_pixels &&
               outline.output->metrics.ink_top_pixels <
                   normal.output->metrics.ink_top_pixels,
           "outside stroking must extend ink to the left and above normal glyphs");

    const auto outline_again = rasterizer.rasterize(outline_request);
    expect(outline_again && outline_again.from_cache &&
               outline_again.output == outline.output,
           "a repeated outline request must reuse its own immutable cache entry");
    const auto stats = rasterizer.cache_stats();
    expect(stats.hits == 1U && stats.misses == 2U && stats.entries == 2U,
           "normal and retail-outline cache accounting must remain deterministic");
}

void executable_assets_can_live_below_a_unicode_directory() {
    const auto root =
        std::filesystem::temp_directory_path() /
        std::filesystem::path{u8"BattleSpades Кирилица – assets"};
    std::error_code error;
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::create_directories(root / "fonts", error);
    expect(!error, "Unicode test asset directory must be creatable");
    std::filesystem::copy_file(asset_root() / "fonts/Spades.ttf",
                               root / "fonts/Spades.ttf",
                               std::filesystem::copy_options::overwrite_existing,
                               error);
    expect(!error, "retail font must copy into a Unicode directory");

    TextRasterizer rasterizer{
        TextRasterizerConfig{root, "fonts/Spades.ttf", 36U, {}}};
    expect(rasterizer.ready(),
           std::string{"Unicode font path failed: "} +
               std::string{rasterizer.initialization_error()});
    const auto output = rasterizer.rasterize(
        TextRasterRequest{"PLAYER IDENTITY", TextCase::preserve, std::nullopt, false});
    expect(output && output.output->metrics.glyph_count > 0U,
           "font loaded from a Unicode path must shape and rasterize");

    std::filesystem::remove_all(root, error);
}

void cache_and_request_limits_remain_bounded() {
    auto config = spades_config();
    config.limits.maximum_cache_entries = 1U;
    config.limits.maximum_cache_bytes = 1U * 1'024U * 1'024U;
    config.limits.maximum_utf8_bytes = 32U;
    config.limits.maximum_pixel_height = 64U;
    TextRasterizer rasterizer{std::move(config)};
    expect(rasterizer.ready(), std::string{rasterizer.initialization_error()});

    const auto first = rasterizer.rasterize(TextRasterRequest{"FIRST", TextCase::preserve, 36U});
    const auto second = rasterizer.rasterize(TextRasterRequest{"SECOND", TextCase::preserve, 36U});
    expect(first && second, "bounded cache must not reject valid raster output");
    auto stats = rasterizer.cache_stats();
    expect(stats.entries == 1U && stats.evictions == 1U,
           "single-entry cache must deterministically evict its LRU item");

    const auto first_again =
        rasterizer.rasterize(TextRasterRequest{"FIRST", TextCase::preserve, 36U});
    expect(first_again && !first_again.from_cache,
           "evicted output must be shaped again rather than accessed stale");

    const auto oversized =
        rasterizer.rasterize(TextRasterRequest{std::string(33U, 'A'), TextCase::preserve, 36U});
    expect(!oversized && oversized.error_code == TextErrorCode::resource_limit,
           "oversized UTF-8 input must fail before allocation");
    const auto too_tall = rasterizer.rasterize(TextRasterRequest{"A", TextCase::preserve, 65U});
    expect(!too_tall && too_tall.error_code == TextErrorCode::resource_limit,
           "pixel height above the configured limit must fail closed");

    rasterizer.clear_cache();
    stats = rasterizer.cache_stats();
    expect(stats.entries == 0U && stats.bytes == 0U,
           "explicit cache clear must release every internally owned bitmap");
}

struct TestCase final {
    std::string_view name;
    std::function<void()> body;
};

} // namespace

int main() {
    const std::vector<TestCase> tests{
        {"initialization_is_scoped_to_the_asset_root_and_fails_closed",
         initialization_is_scoped_to_the_asset_root_and_fails_closed},
        {"deterministic_keys_include_font_content_size_case_and_normalized_text",
         deterministic_keys_include_font_content_size_case_and_normalized_text},
        {"english_menu_text_shapes_measures_and_rasterizes_to_transparent_rgba8",
         english_menu_text_shapes_measures_and_rasterizes_to_transparent_rgba8},
        {"valid_utf8_is_preserved_for_player_names_and_invalid_utf8_is_rejected",
         valid_utf8_is_preserved_for_player_names_and_invalid_utf8_is_rejected},
        {"every_retail_ui_font_face_initializes_and_rasterizes",
         every_retail_ui_font_face_initializes_and_rasterizes},
        {"unicode_coverage_is_detected_before_missing_glyphs_are_shaped",
         unicode_coverage_is_detected_before_missing_glyphs_are_shaped},
        {"major_language_runs_rasterize_as_unicode_not_question_marks",
         major_language_runs_rasterize_as_unicode_not_question_marks},
        {"legacy_player_symbols_use_missing_glyphs_without_stopping_the_ui",
         legacy_player_symbols_use_missing_glyphs_without_stopping_the_ui},
        {"bundled_faces_cover_every_shipped_translation",
         bundled_faces_cover_every_shipped_translation},
        {"retail_ftgl_outline_uses_a_distinct_cache_entry_and_expands_ink",
         retail_ftgl_outline_uses_a_distinct_cache_entry_and_expands_ink},
        {"executable_assets_can_live_below_a_unicode_directory",
         executable_assets_can_live_below_a_unicode_directory},
        {"cache_and_request_limits_remain_bounded", cache_and_request_limits_remain_bounded},
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
