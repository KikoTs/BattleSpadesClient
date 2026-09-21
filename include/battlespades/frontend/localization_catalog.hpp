#pragma once

#include <filesystem>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::frontend {

struct LanguageInfo final {
    std::string locale;
    std::string native_name;
    std::string font_asset;
};

/**
 * Hot-reloadable UTF-8 localization catalogue stored beside the executable.
 *
 * Missing keys intentionally return no value: the native frontend then uses
 * its source-recovered English table.  A partial community translation is
 * therefore useful immediately and cannot turn unknown widgets blank.
 */
class LocalizationCatalog final {
public:
    explicit LocalizationCatalog(std::filesystem::path path = {});

    [[nodiscard]] const std::filesystem::path& path() const noexcept;
    [[nodiscard]] bool load();
    [[nodiscard]] bool reload_if_changed();
    /** Selects an already loaded locale without reparsing translation files. */
    [[nodiscard]] bool set_active_locale(std::string_view locale);
    [[nodiscard]] std::optional<std::string_view> lookup(std::string_view key) const noexcept;
    [[nodiscard]] std::string_view active_locale() const noexcept;
    [[nodiscard]] std::string_view active_font_asset() const noexcept;
    [[nodiscard]] std::span<const LanguageInfo> languages() const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept;
    [[nodiscard]] std::string_view last_error() const noexcept;

private:
    struct Language final {
        LanguageInfo info;
        std::map<std::string, std::string, std::less<>> strings;
    };

    [[nodiscard]] const Language* find_language(std::string_view locale) const noexcept;

    std::filesystem::path path_;
    std::map<std::string, Language, std::less<>> languages_;
    std::vector<LanguageInfo> language_infos_;
    std::string active_locale_{"en"};
    std::string fallback_locale_{"en"};
    std::map<std::filesystem::path, std::filesystem::file_time_type> loaded_write_times_;
    std::uint64_t generation_{};
    std::string last_error_;
};

} // namespace battlespades::frontend
