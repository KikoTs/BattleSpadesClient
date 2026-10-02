#include "battlespades/assets/asset_install.hpp"

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>

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
    std::optional<std::filesystem::path> report{};
    bool help{};
};

struct FolderDialogResult final {
    std::atomic<bool> complete{};
    std::mutex mutex{};
    std::optional<std::filesystem::path> selected{};
    std::string error{};
    bool cancelled{};
};

[[nodiscard]] std::filesystem::path path_from_utf8(std::string_view value) {
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const char raw_character : value) {
        const auto character = static_cast<unsigned char>(raw_character);
        encoded.push_back(static_cast<char8_t>(character));
    }
    return std::filesystem::path{encoded};
}

[[nodiscard]] std::filesystem::path executable_directory() {
    const char* const base = SDL_GetBasePath();
    if (base == nullptr || *base == '\0') {
        return std::filesystem::current_path();
    }
    // SDL returns UTF-8; path(const char*) would decode it through the ANSI
    // code page and break an install folder such as C:\Игры\BattleSpades.
    return path_from_utf8(base);
}

[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& value) {
    const auto encoded = value.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

[[nodiscard]] std::string usage() {
    return
        "BattleSpadesAssetInstaller\n"
        "\n"
        "Interactive: BattleSpadesAssetInstaller\n"
        "Automation:  BattleSpadesAssetInstaller --source <AoS directory> "
        "[--manifest <file>] [--destination <directory>] [--report <file>]\n"
        "\n"
        "--report writes the result (\"ok\", or why the import failed and what\n"
        "to do about it) to <file> as UTF-8, for the launcher to show.\n";
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
            argument != "--destination" && argument != "--report") {
            error = "unknown argument: " + std::string{argument};
            return std::nullopt;
        }
        if (++index >= argc) {
            error = "missing value after " + std::string{argument};
            return std::nullopt;
        }
        const auto value = path_from_utf8(argv[index]);
        if (argument == "--source") {
            options.source = value;
        } else if (argument == "--manifest") {
            options.manifest = value;
        } else if (argument == "--report") {
            options.report = value;
        } else {
            options.destination = value;
        }
    }
    error.clear();
    return options;
}

/// The launcher reads this instead of a bare exit code (a GUI-subsystem
/// process has no stderr the launcher could capture).
void write_report(const std::optional<std::filesystem::path>& report, std::string_view text) {
    if (!report.has_value()) {
        return;
    }
    std::error_code code;
    if (report->has_parent_path()) {
        std::filesystem::create_directories(report->parent_path(), code);
    }
    std::ofstream output{*report, std::ios::binary | std::ios::trunc};
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
}

/// "Choose another folder" (true) or "Cancel" (false).
[[nodiscard]] bool ask_retry(SDL_Window* window, const std::string& message) {
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Choose another folder"},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel"},
    };
    SDL_MessageBoxData data{};
    data.flags = SDL_MESSAGEBOX_ERROR;
    data.window = window;
    data.title = "The game files could not be imported";
    data.message = message.c_str();
    data.numbuttons = static_cast<int>(std::size(buttons));
    data.buttons = buttons;
    int pressed{0};
    if (!SDL_ShowMessageBox(&data, &pressed)) {
        return false;
    }
    return pressed == 1;
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
    const auto suggested = battlespades::assets::default_asset_source_directory();
    const auto suggested_utf8 = suggested.has_value()
                                    ? path_to_utf8(*suggested)
                                    : std::string{};
    SDL_ShowOpenFolderDialog(folder_dialog_callback,
                             &result,
                             window,
                             suggested_utf8.empty() ? nullptr
                                                    : suggested_utf8.c_str(),
                             false);

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
        error = battlespades::assets::explain_asset_install_error(discovery_error);
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
        error = battlespades::assets::explain_asset_install_error(result.error);
        return failure_exit_code;
    }
    const auto steam = battlespades::assets::import_native_steam_runtime(
        *source, executable_directory());
    if (!steam) {
        error = battlespades::assets::explain_asset_install_error(steam.error);
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
        const auto message = battlespades::assets::explain_asset_install_error(loaded.error);
        std::cerr << "BattleSpadesAssetInstaller: " << message << '\n';
        write_report(parsed->report, message);
        if (!parsed->source.has_value()) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                                     "BattleSpades asset installer",
                                     message.c_str(),
                                     nullptr);
        }
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
        write_report(parsed->report, result == success_exit_code ? std::string_view{"ok"}
                                                                  : std::string_view{install_error});
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
        "Select a Windows Ace of Spades: Battle Builder installation folder "
        "or Windows Steam library. On macOS, copy that Windows installation "
        "to the Mac first; the obsolete macOS .app is not compatible. "
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
            write_report(parsed->report, "ok");
            SDL_SetWindowTitle(window, "BattleSpades assets are ready");
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_INFORMATION,
                "Assets installed",
                "The original game assets were verified and imported successfully. "
                "BattleSpades will now continue.",
                window);
            break;
        }

        write_report(parsed->report, install_error);
        if (!ask_retry(window, install_error)) {
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

#if defined(_WIN32)
namespace {

/// __argv/argv are ANSI: a Cyrillic --source folder would arrive as "?".
/// Rebuild UTF-8 arguments from the wide command line instead.
int run_with_utf8_arguments() {
    int count{};
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::string> storage;
    for (int index = 0; wide != nullptr && index < count; ++index) {
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide[index], -1, nullptr, 0, nullptr, nullptr);
        std::string text(static_cast<std::size_t>(bytes > 0 ? bytes - 1 : 0), '\0');
        if (bytes > 1) {
            WideCharToMultiByte(CP_UTF8, 0, wide[index], -1, text.data(), bytes, nullptr, nullptr);
        }
        storage.push_back(std::move(text));
    }
    LocalFree(wide);
    std::vector<char*> arguments;
    for (auto& argument : storage) arguments.push_back(argument.data());
    arguments.push_back(nullptr);
    return run_installer(static_cast<int>(storage.size()), arguments.data());
}

} // namespace
#endif

#if defined(_WIN32) && defined(AOS_WINDOWS_GUI_SUBSYSTEM)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    if (__argc > 1) {
        attach_parent_console_for_cli();
    }
    return run_with_utf8_arguments();
}
#elif defined(_WIN32)
int main() {
    return run_with_utf8_arguments();
}
#else
int main(int argc, char* argv[]) {
    return run_installer(argc, argv);
}
#endif
