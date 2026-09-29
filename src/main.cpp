#include "battlespades/core/application.hpp"
#include "battlespades/core/build_info.hpp"
#include "battlespades/core/command_line.hpp"
#include "battlespades/core/resource_paths.hpp"
#include "battlespades/headless/headless_module.hpp"

#if defined(AOS_HAS_NATIVE_BACKENDS)
#include "battlespades/assets/asset_install.hpp"
#include "battlespades/frontend/native_frontend_module.hpp"
#include "battlespades/platform/sdl_window_module.hpp"
#endif

#include <filesystem>
#include <cstdio>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#if defined(__APPLE__) && defined(AOS_HAS_NATIVE_BACKENDS)
#include <SDL3/SDL.h>
#endif

#if defined(_WIN32) && defined(AOS_WINDOWS_GUI_SUBSYSTEM)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdio>
#include <cstdlib>
#endif

namespace {

#if defined(AOS_HAS_NATIVE_BACKENDS)
struct AssetReadiness final {
    std::optional<std::filesystem::path> root{};
    std::string error{};
    bool cancelled{};
};

[[nodiscard]] AssetReadiness
ensure_runtime_assets(const std::filesystem::path& executable_path) {
    using battlespades::assets::AssetInstallerExit;
    using battlespades::assets::AssetVerificationDepth;

    const auto executable_directory = executable_path.parent_path();
    const auto packaged_manifest = executable_directory / "asset-manifest.json";
    const auto developer_manifest = std::filesystem::path{AOS_DEVELOPER_ASSET_MANIFEST};
    std::filesystem::path manifest_path;
    std::error_code code;
    if (std::filesystem::is_regular_file(packaged_manifest, code) && !code) {
        manifest_path = packaged_manifest;
    } else if (!developer_manifest.empty() &&
               std::filesystem::is_regular_file(developer_manifest, code) && !code) {
        manifest_path = developer_manifest;
    } else {
        return {std::nullopt,
                "the asset verification manifest is missing beside the client",
                false};
    }

    const auto loaded = battlespades::assets::load_asset_manifest(manifest_path);
    if (!loaded) {
        return {std::nullopt, loaded.error, false};
    }

    const auto packaged_root = executable_directory / "assets" / "original";
    const auto developer_root = std::filesystem::path{AOS_DEVELOPER_ASSET_ROOT};
    for (const auto& candidate : {packaged_root, developer_root}) {
        if (candidate.empty()) {
            continue;
        }
        const auto check = battlespades::assets::verify_asset_tree(
            candidate, *loaded.manifest, AssetVerificationDepth::metadata);
        if (check) {
            return {candidate, {}, false};
        }
    }

#if defined(_WIN32)
    const auto installer = executable_directory / "BattleSpadesAssetInstaller.exe";
#else
    const auto installer = executable_directory / "BattleSpadesAssetInstaller";
#endif
    std::string installer_error;
    const auto outcome = battlespades::assets::run_asset_installer(
        installer, manifest_path, packaged_root, installer_error);
    if (outcome == AssetInstallerExit::cancelled) {
        return {std::nullopt, std::move(installer_error), true};
    }
    if (outcome != AssetInstallerExit::installed) {
        return {std::nullopt, std::move(installer_error), false};
    }

    const auto installed = battlespades::assets::verify_asset_tree(
        packaged_root, *loaded.manifest, AssetVerificationDepth::metadata);
    if (!installed) {
        return {std::nullopt,
                "the asset installer completed but runtime verification failed: " +
                    installed.error,
                false};
    }
    return {packaged_root, {}, false};
}
#endif

[[nodiscard]] int exit_code_for(battlespades::core::RunResult result) noexcept {
    using battlespades::core::RunResult;
    return result == RunResult::success ? 0 : 1;
}

int run_client(int argc, char* argv[]) {
    std::vector<std::string_view> arguments;
    if (argc > 1) {
        arguments.reserve(static_cast<std::size_t>(argc - 1));
    }
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }

    const auto parsed = battlespades::core::parse_command_line(arguments);
    if (!parsed) {
        std::cerr << "BattleSpadesClient: " << parsed.error << '\n'
                  << battlespades::core::command_line_usage();
        return 2;
    }

    const auto& options = *parsed.options;
    if (options.action == battlespades::core::LaunchAction::show_help) {
        std::cout << battlespades::core::command_line_usage();
        return 0;
    }
    if (options.action == battlespades::core::LaunchAction::show_version) {
        std::cout << battlespades::core::BuildInfo::product_name << ' '
                  << battlespades::core::BuildInfo::version << " ("
                  << battlespades::core::BuildInfo::profile << ")\n";
        return 0;
    }

    auto runtime = options.runtime;
#if defined(AOS_HAS_NATIVE_BACKENDS)
    const bool graphical = !options.headless;
    if (graphical && !options.runtime_lifetime_explicit) {
        runtime.tick_limit.reset();
        runtime.pace_to_wall_clock = true;
    }
#else
    constexpr bool graphical{false};
#endif

    battlespades::core::Application application{runtime};

#if defined(AOS_HAS_NATIVE_BACKENDS)
    battlespades::frontend::NativeFrontendModule* frontend_observer{};
    if (graphical) {
        std::string executable_error;
        const auto executable_path = battlespades::core::current_executable_path(executable_error);
        if (!executable_path.has_value()) {
            std::cerr << "BattleSpadesClient: " << executable_error << '\n';
            return 1;
        }
        const auto assets = ensure_runtime_assets(*executable_path);
        if (!assets.root.has_value()) {
            if (!assets.cancelled) {
                std::cerr << "BattleSpadesClient: " << assets.error << '\n';
                return 1;
            }
            return 0;
        }
        const auto resources = battlespades::core::discover_resource_paths(
            battlespades::core::ResourceDiscoveryOptions{
                *executable_path,
                *assets.root,
                AOS_DEVELOPER_SHADER_ROOT,
            });
        if (!resources) {
            std::cerr << "BattleSpadesClient: " << resources.error << '\n';
            return 1;
        }

        auto window = std::make_unique<battlespades::platform::SdlWindowModule>(
            battlespades::platform::SdlWindowConfig{
                "Ace of Spades",
                battlespades::platform::WindowExtent{800U, 600U},
                256U,
                true,
                true,
                false,
                true,
            });
        auto* const window_observer = window.get();
        if (!application.add_module(std::move(window))) {
            std::cerr << "BattleSpadesClient: failed to register SDL window runtime\n";
            return 1;
        }

        battlespades::frontend::NativeFrontendConfig frontend_config;
        frontend_config.executable_directory = executable_path->parent_path();
        const auto packaged_client_assets =
            executable_path->parent_path() / "assets" / "client";
        const auto developer_client_assets =
            std::filesystem::path{AOS_DEVELOPER_ASSET_ROOT}.parent_path() / "client";
        std::error_code client_assets_error;
        frontend_config.client_asset_root =
            std::filesystem::is_directory(packaged_client_assets, client_assets_error) &&
                    !client_assets_error
                ? packaged_client_assets
                : developer_client_assets;
        frontend_config.asset_root = resources.paths->assets.root;
        frontend_config.shader_root = resources.paths->shaders.root;
        frontend_config.player_name = "Player";
        frontend_config.enable_audio = true;
        frontend_config.renderer_debug = false;
        frontend_config.settings_path = executable_path->parent_path() / "settings.toml";
        frontend_config.tutorial_map_path =
            std::filesystem::path{"../BattleSpades/maps/Training.vxl"};
        frontend_config.tutorial_debug_tool = options.tutorial_debug_tool;
        frontend_config.tutorial_debug_cosmetic = options.tutorial_debug_cosmetic;
        frontend_config.tutorial_debug_aim = options.tutorial_debug_aim;
        frontend_config.startup_endpoint = options.startup_endpoint;
        frontend_config.startup_steam_lobby = options.startup_steam_lobby;
        frontend_config.steam_only = options.steam_only;
        frontend_config.debug_vfx = options.debug_vfx;
        frontend_config.debug_vfx_age = options.debug_vfx_age;
        frontend_config.debug_ui = options.debug_ui;
        frontend_config.localization_path = executable_path->parent_path() / "localization";
        frontend_config.ui_layout_path =
            executable_path->parent_path() / "ui-layout.json";
        frontend_config.ui_layout_editor = options.ui_layout_editor;
        frontend_config.tutorial_map = options.tutorial_map;
        frontend_config.tutorial_skydome = options.tutorial_skydome;
        frontend_config.tutorial_spawn = options.tutorial_spawn;
        frontend_config.tutorial_stand = options.tutorial_stand;
        frontend_config.tutorial_look = options.tutorial_look;
        if (options.shader_quality.has_value()) {
            const auto tier =
                battlespades::settings::parse_shader_quality(*options.shader_quality);
            if (!tier.has_value()) {
                std::cerr << "BattleSpadesClient: unknown --shader-quality '"
                          << *options.shader_quality
                          << "' (compatibility|low|medium|high|ultra)\n";
                return EXIT_FAILURE;
            }
            frontend_config.shader_quality_override = tier;
        }
        auto frontend = std::make_unique<battlespades::frontend::NativeFrontendModule>(
            *window_observer, std::move(frontend_config));
        frontend_observer = frontend.get();
        if (!application.add_module(std::move(frontend))) {
            std::cerr << "BattleSpadesClient: failed to register native frontend runtime\n";
            return 1;
        }
    }
#endif

    if (graphical) {
        const auto result = application.run();
        if (result != battlespades::core::RunResult::success) {
            std::cerr << "BattleSpadesClient: graphical runtime failed with code "
                      << static_cast<int>(result);
#if defined(AOS_HAS_NATIVE_BACKENDS)
            if (frontend_observer != nullptr && !frontend_observer->last_error().empty()) {
                std::cerr << ": " << frontend_observer->last_error();
            }
#endif
            std::cerr << '\n';
        }
#if defined(AOS_HAS_NATIVE_BACKENDS)
        // A module may stop the loop gracefully after a renderer invariant
        // failure; the retained diagnostic must not vanish with the window.
        else if (frontend_observer != nullptr && !frontend_observer->last_error().empty()) {
            std::cerr << "BattleSpadesClient: frontend stopped: "
                      << frontend_observer->last_error() << '\n';
            return 1;
        }
#endif
        return exit_code_for(result);
    }

    auto headless = std::make_unique<battlespades::headless::HeadlessModule>();
    auto* const headless_observer = headless.get();
    if (!application.add_module(std::move(headless))) {
        std::cerr << "BattleSpadesClient: failed to register headless runtime\n";
        return 1;
    }

    const auto result = application.run();
    if (result != battlespades::core::RunResult::success) {
        std::cerr << "BattleSpadesClient: runtime failed with code " << static_cast<int>(result)
                  << '\n';
        return exit_code_for(result);
    }

    std::cout << "BattleSpadesClient headless bootstrap completed "
              << headless_observer->ticks_observed() << " tick(s)\n";
    return 0;
}

#if defined(_WIN32) && defined(AOS_WINDOWS_GUI_SUBSYSTEM)
[[nodiscard]] bool has_standard_handle(DWORD identifier) noexcept {
    const HANDLE handle = GetStdHandle(identifier);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    SetLastError(ERROR_SUCCESS);
    const DWORD type = GetFileType(handle);
    return type != FILE_TYPE_UNKNOWN || GetLastError() == ERROR_SUCCESS;
}

void attach_parent_console_for_cli() noexcept {
    const bool has_input = has_standard_handle(STD_INPUT_HANDLE);
    const bool has_output = has_standard_handle(STD_OUTPUT_HANDLE);
    const bool has_error = has_standard_handle(STD_ERROR_HANDLE);
    if (has_input && has_output && has_error) {
        return;
    }
    if (AttachConsole(ATTACH_PARENT_PROCESS) == FALSE) {
        return;
    }

    FILE* stream{};
    if (!has_output) {
        static_cast<void>(freopen_s(&stream, "CONOUT$", "w", stdout));
    }
    if (!has_error) {
        static_cast<void>(freopen_s(&stream, "CONOUT$", "w", stderr));
    }
    if (!has_input) {
        static_cast<void>(freopen_s(&stream, "CONIN$", "r", stdin));
    }
}
#endif

#if defined(AOS_HAS_NATIVE_BACKENDS) && (defined(__APPLE__) || \
    (defined(_WIN32) && defined(AOS_WINDOWS_GUI_SUBSYSTEM)))
/**
 * Redirect stderr of a graphical launch to BattleSpadesClient.log beside the
 * executable, keeping one 5 MiB predecessor as BattleSpadesClient.log.1.
 * Returns the log path, or the bare file name if the executable is unknown.
 */
std::filesystem::path open_diagnostic_log() noexcept {
    std::filesystem::path path{"BattleSpadesClient.log"};
    try {
        std::string executable_error;
        const auto executable_path = battlespades::core::current_executable_path(executable_error);
        if (executable_path.has_value()) path = executable_path->parent_path() / path;
        std::error_code error;
        if (std::filesystem::file_size(path, error) > 5U * 1024U * 1024U && !error) {
            auto previous = path;
            previous += ".1";
            std::filesystem::rename(path, previous, error);
        }
#if defined(_WIN32)
        // The _s variant denies all sharing on a write mode, which locked the
        // log against every reader while the game ran. Plain _wfreopen shares
        // (_SH_DENYNO) and, unlike _dup2, still re-associates stderr in a GUI
        // process where the stream has no descriptor to begin with.
#pragma warning(push)
#pragma warning(disable : 4996)
        const bool opened = _wfreopen(path.c_str(), L"a", stderr) != nullptr;
#pragma warning(pop)
#else
        const bool opened = std::freopen(path.c_str(), "a", stderr) != nullptr;
#endif
        if (opened) std::cerr << "\nBattleSpadesClient startup\n";
    } catch (...) {
    }
    return path;
}
#endif

} // namespace

#if defined(_WIN32) && defined(AOS_WINDOWS_GUI_SUBSYSTEM)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // An explicit command line is a developer/automation invocation. Attach to
    // its existing console without allocating a new window so --help,
    // --version, bounded smokes, and --headless keep their diagnostic streams.
    if (__argc > 1) {
        attach_parent_console_for_cli();
    }
#if defined(AOS_HAS_NATIVE_BACKENDS)
    const auto diagnostic_path = __argc == 1 ? open_diagnostic_log() : std::filesystem::path{};
#endif

    const int result = run_client(__argc, __argv);
    if (result != 0 && __argc == 1) {
        std::wstring message{L"BattleSpadesClient could not start."};
#if defined(AOS_HAS_NATIVE_BACKENDS)
        message += L" Diagnostics were written to:\n" + diagnostic_path.wstring();
#endif
        MessageBoxW(nullptr, message.c_str(), L"BattleSpadesClient", MB_OK | MB_ICONERROR);
    }
    return result;
}
#else
int main(int argc, char* argv[]) {
#if defined(__APPLE__) && defined(AOS_HAS_NATIVE_BACKENDS)
    const auto diagnostic_path =
        argc == 1 ? open_diagnostic_log() : std::filesystem::path{"BattleSpadesClient.log"};

    int result{1};
    try {
        result = run_client(argc, argv);
    } catch (const std::exception& exception) {
        std::cerr << "BattleSpadesClient: unhandled startup exception: "
                  << exception.what() << '\n';
    } catch (...) {
        std::cerr << "BattleSpadesClient: unhandled unknown startup exception\n";
    }

    if (result != 0 && argc == 1) {
        const std::string message =
            "BattleSpadesClient could not start. Diagnostics were written to:\n" +
            std::string{reinterpret_cast<const char*>(diagnostic_path.u8string().c_str())};
        static_cast<void>(SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                                                   "BattleSpadesClient",
                                                   message.c_str(),
                                                   nullptr));
    }
    return result;
#else
    return run_client(argc, argv);
#endif
}
#endif
