#pragma once

// Header-only so the separately built 32-bit Steam bridge can share it.

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <TlHelp32.h>

#include <cwchar>
#include <string>

namespace battlespades::platform {

/**
 * Explain why SteamAPI_Init cannot find a Steam that is plainly running.
 *
 * Every steam_api (the 2013 retail DLL and the current SDK alike) decides
 * "is Steam running" by reading HKCU\Software\Valve\Steam\ActiveProcess\pid
 * and checking that process is alive. Steam writes it when it starts. A Steam
 * emulator (the no-Steam retail client, for one) or a crashed Steam can leave
 * a dead pid there, and then every Steam game on the machine fails to attach
 * until Steam restarts, while steam.exe keeps running. The integration used
 * to report that as "start Steam", which is exactly what the player had done.
 *
 * Returns an empty string when the registration is consistent or cannot be
 * read. Never writes the registry: repairing it is Steam's job.
 */
[[nodiscard]] inline std::string diagnose_steam_registration() {
    DWORD pid{};
    DWORD size = sizeof(pid);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"pid",
                     RRF_RT_REG_DWORD, nullptr, &pid, &size) != ERROR_SUCCESS) {
        return {};
    }
    bool registered_alive{};
    if (pid != 0U) {
        if (const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            process != nullptr) {
            DWORD code{};
            registered_alive = GetExitCodeProcess(process, &code) != FALSE && code == STILL_ACTIVE;
            CloseHandle(process);
        }
    }
    DWORD running_steam{};
    if (const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0U);
        snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        for (BOOL more = Process32FirstW(snapshot, &entry); more != FALSE;
             more = Process32NextW(snapshot, &entry)) {
            if (_wcsicmp(entry.szExeFile, L"steam.exe") == 0) {
                running_steam = entry.th32ProcessID;
                break;
            }
        }
        CloseHandle(snapshot);
    }
    if (running_steam == 0U) return "Steam is not running; start Steam and sign in";
    if (!registered_alive || pid != running_steam) {
        return "Steam is running (pid " + std::to_string(running_steam) +
               ") but its registration names pid " + std::to_string(pid) +
               ", which " + (registered_alive ? "is another process" : "has exited") +
               ". A Steam emulator or a crashed Steam left it behind; restart Steam";
    }
    return {};
}

} // namespace battlespades::platform

#endif
