#include "battlespades/updater/semver.hpp"

#include <algorithm>
#include <charconv>

namespace battlespades::updater {
namespace {

[[nodiscard]] bool all_digits(std::string_view text) noexcept {
    return !text.empty() &&
           std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; });
}

[[nodiscard]] bool valid_identifier(std::string_view text) noexcept {
    return !text.empty() && std::ranges::all_of(text, [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               c == '-';
    });
}

[[nodiscard]] std::optional<std::uint64_t> parse_number(std::string_view text) noexcept {
    if (!all_digits(text) || text.size() > 19U) return std::nullopt;
    // semver forbids leading zeroes in numeric parts.
    if (text.size() > 1U && text.front() == '0') return std::nullopt;
    std::uint64_t value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return std::nullopt;
    return value;
}

[[nodiscard]] std::vector<std::string_view> split(std::string_view text, char separator) {
    std::vector<std::string_view> parts;
    std::size_t start{};
    for (;;) {
        const auto next = text.find(separator, start);
        if (next == std::string_view::npos) {
            parts.push_back(text.substr(start));
            return parts;
        }
        parts.push_back(text.substr(start, next - start));
        start = next + 1U;
    }
}

[[nodiscard]] int compare_identifier(std::string_view left, std::string_view right) noexcept {
    const bool left_numeric = all_digits(left);
    const bool right_numeric = all_digits(right);
    if (left_numeric && right_numeric) {
        // Compare by length first so arbitrarily long numbers stay correct.
        const auto trim = [](std::string_view value) {
            while (value.size() > 1U && value.front() == '0') value.remove_prefix(1U);
            return value;
        };
        const auto l = trim(left);
        const auto r = trim(right);
        if (l.size() != r.size()) return l.size() < r.size() ? -1 : 1;
        const auto order = l.compare(r);
        return order < 0 ? -1 : (order > 0 ? 1 : 0);
    }
    if (left_numeric != right_numeric) return left_numeric ? -1 : 1;
    const auto order = left.compare(right);
    return order < 0 ? -1 : (order > 0 ? 1 : 0);
}

} // namespace

std::optional<Version> parse_version(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1U);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' ||
                             text.back() == '\n')) {
        text.remove_suffix(1U);
    }
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) text.remove_prefix(1U);
    if (text.empty()) return std::nullopt;

    Version version;
    if (const auto plus = text.find('+'); plus != std::string_view::npos) {
        const auto build = text.substr(plus + 1U);
        for (const auto part : split(build, '.')) {
            if (!valid_identifier(part)) return std::nullopt;
        }
        version.build = std::string{build};
        text = text.substr(0U, plus);
    }
    std::string_view core = text;
    if (const auto dash = text.find('-'); dash != std::string_view::npos) {
        core = text.substr(0U, dash);
        const auto pre = text.substr(dash + 1U);
        for (const auto part : split(pre, '.')) {
            if (!valid_identifier(part)) return std::nullopt;
            if (all_digits(part) && part.size() > 1U && part.front() == '0') return std::nullopt;
            version.prerelease.emplace_back(part);
        }
    }
    const auto numbers = split(core, '.');
    if (numbers.empty() || numbers.size() > 3U) return std::nullopt;
    std::uint64_t* const fields[] = {&version.major, &version.minor, &version.patch};
    for (std::size_t index = 0; index < numbers.size(); ++index) {
        const auto value = parse_number(numbers[index]);
        if (!value.has_value()) return std::nullopt;
        *fields[index] = *value;
    }
    return version;
}

int compare_versions(const Version& left, const Version& right) noexcept {
    if (left.major != right.major) return left.major < right.major ? -1 : 1;
    if (left.minor != right.minor) return left.minor < right.minor ? -1 : 1;
    if (left.patch != right.patch) return left.patch < right.patch ? -1 : 1;
    if (left.prerelease.empty() != right.prerelease.empty()) {
        return left.prerelease.empty() ? 1 : -1;
    }
    const auto count = std::min(left.prerelease.size(), right.prerelease.size());
    for (std::size_t index = 0; index < count; ++index) {
        if (const auto order = compare_identifier(left.prerelease[index], right.prerelease[index]);
            order != 0) {
            return order;
        }
    }
    if (left.prerelease.size() != right.prerelease.size()) {
        return left.prerelease.size() < right.prerelease.size() ? -1 : 1;
    }
    return 0;
}

std::string to_string(const Version& version) {
    std::string text = std::to_string(version.major) + '.' + std::to_string(version.minor) + '.' +
                       std::to_string(version.patch);
    for (std::size_t index = 0; index < version.prerelease.size(); ++index) {
        text += index == 0U ? '-' : '.';
        text += version.prerelease[index];
    }
    if (!version.build.empty()) text += '+' + version.build;
    return text;
}

bool is_newer_version(std::string_view candidate, std::string_view installed) {
    const auto left = parse_version(candidate);
    const auto right = parse_version(installed);
    return left.has_value() && right.has_value() && compare_versions(*left, *right) > 0;
}

} // namespace battlespades::updater
