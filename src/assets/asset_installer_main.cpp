#include "battlespades/assets/asset_install.hpp"
#include "battlespades/assets/retail_download.hpp"
#include "battlespades/updater/launch_flow.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>
#endif

namespace {

namespace assets = battlespades::assets;
namespace up = battlespades::updater;

constexpr int success_exit_code{0};
constexpr int failure_exit_code{1};
constexpr int cancelled_exit_code{2};

struct InstallerOptions final {
    std::optional<std::filesystem::path> source{};
    std::filesystem::path manifest{};
    std::optional<std::filesystem::path> destination{};
    std::optional<std::filesystem::path> report{};
    std::optional<std::string> manifest_url{};
    bool download{};        ///< headless "Download game assets"
    bool choose_folder{};   ///< the launcher already offered every choice: go straight to the picker
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
        encoded.push_back(static_cast<char8_t>(static_cast<unsigned char>(raw_character)));
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
    return "BattleSpadesAssetInstaller\n"
           "\n"
           "Interactive: BattleSpadesAssetInstaller [--choose-folder]\n"
           "Automation:  BattleSpadesAssetInstaller --source <AoS directory> [options]\n"
           "             BattleSpadesAssetInstaller --download [--manifest-url <url>] [options]\n"
           "\n"
           "Options: --manifest <asset-manifest.json> --destination <directory> --report <file>\n"
           "\n"
           "--download fetches the release manifest (default: updater.json, else\n"
           "https://www.aosplay.net/updates/stable.json), downloads its retail_assets\n"
           "package, verifies and extracts it, then imports it like --source.\n"
           "--choose-folder skips the choice screen and opens the folder picker.\n"
           "--report writes the result (\"ok\", or why it failed and what to do\n"
           "about it) to <file> as UTF-8, for the launcher to show.\n";
}

[[nodiscard]] std::optional<InstallerOptions> parse_options(int argc, char* argv[], std::string& error) {
    InstallerOptions options;
    options.manifest = executable_directory() / "asset-manifest.json";

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--help" || argument == "-h") {
            options.help = true;
            continue;
        }
        if (argument == "--download") {
            options.download = true;
            continue;
        }
        if (argument == "--choose-folder") {
            options.choose_folder = true;
            continue;
        }
        if (argument != "--source" && argument != "--manifest" && argument != "--destination" &&
            argument != "--report" && argument != "--manifest-url") {
            error = "unknown argument: " + std::string{argument};
            return std::nullopt;
        }
        if (++index >= argc) {
            error = "missing value after " + std::string{argument};
            return std::nullopt;
        }
        if (argument == "--manifest-url") {
            options.manifest_url = std::string{argv[index]};
            continue;
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
    if (options.source.has_value() && options.download) {
        error = "--source and --download cannot be combined";
        return std::nullopt;
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

[[nodiscard]] std::string megabytes(std::uint64_t bytes) {
    char text[32]{};
    std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return text;
}

// ---------------------------------------------------------------------------
// A small progress window: two lines of text and a bar, drawn with SDL's
// renderer so it works the same on Windows, macOS and Linux.
// ---------------------------------------------------------------------------

struct ProgressState final {
    std::mutex mutex{};
    std::string headline{};
    std::string detail{};
    double fraction{-1.0};   ///< < 0: busy, no known total
    std::atomic<bool> cancel{};
    std::atomic<bool> finished{};
    bool cancellable{};
};

class ProgressWindow final {
public:
    explicit ProgressWindow(SDL_Window* window) : window_{window} {
        if (window_ != nullptr) renderer_ = SDL_CreateRenderer(window_, nullptr);
    }
    ~ProgressWindow() {
        if (renderer_ != nullptr) SDL_DestroyRenderer(renderer_);
    }
    ProgressWindow(const ProgressWindow&) = delete;
    ProgressWindow& operator=(const ProgressWindow&) = delete;

    void draw(const std::string& headline, const std::string& detail, double fraction, bool cancellable) {
        if (window_ == nullptr) return;
        const auto title = fraction >= 0.0
                               ? headline + " " + std::to_string(static_cast<int>(fraction * 100.0)) + "%"
                               : headline;
        if (title != last_title_) {
            SDL_SetWindowTitle(window_, title.c_str());
            last_title_ = title;
        }
        if (renderer_ == nullptr) return;
        int width{};
        int height{};
        SDL_GetWindowSize(window_, &width, &height);
        constexpr float scale{2.0F};
        SDL_SetRenderScale(renderer_, scale, scale);
        const float logical_width = static_cast<float>(width) / scale;
        SDL_SetRenderDrawColor(renderer_, 24, 26, 30, 255);
        SDL_RenderClear(renderer_);
        SDL_SetRenderDrawColor(renderer_, 235, 235, 235, 255);
        SDL_RenderDebugText(renderer_, 10.0F, 10.0F, fit(headline, logical_width).c_str());
        SDL_SetRenderDrawColor(renderer_, 170, 175, 185, 255);
        SDL_RenderDebugText(renderer_, 10.0F, 26.0F, fit(detail, logical_width).c_str());
        const SDL_FRect frame{10.0F, 46.0F, logical_width - 20.0F, 14.0F};
        SDL_SetRenderDrawColor(renderer_, 60, 64, 72, 255);
        SDL_RenderFillRect(renderer_, &frame);
        SDL_SetRenderDrawColor(renderer_, 214, 160, 48, 255);
        if (fraction >= 0.0) {
            SDL_FRect bar = frame;
            bar.w = frame.w * static_cast<float>(std::clamp(fraction, 0.0, 1.0));
            SDL_RenderFillRect(renderer_, &bar);
        } else {
            // Busy: a block sweeping across the bar.
            const auto ticks = static_cast<float>(SDL_GetTicks() % 1600U) / 1600.0F;
            SDL_FRect bar{frame.x + (frame.w - 40.0F) * ticks, frame.y, 40.0F, frame.h};
            SDL_RenderFillRect(renderer_, &bar);
        }
        if (cancellable) {
            SDL_SetRenderDrawColor(renderer_, 130, 135, 145, 255);
            SDL_RenderDebugText(renderer_, 10.0F, 68.0F, "Close this window or press Esc to stop.");
        }
        SDL_RenderPresent(renderer_);
    }

private:
    [[nodiscard]] static std::string fit(const std::string& text, float width) {
        const auto characters = static_cast<std::size_t>(std::max(8.0F, (width - 20.0F) / 8.0F));
        if (text.size() <= characters) return text;
        return text.substr(0U, characters - 3U) + "...";
    }

    SDL_Window* window_{};
    SDL_Renderer* renderer_{};
    std::string last_title_{};
};

/// Runs `work` on a worker thread while the window stays responsive.
void run_with_progress(SDL_Window* window, ProgressState& state, const std::function<void()>& work) {
    state.cancel.store(false);
    state.finished.store(false);
    if (window == nullptr) {
        work();
        return;
    }
    ProgressWindow view{window};
    std::thread worker{[&] {
        work();
        state.finished.store(true, std::memory_order_release);
    }};
    while (!state.finished.load(std::memory_order_acquire)) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            const bool wants_close = event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
                               (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE);
            if (wants_close && state.cancellable) state.cancel.store(true);
        }
        std::string headline;
        std::string detail;
        double fraction{};
        bool cancellable{};
        {
            const std::scoped_lock lock{state.mutex};
            headline = state.headline;
            detail = state.detail;
            fraction = state.fraction;
            cancellable = state.cancellable && !state.cancel.load();
        }
        if (state.cancel.load()) detail = "Stopping...";
        view.draw(headline, detail, fraction, cancellable);
        SDL_Delay(33U);
    }
    worker.join();
}

void set_progress(ProgressState& state, std::string headline, std::string detail, double fraction) {
    const std::scoped_lock lock{state.mutex};
    state.headline = std::move(headline);
    state.detail = std::move(detail);
    state.fraction = fraction;
}

// ---------------------------------------------------------------------------
// Folder import and download, shared by the GUI and the command line.
// ---------------------------------------------------------------------------

struct Context final {
    const assets::AssetManifest* catalog{};
    std::filesystem::path destination{};
    std::filesystem::path executable_directory{};
};

[[nodiscard]] int import_folder(const std::filesystem::path& selected,
                                const Context& context,
                                bool download_offered,
                                ProgressState* progress,
                                std::string& error) {
    std::string discovery_error;
    const auto source = assets::find_asset_source(selected, *context.catalog, discovery_error);
    if (!source.has_value()) {
        error = assets::explain_asset_install_error(discovery_error, download_offered);
        return failure_exit_code;
    }
    const auto result = assets::install_asset_tree_atomic(
        *source, context.destination, *context.catalog, [progress](const assets::AssetInstallProgress& step) {
            if (progress == nullptr) return;
            const double fraction = step.bytes_total == 0U ? 1.0
                                                           : static_cast<double>(step.bytes_completed) /
                                                                 static_cast<double>(step.bytes_total);
            set_progress(*progress, "Importing the game files",
                         std::to_string(step.files_completed) + " of " + std::to_string(step.files_total) +
                             " files verified and copied",
                         fraction);
        });
    if (!result) {
        error = assets::explain_asset_install_error(result.error, download_offered);
        return failure_exit_code;
    }
    const auto steam = assets::import_native_steam_runtime(*source, context.executable_directory);
    if (!steam) {
        error = assets::explain_asset_install_error(steam.error, download_offered);
        return failure_exit_code;
    }
    error.clear();
    return success_exit_code;
}

[[nodiscard]] std::filesystem::path download_cache(const Context& context) {
    // Beside the destination: same volume, and writable by construction.
    return context.destination.parent_path() / ".retail-download";
}

[[nodiscard]] assets::RetailInstallResult download_assets(const up::ComponentRelease& release,
                                                          const Context& context,
                                                          ProgressState* progress,
                                                          const std::function<bool()>& cancelled) {
    assets::RetailInstallRequest request;
    request.release = release;
    request.catalog = context.catalog;
    request.destination = context.destination;
    request.cache_directory = download_cache(context);
    request.executable_directory = context.executable_directory;
    return assets::download_and_install_retail_assets(
        request, assets::curl_retail_transport(),
        [&](assets::RetailStage stage, std::uint64_t done, std::uint64_t total) {
            const double fraction =
                total == 0U ? -1.0 : static_cast<double>(done) / static_cast<double>(total);
            if (progress != nullptr) {
                switch (stage) {
                case assets::RetailStage::downloading:
                    set_progress(*progress, "Downloading the game files",
                                 megabytes(done) + " of " + megabytes(total), fraction);
                    break;
                case assets::RetailStage::verifying:
                    set_progress(*progress, "Checking the download", "Size and SHA-256", -1.0);
                    break;
                case assets::RetailStage::extracting:
                    set_progress(*progress, "Unpacking the game files", megabytes(done) + " of " + megabytes(total),
                                 fraction);
                    break;
                case assets::RetailStage::importing:
                    set_progress(*progress, "Importing the game files", "Verifying every file", fraction);
                    break;
                }
            }
            return !cancelled();
        });
}

// ---------------------------------------------------------------------------
// Dialogs.
// ---------------------------------------------------------------------------

void SDLCALL folder_dialog_callback(void* userdata, const char* const* file_list, int) {
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
    const auto suggested = assets::default_asset_source_directory();
    const auto suggested_utf8 = suggested.has_value() ? path_to_utf8(*suggested) : std::string{};
    SDL_ShowOpenFolderDialog(folder_dialog_callback, &result, window,
                             suggested_utf8.empty() ? nullptr : suggested_utf8.c_str(), false);

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

/// "Choose another folder" (true) or "Cancel" (false).
[[nodiscard]] bool ask_retry(SDL_Window* window, const std::string& message) {
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Choose another folder"},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel"},
    };
    SDL_MessageBoxData data{};
    data.flags = SDL_MESSAGEBOX_ERROR | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT;
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

[[nodiscard]] std::string choice_message(const up::FirstRunScreen& screen,
                                         const assets::RetailOffer& offer,
                                         const assets::DetectedAssetSource& detected,
                                         const std::string& last_error) {
    std::string message = "BattleSpades does not include the original Ace of Spades files. ";
    message += offer.available() ? "Import them from your own copy of Ace of Spades or download them; both end "
                                   "with the same verified files."
                                 : "Import them from your own copy of Ace of Spades (the folder that contains "
                                   "aos.exe).";
    if (detected.found()) {
        message += "\n\nFound: " + path_to_utf8(*detected.folder);
    }
    if (screen.note == up::FirstRunNote::download_unavailable) {
        message += "\n\n" + assets::download_unavailable_message();
    } else if (screen.note == up::FirstRunNote::offline) {
        message += "\n\n" + assets::download_offline_message(offer.error);
    }
    if (!last_error.empty()) {
        message += "\n\nThe last attempt did not work:\n" + last_error;
    }
    return message;
}

/// The choice screen (same plan as the Windows launcher). nullopt: Cancel.
[[nodiscard]] std::optional<up::FirstRunAction> ask_choice(SDL_Window* window,
                                                           const up::FirstRunScreen& screen,
                                                           const assets::RetailOffer& offer,
                                                           const assets::DetectedAssetSource& detected,
                                                           const std::string& last_error) {
    std::vector<std::string> labels;
    std::vector<up::FirstRunAction> actions;
    for (const auto action : screen.actions) {
        switch (action) {
        case up::FirstRunAction::import_detected:
            labels.emplace_back(last_error.empty() ? "Use the found folder" : "Try the found folder again");
            break;
        case up::FirstRunAction::choose_folder:
            labels.emplace_back("Choose folder...");
            break;
        case up::FirstRunAction::download:
            labels.emplace_back((screen.retry_download ? "Retry download (" : "Download game assets (") +
                                megabytes(offer.release.has_value() ? offer.release->size : 0U) + ")");
            break;
        case up::FirstRunAction::open_download_page:
            labels.emplace_back("Open aosplay.net/download");
            break;
        }
        actions.push_back(action);
    }
    std::vector<SDL_MessageBoxButtonData> buttons;
    for (std::size_t index = 0; index < labels.size(); ++index) {
        SDL_MessageBoxButtonData button{};
        button.flags = actions[index] == screen.preselected ? SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT : 0U;
        button.buttonID = static_cast<int>(index);
        button.text = labels[index].c_str();
        buttons.push_back(button);
    }
    constexpr int cancel_id{1000};
    buttons.push_back({SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, cancel_id, "Cancel"});

    const auto message = choice_message(screen, offer, detected, last_error);
    SDL_MessageBoxData data{};
    data.flags = (last_error.empty() ? SDL_MESSAGEBOX_INFORMATION : SDL_MESSAGEBOX_WARNING) |
                 SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT;
    data.window = window;
    data.title = "Get the original game files";
    data.message = message.c_str();
    data.numbuttons = static_cast<int>(buttons.size());
    data.buttons = buttons.data();
    int pressed{cancel_id};
    if (!SDL_ShowMessageBox(&data, &pressed) || pressed < 0 || pressed >= static_cast<int>(actions.size())) {
        return std::nullopt;
    }
    return actions[static_cast<std::size_t>(pressed)];
}

// ---------------------------------------------------------------------------
// Flows.
// ---------------------------------------------------------------------------

/// The old flow, used by the Windows launcher's "Select my folder" choice:
/// the launcher already offers the download itself.
[[nodiscard]] int run_folder_picker_loop(SDL_Window* window,
                                         const Context& context,
                                         const std::optional<std::filesystem::path>& report) {
    ProgressState progress;
    for (;;) {
        bool cancelled{};
        std::string dialog_error;
        const auto selected = choose_source_folder(window, cancelled, dialog_error);
        if (cancelled) return cancelled_exit_code;
        if (!selected.has_value()) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Could not open the folder picker",
                                     dialog_error.c_str(), window);
            write_report(report, dialog_error);
            return failure_exit_code;
        }
        std::string install_error;
        int result{failure_exit_code};
        set_progress(progress, "Importing the game files", "Checking the selected folder", -1.0);
        run_with_progress(window, progress,
                          [&] { result = import_folder(*selected, context, false, &progress, install_error); });
        if (result == success_exit_code) {
            write_report(report, "ok");
            return success_exit_code;
        }
        write_report(report, install_error);
        if (!ask_retry(window, install_error)) return failure_exit_code;
    }
}

[[nodiscard]] int run_choice_flow(SDL_Window* window,
                                  const Context& context,
                                  const std::string& manifest_url,
                                  const std::optional<std::filesystem::path>& report) {
    ProgressState progress;
    assets::DetectedAssetSource detected;
    assets::RetailOffer offer;
    set_progress(progress, "Looking for Ace of Spades", "Steam libraries and the BattleSpades download server",
                 -1.0);
    run_with_progress(window, progress, [&] {
        detected = assets::detect_asset_source(*context.catalog, assets::current_steam_search_environment());
        offer = assets::fetch_retail_offer(assets::curl_retail_transport(std::chrono::seconds{8}), manifest_url);
    });

    up::FirstRunInputs inputs;
    inputs.game_folder_found = detected.found();
    inputs.manifest_reachable = offer.manifest_reachable;
    inputs.download_offered = offer.available();
    std::string last_error;
    if (detected.found() && !detected.verified_root.has_value()) {
        inputs.import_failed = true;
        last_error = assets::explain_asset_install_error(detected.error, offer.available());
    }

    for (;;) {
        const auto screen = up::plan_first_run(inputs);
        const auto action = ask_choice(window, screen, offer, detected, last_error);
        if (!action.has_value()) {
            write_report(report, last_error.empty() ? std::string{"the import was cancelled"} : last_error);
            return cancelled_exit_code;
        }
        if (*action == up::FirstRunAction::open_download_page) {
            const std::string page{assets::retail_download_page};
            static_cast<void>(SDL_OpenURL(page.c_str()));
            continue;
        }

        std::string error;
        int result{failure_exit_code};
        if (*action == up::FirstRunAction::import_detected && detected.found()) {
            set_progress(progress, "Importing the game files", "Checking the found folder", -1.0);
            progress.cancellable = false;
            run_with_progress(window, progress, [&] {
                result = import_folder(*detected.folder, context, offer.available(), &progress, error);
            });
            if (result != success_exit_code) inputs.import_failed = true;
        } else if (*action == up::FirstRunAction::choose_folder) {
            bool cancelled{};
            std::string dialog_error;
            const auto selected = choose_source_folder(window, cancelled, dialog_error);
            if (cancelled) continue;   // closed the picker: same screen, no error
            if (!selected.has_value()) {
                last_error = "The folder picker could not be opened: " + dialog_error;
                continue;
            }
            set_progress(progress, "Importing the game files", "Checking the selected folder", -1.0);
            progress.cancellable = false;
            run_with_progress(window, progress, [&] {
                result = import_folder(*selected, context, offer.available(), &progress, error);
            });
            if (result != success_exit_code) inputs.import_failed = true;
        } else if (*action == up::FirstRunAction::download && offer.release.has_value()) {
            set_progress(progress, "Downloading the game files", "Connecting...", -1.0);
            progress.cancellable = true;
            assets::RetailInstallResult downloaded;
            run_with_progress(window, progress, [&] {
                downloaded = download_assets(*offer.release, context, &progress,
                                             [&progress] { return progress.cancel.load(); });
            });
            progress.cancellable = false;
            result = downloaded ? success_exit_code : failure_exit_code;
            error = downloaded.error;
            if (!downloaded) inputs.download_failed = true;
        }

        if (result == success_exit_code) {
            write_report(report, "ok");
            SDL_SetWindowTitle(window, "BattleSpades assets are ready");
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Game files installed",
                                     "The original game files were verified and installed. BattleSpades will "
                                     "now continue.",
                                     window);
            return success_exit_code;
        }
        last_error = error;
        write_report(report, error);
        SDL_SetWindowTitle(window, "BattleSpades asset installer");
    }
}

[[nodiscard]] int run_headless_download(const Context& context,
                                        const std::string& manifest_url,
                                        const std::optional<std::filesystem::path>& report) {
    const auto offer = assets::fetch_retail_offer(assets::curl_retail_transport(), manifest_url);
    if (!offer.available()) {
        const auto message = offer.manifest_reachable ? assets::download_unavailable_message()
                                                      : assets::download_offline_message(offer.error);
        std::cerr << "BattleSpadesAssetInstaller: " << message << '\n';
        write_report(report, message);
        return failure_exit_code;
    }
    int last_percent{-1};
    assets::RetailInstallRequest request;
    request.release = *offer.release;
    request.catalog = context.catalog;
    request.destination = context.destination;
    request.cache_directory = download_cache(context);
    request.executable_directory = context.executable_directory;
    const auto result = assets::download_and_install_retail_assets(
        request, assets::curl_retail_transport(),
        [&last_percent](assets::RetailStage stage, std::uint64_t done, std::uint64_t total) {
            const int percent = total == 0U ? 0 : static_cast<int>((done * 100U) / total);
            if (stage == assets::RetailStage::downloading && percent / 10 != last_percent / 10) {
                last_percent = percent;
                std::cout << "download " << percent << "%\n" << std::flush;
            }
            return true;
        });
    if (!result) {
        std::cerr << "BattleSpadesAssetInstaller: " << result.error << '\n';
        write_report(report, result.error);
        return result.status == assets::RetailInstallStatus::cancelled ? cancelled_exit_code : failure_exit_code;
    }
    write_report(report, "ok");
    return success_exit_code;
}

[[nodiscard]] int run_installer(int argc, char* argv[]) {
    if (!SDL_Init(0U)) {
        std::cerr << "BattleSpadesAssetInstaller: SDL initialization failed: " << SDL_GetError() << '\n';
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
    const bool interactive = !parsed->source.has_value() && !parsed->download;

    const auto loaded = assets::load_asset_manifest(parsed->manifest);
    if (!loaded) {
        const auto message = assets::explain_asset_install_error(loaded.error);
        std::cerr << "BattleSpadesAssetInstaller: " << message << '\n';
        write_report(parsed->report, message);
        if (interactive) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "BattleSpades asset installer", message.c_str(), nullptr);
        }
        SDL_Quit();
        return failure_exit_code;
    }

    Context context;
    context.catalog = &*loaded.manifest;
    context.executable_directory = executable_directory();
    if (parsed->destination.has_value()) {
        context.destination = *parsed->destination;
    } else {
        std::string destination_error;
        const auto chosen = assets::choose_asset_destination(context.executable_directory, destination_error);
        context.destination =
            chosen.has_value() ? *chosen : assets::asset_root_locations(context.executable_directory).packaged;
    }
    const auto manifest_url = parsed->manifest_url.has_value()
                                  ? *parsed->manifest_url
                                  : assets::default_release_manifest_url(context.executable_directory);

    if (parsed->source.has_value()) {
        std::string install_error;
        const int result = import_folder(*parsed->source, context, false, nullptr, install_error);
        if (result != success_exit_code) {
            std::cerr << "BattleSpadesAssetInstaller: " << install_error << '\n';
        }
        write_report(parsed->report,
                     result == success_exit_code ? std::string_view{"ok"} : std::string_view{install_error});
        SDL_Quit();
        return result;
    }
    if (parsed->download) {
        const int result = run_headless_download(context, manifest_url, parsed->report);
        SDL_Quit();
        return result;
    }

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        std::cerr << "BattleSpadesAssetInstaller: video initialization failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return failure_exit_code;
    }
    SDL_Window* const window = SDL_CreateWindow("BattleSpades asset installer", 720, 200, 0U);
    if (window == nullptr) {
        std::cerr << "BattleSpadesAssetInstaller: could not create installer window: " << SDL_GetError() << '\n';
        SDL_Quit();
        return failure_exit_code;
    }

    const int result = parsed->choose_folder ? run_folder_picker_loop(window, context, parsed->report)
                                             : run_choice_flow(window, context, manifest_url, parsed->report);

    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
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
