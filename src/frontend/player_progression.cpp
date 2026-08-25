#include "battlespades/frontend/player_progression.hpp"

#include "battlespades/world/weapon_catalog.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <string_view>

namespace battlespades::frontend {
namespace {

enum class RetailValueModifier : std::uint8_t {
    none,
    minutes_to_hours,
    percentage,
};

struct RetailProfileStatDefinition final {
    std::uint32_t stat_id{};
    std::string_view category_key;
    std::string_view label_key;
    bool show_bar{};
    bool show_score{};
    double level1_requirement{10.0};
    double multiplier{1.15};
    RetailValueModifier modifier{RetailValueModifier::none};
};

struct RetailRankCriterion final {
    std::uint32_t stat_id{};
    std::uint32_t required_level{};
};

struct RetailRankDefinition final {
    std::string_view label_key;
    std::array<RetailRankCriterion, 3U> intermediate;
    std::uint8_t intermediate_count{};
    std::array<RetailRankCriterion, 3U> advanced;
    std::uint8_t advanced_count{};
};

#include "player_progression.generated.inc"

constexpr std::array level_ease_factor{
    2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0,
    2.0, 2.0, 2.0, 2.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0,
    8.0, 9.0, 10.0, 11.0, 12.0, 13.0, 14.0, 15.0, 16.0, 17.0,
    18.0, 19.0, 20.0, 21.0, 22.0, 23.0, 24.0, 25.0, 26.0, 27.0,
    28.0, 29.0, 30.0, 31.0, 32.0, 33.0, 34.0, 35.0, 36.0, 37.0,
    38.0, 39.0, 40.0, 41.0, 42.0, 43.0, 44.0, 45.0, 46.0, 47.0,
    48.0, 49.0, 50.0, 51.0, 52.0, 53.0, 54.0, 55.0, 56.0, 57.0,
    58.0, 59.0, 60.0, 61.0, 62.0, 63.0, 64.0, 65.0, 66.0, 67.0,
    68.0, 69.0, 70.0, 71.0, 72.0, 73.0, 74.0, 75.0, 76.0, 77.0,
    78.0, 79.0, 80.0, 81.0, 82.0, 83.0, 84.0, 85.0, 86.0, 87.0,
};

[[nodiscard]] RetailProfileScoreValue value_for(
    const std::map<std::uint32_t, RetailProfileScoreValue>& stats,
    std::uint32_t id) noexcept {
    const auto found = stats.find(id);
    return found == stats.end() ? RetailProfileScoreValue{} : found->second;
}

[[nodiscard]] std::string number_text(double value) {
    if (!std::isfinite(value)) value = 0.0;
    std::ostringstream stream;
    if (std::abs(value - std::round(value)) < 0.000'001) {
        stream << std::fixed << std::setprecision(0) << value;
    } else {
        stream << std::fixed << std::setprecision(2) << value;
        auto result = stream.str();
        while (!result.empty() && result.back() == '0') result.pop_back();
        if (!result.empty() && result.back() == '.') result.pop_back();
        return result;
    }
    return stream.str();
}

[[nodiscard]] std::string display_value(const RetailProfileStatDefinition& definition,
                                        RetailProfileScoreValue value) {
    const auto raw = definition.show_score ? value.score : value.count;
    switch (definition.modifier) {
    case RetailValueModifier::minutes_to_hours:
        if (raw == 0.0) return "0";
        {
            std::ostringstream stream;
            stream << std::fixed << std::setprecision(2) << raw / 60.0;
            return stream.str();
        }
    case RetailValueModifier::percentage:
        return std::to_string(static_cast<long long>(raw * 100.0)) + "%";
    case RetailValueModifier::none:
        return number_text(raw);
    }
    return "0";
}

[[nodiscard]] const RetailProfileStatDefinition* definition_for(
    std::uint32_t id) noexcept {
    const auto found = std::ranges::find_if(
        retail_profile_stats,
        [id](const auto& definition) { return definition.stat_id == id; });
    return found == retail_profile_stats.end() ? nullptr : &*found;
}

[[nodiscard]] std::uint32_t stat_level(
    const std::map<std::uint32_t, RetailProfileScoreValue>& stats,
    std::uint32_t id) noexcept {
    const auto* definition = definition_for(id);
    if (definition == nullptr || !definition->show_bar) return 1U;
    return calculate_retail_progression_level(value_for(stats, id).count,
                                              definition->level1_requirement,
                                              definition->multiplier)
        .level;
}

[[nodiscard]] bool achieved(
    const std::map<std::uint32_t, RetailProfileScoreValue>& stats,
    const std::array<RetailRankCriterion, 3U>& criteria,
    std::uint8_t count) noexcept {
    if (count == 0U || count > criteria.size()) return false;
    for (std::size_t index{}; index < count; ++index) {
        // Retail uses `stat.level > requirement`, not >=.
        if (stat_level(stats, criteria[index].stat_id) <=
            criteria[index].required_level) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string rank_for(
    const std::map<std::uint32_t, RetailProfileScoreValue>& stats,
    const RetailRankDefinition& definition) {
    if (!achieved(stats, definition.intermediate, definition.intermediate_count)) {
        return "BEGINNER";
    }
    return achieved(stats, definition.advanced, definition.advanced_count)
               ? "ADVANCED"
               : "INTERMEDIATE";
}

void append_category(std::vector<PlayerProfileRow>& rows,
                     std::string_view category,
                     const std::map<std::uint32_t, RetailProfileScoreValue>& stats) {
    rows.push_back({PlayerProfileRowKind::category,
                    std::string{category},
                    std::string{category},
                    {},
                    {},
                    {}});
    for (const auto& definition : retail_profile_stats) {
        if (definition.category_key != category) continue;
        const auto value = value_for(stats, definition.stat_id);
        PlayerProfileRow row{PlayerProfileRowKind::statistic,
                             std::string{category},
                             std::string{definition.label_key},
                             display_value(definition, value),
                             {},
                             {}};
        if (definition.show_bar) {
            const auto level = calculate_retail_progression_level(
                value.count, definition.level1_requirement, definition.multiplier);
            row.level_progress = std::clamp(level.percentage / 100.0, 0.0, 1.0);
            row.level_details = PlayerProfileLevelDetails{
                level.level, value.count, level.next_level_min, level.next_level_max};
        }
        rows.push_back(std::move(row));
    }
}

} // namespace

RetailProgressionLevel calculate_retail_progression_level(
    double count,
    double level1_requirement,
    double multiplier) noexcept {
    if (!std::isfinite(count) || count < 0.0) count = 0.0;
    if (!std::isfinite(level1_requirement) || level1_requirement <= 0.0) {
        level1_requirement = 10.0;
    }
    if (!std::isfinite(multiplier) || multiplier <= 0.0) multiplier = 1.15;

    const auto whole_count = std::floor(count);
    std::uint32_t level{1U};
    double next_min{};
    double next_max = level1_requirement * level_ease_factor.front();
    // Equivalent to retail's `for i in range(score): if i >= next_max`, but
    // proportional to the number of levels rather than a player's total hits.
    constexpr std::uint32_t maximum_safe_level{100'000U};
    while (whole_count > next_max && level < maximum_safe_level) {
        ++level;
        next_min = next_max;
        next_max += level1_requirement * (static_cast<double>(level - 1U) * multiplier);
        if (level < level_ease_factor.size()) {
            next_max *= level_ease_factor[level];
        }
        next_max = std::round(next_max);
        if (!std::isfinite(next_max) || next_max <= next_min) {
            next_max = std::numeric_limits<double>::max();
            break;
        }
    }
    const auto range = std::max(0.001, next_max - next_min);
    const auto percentage = 100.0 / range * (count - next_min);
    return {level, percentage, next_min, next_max};
}

std::size_t retail_profile_stat_definition_count() noexcept {
    return retail_profile_stats.size();
}

std::size_t retail_rank_definition_count() noexcept {
    return retail_rank_definitions.size();
}

std::string retail_score_reason_label(std::uint32_t stat_id) {
    if (stat_id < retail_score_reason_labels.size() &&
        !retail_score_reason_labels[stat_id].empty()) {
        return std::string{retail_score_reason_labels[stat_id]};
    }
    return "STAT_" + std::to_string(stat_id);
}

std::optional<RetailProgressionLevel>
retail_progression_for_stat(std::uint32_t stat_id, double count) noexcept {
    const auto* definition = definition_for(stat_id);
    if (definition == nullptr || !definition->show_bar) return std::nullopt;
    return calculate_retail_progression_level(
        count, definition->level1_requirement, definition->multiplier);
}

PlayerProfileData build_retail_player_profile(
    std::string player_name,
    const std::map<std::uint32_t, RetailProfileScoreValue>& stats) {
    PlayerProfileData profile;
    profile.player_name = std::move(player_name);
    const auto kills = value_for(stats, 1U).count;
    const auto deaths = value_for(stats, 220U).count;
    profile.kill_death_ratio = kills / std::max(1.0, deaths);

    for (const auto& definition : retail_rank_definitions) {
        profile.rows[0U].push_back({PlayerProfileRowKind::summary_rank,
                                    {},
                                    std::string{definition.label_key},
                                    rank_for(stats, definition),
                                    {},
                                    {}});
    }

    constexpr std::array mode_categories{
        std::string_view{"GENERAL"},
        std::string_view{"TDM_TITLE"},
        std::string_view{"VIP_MODE_TITLE"},
        std::string_view{"OCCUPATION_MODE_TITLE"},
        std::string_view{"TC_TITLE"},
        std::string_view{"DIAMOND_MINE_TITLE"},
        std::string_view{"CTF_TITLE"},
        std::string_view{"ZOMBIE_MODE_TITLE"},
        std::string_view{"DEMOLITION_TITLE"},
        std::string_view{"MULTIHILL_TITLE"},
        std::string_view{"HOURS_PLAYED"},
    };
    for (const auto category : mode_categories) {
        append_category(profile.rows[1U], category, stats);
    }

    constexpr std::array class_categories{
        std::string_view{"SOLDIER"},
        std::string_view{"SCOUT"},
        std::string_view{"ENGINEER2"},
        std::string_view{"MINER"},
        std::string_view{"SPECIALIST"},
        std::string_view{"MEDIC"},
        std::string_view{"GANGSTER"},
        std::string_view{"CLASSIC"},
        std::string_view{"ZOMBIE"},
    };
    for (const auto category : class_categories) {
        append_category(profile.rows[2U], category, stats);
    }

    auto& equipment = profile.rows[3U];
    equipment.push_back({PlayerProfileRowKind::category,
                         "WEAPON_ACCURACY",
                         "WEAPON_ACCURACY",
                         {},
                         {},
                         {}});
    for (const auto& weapon : world::weapon_catalog()) {
        // Retail creates these stats for every ordinal except its Null and
        // FakePistol placeholders. Tool 65 has no WEAPONS entry and is absent.
        if (weapon.tool_id == 39U || weapon.tool_id == 40U) continue;
        const auto shots = value_for(stats, 1'000U + weapon.tool_id).count;
        const auto hits = value_for(stats, 2'000U + weapon.tool_id).count;
        const auto accuracy = shots > 0.0 ? hits / shots : 0.0;
        equipment.push_back({PlayerProfileRowKind::statistic,
                             "WEAPON_ACCURACY",
                             std::string{weapon.symbolic_name},
                             std::to_string(static_cast<long long>(accuracy * 100.0)) + "%",
                             {},
                             {}});
    }
    equipment.push_back({PlayerProfileRowKind::category,
                         "WEAPON_POINTS",
                         "WEAPON_POINTS",
                         {},
                         {},
                         {}});
    for (const auto& weapon : world::weapon_catalog()) {
        if (weapon.tool_id == 39U || weapon.tool_id == 40U) continue;
        equipment.push_back({PlayerProfileRowKind::statistic,
                             "WEAPON_POINTS",
                             std::string{weapon.symbolic_name},
                             number_text(value_for(stats, 3'000U + weapon.tool_id).score),
                             {},
                             {}});
    }
    return profile;
}

} // namespace battlespades::frontend
