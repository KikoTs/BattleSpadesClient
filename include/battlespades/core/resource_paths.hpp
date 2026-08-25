#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace battlespades::core {

enum class ResourceOrigin : std::uint8_t {
    executable_adjacent,
    developer_fallback,
};

struct ResourceLocation final {
    std::filesystem::path root{};
    ResourceOrigin origin{ResourceOrigin::executable_adjacent};
};

struct ResourcePaths final {
    ResourceLocation assets{};
    ResourceLocation shaders{};
};

/** Inputs kept explicit so resource precedence is deterministic and testable. */
struct ResourceDiscoveryOptions final {
    std::filesystem::path executable_path{};
    std::filesystem::path developer_asset_root{};
    std::filesystem::path developer_shader_root{};
};

struct ResourceDiscoveryResult final {
    std::optional<ResourcePaths> paths{};
    std::string error{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return paths.has_value();
    }
};

/**
 * Returns the current process image path on Windows, Linux, and macOS.
 *
 * This does not inspect the working directory or argv[0], both of which may be
 * unrelated to the installed executable. Failure is reported without throwing.
 */
[[nodiscard]] std::optional<std::filesystem::path>
current_executable_path(std::string& error) noexcept;

/**
 * Finds immutable runtime content with package roots taking precedence.
 *
 * The package layout is `<executable>/assets/original` and
 * `<executable>/shaders`. Asset and shader roots resolve independently so a
 * partial developer build can use a packaged root for one and a source root
 * for the other. Developer roots are considered only after the corresponding
 * executable-adjacent directory is absent.
 */
[[nodiscard]] ResourceDiscoveryResult
discover_resource_paths(const ResourceDiscoveryOptions& options) noexcept;

} // namespace battlespades::core
