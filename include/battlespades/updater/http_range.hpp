#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>

namespace battlespades::updater {

struct HttpContentRange {
    std::uint64_t first{};
    std::uint64_t last{};
    std::uint64_t total{};
};

/// A single, satisfied byte range with a known resource size. Reject unknown
/// totals, multipart replies, trailing text and integer overflow before append.
[[nodiscard]] inline std::optional<HttpContentRange> parse_http_content_range(std::string_view value) {
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' ||
                              value.back() == '\t')) value.remove_suffix(1U);
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1U);
    if (!value.starts_with("bytes ")) return std::nullopt;
    value.remove_prefix(6U);
    const auto dash = value.find('-');
    const auto slash = value.find('/');
    if (dash == std::string_view::npos || slash == std::string_view::npos || dash >= slash) return std::nullopt;
    const auto number = [](std::string_view text, std::uint64_t& out) {
        const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
        return !text.empty() && result.ec == std::errc{} && result.ptr == text.data() + text.size();
    };
    HttpContentRange range;
    if (!number(value.substr(0U, dash), range.first) ||
        !number(value.substr(dash + 1U, slash - dash - 1U), range.last) ||
        !number(value.substr(slash + 1U), range.total) || range.first > range.last || range.last >= range.total) {
        return std::nullopt;
    }
    return range;
}

} // namespace battlespades::updater
