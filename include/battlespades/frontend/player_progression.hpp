#pragma once

#include "battlespades/frontend/player_profile_menu.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace battlespades::frontend {

/** Count/score pair stored by the recovered AoSPlay statistics service. */
struct RetailProfileScoreValue final {
    double count{};
    double score{};
};

/** Exact result of retail PlayerStat.calculate_level(). */
struct RetailProgressionLevel final {
    std::uint32_t level{1U};
    double percentage{};
    double next_level_min{};
    double next_level_max{};
};

/**
 * Evaluate the nonlinear retail profile curve without iterating once per hit.
 *
 * Inputs are untrusted backend values. Invalid/negative values become zero and
 * the loop is bounded, so a corrupt profile cannot stall the frontend thread.
 */
[[nodiscard]] RetailProgressionLevel calculate_retail_progression_level(
    double count,
    double level1_requirement = 10.0,
    double multiplier = 1.15) noexcept;

/** Number of recovered static PlayerStat and class/mode rank definitions. */
[[nodiscard]] std::size_t retail_profile_stat_definition_count() noexcept;
[[nodiscard]] std::size_t retail_rank_definition_count() noexcept;

/** Retail SCORE_REASON_CODES label, with a stable numeric fallback. */
[[nodiscard]] std::string retail_score_reason_label(std::uint32_t stat_id);

/** Level metadata for a stat that retail displays as a progress bar. */
[[nodiscard]] std::optional<RetailProgressionLevel>
retail_progression_for_stat(std::uint32_t stat_id, double count) noexcept;

/**
 * Build every retail profile row, including zero-valued rows and equipment.
 *
 * The client only presents server/backend-owned statistics. It never awards
 * XP, changes counts, or submits progression from this path.
 */
[[nodiscard]] PlayerProfileData build_retail_player_profile(
    std::string player_name,
    const std::map<std::uint32_t, RetailProfileScoreValue>& stats);

} // namespace battlespades::frontend
