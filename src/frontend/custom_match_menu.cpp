#include "battlespades/frontend/custom_match_menu.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <ranges>
#include <unordered_set>

namespace battlespades::frontend {
namespace {

constexpr std::int32_t subpixels{MainMenuModel::subpixels_per_pixel};
constexpr std::int32_t first_row_x{66};
constexpr std::int32_t first_row_y{155};
constexpr std::int32_t row_width{320};
constexpr std::int32_t row_height{25};

[[nodiscard]] constexpr ui::Rect pixels(std::int32_t x,
                                        std::int32_t y,
                                        std::int32_t width,
                                        std::int32_t height) noexcept {
    return ui::Rect{x * subpixels, y * subpixels, width * subpixels, height * subpixels};
}

[[nodiscard]] constexpr ui::Widget widget(std::uint32_t id,
                                          ui::Rect bounds,
                                          bool enabled = true) noexcept {
    return ui::Widget{ui::WidgetId{id}, bounds, {true, enabled, true}};
}

[[nodiscard]] bool contains(ui::Rect bounds, ui::Point point) noexcept {
    const auto left = static_cast<std::int64_t>(bounds.x);
    const auto top = static_cast<std::int64_t>(bounds.y);
    const auto right = left + static_cast<std::int64_t>(bounds.width);
    const auto bottom = top + static_cast<std::int64_t>(bounds.height);
    const auto x = static_cast<std::int64_t>(point.x);
    const auto y = static_cast<std::int64_t>(point.y);
    return x > left && x < right && y > top && y < bottom;
}

} // namespace

bool CustomMatchLobbyRecord::full() const noexcept {
    return maximum_members == 0U || member_count >= maximum_members;
}

bool CustomMatchLobbyRecord::details_ready() const noexcept {
    return !game_info.empty() || !game_rules.empty();
}

CustomMatchLobbyBackRoute CustomMatchLobbyRootState::back_route() const noexcept {
    // MatchSquadLobbyMenu returns a joining member to MatchSquadsMenu. The
    // original owner cancels its lobby and returns all the way to SelectMenu.
    return role == CustomMatchLobbyRole::owner ? CustomMatchLobbyBackRoute::select_menu
                                               : CustomMatchLobbyBackRoute::custom_match_list;
}

std::string_view CustomMatchLobbyRootState::title_localization_key() const noexcept {
    return "MATCH_LOBBY";
}

std::string_view
CustomMatchLobbyRootState::settings_title_localization_key() const noexcept {
    return "MATCH_SETTINGS";
}

CustomMatchMenuModel::CustomMatchMenuModel()
    : controls_{
          CustomMatchControl{widget(1U, pixels(54, 541, 78, 32)),
                             CustomMatchControlKind::navigation_item,
                             "BACK"},
          CustomMatchControl{widget(2U, pixels(242, 114, 130, 24), false),
                             CustomMatchControlKind::source_filter,
                             "OPEN"},
          CustomMatchControl{widget(3U, pixels(405, 456, 332, 50), false),
                             CustomMatchControlKind::join_button,
                             "JOIN_SQUAD"},
      } {}

std::span<const CustomMatchControl> CustomMatchMenuModel::controls() const noexcept {
    return controls_;
}

std::span<const CustomMatchLobbyRecord> CustomMatchMenuModel::lobbies() const noexcept {
    return lobbies_;
}

const CustomMatchLobbyRecord* CustomMatchMenuModel::selected_lobby() const noexcept {
    if (!selected_lobby_id_.has_value()) {
        return nullptr;
    }
    const auto iterator =
        std::ranges::find(lobbies_, *selected_lobby_id_, &CustomMatchLobbyRecord::lobby_id);
    return iterator == lobbies_.end() ? nullptr : &*iterator;
}

std::optional<std::size_t> CustomMatchMenuModel::selected_index() const noexcept {
    if (!selected_lobby_id_.has_value()) {
        return std::nullopt;
    }
    const auto iterator =
        std::ranges::find(lobbies_, *selected_lobby_id_, &CustomMatchLobbyRecord::lobby_id);
    if (iterator == lobbies_.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(lobbies_.begin(), iterator));
}

std::size_t CustomMatchMenuModel::first_visible_row() const noexcept {
    return first_visible_row_;
}

CustomMatchSource CustomMatchMenuModel::source() const noexcept {
    return source_;
}

CustomMatchDiscoveryState CustomMatchMenuModel::discovery_state() const noexcept {
    return discovery_state_;
}

void CustomMatchMenuModel::set_network_available(bool available) noexcept {
    if (!available) {
        clear_discovery();
        discovery_state_ = CustomMatchDiscoveryState::offline;
    } else if (discovery_state_ == CustomMatchDiscoveryState::offline) {
        discovery_state_ = CustomMatchDiscoveryState::idle;
    }
    update_control_states();
}

std::optional<CustomMatchIntent> CustomMatchMenuModel::request_refresh() noexcept {
    if (discovery_state_ == CustomMatchDiscoveryState::offline ||
        discovery_state_ == CustomMatchDiscoveryState::refreshing) {
        return std::nullopt;
    }
    discovery_state_ = CustomMatchDiscoveryState::refreshing;
    update_control_states();
    return CustomMatchIntent{CustomMatchIntentKind::refresh_lobbies, source_, {}};
}

bool CustomMatchMenuModel::complete_refresh(std::vector<CustomMatchLobbyRecord> lobbies,
                                            bool success) {
    if (discovery_state_ != CustomMatchDiscoveryState::refreshing) {
        return false;
    }
    if (!success) {
        clear_discovery();
        discovery_state_ = CustomMatchDiscoveryState::error;
        update_control_states();
        return true;
    }

    // Explicit construction avoids GCC 13's optional-copy uninitialized warning.
    std::optional<std::string> preferred;
    if (selected_lobby_id_.has_value()) preferred.emplace(*selected_lobby_id_);
    std::vector<CustomMatchLobbyRecord> accepted;
    accepted.reserve(std::min(maximum_lobbies, lobbies.size()));
    std::unordered_set<std::string> identifiers;
    identifiers.reserve(std::min(maximum_lobbies, lobbies.size()));
    for (auto& lobby : lobbies) {
        if (accepted.size() >= maximum_lobbies) {
            break;
        }
        if (!valid_lobby(lobby) || !identifiers.insert(lobby.lobby_id).second) {
            continue;
        }
        accepted.push_back(std::move(lobby));
    }

    lobbies_ = std::move(accepted);
    first_visible_row_ = 0U;
    if (preferred.has_value()) {
        repair_selection(std::string_view{*preferred});
    } else {
        repair_selection();
    }
    discovery_state_ = CustomMatchDiscoveryState::ready;
    update_control_states();
    return true;
}

std::optional<CustomMatchIntent>
CustomMatchMenuModel::set_source(CustomMatchSource source) noexcept {
    if (source_ != source) {
        source_ = source;
        controls_[1U].localization_key = localization_key(source_);
        clear_discovery();
        if (discovery_state_ != CustomMatchDiscoveryState::offline) {
            discovery_state_ = CustomMatchDiscoveryState::idle;
        }
    }
    update_control_states();
    return request_refresh();
}

std::optional<CustomMatchIntent> CustomMatchMenuModel::cycle_source(int direction) noexcept {
    const auto next = source_ == CustomMatchSource::open ? CustomMatchSource::friends
                                                         : CustomMatchSource::open;
    static_cast<void>(direction); // Two entries make both directions the same wrap.
    return set_source(next);
}

bool CustomMatchMenuModel::select_lobby(std::size_t index) noexcept {
    if (index >= lobbies_.size()) {
        return false;
    }
    selected_lobby_id_ = lobbies_[index].lobby_id;
    reveal_selection();
    update_control_states();
    return true;
}

bool CustomMatchMenuModel::select_visible_row(std::size_t row) noexcept {
    if (row >= visible_lobby_rows) {
        return false;
    }
    return select_lobby(first_visible_row_ + row);
}

void CustomMatchMenuModel::scroll_rows(int delta) noexcept {
    const auto maximum = lobbies_.size() > visible_lobby_rows
                             ? lobbies_.size() - visible_lobby_rows
                             : 0U;
    if (delta >= 0) {
        const auto amount = static_cast<std::size_t>(delta);
        first_visible_row_ = amount > maximum - first_visible_row_
                                 ? maximum
                                 : first_visible_row_ + amount;
    } else {
        const auto amount = static_cast<std::size_t>(-(static_cast<std::int64_t>(delta)));
        first_visible_row_ = amount > first_visible_row_ ? 0U : first_visible_row_ - amount;
    }
}

bool CustomMatchMenuModel::can_join() const noexcept {
    const auto* lobby = selected_lobby();
    return discovery_state_ == CustomMatchDiscoveryState::ready && lobby != nullptr &&
           lobby->open && !lobby->full() && lobby->content_owned && lobby->details_ready();
}

bool CustomMatchMenuModel::selected_requires_purchase() const noexcept {
    const auto* lobby = selected_lobby();
    return discovery_state_ == CustomMatchDiscoveryState::ready && lobby != nullptr &&
           lobby->open && !lobby->full() && !lobby->content_owned && lobby->details_ready();
}

std::optional<CustomMatchIntent> CustomMatchMenuModel::request_join() const {
    const auto* lobby = selected_lobby();
    if (!can_join() || lobby == nullptr) {
        return std::nullopt;
    }
    return CustomMatchIntent{CustomMatchIntentKind::join_lobby, source_, lobby->lobby_id};
}

std::optional<CustomMatchIntent> CustomMatchMenuModel::request_create() const {
    if (discovery_state_ == CustomMatchDiscoveryState::offline) {
        return std::nullopt;
    }
    return CustomMatchIntent{CustomMatchIntentKind::create_lobby, source_, {}};
}

CustomMatchIntent CustomMatchMenuModel::request_back() const {
    return CustomMatchIntent{CustomMatchIntentKind::back_to_join_match, source_, {}};
}

void CustomMatchMenuModel::pointer_move(std::optional<ui::Point> point) noexcept {
    hovered_ = point.has_value() ? hit_control(*point) : std::nullopt;
}

void CustomMatchMenuModel::pointer_press(std::optional<ui::Point> point) noexcept {
    pointer_move(point);
    pointer_down_ = true;
    navigation_item_armed_ = false;
    if (!hovered_.has_value()) {
        return;
    }
    const auto index = control_index(*hovered_);
    if (index.has_value()) {
        navigation_item_armed_ =
            controls_[*index].kind != CustomMatchControlKind::join_button;
    }
}

std::optional<CustomMatchIntent>
CustomMatchMenuModel::pointer_release(std::optional<ui::Point> point) noexcept {
    const auto released_row = point.has_value() ? hit_visible_row(*point) : std::nullopt;
    pointer_move(point);
    std::optional<CustomMatchIntent> intent;
    if (pointer_down_ && released_row.has_value()) {
        static_cast<void>(select_visible_row(*released_row));
    } else if (pointer_down_ && hovered_.has_value()) {
        const auto index = control_index(*hovered_);
        if (index.has_value() && controls_[*index].widget.state.enabled) {
            const auto kind = controls_[*index].kind;
            if (kind == CustomMatchControlKind::join_button) {
                intent = request_join();
            } else if (navigation_item_armed_) {
                if (kind == CustomMatchControlKind::navigation_item) {
                    intent = request_back();
                } else if (kind == CustomMatchControlKind::source_filter) {
                    intent = cycle_source(1);
                }
            }
        }
    }
    pointer_down_ = false;
    navigation_item_armed_ = false;
    return intent;
}

std::optional<CustomMatchIntent> CustomMatchMenuModel::handle(ui::InputEvent event) noexcept {
    if (!event.triggers_action()) {
        return std::nullopt;
    }
    using ui::InputAction;
    switch (event.action) {
    case InputAction::navigate_up: {
        const auto selected = selected_index();
        if (selected.has_value() && *selected > 0U) {
            static_cast<void>(select_lobby(*selected - 1U));
        } else if (!selected.has_value() && !lobbies_.empty()) {
            static_cast<void>(select_lobby(0U));
        }
        break;
    }
    case InputAction::navigate_down: {
        const auto selected = selected_index();
        if (selected.has_value() && *selected + 1U < lobbies_.size()) {
            static_cast<void>(select_lobby(*selected + 1U));
        } else if (!selected.has_value() && !lobbies_.empty()) {
            static_cast<void>(select_lobby(0U));
        }
        break;
    }
    case InputAction::navigate_left:
        return cycle_source(-1);
    case InputAction::navigate_right:
        return cycle_source(1);
    case InputAction::activate:
        return request_join();
    case InputAction::cancel:
        return request_back();
    case InputAction::focus_next:
    case InputAction::focus_previous:
        break;
    }
    return std::nullopt;
}

WidgetVisualState CustomMatchMenuModel::visual_state(ui::WidgetId id) const noexcept {
    const auto index = control_index(id);
    if (!index.has_value() || !controls_[*index].widget.state.enabled ||
        !controls_[*index].widget.state.visible) {
        return WidgetVisualState::disabled;
    }
    if (pointer_down_ && hovered_ == id &&
        (controls_[*index].kind == CustomMatchControlKind::join_button ||
         navigation_item_armed_)) {
        return WidgetVisualState::pressed;
    }
    return hovered_ == id ? WidgetVisualState::hovered : WidgetVisualState::normal;
}

std::string_view CustomMatchMenuModel::status_localization_key() const noexcept {
    switch (discovery_state_) {
    case CustomMatchDiscoveryState::offline:
        return "MATCHMAKING_OFFLINE";
    case CustomMatchDiscoveryState::idle:
        return "MATCHMAKING_READY_TO_REFRESH";
    case CustomMatchDiscoveryState::refreshing:
        return "SEARCHING_FOR_MATCH_LOBBIES";
    case CustomMatchDiscoveryState::error:
        return "MATCHMAKING_UNAVAILABLE";
    case CustomMatchDiscoveryState::ready:
        return lobbies_.empty() ? std::string_view{"NO_MATCH_LOBBIES_FOUND"}
                                : std::string_view{};
    }
    return "MATCHMAKING_UNAVAILABLE";
}

std::optional<std::size_t>
CustomMatchMenuModel::control_index(ui::WidgetId id) const noexcept {
    const auto iterator =
        std::ranges::find(controls_, id, [](const CustomMatchControl& control) {
            return control.widget.id;
        });
    if (iterator == controls_.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(controls_.begin(), iterator));
}

std::optional<ui::WidgetId> CustomMatchMenuModel::hit_control(ui::Point point) const noexcept {
    for (auto iterator = controls_.rbegin(); iterator != controls_.rend(); ++iterator) {
        if (iterator->widget.state.visible && contains(iterator->widget.bounds, point)) {
            return iterator->widget.id;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t>
CustomMatchMenuModel::hit_visible_row(ui::Point point) const noexcept {
    const auto bounds = pixels(first_row_x,
                               first_row_y,
                               row_width,
                               row_height * static_cast<std::int32_t>(visible_lobby_rows));
    if (!contains(bounds, point)) {
        return std::nullopt;
    }
    const auto y = point.y / subpixels;
    const auto row = static_cast<std::size_t>((y - first_row_y) / row_height);
    return first_visible_row_ + row < lobbies_.size() ? std::optional<std::size_t>{row}
                                                       : std::nullopt;
}

bool CustomMatchMenuModel::valid_lobby(const CustomMatchLobbyRecord& lobby) const noexcept {
    constexpr std::size_t maximum_identifier_bytes{256U};
    constexpr std::size_t maximum_name_bytes{256U};
    constexpr std::size_t maximum_detail_rows{128U};
    constexpr std::size_t maximum_detail_bytes{1'024U};
    if (lobby.lobby_id.empty() || lobby.lobby_id.size() > maximum_identifier_bytes ||
        lobby.name.empty() || lobby.name.size() > maximum_name_bytes ||
        lobby.maximum_members == 0U || lobby.member_count > lobby.maximum_members ||
        lobby.game_info.size() > maximum_detail_rows ||
        lobby.game_rules.size() > maximum_detail_rows) {
        return false;
    }
    const auto valid_detail = [](const std::string& value) {
        return !value.empty() && value.size() <= maximum_detail_bytes;
    };
    return std::ranges::all_of(lobby.game_info, valid_detail) &&
           std::ranges::all_of(lobby.game_rules, valid_detail);
}

void CustomMatchMenuModel::clear_discovery() noexcept {
    lobbies_.clear();
    selected_lobby_id_.reset();
    first_visible_row_ = 0U;
}

void CustomMatchMenuModel::repair_selection(
    std::optional<std::string_view> preferred_id) noexcept {
    if (preferred_id.has_value()) {
        const auto iterator =
            std::ranges::find(lobbies_, *preferred_id, &CustomMatchLobbyRecord::lobby_id);
        if (iterator != lobbies_.end()) {
            selected_lobby_id_ = iterator->lobby_id;
            reveal_selection();
            return;
        }
    }
    if (lobbies_.empty()) {
        selected_lobby_id_.reset();
    } else {
        selected_lobby_id_ = lobbies_.front().lobby_id;
    }
    reveal_selection();
}

void CustomMatchMenuModel::reveal_selection() noexcept {
    const auto index = selected_index();
    if (!index.has_value()) {
        first_visible_row_ = 0U;
        return;
    }
    if (*index < first_visible_row_) {
        first_visible_row_ = *index;
    } else if (*index >= first_visible_row_ + visible_lobby_rows) {
        first_visible_row_ = *index - visible_lobby_rows + 1U;
    }
}

void CustomMatchMenuModel::update_control_states() noexcept {
    controls_[0U].widget.state.enabled = true;
    controls_[1U].widget.state.enabled =
        discovery_state_ != CustomMatchDiscoveryState::offline;
    controls_[2U].widget.state.enabled = can_join();
}

std::string_view localization_key(CustomMatchSource source) noexcept {
    return source == CustomMatchSource::friends ? std::string_view{"FRIENDS"}
                                                : std::string_view{"OPEN"};
}

} // namespace battlespades::frontend
