#include "battlespades/frontend/player_profile_menu.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <string_view>
#include <utility>

namespace battlespades::frontend {
namespace {

constexpr std::array player_filters{std::string_view{"SUMMARY"}};
constexpr std::array game_mode_filters{
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
constexpr std::array class_filters{
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
constexpr std::array equipment_filters{
    std::string_view{"WEAPON_ACCURACY"},
    std::string_view{"WEAPON_POINTS"},
};

const std::array definitions{
    PlayerProfileTabDefinition{PlayerProfileTab::player_stats, "PLAYER_STATS", player_filters},
    PlayerProfileTabDefinition{PlayerProfileTab::game_modes, "GAME_MODES", game_mode_filters},
    PlayerProfileTabDefinition{PlayerProfileTab::classes, "CLASSES", class_filters},
    PlayerProfileTabDefinition{PlayerProfileTab::equipment, "EQUIPMENT", equipment_filters},
    PlayerProfileTabDefinition{PlayerProfileTab::inventory, "INVENTORY", {}},
};

[[nodiscard]] constexpr std::size_t index(PlayerProfileTab tab) noexcept {
    return static_cast<std::size_t>(tab);
}

[[nodiscard]] bool valid_data(const PlayerProfileData& data) noexcept {
    if (data.player_name.empty() || !std::isfinite(data.kill_death_ratio) ||
        data.kill_death_ratio < 0.0) {
        return false;
    }
    for (const auto& tab_rows : data.rows) {
        for (const auto& row : tab_rows) {
            if (row.label_key.empty() ||
                (row.level_progress.has_value() &&
                 (!std::isfinite(*row.level_progress) || *row.level_progress < 0.0 ||
                  *row.level_progress > 1.0))) {
                return false;
            }
            if (row.level_details.has_value()) {
                const auto& details = *row.level_details;
                if (!std::isfinite(details.current) || !std::isfinite(details.next_level_min) ||
                    !std::isfinite(details.next_level_max) ||
                    details.next_level_max < details.next_level_min) {
                    return false;
                }
            }
        }
    }
    return true;
}

} // namespace

PlayerProfileMenuModel::PlayerProfileMenuModel(std::uint64_t account_id) : account_id_{account_id} {
    reload(account_id);
}

PlayerProfileTab PlayerProfileMenuModel::selected_tab() const noexcept {
    return selected_tab_;
}

PlayerProfileLoadState PlayerProfileMenuModel::state() const noexcept {
    return state_;
}

std::uint64_t PlayerProfileMenuModel::account_id() const noexcept {
    return account_id_;
}

const PlayerProfileData* PlayerProfileMenuModel::data() const noexcept {
    return data_.has_value() ? &*data_ : nullptr;
}

std::size_t PlayerProfileMenuModel::selected_filter() const noexcept {
    return selected_filters_[index(selected_tab_)];
}

std::span<const PlayerProfileRow> PlayerProfileMenuModel::displayed_rows() const noexcept {
    return displayed_rows_;
}

std::size_t PlayerProfileMenuModel::first_visible_row() const noexcept {
    return first_visible_row_;
}

std::size_t PlayerProfileMenuModel::visible_row_capacity() const noexcept {
    return selected_tab_ == PlayerProfileTab::player_stats ? summary_visible_rows
                                                           : statistic_visible_rows;
}

bool PlayerProfileMenuModel::filter_visible() const noexcept {
    return selected_tab_ != PlayerProfileTab::player_stats &&
           player_profile_tab_definition(selected_tab_).filter_keys.size() > 1U;
}

bool PlayerProfileMenuModel::filter_open() const noexcept {
    return filter_open_;
}

std::optional<PlayerProfileRequest> PlayerProfileMenuModel::take_request() noexcept {
    if (!pending_request_.has_value() || request_taken_) {
        return std::nullopt;
    }
    request_taken_ = true;
    return pending_request_;
}

bool PlayerProfileMenuModel::select_tab(PlayerProfileTab tab) {
    if (index(tab) >= tab_count) {
        return false;
    }
    const auto changed = tab != selected_tab_ || selected_filters_[index(tab)] != 0U ||
                         filter_open_ || first_visible_row_ != 0U || achievements_open_;
    selected_tab_ = tab;
    // A tab leads back out of the achievements list.
    achievements_open_ = false;
    // playerProfileMenu.set_tab() constructs a fresh DropBoxControl with index
    // zero and then clears current_filter. This reset is observable retail UI.
    selected_filters_[index(selected_tab_)] = 0U;
    filter_open_ = false;
    first_visible_row_ = 0U;
    rebuild_display_rows();
    return changed;
}

bool PlayerProfileMenuModel::toggle_filter() noexcept {
    if (!filter_visible()) {
        return false;
    }
    filter_open_ = !filter_open_;
    return true;
}

bool PlayerProfileMenuModel::close_filter() noexcept {
    if (!filter_open_) {
        return false;
    }
    filter_open_ = false;
    return true;
}

bool PlayerProfileMenuModel::select_filter(std::size_t selected) {
    const auto& definition = player_profile_tab_definition(selected_tab_);
    // Zero is All, followed by every recovered category.
    if (!filter_visible() || selected > definition.filter_keys.size()) {
        return false;
    }
    const auto changed = selected != selected_filters_[index(selected_tab_)];
    selected_filters_[index(selected_tab_)] = selected;
    filter_open_ = false;
    first_visible_row_ = 0U;
    if (changed) {
        rebuild_display_rows();
    }
    return changed;
}

bool PlayerProfileMenuModel::scroll_rows(int delta) noexcept {
    const auto capacity = visible_row_capacity();
    const auto maximum = displayed_rows_.size() > capacity ? displayed_rows_.size() - capacity : 0U;
    const auto before = first_visible_row_;
    if (delta < 0) {
        const auto magnitude = static_cast<std::size_t>(-(static_cast<long long>(delta)));
        first_visible_row_ = magnitude > first_visible_row_ ? 0U : first_visible_row_ - magnitude;
    } else {
        first_visible_row_ =
            std::min(maximum, first_visible_row_ + static_cast<std::size_t>(delta));
    }
    return before != first_visible_row_;
}

bool PlayerProfileMenuModel::achievements_open() const noexcept {
    return achievements_open_;
}

std::size_t PlayerProfileMenuModel::first_visible_achievement() const noexcept {
    return first_visible_achievement_;
}

void PlayerProfileMenuModel::activate_achievements() noexcept {
    achievements_open_ = !achievements_open_;
    first_visible_achievement_ = 0U;
    filter_open_ = false;
}

bool PlayerProfileMenuModel::scroll_achievements(int delta, std::size_t total) noexcept {
    const auto maximum = total > achievement_visible_rows ? total - achievement_visible_rows : 0U;
    const auto before = std::min(first_visible_achievement_, maximum);
    if (delta < 0) {
        const auto magnitude = static_cast<std::size_t>(-(static_cast<long long>(delta)));
        first_visible_achievement_ = magnitude > before ? 0U : before - magnitude;
    } else {
        first_visible_achievement_ = std::min(maximum, before + static_cast<std::size_t>(delta));
    }
    return before != first_visible_achievement_;
}

bool PlayerProfileMenuModel::complete(PlayerProfileRequest request, PlayerProfileData data) {
    if (!pending_request_.has_value() || request != *pending_request_ || !valid_data(data)) {
        return false;
    }
    data_ = std::move(data);
    pending_request_.reset();
    request_taken_ = false;
    state_ = PlayerProfileLoadState::ready;
    if (selected_tab_ != PlayerProfileTab::inventory) selected_tab_ = PlayerProfileTab::player_stats;
    selected_filters_[index(selected_tab_)] = 0U;
    filter_open_ = false;
    first_visible_row_ = 0U;
    rebuild_display_rows();
    return true;
}

bool PlayerProfileMenuModel::fail(PlayerProfileRequest request) noexcept {
    if (!pending_request_.has_value() || request != *pending_request_) {
        return false;
    }
    pending_request_.reset();
    request_taken_ = false;
    state_ = PlayerProfileLoadState::not_found;
    data_.reset();
    displayed_rows_.clear();
    first_visible_row_ = 0U;
    filter_open_ = false;
    return true;
}

void PlayerProfileMenuModel::reload(std::uint64_t account_id) noexcept {
    account_id_ = account_id;
    selected_tab_ = PlayerProfileTab::player_stats;
    selected_filters_.fill(0U);
    data_.reset();
    displayed_rows_.clear();
    first_visible_row_ = 0U;
    state_ = PlayerProfileLoadState::loading;
    pending_request_ = PlayerProfileRequest{next_generation_++, account_id_};
    request_taken_ = false;
    filter_open_ = false;
    achievements_open_ = false;
    first_visible_achievement_ = 0U;
}

void PlayerProfileMenuModel::rebuild_display_rows() {
    displayed_rows_.clear();
    if (!data_.has_value() || selected_tab_ == PlayerProfileTab::inventory) {
        return;
    }
    const auto tab_index = index(selected_tab_);
    const auto& source = data_->rows[tab_index];
    const auto selected = selected_filters_[tab_index];
    if (selected == 0U || selected_tab_ == PlayerProfileTab::player_stats) {
        displayed_rows_ = source;
        return;
    }

    const auto& filters = player_profile_tab_definition(selected_tab_).filter_keys;
    const auto selected_key = filters[selected - 1U];
    std::ranges::copy_if(source, std::back_inserter(displayed_rows_), [&](const auto& row) {
        return row.category_key == selected_key;
    });
}

std::span<const PlayerProfileTabDefinition> player_profile_tab_definitions() noexcept {
    return definitions;
}

const PlayerProfileTabDefinition& player_profile_tab_definition(PlayerProfileTab tab) noexcept {
    const auto selected = index(tab);
    return definitions[selected < definitions.size() ? selected : 0U];
}

} // namespace battlespades::frontend
