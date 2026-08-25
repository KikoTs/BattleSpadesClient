#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::network {

/** The legacy score service stores both event count and awarded score. */
struct AosPlayScoreValue final {
    double count{};
    double score{};

    [[nodiscard]] friend constexpr bool operator==(const AosPlayScoreValue&,
                                                   const AosPlayScoreValue&) = default;
};

/** One validated row from AoSPlay's recovered retail leaderboard contract. */
struct AosPlayLeaderboardRecord final {
    std::uint64_t account_id{};
    std::string player_name;
    AosPlayScoreValue total;
    std::map<std::uint32_t, AosPlayScoreValue> stats;
    std::uint32_t global_rank{};
};

/** One validated profile returned by POST /profile. */
struct AosPlayProfileRecord final {
    std::string player_name;
    AosPlayScoreValue total;
    std::map<std::uint32_t, AosPlayScoreValue> stats;
};

struct AosPlayLeaderboardResult final {
    std::vector<AosPlayLeaderboardRecord> rows;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept { return error.empty(); }
};

struct AosPlayProfileResult final {
    std::optional<AosPlayProfileRecord> profile;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept { return error.empty(); }
};

enum class AosPlayLeaderboardScope : std::uint8_t {
    global,
    local,
    friends,
};

/** The ten presets published by AoSPlay and recovered from retail constants.py. */
struct AosPlayLeaderboardPreset final {
    std::uint32_t sort_stat{};
    std::span<const std::uint32_t> stat_ids;
};

struct AosPlayScoreServiceConfig final {
    std::string leaderboard_url{"https://www.aosplay.net/leaderboard"};
    std::string profile_url{"https://www.aosplay.net/profile"};
    std::chrono::milliseconds timeout{5'000};
    std::size_t maximum_payload_bytes{2U << 20U};
    std::size_t maximum_rows{512U};
};

[[nodiscard]] std::optional<AosPlayLeaderboardPreset>
aosplay_leaderboard_preset(std::uint8_t type) noexcept;

/**
 * Builds the numeric application/x-www-form-urlencoded body used by retail.
 *
 * `account_id` is required for local/friends scope. Friend IDs are bounded,
 * de-duplicated and followed by the signed-in player, matching retail's
 * SteamGetFriendList() + GetUserSteamID() request shape.
 */
[[nodiscard]] std::string build_aosplay_leaderboard_form(
    std::uint8_t type,
    AosPlayLeaderboardScope scope,
    std::uint64_t account_id,
    std::size_t result_limit,
    std::span<const std::uint64_t> friend_account_ids = {});

/** Strict, size-bounded JSON parsers kept public for fixture tests. */
[[nodiscard]] AosPlayLeaderboardResult parse_aosplay_leaderboard(
    std::string_view json,
    std::size_t maximum_rows = 512U);
[[nodiscard]] AosPlayProfileResult parse_aosplay_profile(std::string_view json);

/** Blocking HTTPS adapters. Invoke only from a bounded worker. */
[[nodiscard]] AosPlayLeaderboardResult fetch_aosplay_leaderboard(
    const AosPlayScoreServiceConfig& config,
    std::uint8_t type,
    AosPlayLeaderboardScope scope,
    std::uint64_t account_id = 0U,
    std::span<const std::uint64_t> friend_account_ids = {});
[[nodiscard]] AosPlayProfileResult fetch_aosplay_profile(
    const AosPlayScoreServiceConfig& config,
    std::uint64_t account_id);

/**
 * Resolves the current revival account without trusting a display name as an
 * identity. The name is used only to locate the durable ID in an authoritative
 * leaderboard row; ambiguous or absent names fail closed.
 */
[[nodiscard]] std::optional<std::uint64_t> resolve_aosplay_account_id(
    const AosPlayScoreServiceConfig& config,
    std::string_view player_name,
    std::string& error);

} // namespace battlespades::network
