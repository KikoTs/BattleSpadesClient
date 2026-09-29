#include "battlespades/platform/steam_connect.hpp"

#include <algorithm>
#include <charconv>
#include <vector>

namespace battlespades::platform {
namespace {

[[nodiscard]] std::optional<std::uint64_t> parse_id(std::string_view digits) noexcept {
    if (digits.empty() || digits.size() > 20U ||
        !std::ranges::all_of(digits, [](char c) { return c >= '0' && c <= '9'; })) {
        return std::nullopt;
    }
    std::uint64_t value{};
    const auto* const end = digits.data() + digits.size();
    const auto parsed = std::from_chars(digits.data(), end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end || value == 0U) return std::nullopt;
    return value;
}

[[nodiscard]] bool is_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

[[nodiscard]] std::vector<std::string_view> tokens(std::string_view value) {
    std::vector<std::string_view> result;
    std::size_t index{};
    while (index < value.size()) {
        while (index < value.size() && is_space(value[index])) ++index;
        const auto start = index;
        while (index < value.size() && !is_space(value[index])) ++index;
        if (index > start) result.push_back(value.substr(start, index - start));
    }
    return result;
}

/** host:port with a DNS-shaped host; the loader re-validates it properly. */
[[nodiscard]] bool looks_like_endpoint(std::string_view value) noexcept {
    const auto colon = value.rfind(':');
    if (colon == std::string_view::npos || colon == 0U || colon + 1U >= value.size() ||
        value.size() > 260U) {
        return false;
    }
    const auto host = value.substr(0U, colon);
    const auto port = value.substr(colon + 1U);
    if (!std::ranges::all_of(host, [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '.' || c == '-';
        })) {
        return false;
    }
    const auto number = parse_id(port);
    return number.has_value() && *number <= 65'535U;
}

[[nodiscard]] std::optional<SteamJoinTarget> single_target(std::string_view value) {
    if (value.starts_with("steam:")) {
        const auto id = parse_id(value.substr(6U));
        if (!id.has_value()) return std::nullopt;
        return SteamJoinTarget{SteamJoinTargetKind::steam_host, *id, {}};
    }
    if (looks_like_endpoint(value)) {
        return SteamJoinTarget{SteamJoinTargetKind::endpoint, 0U, std::string{value}};
    }
    return std::nullopt;
}

} // namespace

std::uint64_t parse_steam_host_address(std::string_view value) noexcept {
    try {
        const auto target = parse_steam_join_target(value);
        return target.has_value() && target->kind == SteamJoinTargetKind::steam_host
                   ? target->steam_id
                   : 0U;
    } catch (...) {
        return 0U;
    }
}

std::optional<SteamJoinTarget> parse_steam_join_target(std::string_view value) {
    if (value.size() > 1'024U) return std::nullopt;
    const auto parts = tokens(value);
    if (parts.empty()) return std::nullopt;
    for (std::size_t index{}; index < parts.size(); ++index) {
        const auto part = parts[index];
        if (part == "+connect" || part == "--connect") {
            if (index + 1U >= parts.size()) return std::nullopt;
            return single_target(parts[index + 1U]);
        }
        if (part == "+connect_lobby" || part == "--connect-lobby") {
            if (index + 1U >= parts.size()) return std::nullopt;
            const auto lobby = parse_id(parts[index + 1U]);
            if (!lobby.has_value()) return std::nullopt;
            return SteamJoinTarget{SteamJoinTargetKind::lobby, *lobby, {}};
        }
    }
    // No switch: only a lone address is meaningful. Several loose tokens are
    // some other launcher's arguments, not a connect value.
    return parts.size() == 1U ? single_target(parts.front()) : std::nullopt;
}

std::string steam_host_connect_string(std::uint64_t host_steam_id) {
    return "+connect steam:" + std::to_string(host_steam_id);
}

std::optional<std::string> endpoint_connect_string(std::string_view host, std::uint16_t port) {
    if (host.empty() || port == 0U || host == "localhost" || host == "0.0.0.0" ||
        host.starts_with("127.") || host == "::1" || host == "[::1]") {
        return std::nullopt;
    }
    auto value = std::string{host} + ":" + std::to_string(port);
    if (!looks_like_endpoint(value)) return std::nullopt;
    return "+connect " + value;
}

bool SteamJoinDeduplicator::accept(const SteamJoinTarget& target,
                                   std::chrono::steady_clock::time_point now) {
    if (last_.has_value() && *last_ == target && now - last_at_ < window_) return false;
    last_ = target;
    last_at_ = now;
    return true;
}

} // namespace battlespades::platform
