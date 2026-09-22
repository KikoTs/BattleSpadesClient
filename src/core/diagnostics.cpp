#include "battlespades/core/diagnostics.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace battlespades::core {

void diagnostic(std::string_view category, std::string_view message) noexcept {
    static std::mutex mutex;
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::system_clock::to_time_t(now);
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm utc{};
#if defined(_WIN32)
    if (gmtime_s(&utc, &seconds) != 0) return;
#else
    if (gmtime_r(&seconds, &utc) == nullptr) return;
#endif
    char stamp[32]{};
    if (std::strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S", &utc) == 0U) return;
    const std::scoped_lock lock{mutex};
    std::fprintf(stderr, "%s.%03dZ [%.*s] %.*s\n", stamp, static_cast<int>(milliseconds),
                 static_cast<int>(category.size()), category.data(),
                 static_cast<int>(message.size()), message.data());
    std::fflush(stderr);
}

} // namespace battlespades::core
