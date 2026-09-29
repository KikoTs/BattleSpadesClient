// Stand-in for BattleSpadesSteamBridge32 in tests: speaks the bridge protocol
// without Steam. AOS_FAKE_BRIDGE_MODE selects the behaviour under test.

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <chrono>
#include <cstdlib>
#include <string>
#include <string_view>
#include <thread>

namespace {

void write_line(std::string_view value) {
    std::string line{value};
    line.push_back('\n');
    DWORD written{};
    static_cast<void>(WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), line.data(),
                                static_cast<DWORD>(line.size()), &written, nullptr));
}

std::string hex(std::string_view value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        result.push_back(digits[byte >> 4U]);
        result.push_back(digits[byte & 0x0FU]);
    }
    return result;
}

std::string mode() {
    char* value{};
    std::size_t size{};
    if (_dupenv_s(&value, &size, "AOS_FAKE_BRIDGE_MODE") != 0 || value == nullptr) return {};
    std::string result{value};
    std::free(value);
    return result;
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    std::string nonce;
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::wstring_view{argv[index]} == L"--nonce") {
            for (const auto character : std::wstring_view{argv[index + 1]}) {
                nonce.push_back(static_cast<char>(character));
            }
        }
    }
    const auto selected = mode();
    if (selected == "fail") {
        write_line("ERROR\t" + hex("Steam is not running; start Steam and sign in"));
        return 3;
    }
    if (selected == "permanent") {
        write_line("ERROR\t" + hex("the imported steam_api.dll is not the retail runtime"));
        return 6;
    }
    if (selected == "junk") {
        // What the 2013 steam_api.dll printed onto the old bridge's pipe.
        write_line("Setting breakpad minidump AppID = 224540");
        write_line("Steam_SetMinidumpSteamID:  Caching Steam ID:  76561198000000001 [API loaded no]");
    }
    if (selected == "slow") std::this_thread::sleep_for(std::chrono::milliseconds{2'500});
    write_line("READY\t" + nonce + "\t76561198000000001\t" + hex("Spade") + "\t" + hex("english"));

    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    std::string command;
    for (;;) {
        char value{};
        DWORD read{};
        if (ReadFile(input, &value, 1U, &read, nullptr) == FALSE || read != 1U) return 0;
        if (value != '\n') {
            command.push_back(value);
            continue;
        }
        if (command == "QUIT") {
            write_line("OK");
            return 0;
        }
        if (command == "SUBSCRIBE") {
            write_line("OK");
            write_line("EVENT\tJOIN\t" + hex("+connect steam:76561198000000002") + "\t42");
            write_line("EVENT\tLOBBY\t109775241021923456\t43");
        } else if (command == "TICKET") {
            if (selected == "junk") write_line("Steam_SetMinidumpSteamID:  Setting Steam ID:  1");
            write_line("TICKET\t7\t0011aabb");
        } else {
            write_line("ERROR\t" + hex("unknown Steam bridge command"));
        }
        command.clear();
    }
}
#else
int main() { return 0; }
#endif
