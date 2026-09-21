#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif

#include "battlespades/network/aosplay_scores.hpp"
#include "battlespades/core/build_info.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
#include <utility>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace battlespades::network {
namespace {

constexpr std::size_t maximum_player_name_bytes{96U};
constexpr double maximum_safe_score{9.0e15};

constexpr std::array general_stats{1U, 220U, 1U, 159U, 160U};
constexpr std::array tdm_stats{3U, 4U, 1U, 202U, 203U, 11U};
constexpr std::array vip_stats{13U, 204U, 205U, 206U};
constexpr std::array tc_stats{20U, 21U, 22U, 23U, 208U};
constexpr std::array occupation_stats{26U, 28U, 209U, 210U, 211U, 212U};
constexpr std::array diamond_stats{38U, 39U, 40U, 213U, 214U, 215U};
constexpr std::array ctf_stats{49U, 50U, 52U, 216U, 217U, 218U};
constexpr std::array zombie_stats{59U, 60U, 61U, 62U};
constexpr std::array demolition_stats{69U, 70U, 71U, 72U};
constexpr std::array multihill_stats{76U, 77U, 219U, 80U, 81U, 82U};

constexpr std::array presets{
    AosPlayLeaderboardPreset{201U, general_stats},
    AosPlayLeaderboardPreset{192U, tdm_stats},
    AosPlayLeaderboardPreset{193U, vip_stats},
    AosPlayLeaderboardPreset{194U, tc_stats},
    AosPlayLeaderboardPreset{195U, occupation_stats},
    AosPlayLeaderboardPreset{196U, diamond_stats},
    AosPlayLeaderboardPreset{197U, ctf_stats},
    AosPlayLeaderboardPreset{198U, zombie_stats},
    AosPlayLeaderboardPreset{199U, demolition_stats},
    AosPlayLeaderboardPreset{200U, multihill_stats},
};

struct CurlBuffer final {
    std::string bytes;
    std::size_t limit{};
    bool overflow{};
};

std::size_t curl_write(char* data, std::size_t size, std::size_t count, void* context) {
    auto& buffer = *static_cast<CurlBuffer*>(context);
    if (size != 0U && count > std::numeric_limits<std::size_t>::max() / size) {
        buffer.overflow = true;
        return 0U;
    }
    const auto byte_count = size * count;
    if (buffer.bytes.size() > buffer.limit ||
        byte_count > buffer.limit - buffer.bytes.size()) {
        buffer.overflow = true;
        return 0U;
    }
    buffer.bytes.append(data, byte_count);
    return byte_count;
}

[[nodiscard]] bool score_number(const nlohmann::json& value, double& output) {
    if (!value.is_number()) return false;
    output = value.get<double>();
    return std::isfinite(output) && output >= -maximum_safe_score &&
           output <= maximum_safe_score;
}

[[nodiscard]] std::optional<AosPlayScoreValue> score_pair(const nlohmann::json& value) {
    if (!value.is_array() || value.size() != 2U) return std::nullopt;
    AosPlayScoreValue result;
    if (!score_number(value[0U], result.count) || !score_number(value[1U], result.score)) {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] bool parse_stats(const nlohmann::json& value,
                               std::map<std::uint32_t, AosPlayScoreValue>& output) {
    if (!value.is_object() || value.size() > 8'192U) return false;
    for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
        std::uint64_t parsed{};
        try {
            std::size_t consumed{};
            parsed = std::stoull(iterator.key(), &consumed, 10);
            if (consumed != iterator.key().size() ||
                parsed > std::numeric_limits<std::uint32_t>::max()) {
                return false;
            }
        } catch (...) {
            return false;
        }
        const auto pair = score_pair(iterator.value());
        if (!pair.has_value()) return false;
        output.emplace(static_cast<std::uint32_t>(parsed), *pair);
    }
    return true;
}

[[nodiscard]] std::string lowercase_ascii(std::string_view value) {
    std::string result{value};
    std::ranges::transform(result, result.begin(), [](char character) {
        if (character >= 'A' && character <= 'Z') {
            return static_cast<char>(character - 'A' + 'a');
        }
        return character;
    });
    return result;
}

struct HttpResult final {
    std::string body;
    std::string error;
};

[[nodiscard]] HttpResult post_form(const AosPlayScoreServiceConfig& config,
                                   std::string_view url,
                                   std::string_view form) {
    if (url.empty() || !url.starts_with("https://") || config.timeout.count() <= 0 ||
        config.maximum_payload_bytes == 0U) {
        return {{}, "invalid AoSPlay score-service configuration"};
    }
    static std::once_flag curl_once;
    static CURLcode curl_initialization{CURLE_FAILED_INIT};
    std::call_once(curl_once, [] { curl_initialization = curl_global_init(CURL_GLOBAL_DEFAULT); });
    if (curl_initialization != CURLE_OK) {
        return {{}, "curl global initialization failed"};
    }
    auto* handle = curl_easy_init();
    if (handle == nullptr) {
        return {{}, "cannot create AoSPlay score request"};
    }
    struct CurlGuard final {
        CURL* value{};
        ~CurlGuard() { curl_easy_cleanup(value); }
    } guard{handle};
    CurlBuffer buffer{{}, config.maximum_payload_bytes, false};
    const std::string url_copy{url};
    const std::string form_copy{form};
    curl_easy_setopt(handle, CURLOPT_URL, url_copy.c_str());
    curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 2L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS,
                     static_cast<long>(config.timeout.count()));
    curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, static_cast<long>(config.timeout.count()));
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(handle, CURLOPT_USERAGENT, "BattleSpadesClient/" AOS_VERSION_STRING " Protocol168/1");
    curl_easy_setopt(handle, CURLOPT_POSTFIELDS, form_copy.c_str());
    curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE,
                     static_cast<long>(form_copy.size()));
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &curl_write);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &buffer);
    const auto performed = curl_easy_perform(handle);
    long status{};
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
    if (performed != CURLE_OK) {
        return {{},
                buffer.overflow ? "AoSPlay score response exceeded its size limit"
                                : std::string{"AoSPlay score request failed: "} +
                                      curl_easy_strerror(performed)};
    }
    if (status != 200L) {
        return {{}, "AoSPlay score service returned HTTP " + std::to_string(status)};
    }
    return {std::move(buffer.bytes), {}};
}

} // namespace

std::optional<AosPlayLeaderboardPreset>
aosplay_leaderboard_preset(std::uint8_t type) noexcept {
    if (type >= presets.size()) return std::nullopt;
    return presets[type];
}

std::string build_aosplay_leaderboard_form(std::uint8_t type,
                                           AosPlayLeaderboardScope scope,
                                           std::uint64_t account_id,
                                           std::size_t result_limit,
                                           std::span<const std::uint64_t> friend_account_ids) {
    const auto preset = aosplay_leaderboard_preset(type);
    if (!preset.has_value() || result_limit == 0U) return {};
    std::string form{"sortid="};
    form += std::to_string(preset->sort_stat);
    form += "&noof_stats=";
    form += std::to_string(preset->stat_ids.size());
    for (const auto stat : preset->stat_ids) {
        form += "&statid=";
        form += std::to_string(stat);
    }
    form += "&noof_results=";
    form += std::to_string(result_limit);
    if (scope == AosPlayLeaderboardScope::local && account_id != 0U) {
        form += "&steamid=";
        form += std::to_string(account_id);
    } else if (scope == AosPlayLeaderboardScope::friends) {
        // Retail submits the Steam friend list followed by the local player.
        // Bound and de-duplicate revival IDs so malformed social data cannot
        // create an unbounded form body or count one account twice.
        constexpr std::size_t maximum_friend_accounts{512U};
        std::vector<std::uint64_t> accounts;
        accounts.reserve((std::min)(friend_account_ids.size() + 1U,
                                    maximum_friend_accounts));
        const auto append_unique = [&accounts](std::uint64_t candidate) {
            if (candidate == 0U ||
                std::find(accounts.cbegin(), accounts.cend(), candidate) != accounts.cend()) {
                return;
            }
            accounts.push_back(candidate);
        };
        for (const auto candidate : friend_account_ids) {
            if (accounts.size() >= maximum_friend_accounts - 1U) break;
            append_unique(candidate);
        }
        append_unique(account_id);
        form += "&noof_friends=";
        form += std::to_string(accounts.size());
        for (const auto friend_account : accounts) {
            form += "&friend=";
            form += std::to_string(friend_account);
        }
    }
    return form;
}

AosPlayLeaderboardResult parse_aosplay_leaderboard(std::string_view json,
                                                   std::size_t maximum_rows) {
    AosPlayLeaderboardResult output;
    if (json.empty() || maximum_rows == 0U) {
        output.error = "AoSPlay leaderboard response is empty";
        return output;
    }
    try {
        const auto document = nlohmann::json::parse(json.begin(), json.end());
        const auto rows = document.find("leaderboard");
        if (!document.is_object() || rows == document.end() || !rows->is_array()) {
            output.error = "AoSPlay leaderboard response is missing leaderboard rows";
            return output;
        }
        output.rows.reserve(std::min(maximum_rows, rows->size()));
        for (const auto& value : *rows) {
            if (output.rows.size() >= maximum_rows) break;
            if (!value.is_array() || value.size() < 5U || !value[0U].is_number_unsigned() ||
                !value[1U].is_string() || !value[4U].is_number_unsigned()) {
                output.error = "AoSPlay leaderboard contains a malformed row";
                output.rows.clear();
                return output;
            }
            AosPlayLeaderboardRecord record;
            record.account_id = value[0U].get<std::uint64_t>();
            record.player_name = value[1U].get<std::string>();
            const auto total = score_pair(value[2U]);
            const auto rank = value[4U].get<std::uint64_t>();
            if (record.account_id == 0U || record.player_name.empty() ||
                record.player_name.size() > maximum_player_name_bytes || !total.has_value() ||
                rank > std::numeric_limits<std::uint32_t>::max() ||
                !parse_stats(value[3U], record.stats)) {
                output.error = "AoSPlay leaderboard contains an invalid row value";
                output.rows.clear();
                return output;
            }
            record.total = *total;
            record.global_rank = static_cast<std::uint32_t>(rank);
            output.rows.push_back(std::move(record));
        }
    } catch (const nlohmann::json::exception& exception) {
        output.error = std::string{"invalid AoSPlay leaderboard JSON: "} + exception.what();
    }
    return output;
}

AosPlayProfileResult parse_aosplay_profile(std::string_view json) {
    AosPlayProfileResult output;
    if (json.empty()) {
        output.error = "AoSPlay profile response is empty";
        return output;
    }
    try {
        const auto document = nlohmann::json::parse(json.begin(), json.end());
        if (!document.is_object()) {
            output.error = "AoSPlay profile response must be an object";
            return output;
        }
        const auto value = document.find("profile");
        if (value == document.end()) {
            return output; // A valid empty object means profile not found.
        }
        if (!value->is_object()) {
            output.error = "AoSPlay profile member must be an object";
            return output;
        }
        const auto name = value->find("name");
        const auto total_value = value->find("total");
        const auto stats = value->find("stats");
        if (name == value->end() || !name->is_string() ||
            total_value == value->end() || stats == value->end()) {
            output.error = "AoSPlay profile is missing required fields";
            return output;
        }
        AosPlayProfileRecord record;
        record.player_name = name->get<std::string>();
        const auto total = score_pair(*total_value);
        if (record.player_name.empty() ||
            record.player_name.size() > maximum_player_name_bytes || !total.has_value() ||
            !parse_stats(*stats, record.stats)) {
            output.error = "AoSPlay profile contains an invalid field";
            return output;
        }
        record.total = *total;
        output.profile = std::move(record);
    } catch (const nlohmann::json::exception& exception) {
        output.error = std::string{"invalid AoSPlay profile JSON: "} + exception.what();
    }
    return output;
}

AosPlayLeaderboardResult fetch_aosplay_leaderboard(
    const AosPlayScoreServiceConfig& config,
    std::uint8_t type,
    AosPlayLeaderboardScope scope,
    std::uint64_t account_id,
    std::span<const std::uint64_t> friend_account_ids) {
    const auto form = build_aosplay_leaderboard_form(
        type, scope, account_id, config.maximum_rows, friend_account_ids);
    if (form.empty()) return {{}, "invalid AoSPlay leaderboard request"};
    const auto response = post_form(config, config.leaderboard_url, form);
    if (!response.error.empty()) return {{}, response.error};
    return parse_aosplay_leaderboard(response.body, config.maximum_rows);
}

AosPlayProfileResult fetch_aosplay_profile(const AosPlayScoreServiceConfig& config,
                                           std::uint64_t account_id) {
    if (account_id == 0U) return {{}, "AoSPlay profile account ID is missing"};
    const auto form = "steamid=" + std::to_string(account_id);
    const auto response = post_form(config, config.profile_url, form);
    if (!response.error.empty()) return {{}, response.error};
    return parse_aosplay_profile(response.body);
}

std::optional<std::uint64_t> resolve_aosplay_account_id(
    const AosPlayScoreServiceConfig& config,
    std::string_view player_name,
    std::string& error) {
    error.clear();
    if (player_name.empty()) {
        error = "player name is empty";
        return std::nullopt;
    }
    const auto rows =
        fetch_aosplay_leaderboard(config, 0U, AosPlayLeaderboardScope::global);
    if (!rows) {
        error = rows.error;
        return std::nullopt;
    }
    const auto wanted = lowercase_ascii(player_name);
    std::optional<std::uint64_t> match;
    for (const auto& row : rows.rows) {
        if (lowercase_ascii(row.player_name) != wanted) continue;
        if (match.has_value() && *match != row.account_id) {
            error = "AoSPlay player name is ambiguous";
            return std::nullopt;
        }
        match = row.account_id;
    }
    if (!match.has_value()) error = "AoSPlay player profile was not found";
    return match;
}

} // namespace battlespades::network
