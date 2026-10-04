// BattleSpadesLauncher.exe: the stable entry point Steam (and the Start menu)
// runs.
//  * Started by Steam through the Ace of Spades launch options
//    ("<launcher>" %command%): a small chooser, "Play BattleSpades" (default)
//    or "Play Ace of Spades (original)", which runs Steam's %command%
//    unchanged. "Remember my choice" skips it; holding Shift, --choose or
//    --reset-launch-choice brings it back. No %command%: BattleSpades starts.
//  * First launch without game files: one screen to import them from the
//    Steam copy (preselected when found), download them (manifest component
//    retail_assets; the fallback when no Steam copy exists or the import
//    failed) or pick a folder, with an optional "enable hosting" download.
//  * Every launch: reads the update manifest and updates each INSTALLED
//    component independently (client, server, assets, retail_assets), with
//    resumable, mirror-by-mirror, size + SHA-256 verified downloads.
//  * Writes update/hosting.json so Create Match / Map Creator know whether the
//    bundled server can host; playing never depends on it.
//  * --install-component NAME: opt-in download requested by the client.
// Then it starts BattleSpadesClient.exe with the forwarded arguments and
// waits, so Steam keeps showing the game as running.
//
// It links only the static CRT and system DLLs, which is what allows it to
// replace every DLL in the install folder.

#include "update_session.hpp"
#include "win_util.hpp"

#include "battlespades/updater/file_util.hpp"
#include "battlespades/updater/launch_flow.hpp"
#include "battlespades/updater/launcher_args.hpp"
#include "battlespades/updater/steam_library.hpp"
#include "battlespades/updater/update_apply.hpp"
#include "battlespades/updater/update_plan.hpp"
#include "battlespades/updater/updater_config.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace up = battlespades::updater;
namespace win = battlespades::updater::win;

constexpr UINT message_status = WM_APP + 1;
constexpr UINT message_progress = WM_APP + 2;
constexpr UINT message_done = WM_APP + 4;
constexpr int control_skip = 1001;
constexpr int button_update_now = 2001;
constexpr int button_later = 2002;
constexpr int button_download_assets = 3001;
constexpr int button_use_folder = 3002;
constexpr int button_choose_folder = 3003;
constexpr int button_download_page = 3004;
constexpr int button_play_battlespades = 4001;
constexpr int button_play_original = 4002;
constexpr wchar_t window_class[] = L"BattleSpadesLauncherProgress";

std::mutex log_mutex;
fs::path log_path;

void write_log(std::string_view text) {
    const std::scoped_lock lock{log_mutex};
    if (log_path.empty()) return;
    std::error_code code;
    fs::create_directories(log_path.parent_path(), code);
    if (fs::file_size(log_path, code) > 512U * 1024U) fs::remove(log_path, code);
    std::ofstream output{log_path, std::ios::app | std::ios::binary};
    SYSTEMTIME now{};
    GetLocalTime(&now);
    char stamp[32]{};
    std::snprintf(stamp, sizeof(stamp), "%04u-%02u-%02u %02u:%02u:%02u ", now.wYear, now.wMonth, now.wDay, now.wHour,
                  now.wMinute, now.wSecond);
    output << stamp << text << "\r\n";
}

std::string megabytes(std::uint64_t bytes) {
    char text[32]{};
    std::snprintf(text, sizeof(text), "%.0f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return text;
}

struct UiState {
    HWND window{};
    HWND label{};
    HWND bar{};
    HWND skip{};
    std::atomic_bool cancel{};
};

UiState ui;

LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_COMMAND:
        if (LOWORD(wparam) == control_skip) {
            ui.cancel = true;
            EnableWindow(ui.skip, FALSE);
            SetWindowTextW(ui.label, L"Stopping the update...");
        }
        return 0;
    case WM_CLOSE:
        ui.cancel = true;
        return 0;
    case message_status: {
        std::unique_ptr<std::wstring> text{reinterpret_cast<std::wstring*>(lparam)};
        SetWindowTextW(ui.label, text->c_str());
        return 0;
    }
    case message_progress:
        if (wparam == static_cast<WPARAM>(-1)) {
            SetWindowLongPtrW(ui.bar, GWL_STYLE, GetWindowLongPtrW(ui.bar, GWL_STYLE) | PBS_MARQUEE);
            SendMessageW(ui.bar, PBM_SETMARQUEE, TRUE, 30);
        } else {
            SendMessageW(ui.bar, PBM_SETMARQUEE, FALSE, 0);
            SetWindowLongPtrW(ui.bar, GWL_STYLE,
                              GetWindowLongPtrW(ui.bar, GWL_STYLE) & ~static_cast<LONG_PTR>(PBS_MARQUEE));
            SendMessageW(ui.bar, PBM_SETPOS, wparam, 0);
        }
        return 0;
    case message_done:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

/// `button_label`: "Play without updating", "Quit" (required) or "Cancel" (opt-in installs).
/// A null label shows a disabled "Please wait" button (work that cannot be stopped).
bool create_window(HINSTANCE instance, const wchar_t* button_label, const wchar_t* title = L"BattleSpades update",
                   const wchar_t* first_status = L"Preparing the update...") {
    ui.cancel = false;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSEXW klass{};
    klass.cbSize = sizeof(klass);
    klass.lpfnWndProc = window_procedure;
    klass.hInstance = instance;
    klass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    klass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    klass.lpszClassName = window_class;
    RegisterClassExW(&klass);

    const UINT dpi = GetDpiForSystem();
    const auto scale = [dpi](int value) { return MulDiv(value, static_cast<int>(dpi), 96); };
    RECT area{0, 0, scale(460), scale(130)};
    AdjustWindowRectEx(&area, WS_CAPTION | WS_SYSMENU, FALSE, 0U);
    const int width = area.right - area.left;
    const int height = area.bottom - area.top;
    ui.window = CreateWindowExW(0U, window_class, title, WS_CAPTION | WS_SYSMENU,
                                (GetSystemMetrics(SM_CXSCREEN) - width) / 2, (GetSystemMetrics(SM_CYSCREEN) - height) / 2,
                                width, height, nullptr, nullptr, instance, nullptr);
    if (ui.window == nullptr) return false;
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0U, dpi);
    HFONT font = CreateFontIndirectW(&metrics.lfMessageFont);
    ui.label = CreateWindowExW(0U, L"STATIC", first_status, WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
                               scale(16), scale(16), scale(428), scale(22), ui.window, nullptr, instance, nullptr);
    ui.bar = CreateWindowExW(0U, PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE, scale(16), scale(46), scale(428),
                             scale(20), ui.window, nullptr, instance, nullptr);
    SendMessageW(ui.bar, PBM_SETRANGE32, 0, 1000);
    ui.skip = CreateWindowExW(0U, L"BUTTON", button_label != nullptr ? button_label : L"Please wait",
                              WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | (button_label != nullptr ? 0 : WS_DISABLED),
                              scale(264), scale(84), scale(180), scale(30),
                              ui.window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_skip)), instance, nullptr);
    for (HWND control : {ui.label, ui.skip}) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    ShowWindow(ui.window, SW_SHOWNORMAL);
    SetForegroundWindow(ui.window);
    return true;
}

void post_status(std::string_view text) {
    if (ui.window == nullptr) return;
    auto* heap = new std::wstring{win::widen(text)};
    if (!PostMessageW(ui.window, message_status, 0, reinterpret_cast<LPARAM>(heap))) delete heap;
}

/// "Update now / Later" for one large update. True = update now.
bool ask_large_update(const up::PlannedUpdate& update, std::uint32_t max_deferrals) {
    const auto left = max_deferrals - update.deferrals;
    const auto content = win::widen(
        "BattleSpades " + update.release.name + " " + update.release.version + " is ready to download (" +
        megabytes(update.release.size) + ").\n\nIf you choose Later you can play now. After " + std::to_string(left) +
        " more time" + (left == 1U ? "" : "s") + " this update is installed before the game starts.");
    TASKDIALOG_BUTTON buttons[] = {{button_update_now, L"Update now"}, {button_later, L"Later"}};
    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    config.pszWindowTitle = L"BattleSpades update";
    config.pszMainIcon = TD_INFORMATION_ICON;
    config.pszMainInstruction = L"A large update is available";
    config.pszContent = content.c_str();
    config.cButtons = static_cast<UINT>(std::size(buttons));
    config.pButtons = buttons;
    config.nDefaultButton = button_update_now;
    int pressed{};
    if (FAILED(TaskDialogIndirect(&config, &pressed, nullptr, nullptr))) return true;
    return pressed == button_update_now;
}

/// Download + apply on a worker thread while this thread pumps the window.
win::SessionResult run_session_with_window(HINSTANCE instance, const up::UpdateLayout& layout,
                                           const up::UpdateManifest& manifest,
                                           const std::vector<up::PlannedUpdate>& updates, const up::UpdaterConfig& config,
                                           const wchar_t* button_label) {
    win::SessionResult result;
    result.outcome = win::SessionOutcome::failed;
    if (!create_window(instance, button_label)) {
        result.error = "cannot create the progress window";
        return result;
    }
    std::thread worker{[&] {
        win::SessionCallbacks callbacks;
        callbacks.log = write_log;
        callbacks.status = [](std::string_view text) { post_status(text); };
        auto last = std::chrono::steady_clock::now() - std::chrono::seconds{1};
        callbacks.progress = [&last](std::uint64_t received, std::uint64_t total) {
            const auto now = std::chrono::steady_clock::now();
            if (now - last >= std::chrono::milliseconds{100} || received == total) {
                last = now;
                const WPARAM permille =
                    total == 0U ? static_cast<WPARAM>(-1) : static_cast<WPARAM>(received * 1000U / total);
                PostMessageW(ui.window, message_progress, permille, 0);
                if (total != 0U) post_status("Downloading: " + megabytes(received) + " of " + megabytes(total));
            }
            return !ui.cancel.load();
        };
        result = win::run_update_session(layout, manifest, updates, config, callbacks);
        if (result.outcome == win::SessionOutcome::applied) {
            post_status("Update complete. Starting the game...");
            PostMessageW(ui.window, message_progress, 1000, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds{600});
        } else if (result.outcome == win::SessionOutcome::failed || result.outcome == win::SessionOutcome::partial) {
            post_status("Update failed: " + result.error.substr(0U, 120U));
            std::this_thread::sleep_for(std::chrono::milliseconds{2500});
        }
        PostMessageW(ui.window, message_done, 0, 0);
    }};
    MSG message{};
    while (GetMessageW(&message, nullptr, 0U, 0U) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    worker.join();
    ui.window = nullptr;
    return result;
}

enum class UpdateDecision { launch, quit };

[[nodiscard]] up::PlanPolicy policy_of(const up::UpdaterConfig& config) {
    up::PlanPolicy policy;
    policy.large_update_bytes = config.large_update_bytes;
    policy.max_deferrals = config.max_deferrals;
    return policy;
}

/// update/hosting.json: what Create Match / Map Creator should do.
void write_hosting_status(const up::UpdateLayout& layout, const up::UpdateManifest* manifest) {
    const auto status = up::hosting_status(manifest, up::read_installed_versions(layout.install),
                                           up::read_client_protocol(layout.install), std::nullopt);
    std::string ignored;
    std::error_code code;
    fs::create_directories(layout.root(), code);
    static_cast<void>(up::write_file_atomic(layout.root() / "hosting.json", up::hosting_status_json(status), ignored));
    write_log(std::string{"hosting: "} + up::to_string(status.state) +
              (status.reason.empty() ? std::string{} : " (" + status.reason + ")"));
}

/// Runs `work` on a worker thread behind a progress window with a marquee bar
/// (work that reports no percentage and cannot be cancelled, e.g. an import).
void run_with_progress(HINSTANCE instance, const wchar_t* title, const wchar_t* status,
                       const std::function<void()>& work) {
    if (!create_window(instance, nullptr, title, status)) {
        work();
        return;
    }
    PostMessageW(ui.window, message_progress, static_cast<WPARAM>(-1), 0);
    std::thread worker{[&] {
        work();
        PostMessageW(ui.window, message_done, 0, 0);
    }};
    MSG message{};
    while (GetMessageW(&message, nullptr, 0U, 0U) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    worker.join();
    ui.window = nullptr;
}

/// "Use my Ace of Spades folder": the importer, hidden, behind a progress window.
win::AssetImportResult import_detected_folder(HINSTANCE instance, const up::UpdateLayout& layout,
                                              const fs::path& folder) {
    win::AssetImportResult result;
    run_with_progress(instance, L"BattleSpades", L"Copying and verifying the original game files...",
                      [&] { result = win::run_asset_import(layout, folder); });
    write_log("import from " + up::path_to_utf8(folder) + ": " +
              (result.ok() ? std::string{"ok"} : result.message));
    return result;
}

struct FirstRunChoice {
    std::optional<up::FirstRunAction> action;   ///< nullopt: the player quit
    bool enable_hosting{};
};

/**
 * The one first-run screen: how to get the original game files, with the
 * best option preselected (see up::plan_first_run), the last failure and what
 * to do about it, plus an optional "enable hosting" checkbox.
 */
FirstRunChoice ask_first_run(const up::FirstRunScreen& screen, const up::UpdateManifest* manifest,
                             const std::optional<fs::path>& detected, bool server_installed,
                             const std::string& last_error) {
    const auto* retail = manifest != nullptr ? manifest->component(up::component_retail_assets) : nullptr;
    const auto* server = manifest != nullptr ? manifest->component(up::component_server) : nullptr;
    const auto size = retail != nullptr ? megabytes(retail->size) : std::string{};

    struct Link {
        up::FirstRunAction action;
        int id;
        std::wstring text;
    };
    std::vector<Link> links;
    for (const auto action : screen.actions) {
        switch (action) {
        case up::FirstRunAction::import_detected:
            links.push_back({action, button_use_folder,
                             win::widen((last_error.empty() ? "Use my Ace of Spades folder\nFound: "
                                                            : "Try my Ace of Spades folder again\n") +
                                        (detected.has_value() ? up::path_to_utf8(*detected) : std::string{}))});
            break;
        case up::FirstRunAction::choose_folder:
            links.push_back({action, button_choose_folder,
                             L"Select my Ace of Spades folder\nPick the folder that contains aos.exe"});
            break;
        case up::FirstRunAction::download:
            links.push_back({action, button_download_assets,
                             win::widen(screen.retry_download
                                            ? "Retry download\nContinues where it stopped (" + size + " in total)"
                                            : "Download game assets\n" + size +
                                                  " from the BattleSpades download servers")});
            break;
        case up::FirstRunAction::open_download_page:
            links.push_back({action, button_download_page,
                             win::widen("How to get the game files\nOpens " +
                                        std::string{up::retail_download_page_url} + " in your browser")});
            break;
        }
    }
    std::vector<TASKDIALOG_BUTTON> buttons;
    int default_button = links.empty() ? 0 : links.front().id;
    for (const auto& link : links) {
        buttons.push_back({link.id, link.text.c_str()});
        if (link.action == screen.preselected) default_button = link.id;
    }

    std::string content = "BattleSpades does not include the original Ace of Spades files. ";
    content += retail != nullptr ? "Import them from your own copy or download them; both end with the same "
                                   "verified files."
                                 : "Import them from your own copy of Ace of Spades.";
    if (screen.note == up::FirstRunNote::download_unavailable) {
        content += "\n\nAutomatic download of the game files is not available yet. You can get them from " +
                   std::string{up::retail_download_page_url} + ".";
    } else if (screen.note == up::FirstRunNote::offline) {
        content += "\n\nThe BattleSpades download servers could not be reached, so the automatic download is not "
                   "available right now. Some internet providers block them (for example in Russia): turning on "
                   "a VPN and starting the game again usually fixes it. Help: " +
                   std::string{up::retail_download_page_url};
    }
    if (!last_error.empty()) content += "\n\nThe last attempt did not work:\n" + last_error;
    const auto content_text = win::widen(content);
    const auto hosting_text = win::widen(
        server != nullptr ? "Also enable hosting (Create Match, Map Creator): download the server, " + megabytes(server->size)
                          : std::string{});

    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION;
    config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    config.pszWindowTitle = L"BattleSpades";
    config.pszMainIcon = last_error.empty() ? TD_INFORMATION_ICON : TD_WARNING_ICON;
    config.pszMainInstruction = L"Get the original game files";
    config.pszContent = content_text.c_str();
    config.cButtons = static_cast<UINT>(buttons.size());
    config.pButtons = buttons.data();
    config.nDefaultButton = default_button;
    if (server != nullptr && !server_installed) config.pszVerificationText = hosting_text.c_str();
    int pressed{};
    BOOL hosting{};
    FirstRunChoice choice;
    if (FAILED(TaskDialogIndirect(&config, &pressed, nullptr, &hosting))) return choice;
    choice.enable_hosting = hosting != FALSE;
    for (const auto& link : links) {
        if (link.id == pressed) choice.action = link.action;
    }
    return choice;
}

[[nodiscard]] std::optional<fs::path> detect_ace_of_spades() {
    const auto steam = win::registry_steam_root();
    if (!steam.has_value()) return std::nullopt;
    std::string error;
    const auto located = up::locate_steam_app(*steam, up::ace_of_spades_app_id, error);
    if (!located.has_value()) return std::nullopt;
    return located->install_dir;
}

/// First launch without game files. Returns false when the player quits.
bool first_run(HINSTANCE instance, const up::UpdateLayout& layout, const up::UpdateManifest* manifest,
               const up::UpdaterConfig& config) {
    const auto detected = detect_ace_of_spades();
    up::FirstRunInputs inputs;
    inputs.game_folder_found = detected.has_value();
    inputs.manifest_reachable = manifest != nullptr;
    inputs.download_offered = manifest != nullptr && manifest->component(up::component_retail_assets) != nullptr;
    std::string last_error;
    for (;;) {
        const auto installed = up::read_installed_versions(layout.install);
        const auto screen = up::plan_first_run(inputs);
        const auto choice = ask_first_run(screen, manifest, detected, installed.contains("server"), last_error);
        if (!choice.action.has_value()) return false;
        const auto action = *choice.action;
        if (action == up::FirstRunAction::open_download_page) {
            ShellExecuteW(nullptr, L"open", win::widen(up::retail_download_page_url).c_str(), nullptr, nullptr,
                          SW_SHOWNORMAL);
            continue;
        }

        std::vector<std::string> wanted;
        if (action == up::FirstRunAction::import_detected && detected.has_value()) {
            const auto imported = import_detected_folder(instance, layout, *detected);
            if (!imported.ok()) {
                inputs.import_failed = true;
                last_error = imported.message;
                continue;
            }
        } else if (action == up::FirstRunAction::choose_folder) {
            const auto imported = win::run_asset_import(layout, std::nullopt);
            write_log("interactive import: " + (imported.ok() ? std::string{"ok"} : imported.message));
            if (imported.cancelled()) continue;   // the player closed the picker: same screen, no error
            if (!imported.ok()) {
                inputs.import_failed = true;
                last_error = imported.message;
                continue;
            }
        } else if (action == up::FirstRunAction::download) {
            wanted.emplace_back(up::component_retail_assets);
        }
        if (choice.enable_hosting) wanted.emplace_back(up::component_server);

        if (manifest != nullptr && !wanted.empty()) {
            const auto updates = up::plan_install(*manifest, installed, wanted);
            const auto result = run_session_with_window(instance, layout, *manifest, updates, config, L"Cancel");
            for (const auto& item : result.applied) write_log("installed " + item);
            const bool retail_failed =
                std::ranges::find(result.failed, std::string{up::component_retail_assets}) != result.failed.end();
            if (result.outcome == win::SessionOutcome::cancelled) {
                write_log("first-run download cancelled");
                if (action == up::FirstRunAction::download) {
                    inputs.download_failed = true;
                    last_error = "The download was stopped. It continues where it stopped when you retry.";
                }
            } else if (!result.failed.empty()) {
                write_log("first-run download failed: " + result.error);
                if (retail_failed) {
                    inputs.download_failed = true;
                    last_error = result.error +
                                 "\n\nCheck your internet connection and retry; the download continues where it stopped.";
                }
            }
        }
        if (up::retail_assets_present(layout.install)) return true;
        if (last_error.empty()) last_error = "The game files are not installed yet.";
    }
}

/// The launch options the player had before BattleSpades (for the original game).
std::string recorded_previous_launch_options() {
    std::string error;
    const auto state =
        up::load_registration_state(win::local_app_data() / L"BattleSpades" / L"steam-registration.json", error);
    return state.has_value() ? up::previous_launch_options(*state, up::ace_of_spades_app_id) : std::string{};
}

struct LaunchChoice {
    std::optional<up::LaunchTarget> target;   ///< nullopt: closed / Esc
    bool remember{};
};

/// "What do you want to play?" when Steam starts Ace of Spades. Enter plays
/// BattleSpades, arrow keys / Tab move, Esc closes without starting anything.
LaunchChoice ask_launch_target(HINSTANCE instance) {
    TASKDIALOG_BUTTON buttons[] = {
        {button_play_battlespades, L"Play &BattleSpades\nThe modern Ace of Spades client with the BattleSpades servers"},
        {button_play_original, L"Play &Ace of Spades (original)\nStarts the original game exactly as Steam would"},
    };
    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.hInstance = instance;
    config.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_HICON_MAIN;
    config.pszWindowTitle = L"Ace of Spades";
    config.hMainIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    config.pszMainInstruction = L"What do you want to play?";
    config.cButtons = static_cast<UINT>(std::size(buttons));
    config.pButtons = buttons;
    config.nDefaultButton = button_play_battlespades;
    config.pszVerificationText = L"&Remember my choice";
    config.pszFooter = L"Hold Shift while pressing Play in Steam to choose again.";
    int pressed{};
    BOOL remember{};
    LaunchChoice choice;
    if (FAILED(TaskDialogIndirect(&config, &pressed, nullptr, &remember))) {
        choice.target = up::LaunchTarget::battlespades;   // no dialog possible: the default
        return choice;
    }
    if (pressed == button_play_battlespades) choice.target = up::LaunchTarget::battlespades;
    if (pressed == button_play_original) choice.target = up::LaunchTarget::original;
    choice.remember = remember != FALSE && choice.target.has_value();
    return choice;
}

/// Runs Steam's %command% (the retail game) unchanged and waits, so Steam's
/// playtime and overlay stay attached to it.
int launch_original(const std::vector<std::string>& original_command) {
    const auto previous = recorded_previous_launch_options();
    const auto line = up::build_original_command_line(original_command, previous);
    write_log("starting the original game: " + line);
    const auto executable = up::path_from_utf8(original_command.front());
    std::error_code code;
    if (!fs::is_regular_file(executable, code)) {
        MessageBoxW(nullptr,
                    (L"The original game was not found:\n" + executable.wstring() +
                     L"\n\nIn Steam, right-click Ace of Spades > Properties > Installed Files > Verify integrity of "
                     L"game files.")
                        .c_str(),
                    L"Ace of Spades", MB_ICONERROR | MB_OK);
        return 1;
    }
    auto command_line = win::widen(line);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const auto directory = executable.parent_path();
    // No application name: a wrapper from the previous launch options may be
    // first on the line; otherwise the quoted retail exe is.
    if (!CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, FALSE, 0U, nullptr, directory.c_str(),
                        &startup, &process)) {
        const auto error = GetLastError();
        MessageBoxW(nullptr, (L"Could not start Ace of Spades (error " + std::to_wstring(error) + L").").c_str(),
                    L"Ace of Spades", MB_ICONERROR | MB_OK);
        return 1;
    }
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code{};
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hProcess);
    return static_cast<int>(exit_code);
}

/**
 * The installer could not register with Steam because Steam was running (it
 * rewrites its config on exit). It left update\steam-register.pending with
 * the helper flags; finish it now if Steam is closed, silently.
 */
void complete_pending_steam_registration(const up::UpdateLayout& layout) {
    const auto marker = layout.root() / "steam-register.pending";
    std::string error;
    const auto text = up::read_text_file(marker, error);
    if (!text.has_value()) return;
    if (win::is_steam_running()) {
        write_log("Steam registration still pending: Steam is running");
        return;
    }
    std::vector<std::string> arguments{"register", "--launcher", up::path_to_utf8(win::current_executable())};
    for (const char* flag : {"--launch-options", "--shortcut"}) {
        if (text->find(flag) != std::string::npos) arguments.emplace_back(flag);
    }
    std::error_code code;
    if (arguments.size() == 3U) {
        fs::remove(marker, code);
        return;
    }
    const auto helper = layout.install / L"BattleSpadesSetupHelper.exe";
    const auto line = win::widen(up::build_windows_command_line(up::path_to_utf8(helper), arguments));
    const auto exit = win::run_hidden(helper.wstring(), line, 60U * 1000U);
    write_log("pending Steam registration: exit " + (exit.has_value() ? std::to_string(*exit) : std::string{"failed"}));
    if (exit.has_value() && *exit != 2U) fs::remove(marker, code);
}

/// The per-launch phase. Returns whether the game may start.
UpdateDecision update_phase(HINSTANCE instance, const up::UpdateLayout& layout, const up::UpdaterConfig& config,
                            bool check_updates) {
    std::optional<up::UpdateManifest> manifest;
    const bool needs_game_files = !up::retail_assets_present(layout.install);
    const auto saved_manifest = layout.root() / std::string{up::saved_manifest_name};
    if (check_updates) {
        win::SessionCallbacks silent;
        silent.log = write_log;
        // Without the game files there is nothing to play yet, so a slow
        // connection may take longer before the download button is given up on.
        auto patient = config;
        if (needs_game_files) patient.check_timeout_ms = std::max<std::uint32_t>(config.check_timeout_ms, 20000U);
        auto discovery = win::discover_updates(patient, silent);
        if (discovery.manifest.has_value()) {
            manifest = std::move(discovery.manifest);
            if (!discovery.body.empty()) {
                std::string ignored;
                static_cast<void>(up::write_file_atomic(saved_manifest, discovery.body, ignored));
            }
        } else {
            write_log("update check skipped: " + discovery.error);
        }
    }
    const up::UpdateManifest* known = manifest.has_value() ? &*manifest : nullptr;

    // Server slow or blocked this time: the first-run screen still offers the
    // download from the last list that arrived. Updates never use it.
    std::optional<up::UpdateManifest> saved;
    if (known == nullptr && needs_game_files) {
        std::string error;
        if (const auto text = up::read_text_file(saved_manifest, error); text.has_value()) {
            saved = up::parse_update_manifest(*text, error);
            if (saved.has_value()) write_log("update list unreachable; offering the game files from the saved copy");
        }
    }
    const up::UpdateManifest* first_run_list = known != nullptr ? known : saved.has_value() ? &*saved : nullptr;

    if (needs_game_files && !first_run(instance, layout, first_run_list, config)) {
        return UpdateDecision::quit;
    }
    if (known == nullptr) return UpdateDecision::launch;   // offline: play what is installed

    const auto installed = up::read_installed_versions(layout.install);
    auto state = up::load_updater_state(layout);
    const auto plan = up::plan_updates(*known, installed, state, policy_of(config));
    for (const auto& note : plan.notes) write_log(note);

    std::vector<std::string> declined;
    for (const auto& update : plan.updates) {
        if (update.prompt != up::UpdatePrompt::ask) continue;
        if (ask_large_update(update, config.max_deferrals)) continue;
        declined.push_back(update.release.name);
        state.deferrals[up::deferral_key(update.release)] = update.deferrals + 1U;
        write_log("large update " + up::deferral_key(update.release) + " deferred (" +
                  std::to_string(update.deferrals + 1U) + " of " + std::to_string(config.max_deferrals) + ")");
    }
    if (!declined.empty()) {
        std::string ignored;
        static_cast<void>(up::save_updater_state(layout, state, ignored));
    }
    const auto updates = up::without_declined(plan.updates, declined);
    if (!updates.empty()) {
        const bool blocking = std::ranges::any_of(updates, up::blocks_play);
        std::string list;
        for (const auto& update : updates) list += " " + up::deferral_key(update.release);
        write_log("updating:" + list + (blocking ? " (required)" : ""));
        const auto result = run_session_with_window(instance, layout, *known, updates, config,
                                                    blocking ? L"Quit" : L"Play without updating");
        for (const auto& item : result.applied) write_log("installed " + item);
        if (result.outcome == win::SessionOutcome::cancelled) {
            write_log(blocking ? "required update cancelled; quitting" : "update skipped by the player");
            if (blocking) {
                write_hosting_status(layout, known);
                return UpdateDecision::quit;
            }
        }
        // Only a required client/assets update that failed stops the game; a
        // server failure only affects hosting.
        for (const auto& update : updates) {
            if (!up::blocks_play(update) ||
                std::ranges::find(result.failed, update.release.name) == result.failed.end()) {
                continue;
            }
            const auto text = win::widen("A required BattleSpades update (" + update.release.name + " " +
                                         update.release.version + ") could not be installed:\n" + result.error +
                                         "\n\nCheck your internet connection and start the game again.");
            MessageBoxW(nullptr, text.c_str(), L"BattleSpades", MB_ICONERROR | MB_OK);
            write_hosting_status(layout, known);
            return UpdateDecision::quit;
        }
    }
    write_hosting_status(layout, known);
    return UpdateDecision::launch;
}

/// --install-component NAME...: opt-in download (e.g. the server when the
/// player opens Create Match), then exit. The client re-reads hosting.json.
int install_command(HINSTANCE instance, const up::UpdateLayout& layout, const up::UpdaterConfig& config,
                    const std::vector<std::string>& names) {
    win::SessionCallbacks silent;
    silent.log = write_log;
    auto discovery = win::discover_updates(config, silent);
    if (!discovery.manifest.has_value()) {
        const auto text = win::widen("The download list could not be reached:\n" + discovery.error +
                                     "\n\nCheck your internet connection and try again.");
        MessageBoxW(nullptr, text.c_str(), L"BattleSpades", MB_ICONWARNING | MB_OK);
        return 1;
    }
    const auto updates = up::plan_install(*discovery.manifest, up::read_installed_versions(layout.install), names);
    int status = 0;
    if (!updates.empty()) {
        const auto result = run_session_with_window(instance, layout, *discovery.manifest, updates, config, L"Cancel");
        for (const auto& item : result.applied) write_log("installed " + item);
        if (result.outcome != win::SessionOutcome::applied) {
            status = 1;
            if (result.outcome != win::SessionOutcome::cancelled) {
                MessageBoxW(nullptr, win::widen("The download failed:\n" + result.error).c_str(), L"BattleSpades",
                            MB_ICONERROR | MB_OK);
            }
        }
    }
    write_hosting_status(layout, &*discovery.manifest);
    return status;
}

int launch_client(const fs::path& install, const std::vector<std::string>& arguments) {
    const auto client = install / L"BattleSpadesClient.exe";
    std::error_code code;
    if (!fs::is_regular_file(client, code)) {
        MessageBoxW(nullptr,
                    (L"BattleSpadesClient.exe is missing from\n" + install.wstring() +
                     L"\n\nReinstall BattleSpades, or run BattleSpadesLauncher.exe --rollback.")
                        .c_str(),
                    L"BattleSpades", MB_ICONERROR | MB_OK);
        return 1;
    }
    auto command_line = win::widen(up::build_windows_command_line(up::path_to_utf8(client), arguments));
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(client.c_str(), command_line.data(), nullptr, nullptr, FALSE, 0U, nullptr, install.c_str(),
                        &startup, &process)) {
        const auto error = GetLastError();
        MessageBoxW(nullptr, (L"Could not start BattleSpadesClient.exe (error " + std::to_wstring(error) + L").").c_str(),
                    L"BattleSpades", MB_ICONERROR | MB_OK);
        return 1;
    }
    CloseHandle(process.hThread);
    // Waiting keeps Steam's "running" state and playtime tied to the game.
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code{};
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hProcess);
    return static_cast<int>(exit_code);
}

std::vector<std::string> command_line_arguments() {
    int count{};
    LPWSTR* values = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::string> arguments;
    for (int index = 1; index < count; ++index) arguments.push_back(win::narrow(values[index]));
    LocalFree(values);
    return arguments;
}

int rollback_command(const up::UpdateLayout& layout) {
    const auto result = win::rollback_last_session(layout);
    std::string restored;
    for (const auto& item : result.applied) restored += "\n  " + item;
    write_log(result.outcome == win::SessionOutcome::applied ? "rolled back:" + restored : "rollback: " + result.error);
    const auto text = result.outcome == win::SessionOutcome::applied
                          ? "Restored the previous BattleSpades version:" + restored +
                                "\n\nThe update you rolled back from will not be reinstalled automatically."
                          : "Nothing was restored: " + result.error;
    MessageBoxW(nullptr, win::widen(text).c_str(), L"BattleSpades",
                (result.outcome == win::SessionOutcome::failed ? MB_ICONERROR : MB_ICONINFORMATION) | MB_OK);
    return result.outcome == win::SessionOutcome::failed ? 1 : 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto install = win::current_executable().parent_path();
    const up::UpdateLayout layout{install};
    log_path = layout.root() / "launcher.log";

    const auto arguments = up::parse_launcher_arguments(command_line_arguments());
    if (!arguments.error.empty()) {
        MessageBoxW(nullptr, win::widen(arguments.error).c_str(), L"BattleSpades", MB_ICONERROR | MB_OK);
        return 2;
    }
    const auto choice_file = layout.root() / "launch-choice.json";
    if (arguments.reset_launch_choice) {
        std::string error;
        const bool cleared = up::save_launch_choice(choice_file, std::nullopt, error);
        write_log(cleared ? "launch choice reset" : "launch choice reset failed: " + error);
        MessageBoxW(nullptr,
                    cleared ? L"Done. The next time you press Play on Ace of Spades in Steam, BattleSpades asks "
                              L"whether to play BattleSpades or the original game."
                            : win::widen("The remembered choice could not be reset: " + error).c_str(),
                    L"BattleSpades", (cleared ? MB_ICONINFORMATION : MB_ICONERROR) | MB_OK);
        return cleared ? 0 : 1;
    }

    // Steam's Play on Ace of Spades: BattleSpades or the original game?
    up::LaunchChoiceInputs choice_inputs;
    choice_inputs.has_original_command = !arguments.original_command.empty();
    choice_inputs.remembered = up::load_launch_choice(choice_file);
    choice_inputs.shift_held = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    choice_inputs.force_chooser = arguments.choose;
    auto decision = up::decide_launch(choice_inputs);
    if (decision == up::LaunchDecision::ask) {
        const auto picked = ask_launch_target(instance);
        if (!picked.target.has_value()) return 0;   // closed: start nothing
        if (picked.remember) {
            std::string error;
            if (!up::save_launch_choice(choice_file, picked.target, error)) write_log("cannot remember: " + error);
        }
        decision = *picked.target == up::LaunchTarget::original ? up::LaunchDecision::original
                                                                 : up::LaunchDecision::battlespades;
    }
    if (decision == up::LaunchDecision::original) return launch_original(arguments.original_command);
    if (!arguments.dropped_command.empty()) write_log("playing BattleSpades instead of " + arguments.dropped_command);

    up::empty_trash(layout);

    // Serialise update work between concurrent launches (double-clicks, or a
    // server download requested by the running client).
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\BattleSpadesLauncherUpdate");
    const bool owns_update = mutex != nullptr && GetLastError() != ERROR_ALREADY_EXISTS;
    if (mutex != nullptr && !owns_update) {
        if (!arguments.install_components.empty()) {
            // A download is already running; the client shows its own notice.
            CloseHandle(mutex);
            return 0;
        }
        WaitForSingleObject(mutex, 10U * 60U * 1000U);
    }

    if (owns_update) {
        for (const auto& component : up::interrupted_components(layout)) {
            const auto restored = up::rollback_last_update(layout, component);
            write_log(restored.ok ? "rolled back an interrupted " + component + " update"
                                  : "interrupted " + component + " rollback failed: " + restored.error);
        }
        complete_pending_steam_registration(layout);
    }

    auto config = up::load_updater_config(install / "updater.json");
    if (!arguments.manifest_url.empty()) config.manifest_url = arguments.manifest_url;
    if (!arguments.api_url.empty()) {
        config.api_url = arguments.api_url;
        config.github_fallback = true;
    }

    int exit_code = 0;
    bool launch = !arguments.update_only;
    if (arguments.rollback) {
        exit_code = owns_update ? rollback_command(layout) : 1;
        launch = false;
    } else if (!arguments.install_components.empty()) {
        exit_code = install_command(instance, layout, config, arguments.install_components);
        launch = false;
    } else if (update_phase(instance, layout, config, owns_update && config.enabled && !arguments.no_update) ==
               UpdateDecision::quit) {
        launch = false;
        exit_code = 1;
    }
    if (mutex != nullptr) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return launch ? launch_client(install, arguments.forwarded) : exit_code;
}
