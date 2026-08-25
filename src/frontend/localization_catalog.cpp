#include "battlespades/frontend/localization_catalog.hpp"

#include "battlespades/core/utf8.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <system_error>
#include <utility>

namespace battlespades::frontend {
namespace {

constexpr std::uint32_t schema_version{1U};
constexpr std::uintmax_t maximum_file_bytes{8U * 1'024U * 1'024U};
constexpr std::size_t maximum_languages{64U};
constexpr std::size_t maximum_strings_per_language{16'384U};
constexpr std::size_t maximum_key_bytes{256U};
constexpr std::size_t maximum_value_bytes{8'192U};

[[nodiscard]] bool valid_locale(std::string_view locale) noexcept {
    if (locale.empty() || locale.size() > 32U) return false;
    return std::ranges::all_of(locale, [](unsigned char value) {
        return std::isalnum(value) != 0 || value == '-';
    });
}

[[nodiscard]] bool valid_utf8(std::string_view value) {
    return core::utf8_code_point_prefix(value, value.size()).size() == value.size();
}

[[nodiscard]] std::optional<std::string> bounded_text(const nlohmann::json& value,
                                                      std::size_t maximum) {
    if (!value.is_string()) return std::nullopt;
    auto text = value.get<std::string>();
    if (text.size() > maximum || !valid_utf8(text)) return std::nullopt;
    return text;
}

} // namespace

LocalizationCatalog::LocalizationCatalog(std::filesystem::path path) : path_{std::move(path)} {}

const std::filesystem::path& LocalizationCatalog::path() const noexcept { return path_; }

bool LocalizationCatalog::load() {
    last_error_.clear();
    if (path_.empty()) {
        last_error_ = "localization path is empty";
        return false;
    }
    std::error_code error;
    if (!std::filesystem::exists(path_, error)) {
        if (error) {
            last_error_ = "cannot inspect localization file: " + error.message();
            return false;
        }
        languages_.clear();
        language_infos_.clear();
        loaded_write_time_.reset();
        active_locale_ = "en";
        fallback_locale_ = "en";
        ++generation_;
        return true;
    }
    const auto size = std::filesystem::file_size(path_, error);
    if (error || size > maximum_file_bytes) {
        last_error_ = error ? "cannot inspect localization size: " + error.message()
                            : "localization file exceeds the 8 MiB safety limit";
        return false;
    }

    try {
        std::ifstream input{path_, std::ios::binary};
        if (!input) {
            last_error_ = "cannot open localization file";
            return false;
        }
        const auto document = nlohmann::json::parse(input);
        if (!document.is_object() || document.value("schema_version", 0U) != schema_version ||
            !document.contains("locales") || !document["locales"].is_object()) {
            last_error_ = "localization file must contain schema_version 1 and a locales object";
            return false;
        }
        if (document["locales"].size() == 0U ||
            document["locales"].size() > maximum_languages) {
            last_error_ = "localization file has an invalid locale count";
            return false;
        }

        const auto active = bounded_text(document.value("active_locale", nlohmann::json{"en"}),
                                         32U);
        const auto fallback = bounded_text(
            document.value("fallback_locale", nlohmann::json{"en"}), 32U);
        if (!active.has_value() || !fallback.has_value() || !valid_locale(*active) ||
            !valid_locale(*fallback)) {
            last_error_ = "active_locale and fallback_locale must be BCP-47 style tags";
            return false;
        }

        std::map<std::string, Language, std::less<>> parsed;
        std::vector<LanguageInfo> infos;
        infos.reserve(document["locales"].size());
        for (const auto& [locale, value] : document["locales"].items()) {
            if (!valid_locale(locale) || !value.is_object() ||
                !value.contains("strings") || !value["strings"].is_object() ||
                value["strings"].size() > maximum_strings_per_language) {
                last_error_ = "invalid locale entry: " + locale;
                return false;
            }
            const auto native_name = bounded_text(
                value.value("native_name", nlohmann::json{locale}), 96U);
            const auto font_asset = bounded_text(
                value.value("font_asset", nlohmann::json{""}), 256U);
            if (!native_name.has_value() || !font_asset.has_value()) {
                last_error_ = "invalid locale metadata: " + locale;
                return false;
            }
            Language language;
            language.info = {locale, *native_name, *font_asset};
            for (const auto& [key, text_value] : value["strings"].items()) {
                const auto translated = bounded_text(text_value, maximum_value_bytes);
                if (key.empty() || key.size() > maximum_key_bytes || !valid_utf8(key) ||
                    !translated.has_value()) {
                    last_error_ = "invalid translation in locale: " + locale;
                    return false;
                }
                language.strings.emplace(key, *translated);
            }
            infos.push_back(language.info);
            parsed.emplace(locale, std::move(language));
        }
        if (!parsed.contains(*active) || !parsed.contains(*fallback)) {
            last_error_ = "active or fallback locale is not present in locales";
            return false;
        }
        languages_ = std::move(parsed);
        language_infos_ = std::move(infos);
        active_locale_ = *active;
        fallback_locale_ = *fallback;
        loaded_write_time_ = std::filesystem::last_write_time(path_, error);
        if (error) loaded_write_time_.reset();
        ++generation_;
        return true;
    } catch (const nlohmann::json::exception& exception) {
        last_error_ = "invalid localization JSON: " + std::string{exception.what()};
        return false;
    }
}

bool LocalizationCatalog::reload_if_changed() {
    if (path_.empty()) return true;
    std::error_code error;
    if (!std::filesystem::exists(path_, error)) return !error;
    const auto current = std::filesystem::last_write_time(path_, error);
    if (error || (loaded_write_time_.has_value() && current == *loaded_write_time_)) {
        return !error;
    }
    return load();
}

std::optional<std::string_view>
LocalizationCatalog::lookup(std::string_view key) const noexcept {
    const auto lookup_in = [key](const Language* language) -> std::optional<std::string_view> {
        if (language == nullptr) return std::nullopt;
        const auto found = language->strings.find(key);
        if (found == language->strings.end()) return std::nullopt;
        return found->second;
    };
    if (const auto exact = lookup_in(find_language(active_locale_)); exact.has_value()) {
        return exact;
    }
    if (const auto separator = active_locale_.find('-'); separator != std::string::npos) {
        if (const auto base = lookup_in(find_language(
                std::string_view{active_locale_}.substr(0U, separator))); base.has_value()) {
            return base;
        }
    }
    return lookup_in(find_language(fallback_locale_));
}

std::string_view LocalizationCatalog::active_locale() const noexcept { return active_locale_; }

std::string_view LocalizationCatalog::active_font_asset() const noexcept {
    const auto* language = find_language(active_locale_);
    return language == nullptr ? std::string_view{} : language->info.font_asset;
}

std::span<const LanguageInfo> LocalizationCatalog::languages() const noexcept {
    return language_infos_;
}

std::uint64_t LocalizationCatalog::generation() const noexcept { return generation_; }

std::string_view LocalizationCatalog::last_error() const noexcept { return last_error_; }

const LocalizationCatalog::Language*
LocalizationCatalog::find_language(std::string_view locale) const noexcept {
    const auto found = languages_.find(locale);
    return found == languages_.end() ? nullptr : &found->second;
}

} // namespace battlespades::frontend
