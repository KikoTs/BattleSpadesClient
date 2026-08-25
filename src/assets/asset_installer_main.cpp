#include "battlespades/assets/asset_install.hpp"

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32) && defined(AOS_WINDOWS_GUI_SUBSYSTEM)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdio>
#endif

namespace {

constexpr int success_exit_code{0};
constexpr int failure_exit_code{1};
constexpr int cancelled_exit_code{2};

struct InstallerOptions final {
    std::optional<std::filesystem::path> source{};
    std::filesystem::path manifest{};
    std::filesystem::path destination{};
    bool help{};
};

struct FolderDialogResult final {
    std::atomic<bool> complete{};
    std::mutex mutex{};
    std::optional<std::filesystem::path> selected{};
    std::string error{};
    bool cancelled{};
};

[[nodiscard]] std::filesystem::path executable_directory() {
    const char* const base = SDL_GetBasePath();
    if (base == nullptr || *base == '\0') {
        return std::filesystem::current_path();
    }
    return std::filesystem::path{base};
}

[[nodiscard]] std::filesystem::path path_from_utf8(std::string_view value) {
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const unsigned char character : value) {
        encoded.push_back(static_cast<char8_t>(character));
    }
    return std::filesystem::path{encoded};
}

[[nodiscard]] std::string usage() {
    return
        "BattleSpadesAssetInstaller\n"
        "\n"
        "Interactive: BattleSpadesAssetInstaller\n"
        "Automation:  BattleSpadesAssetInstaller --source <AoS directory> "
        "[--manifest <file>] [--destination <directory>]\n";
}

[[nodiscard]] std::optional<InstallerOptions> parse_options(int argc,
                                                            char* argv[],
                                                            std::string& error) {
    InstallerOptions options;
    const auto base = executable_directory();
    options.manifest = base / "asset-manifest.json";
    options.destination = base / "assets" / "original";

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--help" || argument == "-h") {
            options.help = true;
            continue;
        }
        if (argument != "--source" && argument != "--manifest" &&
            argument != "--destination") {
            error = "unknown argument: " + std::string{argument};
            return std::nullopt;
        }
        if (++index >= argc) {
            error = "missing value after " + std::string{argument};
            return std::nullopt;
        }
        const std::filesystem::path value{argv[index]};
        if (argument == "--source") {
            options.source = value;
        } else if (argument == "--manifest") {
            options.manifest = value;
        } else {
            options.destination = value;
        }
    }
    error.clear();
    return options;
}

void SDLCALL folder_dialog_callback(void* userdata,
                                    const char* const* file_list,
                                    int) {
    auto& result = *static_cast<FolderDialogResult*>(userdata);
    {
        const std::scoped_lock lock{result.mutex};
        if (file_list == nullptr) {
            result.error = SDL_GetError();
            if (result.error.empty()) {
                result.error = "the operating-system folder picker failed";
            }
        } else if (file_list[0] == nullptr) {
            result.cancelled = true;
        } else {
            result.selected = path_from_utf8(file_list[0]);
        }
    }
    result.complete.store(true, std::memory_order_release);
}

[[nodiscard]] std::optional<std::filesystem::path>
choose_source_folder(SDL_Window* window, bool& cancelled, std::string& error) {
    FolderDialogResult result;
    SDL_ShowOpenFolderDialog(folder_dialog_callback, &result, window, nullptr, false);

    while (!result.complete.load(std::memory_order_acquire)) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                cancelled = true;
                return std::nullopt;
            }
        }
        SDL_Delay(16U);
    }

    const std::scoped_lock lock{result.mutex};
    cancelled = result.cancelled;
    error = result.error;
    return result.selected;
}

[[nodiscard]] int install_from(const std::filesystem::path& selected,
                               const battlespades::assets::AssetManifest& manifest,
                               const std::filesystem::path& destination,
                               SDL_Window* window,
                               std::string& error) {
    std::string discovery_error;
    const auto source = battlespades::assets::find_asset_source(
        selected, manifest, discovery_error);
    if (!source.has_value()) {
        error = std::move(discovery_error);
        return failure_exit_code;
    }

    std::size_t last_percent{101U};
    const auto result = battlespades::assets::install_asset_tree_atomic(
        *source,
        destination,
        manifest,
        [window, &last_percent](const battlespades::assets::AssetInstallProgress& progress) {
            const auto percent = progress.bytes_total == 0U
                                     ? 100U
                                     : static_cast<std::size_t>(
                                           (progress.bytes_completed * 100U) /
                                           progress.bytes_total);
            if (window != nullptr && percent != last_percent) {
                last_percent = percent;
                const auto title = "Importing Ace of Spades assets... " +
                                   std::to_string(percent) + "%";
                SDL_SetWindowTitle(window, title.c_str());
                SDL_PumpEvents();
            }
        });
    if (!result) {
        error = result.error;
        return failure_exit_code;
    }
    error.clear();
    return success_exit_code;
}

[[nodiscard]] int run_installer(int argc, char* argv[]) {
    if (!SDL_Init(0U)) {
        std::cerr << "BattleSpadesAssetInstaller: SDL initialization failed: "
                  << SDL_GetError() << '\n';
        return failure_exit_code;
    }

    std::string option_error;
    const auto parsed = parse_options(argc, argv, option_error);
    if (!parsed.has_value()) {
        std::cerr << "BattleSpadesAssetInstaller: " << option_error << "\n\n" << usage();
        SDL_Quit();
        return failure_exit_code;
    }
    if (parsed->help) {
        std::cout << usage();
        SDL_Quit();
        return success_exit_code;
    }

    const auto loaded = battlespades::assets::load_asset_manifest(parsed->manifest);
    if (!loaded) {
        std::cerr << "BattleSpadesAssetInstaller: " << loaded.error << '\n';
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                                 "BattleSpades asset installer",
                                 loaded.error.c_str(),
                                 nullptr);
        SDL_Quit();
        return failure_exit_code;
    }

    if (parsed->source.has_value()) {
        std::string install_error;
        const int result = install_from(*parsed->source,
                                        *loaded.manifest,
                                        parsed->destination,
                                        nullptr,
                                        install_error);
        if (result != success_exit_code) {
            std::cerr << "BattleSpadesAssetInstaller: " << install_error << '\n';
        }
        SDL_Quit();
        return result;
    }

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        std::cerr << "BattleSpadesAssetInstaller: video initialization failed: "
                  << SDL_GetError() << '\n';
        SDL_Quit();
        return failure_exit_code;
    }

    SDL_Window* const window = SDL_CreateWindow(
        "BattleSpades asset installer", 640, 180, SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        std::cerr << "BattleSpadesAssetInstaller: could not create installer window: "
                  << SDL_GetError() << '\n';
        SDL_Quit();
        return failure_exit_code;
    }

    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_INFORMATION,
        "BattleSpades needs the original game assets",
        "BattleSpades does not redistribute Ace of Spades content.\n\n"
        "Select your existing Ace of Spades: Battle Builder installation folder. "
        "The required files will be verified and copied beside this client.",
        window);

    int result_code{failure_exit_code};
    for (;;) {
        bool cancelled{};
        std::string dialog_error;
        const auto selected = choose_source_folder(window, cancelled, dialog_error);
        if (cancelled) {
            result_code = cancelled_exit_code;
            break;
        }
        if (!selected.has_value()) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                                     "Could not open the folder picker",
                                     dialog_error.c_str(),
                                     window);
            result_code = failure_exit_code;
            break;
        }

        std::string install_error;
        result_code = install_from(*selected,
                                   *loaded.manifest,
                                   parsed->destination,
                                   window,
                                   install_error);
        if (result_code == success_exit_code) {
            SDL_SetWindowTitle(window, "BattleSpades assets are ready");
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_INFORMATION,
                "Assets installed",
                "The original game assets were verified and imported successfully. "
                "BattleSpades will now continue.",
                window);
            break;
        }

        const bool retry = SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR,
            "This is not a compatible Ace of Spades installation",
            install_error.c_str(),
            window);
        if (!retry) {
            result_code = failure_exit_code;
            break;
        }
        SDL_SetWindowTitle(window, "BattleSpades asset installer");
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return result_code;
}

#if defined(_WIN32) && defined(AOS_WINDOWS_GUI_SUBSYSTEM)
void attach_parent_console_for_cli() {
    if (AttachConsole(ATTACH_PARENT_PROCESS) == FALSE) {
        return;
    }
    FILE* stream{};
    static_cast<void>(freopen_s(&stream, "CONOUT$", "w", stdout));
    static_cast<void>(freopen_s(&stream, "CONOUT$", "w", stderr));
}
#endif

} // namespace

#if defined(_WIN32) && defined(AOS_WINDOWS_GUI_SUBSYSTEM)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    if (__argc > 1) {
        attach_parent_console_for_cli();
    }
    return run_installer(__argc, __argv);
}
#else
int main(int argc, char* argv[]) {
    return run_installer(argc, argv);
}
#endif
