#include "battlespades/frontend/achievements.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace battlespades::frontend {
namespace {

[[nodiscard]] bool equal_ignoring_case(std::string_view left, std::string_view right) noexcept {
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) ==
                      std::tolower(static_cast<unsigned char>(b));
           });
}

/** NEW_ACHIEVEMENT_<set>_<number> as a sortable pair: retail's own order. */
[[nodiscard]] std::pair<unsigned, unsigned> token_order(std::string_view token) noexcept {
    constexpr std::string_view prefix{"NEW_ACHIEVEMENT_"};
    constexpr std::pair<unsigned, unsigned> last{~0U, ~0U};
    if (!token.starts_with(prefix)) return last;
    token.remove_prefix(prefix.size());
    const auto* const end = token.data() + token.size();
    std::pair<unsigned, unsigned> order{};
    const auto set = std::from_chars(token.data(), end, order.first);
    if (set.ec != std::errc{} || set.ptr == end || *set.ptr != '_') return last;
    if (std::from_chars(set.ptr + 1, end, order.second).ec != std::errc{}) return last;
    return order;
}

}  // namespace

const AchievementDefinition* find_achievement(std::string_view api_name) noexcept {
    const auto definitions = retail_achievement_definitions();
    const auto found = std::ranges::lower_bound(definitions, api_name, {},
                                                &AchievementDefinition::api_name);
    return found != definitions.end() && found->api_name == api_name ? &*found : nullptr;
}

const AchievementDefinition*
find_achievement_by_display_name(std::string_view display_name) noexcept {
    for (const auto& definition : retail_achievement_definitions()) {
        if (equal_ignoring_case(definition.display_name, display_name)) return &definition;
    }
    return nullptr;
}

AchievementLedger::AchievementLedger(std::filesystem::path file) : file_{std::move(file)} {
    std::ifstream in{file_, std::ios::binary};
    if (!in) return;
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    const auto document = nlohmann::json::parse(text, nullptr, false);
    if (!document.is_object()) return;
    const auto rows = document.find("unlocked");
    if (rows == document.end() || !rows->is_array()) return;
    for (const auto& row : *rows) {
        if (!row.is_object()) continue;
        const auto name = row.find("api_name");
        if (name == row.end() || !name->is_string()) continue;
        const auto at = row.find("unlocked_at");
        unlock(name->get<std::string>(),
               at != row.end() && at->is_number_integer() ? at->get<std::int64_t>() : 0);
    }
}

bool AchievementLedger::unlock(std::string_view api_name, std::int64_t unlocked_at) {
    const auto* const definition = find_achievement(api_name);
    if (definition == nullptr || unlocked(api_name)) return false;
    entries_.push_back({std::string{definition->api_name}, unlocked_at});
    return true;
}

bool AchievementLedger::unlocked(std::string_view api_name) const noexcept {
    return std::ranges::any_of(entries_, [api_name](const UnlockedAchievement& entry) {
        return entry.api_name == api_name;
    });
}

bool AchievementLedger::save() const {
    if (file_.empty()) return false;
    nlohmann::json rows = nlohmann::json::array();
    for (const auto& entry : entries_) {
        rows.push_back({{"api_name", entry.api_name}, {"unlocked_at", entry.unlocked_at}});
    }
    const nlohmann::json document{{"schema_version", 1}, {"unlocked", std::move(rows)}};
    std::error_code code;
    std::filesystem::create_directories(file_.parent_path(), code);
    auto temporary = file_;
    temporary += ".tmp";
    {
        std::ofstream out{temporary, std::ios::binary | std::ios::trunc};
        if (!out) return false;
        out << document.dump(2) << '\n';
        if (!out) return false;
    }
    std::filesystem::rename(temporary, file_, code);
    if (code) {
        std::filesystem::remove(temporary, code);
        return false;
    }
    return true;
}

std::vector<AchievementListRow> achievement_list(std::span<const UnlockedAchievement> ledger,
                                                 std::span<const UnlockedAchievement> earlier) {
    std::vector<AchievementListRow> rows;
    const auto definitions = retail_achievement_definitions();
    rows.reserve(definitions.size());
    for (const auto& definition : definitions) rows.push_back({&definition, false, 0});
    const auto merge = [&rows](std::span<const UnlockedAchievement> unlocked) {
        for (const auto& entry : unlocked) {
            const auto row = std::ranges::find_if(rows, [&entry](const AchievementListRow& candidate) {
                return candidate.definition->api_name == entry.api_name;
            });
            if (row == rows.end()) continue;
            if (entry.unlocked_at > 0 && (row->unlocked_at == 0 || entry.unlocked_at < row->unlocked_at)) {
                row->unlocked_at = entry.unlocked_at;
            }
            row->unlocked = true;
        }
    };
    merge(ledger);
    merge(earlier);
    std::ranges::stable_sort(rows, [](const AchievementListRow& left, const AchievementListRow& right) {
        if (left.unlocked != right.unlocked) return left.unlocked;
        if (left.unlocked && left.unlocked_at != right.unlocked_at) {
            return left.unlocked_at > right.unlocked_at;
        }
        return token_order(left.definition->token) < token_order(right.definition->token);
    });
    return rows;
}

std::string achievement_date(std::int64_t unlocked_at) {
    if (unlocked_at <= 0) return {};
    const std::chrono::sys_seconds moment{std::chrono::seconds{unlocked_at}};
    const std::chrono::year_month_day date{std::chrono::floor<std::chrono::days>(moment)};
    char text[16]{};
    std::snprintf(text, sizeof(text), "%04d-%02u-%02u", static_cast<int>(date.year()),
                  static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()));
    return text;
}

}  // namespace battlespades::frontend
