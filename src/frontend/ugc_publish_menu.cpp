#include "battlespades/frontend/ugc_publish_menu.hpp"

#include "battlespades/core/utf8.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iterator>
#include <ranges>
#include <string>
#include <utility>

namespace battlespades::frontend {
namespace {

[[nodiscard]] UgcPublishEffect effect(UgcPublishEffectKind kind,
                                      std::string_view sound = {}) {
    return UgcPublishEffect{kind, sound, std::nullopt, std::nullopt, std::nullopt};
}

[[nodiscard]] std::string safe_title(std::string_view title, std::string_view fallback) {
    const auto prefix = core::utf8_code_point_prefix(
        title, UgcPublishMenuModel::maximum_title_code_points);
    if (prefix.size() != title.size() || prefix.empty()) {
        const auto safe_fallback = core::utf8_code_point_prefix(
            fallback, UgcPublishMenuModel::maximum_title_code_points);
        return safe_fallback.size() == fallback.size() ? safe_fallback : std::string{};
    }
    return prefix;
}

} // namespace

bool UgcLocalMapRecord::has_publishable_mode() const noexcept {
    return std::ranges::any_of(modes, [](const UgcPublishModeStatus& mode) {
        return mode.publishable;
    });
}

void UgcPublishMenuModel::replace_local_maps(std::vector<UgcLocalMapRecord> maps) {
    std::vector<UgcLocalMapRecord> accepted;
    accepted.reserve(std::min(maps.size(), maximum_local_maps));
    for (auto& map : maps) {
        if (accepted.size() >= maximum_local_maps) {
            break;
        }
        if (map.uid.empty()) {
            continue;
        }
        const auto duplicate = std::ranges::any_of(accepted, [&map](const UgcLocalMapRecord& row) {
            return row.uid == map.uid;
        });
        if (duplicate) {
            continue;
        }
        map.title = safe_title(map.title, map.uid);
        if (map.title.empty()) {
            continue;
        }
        accepted.push_back(std::move(map));
    }
    local_maps_ = std::move(accepted);
    repair_after_repository_change();
}

std::span<const UgcLocalMapRecord> UgcPublishMenuModel::local_maps() const noexcept {
    return local_maps_;
}

const UgcLocalMapRecord* UgcPublishMenuModel::selected_map() const noexcept {
    const auto index = selected_index();
    return index.has_value() ? &local_maps_[*index] : nullptr;
}

std::optional<std::size_t> UgcPublishMenuModel::selected_index() const noexcept {
    if (!selected_uid_.has_value()) {
        return std::nullopt;
    }
    return index_for_uid(*selected_uid_);
}

std::size_t UgcPublishMenuModel::first_visible_row() const noexcept {
    return first_visible_row_;
}

bool UgcPublishMenuModel::select_map(std::size_t index) noexcept {
    if (input_blocked() || page_ != UgcPublishPage::map_list || index >= local_maps_.size()) {
        return false;
    }
    selected_uid_ = local_maps_[index].uid;
    reveal_selection();
    return true;
}

bool UgcPublishMenuModel::select_visible_row(std::size_t row) noexcept {
    if (row >= visible_map_rows || first_visible_row_ > local_maps_.size()) {
        return false;
    }
    return select_map(first_visible_row_ + row);
}

void UgcPublishMenuModel::scroll_rows(int delta) noexcept {
    if (input_blocked() || page_ != UgcPublishPage::map_list || delta == 0) {
        return;
    }
    const auto maximum_first = local_maps_.size() > visible_map_rows
                                   ? local_maps_.size() - visible_map_rows
                                   : 0U;
    if (delta > 0) {
        const auto amount = static_cast<std::size_t>(delta);
        first_visible_row_ = amount > maximum_first - first_visible_row_
                                 ? maximum_first
                                 : first_visible_row_ + amount;
    } else {
        const auto amount = static_cast<std::size_t>(-(static_cast<long long>(delta)));
        first_visible_row_ = amount > first_visible_row_ ? 0U : first_visible_row_ - amount;
    }
}

UgcPublishPage UgcPublishMenuModel::page() const noexcept {
    return page_;
}

UgcPublishDialog UgcPublishMenuModel::dialog() const noexcept {
    return dialog_;
}

std::string_view UgcPublishMenuModel::publish_title() const noexcept {
    return publish_title_;
}

std::string_view UgcPublishMenuModel::primary_localization_key() const noexcept {
    return page_ == UgcPublishPage::map_list ? std::string_view{"UGC_PREVIEW_PUBLISH"}
                                             : std::string_view{"PUBLISH"};
}

bool UgcPublishMenuModel::primary_enabled() const noexcept {
    if (input_blocked()) {
        return false;
    }
    const auto* map = selected_map();
    if (map == nullptr || !map->has_publishable_mode()) {
        return false;
    }
    return page_ == UgcPublishPage::map_list || contains_non_whitespace(publish_title_);
}

bool UgcPublishMenuModel::input_blocked() const noexcept {
    return dialog_ != UgcPublishDialog::none;
}

bool UgcPublishMenuModel::set_publish_title(std::string_view title) {
    if (page_ != UgcPublishPage::name_map || input_blocked()) {
        return false;
    }
    const auto prefix = core::utf8_code_point_prefix(title, maximum_title_code_points);
    if (prefix.size() != title.size()) {
        return false;
    }
    publish_title_ = prefix;
    return true;
}

std::optional<UgcPublishEffect> UgcPublishMenuModel::activate_primary() {
    if (!primary_enabled()) {
        return std::nullopt;
    }
    if (page_ == UgcPublishPage::map_list) {
        const auto* map = selected_map();
        if (map == nullptr) {
            return std::nullopt;
        }
        publish_title_ = map->title;
        page_ = UgcPublishPage::name_map;
        return effect(UgcPublishEffectKind::show_name_map,
                      main_menu_assets::confirmation_sound);
    }
    dialog_ = UgcPublishDialog::confirm_publish;
    return effect(UgcPublishEffectKind::show_publish_confirmation,
                  main_menu_assets::confirmation_sound);
}

std::optional<UgcPublishEffect> UgcPublishMenuModel::request_delete() {
    if (input_blocked() || page_ != UgcPublishPage::map_list) {
        return std::nullopt;
    }
    const auto* map = selected_map();
    if (map == nullptr) {
        return std::nullopt;
    }
    pending_delete_ = UgcDeleteRequest{map->uid};
    dialog_ = UgcPublishDialog::confirm_delete;
    return effect(UgcPublishEffectKind::show_delete_confirmation,
                  main_menu_assets::confirmation_sound);
}

std::optional<UgcPublishEffect> UgcPublishMenuModel::confirm_dialog() {
    if (dialog_ == UgcPublishDialog::confirm_publish) {
        const auto* map = selected_map();
        if (map == nullptr || !map->has_publishable_mode() ||
            !contains_non_whitespace(publish_title_)) {
            dialog_ = UgcPublishDialog::none;
            return std::nullopt;
        }
        pending_publish_ = UgcPublishRequest{map->uid, publish_title_};
        dialog_ = UgcPublishDialog::uploading;
        auto result = effect(UgcPublishEffectKind::publish_requested,
                             main_menu_assets::confirmation_sound);
        result.publish_request = pending_publish_;
        return result;
    }
    if (dialog_ == UgcPublishDialog::confirm_delete && pending_delete_.has_value()) {
        dialog_ = UgcPublishDialog::deleting;
        auto result = effect(UgcPublishEffectKind::delete_requested,
                             main_menu_assets::confirmation_sound);
        result.delete_request = pending_delete_;
        return result;
    }
    return std::nullopt;
}

std::optional<UgcPublishEffect> UgcPublishMenuModel::cancel_dialog() {
    if (dialog_ != UgcPublishDialog::confirm_publish &&
        dialog_ != UgcPublishDialog::confirm_delete) {
        return std::nullopt;
    }
    dialog_ = UgcPublishDialog::none;
    pending_delete_.reset();
    pending_publish_.reset();
    return effect(UgcPublishEffectKind::dialog_dismissed, main_menu_assets::back_sound);
}

std::optional<UgcPublishEffect> UgcPublishMenuModel::back() {
    if (dialog_ == UgcPublishDialog::confirm_publish ||
        dialog_ == UgcPublishDialog::confirm_delete) {
        return cancel_dialog();
    }
    if (dialog_ == UgcPublishDialog::upload_error ||
        dialog_ == UgcPublishDialog::delete_complete ||
        dialog_ == UgcPublishDialog::operation_error) {
        return acknowledge_dialog();
    }
    if (dialog_ == UgcPublishDialog::uploading || dialog_ == UgcPublishDialog::deleting) {
        return std::nullopt;
    }
    if (page_ == UgcPublishPage::name_map) {
        page_ = UgcPublishPage::map_list;
        return effect(UgcPublishEffectKind::show_map_list, main_menu_assets::back_sound);
    }
    return effect(UgcPublishEffectKind::return_to_ugc_select, main_menu_assets::back_sound);
}

std::optional<UgcPublishEffect> UgcPublishMenuModel::handle(ui::InputEvent event) {
    if (!event.triggers_action()) {
        return std::nullopt;
    }
    using ui::InputAction;
    if (dialog_ == UgcPublishDialog::confirm_publish ||
        dialog_ == UgcPublishDialog::confirm_delete) {
        if (event.action == InputAction::activate) {
            return confirm_dialog();
        }
        if (event.action == InputAction::cancel) {
            return cancel_dialog();
        }
        return std::nullopt;
    }
    if (dialog_ == UgcPublishDialog::upload_error ||
        dialog_ == UgcPublishDialog::delete_complete ||
        dialog_ == UgcPublishDialog::operation_error) {
        if (event.action == InputAction::activate || event.action == InputAction::cancel) {
            return acknowledge_dialog();
        }
        return std::nullopt;
    }
    if (input_blocked()) {
        return std::nullopt;
    }

    const auto current = selected_index();
    switch (event.action) {
    case InputAction::navigate_up:
    case InputAction::focus_previous:
        if (page_ == UgcPublishPage::map_list && !local_maps_.empty()) {
            static_cast<void>(select_map(!current.has_value() || *current == 0U
                                             ? 0U
                                             : *current - 1U));
        }
        break;
    case InputAction::navigate_down:
    case InputAction::focus_next:
        if (page_ == UgcPublishPage::map_list && !local_maps_.empty()) {
            static_cast<void>(select_map(!current.has_value()
                                             ? 0U
                                             : std::min(*current + 1U, local_maps_.size() - 1U)));
        }
        break;
    case InputAction::activate:
        return activate_primary();
    case InputAction::cancel:
        return back();
    case InputAction::navigate_left:
    case InputAction::navigate_right:
        break;
    }
    return std::nullopt;
}

std::optional<UgcPublishEffect> UgcPublishMenuModel::finish_publish(bool success) {
    if (dialog_ != UgcPublishDialog::uploading || !pending_publish_.has_value()) {
        return std::nullopt;
    }
    pending_publish_.reset();
    if (success) {
        dialog_ = UgcPublishDialog::none;
        auto result = effect(UgcPublishEffectKind::publish_succeeded);
        result.external_url = workshop_url();
        return result;
    }
    dialog_ = UgcPublishDialog::upload_error;
    return effect(UgcPublishEffectKind::publish_failed);
}

std::optional<UgcPublishEffect>
UgcPublishMenuModel::finish_delete(std::string_view local_uid, bool success) {
    if (dialog_ != UgcPublishDialog::deleting || !pending_delete_.has_value() ||
        pending_delete_->local_uid != local_uid) {
        return std::nullopt;
    }
    const auto deleted_index = index_for_uid(local_uid);
    pending_delete_.reset();
    if (!success || !deleted_index.has_value()) {
        dialog_ = UgcPublishDialog::operation_error;
        return effect(UgcPublishEffectKind::delete_failed);
    }

    local_maps_.erase(local_maps_.begin() + static_cast<std::ptrdiff_t>(*deleted_index));
    if (local_maps_.empty()) {
        selected_uid_.reset();
        first_visible_row_ = 0U;
    } else {
        const auto replacement = std::min(*deleted_index, local_maps_.size() - 1U);
        selected_uid_ = local_maps_[replacement].uid;
        reveal_selection();
    }
    page_ = UgcPublishPage::map_list;
    dialog_ = UgcPublishDialog::delete_complete;
    return effect(UgcPublishEffectKind::delete_succeeded);
}

std::optional<UgcPublishEffect> UgcPublishMenuModel::acknowledge_dialog() {
    if (dialog_ != UgcPublishDialog::upload_error &&
        dialog_ != UgcPublishDialog::delete_complete &&
        dialog_ != UgcPublishDialog::operation_error) {
        return std::nullopt;
    }
    dialog_ = UgcPublishDialog::none;
    return effect(UgcPublishEffectKind::dialog_dismissed,
                  main_menu_assets::confirmation_sound);
}

std::string UgcPublishMenuModel::workshop_url() {
    return "http://steamcommunity.com/workshop/browse/?appid=" +
           std::to_string(retail_steam_app_id);
}

std::optional<std::size_t>
UgcPublishMenuModel::index_for_uid(std::string_view uid) const noexcept {
    const auto iterator = std::ranges::find(
        local_maps_, uid, [](const UgcLocalMapRecord& map) { return std::string_view{map.uid}; });
    if (iterator == local_maps_.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(local_maps_.begin(), iterator));
}

void UgcPublishMenuModel::reveal_selection() noexcept {
    const auto selected = selected_index();
    if (!selected.has_value()) {
        first_visible_row_ = 0U;
        return;
    }
    if (*selected < first_visible_row_) {
        first_visible_row_ = *selected;
    } else if (*selected >= first_visible_row_ + visible_map_rows) {
        first_visible_row_ = *selected - visible_map_rows + 1U;
    }
    const auto maximum_first = local_maps_.size() > visible_map_rows
                                   ? local_maps_.size() - visible_map_rows
                                   : 0U;
    first_visible_row_ = std::min(first_visible_row_, maximum_first);
}

void UgcPublishMenuModel::repair_after_repository_change() {
    if (selected_uid_.has_value() && index_for_uid(*selected_uid_).has_value()) {
        reveal_selection();
        return;
    }
    if (local_maps_.empty()) {
        selected_uid_.reset();
        first_visible_row_ = 0U;
        if (dialog_ == UgcPublishDialog::none) {
            page_ = UgcPublishPage::map_list;
            publish_title_.clear();
        }
        return;
    }
    selected_uid_ = local_maps_.front().uid;
    first_visible_row_ = 0U;
    if (page_ == UgcPublishPage::name_map && dialog_ == UgcPublishDialog::none) {
        page_ = UgcPublishPage::map_list;
        publish_title_.clear();
    }
}

bool UgcPublishMenuModel::contains_non_whitespace(std::string_view value) noexcept {
    return std::ranges::any_of(value, [](char character) {
        return std::isspace(static_cast<unsigned char>(character)) == 0;
    });
}

std::string_view ugc_map_state_localization_key(UgcLocalMapState state) noexcept {
    switch (state) {
    case UgcLocalMapState::published:
        return "PUBLISHED";
    case UgcLocalMapState::unpublished:
        return "UNPUBLISHED";
    case UgcLocalMapState::data_required:
        return "CANNOT_BE_PUBLISHED";
    case UgcLocalMapState::changed_since_publish:
        return "UGC_CHANGED_SINCE_PUBLISH";
    }
    return "CANNOT_BE_PUBLISHED";
}

} // namespace battlespades::frontend
