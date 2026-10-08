#if defined(__HAIKU__)
#include <OS.h>
#include <kernel/image.h>
#endif

#include "battlespades/core/resource_paths.hpp"

#include <sstream>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace battlespades::core {
namespace {

[[nodiscard]] std::optional<std::filesystem::path>
existing_directory(const std::filesystem::path& candidate) noexcept {
    if (candidate.empty()) {
        return std::nullopt;
    }

    std::error_code error;
    const auto normalized = std::filesystem::weakly_canonical(candidate, error);
    if (error || !std::filesystem::is_directory(normalized, error) || error) {
        return std::nullopt;
    }
    return normalized;
}

[[nodiscard]] std::optional<ResourceLocation>
select_root(const std::filesystem::path& packaged,
            const std::filesystem::path& developer) noexcept {
    if (const auto root = existing_directory(packaged); root.has_value()) {
        return ResourceLocation{*root, ResourceOrigin::executable_adjacent};
    }
    if (const auto root = existing_directory(developer); root.has_value()) {
        return ResourceLocation{*root, ResourceOrigin::developer_fallback};
    }
    return std::nullopt;
}

void append_attempts(std::ostringstream& message,
                     std::string_view label,
                     const std::filesystem::path& packaged,
                     const std::filesystem::path& developer) {
    // u8string: string() throws for paths outside the ANSI code page.
    const auto text = [](const std::filesystem::path& path) {
        const auto encoded = path.u8string();
        return std::string{reinterpret_cast<const char*>(encoded.data()), encoded.size()};
    };
    message << label << " roots tried: '" << text(packaged) << '\'';
    if (!developer.empty()) {
        message << ", '" << text(developer) << '\'';
    }
}

} // namespace

std::optional<std::filesystem::path> current_executable_path(std::string& error) noexcept {
    try {
#if defined(_WIN32)
        constexpr std::size_t maximum_windows_path{32'768U};
        std::vector<wchar_t> buffer(512U);
        while (buffer.size() <= maximum_windows_path) {
            const auto copied =
                GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (copied == 0U) {
                error = "GetModuleFileNameW failed with error " +
                        std::to_string(static_cast<unsigned long>(GetLastError()));
                return std::nullopt;
            }
            if (static_cast<std::size_t>(copied) < buffer.size()) {
                error.clear();
                return std::filesystem::path{
                    std::wstring{buffer.data(), static_cast<std::size_t>(copied)}};
            }
            buffer.resize(buffer.size() * 2U);
        }
        error = "current executable path exceeds the Windows path limit";
        return std::nullopt;
#elif defined(__APPLE__)
        std::uint32_t required{};
        static_cast<void>(_NSGetExecutablePath(nullptr, &required));
        if (required == 0U) {
            error = "_NSGetExecutablePath did not report a buffer size";
            return std::nullopt;
        }
        std::vector<char> buffer(static_cast<std::size_t>(required) + 1U, '\0');
        if (_NSGetExecutablePath(buffer.data(), &required) != 0) {
            error = "_NSGetExecutablePath failed to copy the process image path";
            return std::nullopt;
        }
        error.clear();
        return std::filesystem::path{buffer.data()};
#elif defined(__linux__)
        std::vector<char> buffer(1'024U);
        constexpr std::size_t maximum_linux_path{1U << 20U};
        while (buffer.size() <= maximum_linux_path) {
            const auto copied = ::readlink("/proc/self/exe", buffer.data(), buffer.size());
            if (copied < 0) {
                error = "readlink(/proc/self/exe) failed";
                return std::nullopt;
            }
            if (static_cast<std::size_t>(copied) < buffer.size()) {
                error.clear();
                return std::filesystem::path{
                    std::string{buffer.data(), static_cast<std::size_t>(copied)}};
            }
            buffer.resize(buffer.size() * 2U);
        }
        error = "current executable path exceeds the Linux safety limit";
        return std::nullopt;
#elif defined(__HAIKU__)
    int32 cookie = 0;
    image_info info{};

    while (get_next_image_info(
               B_CURRENT_TEAM,
               &cookie,
               &info) == B_OK) {
        if (info.type == B_APP_IMAGE) {
            return info.name;
        }
    }

    throw std::runtime_error(
        "failed to discover current executable path on Haiku");
#else
        error = "current executable path discovery is unsupported on this platform";
        return std::nullopt;
#endif
    } catch (const std::exception& exception) {
        error = std::string{"current executable path discovery failed: "} + exception.what();
        return std::nullopt;
    } catch (...) {
        error = "current executable path discovery failed with an unknown exception";
        return std::nullopt;
    }
}

ResourceDiscoveryResult discover_resource_paths(const ResourceDiscoveryOptions& options) noexcept {
    try {
        if (options.executable_path.empty() || options.executable_path.parent_path().empty()) {
            return ResourceDiscoveryResult{
                std::nullopt,
                "resource discovery requires an executable path with a parent directory",
            };
        }

        std::error_code error;
        const auto executable_directory =
            std::filesystem::weakly_canonical(options.executable_path.parent_path(), error);
        if (error) {
            return ResourceDiscoveryResult{
                std::nullopt,
                "could not normalize executable directory: " + error.message(),
            };
        }

        const auto packaged_assets = executable_directory / "assets" / "original";
        const auto packaged_shaders = executable_directory / "shaders";
        const auto assets = select_root(packaged_assets, options.developer_asset_root);
        const auto shaders = select_root(packaged_shaders, options.developer_shader_root);
        if (assets.has_value() && shaders.has_value()) {
            return ResourceDiscoveryResult{
                ResourcePaths{*assets, *shaders},
                {},
            };
        }

        std::ostringstream message;
        message << "required BattleSpades resources were not found; ";
        if (!assets.has_value()) {
            append_attempts(message, "asset", packaged_assets, options.developer_asset_root);
        }
        if (!assets.has_value() && !shaders.has_value()) {
            message << "; ";
        }
        if (!shaders.has_value()) {
            append_attempts(message, "shader", packaged_shaders, options.developer_shader_root);
        }
        return ResourceDiscoveryResult{std::nullopt, message.str()};
    } catch (const std::exception& exception) {
        return ResourceDiscoveryResult{
            std::nullopt,
            std::string{"resource discovery failed: "} + exception.what(),
        };
    } catch (...) {
        return ResourceDiscoveryResult{
            std::nullopt,
            "resource discovery failed with an unknown exception",
        };
    }
}

} // namespace battlespades::core
