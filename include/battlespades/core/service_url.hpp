#pragma once

#include <algorithm>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>

namespace battlespades::core {

/** A service origin, never credentials, a path, or a prefix-matched loopback host. */
[[nodiscard]] inline std::optional<std::string> service_origin(std::string_view value) {
    if (value.ends_with('/')) value.remove_suffix(1U);
    const bool secure = value.starts_with("https://");
    if (!secure && !value.starts_with("http://")) return std::nullopt;
    const auto authority = value.substr(secure ? 8U : 7U);
    if (authority.empty() || authority.size() > 259U ||
        authority.find_first_of("/@?#\\%\"' \t\r\n") != std::string_view::npos)
        return std::nullopt;
    auto host = authority;
    std::string_view port;
    if (host.starts_with('[')) {
        const auto end = host.find(']');
        if (end == std::string_view::npos) return std::nullopt;
        host = authority.substr(0U, end + 1U);
        if (end + 1U < authority.size()) {
            if (authority[end + 1U] != ':') return std::nullopt;
            port = authority.substr(end + 2U);
            if (port.empty()) return std::nullopt;
        }
        if (host != "[::1]") return std::nullopt;
    } else {
        if (const auto colon = authority.find(':'); colon != std::string_view::npos) {
            host = authority.substr(0U, colon);
            port = authority.substr(colon + 1U);
            if (port.empty()) return std::nullopt;
        }
        if (host.empty() || host.front() == '.' || host.back() == '.' ||
            !std::ranges::all_of(host, [](unsigned char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '-' || c == '.';
            })) return std::nullopt;
    }
    if (!port.empty()) {
        unsigned number{};
        const auto parsed = std::from_chars(port.data(), port.data() + port.size(), number);
        if (parsed.ec != std::errc{} || parsed.ptr != port.data() + port.size() ||
            number == 0U || number > 65535U) return std::nullopt;
    }
    std::string lower{host};
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
    });
    if (!secure && lower != "localhost" && lower != "127.0.0.1" && lower != "[::1]")
        return std::nullopt;
    return std::string{secure ? "https://" : "http://"} + lower +
           (port.empty() ? std::string{} : ":" + std::string{port});
}

/** URL under a valid origin; loopback HTTP is deliberately limited to local hosting. */
[[nodiscard]] inline bool valid_service_url(std::string_view value) {
    const auto scheme = value.find("://");
    if (scheme == std::string_view::npos) return false;
    const auto slash = value.find('/', scheme + 3U);
    if (value.find_first_of("\\\r\n\t") != std::string_view::npos) return false;
    return service_origin(value.substr(0U, slash)).has_value();
}

} // namespace battlespades::core
