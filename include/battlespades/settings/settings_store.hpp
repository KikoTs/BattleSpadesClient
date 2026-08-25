#pragma once

#include "battlespades/settings/client_settings.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace battlespades::settings {

struct SettingsLoadResult final {
    ClientSettings settings{retail_default_settings()};
    bool success{false};
    bool file_found{false};
    std::string error{};
    std::vector<std::string> ignored_keys{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return success;
    }
};

struct SettingsSaveResult final {
    bool success{false};
    std::string error{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return success;
    }
};

/** Persistence boundary for loading and atomically saving client settings. */
class SettingsStore {
public:
    virtual ~SettingsStore() = default;

    [[nodiscard]] virtual SettingsLoadResult load() const = 0;
    [[nodiscard]] virtual SettingsSaveResult save(const ClientSettings& settings) const = 0;
};

/**
 * Dependency-free, bounded TOML store.
 *
 * The parser accepts the deliberately small schema emitted by save(). Unknown
 * sections and keys are retained as diagnostics and ignored for forward
 * compatibility. A malformed recognized value fails the entire load and
 * returns retail defaults, so partially parsed state is never installed.
 */
class TomlSettingsStore final : public SettingsStore {
public:
    explicit TomlSettingsStore(std::filesystem::path path);

    [[nodiscard]] const std::filesystem::path& path() const noexcept;
    [[nodiscard]] SettingsLoadResult load() const override;
    [[nodiscard]] SettingsSaveResult save(const ClientSettings& settings) const override;

private:
    std::filesystem::path path_{};
};

} // namespace battlespades::settings
