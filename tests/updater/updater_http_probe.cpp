// Headless entry point for local synthetic HTTP tests of the real WinHTTP
// download path. Does not install, launch, or contact a production service.
#include "../../src/updater/windows/win_util.hpp"

#include <iostream>
#include <string>

int wmain(int argc, wchar_t** argv) {
    if (argc != 4) return 2;
    namespace win = battlespades::updater::win;
    win::HttpOptions options;
    options.timeout_ms = 3000U;
    const auto result = win::http_download(win::narrow(argv[1]), argv[2], options, {}, std::stoull(argv[3]));
    if (result.status != 200U || !result.error.empty()) {
        std::cerr << result.status << ": " << result.error << '\n';
        return 1;
    }
    return 0;
}
