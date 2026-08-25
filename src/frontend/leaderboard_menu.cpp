#include "battlespades/frontend/leaderboard_menu.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <string_view>

namespace battlespades::frontend {
namespace {

constexpr std::array general_columns{
    std::string_view{"LEADERBOARD_RANK"},
    std::string_view{"LEADERBOARD_NAME"},
    std::string_view{"LEADERBOARD_TOTAL"},
    std::string_view{"LEADERBOARD_KILLS"},
    std::string_view{"LEADERBOARD_DEATHS"},
    std::string_view{"LEADERBOARD_KDR"},
    std::string_view{"LEADERBOARD_WINS"},
    std::string_view{"LEADERBOARD_LOSSES"},
};
constexpr std::array tdm_columns{
    std::string_view{"LEADERBOARD_RANK"},
    std::string_view{"LEADERBOARD_NAME"},
    std::string_view{"LEADERBOARD_TOTAL"},
    std::string_view{"LEADERBOARD_HEADSHOT"},
    std::string_view{"LEADERBOARD_MELEE"},
    std::string_view{"LEADERBOARD_KILLS"},
    std::string_view{"LEADERBOARD_ASSIST"},
    std::string_view{"LEADERBOARD_RETRIBUTION"},
    std::string_view{"LEADERBOARD_DEFENCE"},
};
constexpr std::array vip_columns{
    std::string_view{"LEADERBOARD_RANK"},
    std::string_view{"LEADERBOARD_NAME"},
    std::string_view{"LEADERBOARD_TOTAL"},
    std::string_view{"LEADERBOARD_SURVIVAL"},
    std::string_view{"LEADERBOARD_ASSAULT"},
    std::string_view{"LEADERBOARD_DEFEND"},
    std::string_view{"LEADERBOARD_ESCORT"},
};
constexpr std::array tc_columns{
    std::string_view{"LEADERBOARD_RANK"},
    std::string_view{"LEADERBOARD_NAME"},
    std::string_view{"LEADERBOARD_TOTAL"},
    std::string_view{"LEADERBOARD_OCCUPY"},
    std::string_view{"LEADERBOARD_CLAIM"},
    std::string_view{"LEADERBOARD_CONTROL"},
    std::string_view{"LEADERBOARD_DEFEND"},
    std::string_view{"LEADERBOARD_CONTEST"},
};
constexpr std::array occupation_columns{
    std::string_view{"LEADERBOARD_RANK"},
    std::string_view{"LEADERBOARD_NAME"},
    std::string_view{"LEADERBOARD_TOTAL"},
    std::string_view{"LEADERBOARD_OCCUPY"},
    std::string_view{"LEADERBOARD_BOMB"},
    std::string_view{"LEADERBOARD_CARRY"},
    std::string_view{"LEADERBOARD_ASSIST"},
    std::string_view{"LEADERBOARD_DEFEND"},
    std::string_view{"LEADERBOARD_SURVIVAL"},
};
constexpr std::array diamond_columns{
    std::string_view{"LEADERBOARD_RANK"},
    std::string_view{"LEADERBOARD_NAME"},
    std::string_view{"LEADERBOARD_TOTAL"},
    std::string_view{"LEADERBOARD_CAPTURE"},
    std::string_view{"LEADERBOARD_UNCOVER"},
    std::string_view{"LEADERBOARD_CARRY"},
    std::string_view{"LEADERBOARD_ASSIST"},
    std::string_view{"LEADERBOARD_ASSAULT"},
    std::string_view{"LEADERBOARD_STEAL"},
};
constexpr std::array ctf_columns{
    std::string_view{"LEADERBOARD_RANK"},
    std::string_view{"LEADERBOARD_NAME"},
    std::string_view{"LEADERBOARD_TOTAL"},
    std::string_view{"LEADERBOARD_CAPTURE"},
    std::string_view{"LEADERBOARD_CARRY"},
    std::string_view{"LEADERBOARD_CLAIM"},
    std::string_view{"LEADERBOARD_ASSIST"},
    std::string_view{"LEADERBOARD_DEFEND"},
    std::string_view{"LEADERBOARD_ASSAULT"},
};
constexpr std::array zombie_columns{
    std::string_view{"LEADERBOARD_RANK"},
    std::string_view{"LEADERBOARD_NAME"},
    std::string_view{"LEADERBOARD_TOTAL"},
    std::string_view{"LEADERBOARD_SURVIVAL"},
    std::string_view{"LEADERBOARD_LASTMANSTANDING"},
    std::string_view{"LEADERBOARD_KILLSURVIVOR"},
    std::string_view{"LEADERBOARD_KILLSASLASTMAN"},
};
constexpr std::array demolition_columns{
    std::string_view{"LEADERBOARD_RANK"},
    std::string_view{"LEADERBOARD_NAME"},
    std::string_view{"LEADERBOARD_TOTAL"},
    std::string_view{"LEADERBOARD_DESTROY"},
    std::string_view{"LEADERBOARD_REPAIR"},
    std::string_view{"LEADERBOARD_DEFEND"},
    std::string_view{"LEADERBOARD_ASSAULT"},
};
constexpr std::array multihill_columns{
    std::string_view{"LEADERBOARD_RANK"},
    std::string_view{"LEADERBOARD_NAME"},
    std::string_view{"LEADERBOARD_TOTAL"},
    std::string_view{"LEADERBOARD_OCCUPY"},
    std::string_view{"LEADERBOARD_FIRST"},
    std::string_view{"LEADERBOARD_CONTROL"},
    std::string_view{"LEADERBOARD_DEFEND"},
    std::string_view{"LEADERBOARD_ASSAULT"},
    std::string_view{"LEADERBOARD_CONTEST"},
};

const std::array definitions{
    LeaderboardDefinition{LeaderboardType::general, "GENERAL", general_columns},
    LeaderboardDefinition{LeaderboardType::team_deathmatch, "TDM_TITLE", tdm_columns},
    LeaderboardDefinition{LeaderboardType::vip, "VIP_MODE_TITLE", vip_columns},
    LeaderboardDefinition{LeaderboardType::territory_control, "TC_TITLE", tc_columns},
    LeaderboardDefinition{LeaderboardType::occupation, "OCCUPATION_MODE_TITLE", occupation_columns},
    LeaderboardDefinition{LeaderboardType::diamond_mine, "DIAMOND_MINE_TITLE", diamond_columns},
    LeaderboardDefinition{LeaderboardType::capture_the_flag, "CTF_TITLE", ctf_columns},
    LeaderboardDefinition{LeaderboardType::zombie, "ZOMBIE_MODE_TITLE", zombie_columns},
    LeaderboardDefinition{LeaderboardType::demolition, "DEMOLITION_TITLE", demolition_columns},
    LeaderboardDefinition{LeaderboardType::multihill, "MULTIHILL_TITLE", multihill_columns},
};

[[nodiscard]] constexpr std::size_t index(LeaderboardType type) noexcept {
    return static_cast<std::size_t>(type);
}

[[nodiscard]] constexpr std::size_t index(LeaderboardScope scope) noexcept {
    return static_cast<std::size_t>(scope);
}

[[nodiscard]] double numeric_value(const LeaderboardRow& row, std::size_t column) noexcept {
    if (column == 0U) {
        return static_cast<double>(row.rank);
    }
    if (column < 2U || column - 2U >= row.values.size()) {
        return 0.0;
    }
    const auto& value = row.values[column - 2U];
    if (value.empty()) {
        return 0.0;
    }
    char* parsed_end{};
    errno = 0;
    const double result = std::strtod(value.c_str(), &parsed_end);
    if (errno == ERANGE || parsed_end != value.c_str() + value.size() ||
        !std::isfinite(result)) {
        return 0.0;
    }
    return result;
}

} // namespace

void apply_retail_leaderboard_personas(
    std::vector<LeaderboardRow>& rows,
    std::uint64_t local_account_id,
    std::string_view local_display_name,
    std::span<const LeaderboardPersona> friends) {
    for (auto& row : rows) {
        if (local_account_id != 0U && row.account_id == local_account_id &&
            !local_display_name.empty()) {
            row.player_name = local_display_name;
            continue;
        }
        const auto found = std::ranges::find_if(
            friends, [&](const LeaderboardPersona& persona) {
                return persona.account_id != 0U &&
                       persona.account_id == row.account_id &&
                       !persona.display_name.empty();
            });
        if (found != friends.end()) row.player_name = found->display_name;
    }
}

LeaderboardMenuModel::LeaderboardMenuModel() {
    activate_selection();
}

LeaderboardType LeaderboardMenuModel::selected_type() const noexcept {
    return selected_type_;
}

LeaderboardScope LeaderboardMenuModel::selected_scope() const noexcept {
    return selected_scope_;
}

LeaderboardLoadState LeaderboardMenuModel::state() const noexcept {
    return state_;
}

bool LeaderboardMenuModel::selectors_enabled() const noexcept {
    return state_ != LeaderboardLoadState::loading;
}

std::span<const LeaderboardRow> LeaderboardMenuModel::rows() const noexcept {
    return selected_cache().rows;
}

std::size_t LeaderboardMenuModel::first_visible_row() const noexcept {
    return first_visible_row_;
}

std::size_t LeaderboardMenuModel::first_visible_stat_column() const noexcept {
    return first_visible_stat_column_;
}

std::optional<std::size_t> LeaderboardMenuModel::sorted_column() const noexcept {
    return sorted_column_;
}

LeaderboardSortDirection LeaderboardMenuModel::sort_direction() const noexcept {
    return sort_direction_;
}

LeaderboardDropdown LeaderboardMenuModel::open_dropdown() const noexcept {
    return open_dropdown_;
}

std::optional<std::size_t> LeaderboardMenuModel::selected_row() const noexcept {
    return row_index_for_account(selected_account_id_);
}

std::optional<std::size_t> LeaderboardMenuModel::hovered_row() const noexcept {
    return row_index_for_account(hovered_account_id_);
}

std::optional<LeaderboardRequest> LeaderboardMenuModel::take_request() noexcept {
    if (!pending_request_.has_value() || request_taken_) {
        return std::nullopt;
    }
    request_taken_ = true;
    return pending_request_;
}

bool LeaderboardMenuModel::select_type(LeaderboardType type) noexcept {
    if (!selectors_enabled() || index(type) >= type_count) {
        return false;
    }
    open_dropdown_ = LeaderboardDropdown::none;
    if (type == selected_type_) return false;
    selected_type_ = type;
    activate_selection();
    return true;
}

bool LeaderboardMenuModel::select_scope(LeaderboardScope scope) noexcept {
    if (!selectors_enabled() || index(scope) >= scope_count) {
        return false;
    }
    open_dropdown_ = LeaderboardDropdown::none;
    if (scope == selected_scope_) return false;
    selected_scope_ = scope;
    activate_selection();
    return true;
}

bool LeaderboardMenuModel::toggle_dropdown(LeaderboardDropdown dropdown) noexcept {
    if (!selectors_enabled() || dropdown == LeaderboardDropdown::none) {
        return false;
    }
    open_dropdown_ = open_dropdown_ == dropdown ? LeaderboardDropdown::none : dropdown;
    return true;
}

bool LeaderboardMenuModel::close_dropdown() noexcept {
    if (open_dropdown_ == LeaderboardDropdown::none) return false;
    open_dropdown_ = LeaderboardDropdown::none;
    return true;
}

bool LeaderboardMenuModel::scroll_rows(int delta, std::size_t visible_capacity) noexcept {
    const auto row_count = selected_cache().rows.size();
    visible_capacity = std::max<std::size_t>(visible_capacity, 1U);
    const auto maximum = row_count > visible_capacity ? row_count - visible_capacity : 0U;
    const auto before = first_visible_row_;
    if (delta < 0) {
        const auto magnitude = static_cast<std::size_t>(-(static_cast<long long>(delta)));
        first_visible_row_ = magnitude > first_visible_row_ ? 0U : first_visible_row_ - magnitude;
    } else {
        const auto magnitude = static_cast<std::size_t>(delta);
        first_visible_row_ = std::min(maximum, first_visible_row_ + magnitude);
    }
    return before != first_visible_row_;
}

bool LeaderboardMenuModel::scroll_stat_columns(int delta,
                                               std::size_t visible_stat_columns) noexcept {
    const auto column_count = leaderboard_definition(selected_type_).column_keys.size();
    const auto stat_count = column_count > 2U ? column_count - 2U : 0U;
    visible_stat_columns = std::min(visible_stat_columns, stat_count);
    const auto maximum = stat_count > visible_stat_columns ? stat_count - visible_stat_columns : 0U;
    const auto before = first_visible_stat_column_;
    if (delta < 0) {
        const auto magnitude = static_cast<std::size_t>(-(static_cast<long long>(delta)));
        first_visible_stat_column_ =
            magnitude > first_visible_stat_column_ ? 0U : first_visible_stat_column_ - magnitude;
    } else {
        first_visible_stat_column_ =
            std::min(maximum, first_visible_stat_column_ + static_cast<std::size_t>(delta));
    }
    return before != first_visible_stat_column_;
}

bool LeaderboardMenuModel::sort_by(std::size_t column_index) noexcept {
    auto& cache = selected_cache();
    const auto column_count = leaderboard_definition(selected_type_).column_keys.size();
    if (state_ != LeaderboardLoadState::ready || column_index >= column_count) {
        return false;
    }

    if (sorted_column_ == column_index) {
        sort_direction_ = sort_direction_ == LeaderboardSortDirection::ascending
                              ? LeaderboardSortDirection::descending
                              : LeaderboardSortDirection::ascending;
    } else {
        sorted_column_ = column_index;
        // Recovered behavior starts numeric columns descending and names ascending.
        sort_direction_ = column_index == 1U ? LeaderboardSortDirection::ascending
                                             : LeaderboardSortDirection::descending;
    }

    const auto ascending = sort_direction_ == LeaderboardSortDirection::ascending;
    std::stable_sort(
        cache.rows.begin(), cache.rows.end(), [&](const auto& left, const auto& right) {
            if (column_index == 1U) {
                return ascending ? left.player_name < right.player_name
                                 : left.player_name > right.player_name;
            }
            const auto left_value = numeric_value(left, column_index);
            const auto right_value = numeric_value(right, column_index);
            return ascending ? left_value < right_value : left_value > right_value;
        });
    first_visible_row_ = 0U;
    return true;
}

bool LeaderboardMenuModel::select_row(std::size_t row_index) noexcept {
    const auto& current_rows = selected_cache().rows;
    if (state_ != LeaderboardLoadState::ready || row_index >= current_rows.size()) {
        return false;
    }
    // LeaderboardListPanel inherits ListPanelBase's non-unselectable row
    // behavior. Clicking the current row selects it again and replays the
    // scroll cue rather than activating a profile route.
    selected_account_id_ = current_rows[row_index].account_id;
    return true;
}

bool LeaderboardMenuModel::hover_row(std::optional<std::size_t> row_index) noexcept {
    std::optional<std::uint64_t> next;
    const auto& current_rows = selected_cache().rows;
    if (state_ == LeaderboardLoadState::ready && row_index.has_value() &&
        *row_index < current_rows.size()) {
        next = current_rows[*row_index].account_id;
    }
    if (next == hovered_account_id_) {
        return false;
    }
    hovered_account_id_ = next;
    return true;
}

bool LeaderboardMenuModel::complete(LeaderboardRequest request,
                                    std::vector<LeaderboardRow> rows) noexcept {
    if (!pending_request_.has_value() || request != *pending_request_) {
        return false;
    }
    const auto expected_values = leaderboard_definition(request.type).column_keys.size() - 2U;
    if (std::ranges::any_of(rows, [&](const auto& row) {
            return row.player_name.empty() || row.values.size() != expected_values;
        })) {
        return false;
    }

    auto& cache = selected_cache();
    cache.populated = true;
    cache.rows = std::move(rows);
    pending_request_.reset();
    request_taken_ = false;
    state_ = LeaderboardLoadState::ready;
    reset_sort_and_scroll();
    reset_row_interaction();
    return true;
}

bool LeaderboardMenuModel::fail(LeaderboardRequest request) noexcept {
    if (!pending_request_.has_value() || request != *pending_request_) {
        return false;
    }
    pending_request_.reset();
    request_taken_ = false;
    selected_cache().rows.clear();
    state_ = LeaderboardLoadState::unavailable;
    reset_sort_and_scroll();
    reset_row_interaction();
    return true;
}

void LeaderboardMenuModel::invalidate() noexcept {
    for (auto& by_scope : cache_) {
        for (auto& entry : by_scope) {
            entry = {};
        }
    }
    pending_request_.reset();
    request_taken_ = false;
    open_dropdown_ = LeaderboardDropdown::none;
    activate_selection();
}

LeaderboardMenuModel::CacheEntry& LeaderboardMenuModel::selected_cache() noexcept {
    return cache_[index(selected_type_)][index(selected_scope_)];
}

const LeaderboardMenuModel::CacheEntry& LeaderboardMenuModel::selected_cache() const noexcept {
    return cache_[index(selected_type_)][index(selected_scope_)];
}

void LeaderboardMenuModel::activate_selection() noexcept {
    open_dropdown_ = LeaderboardDropdown::none;
    reset_sort_and_scroll();
    reset_row_interaction();
    const auto& cache = selected_cache();
    if (cache.populated) {
        pending_request_.reset();
        request_taken_ = false;
        state_ = LeaderboardLoadState::ready;
        return;
    }
    pending_request_ = LeaderboardRequest{next_generation_++, selected_type_, selected_scope_};
    request_taken_ = false;
    state_ = LeaderboardLoadState::loading;
}

void LeaderboardMenuModel::reset_sort_and_scroll() noexcept {
    first_visible_row_ = 0U;
    first_visible_stat_column_ = 0U;
    sorted_column_.reset();
    sort_direction_ = LeaderboardSortDirection::none;
}

void LeaderboardMenuModel::reset_row_interaction() noexcept {
    selected_account_id_.reset();
    hovered_account_id_.reset();
}

std::optional<std::size_t> LeaderboardMenuModel::row_index_for_account(
    std::optional<std::uint64_t> account_id) const noexcept {
    if (!account_id.has_value()) {
        return std::nullopt;
    }
    const auto& current_rows = selected_cache().rows;
    const auto found = std::ranges::find_if(current_rows, [&](const auto& row) {
        return row.account_id == *account_id;
    });
    if (found == current_rows.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(current_rows.begin(), found));
}

std::span<const LeaderboardDefinition> leaderboard_definitions() noexcept {
    return definitions;
}

const LeaderboardDefinition& leaderboard_definition(LeaderboardType type) noexcept {
    const auto selected = index(type);
    return definitions[selected < definitions.size() ? selected : 0U];
}

} // namespace battlespades::frontend
