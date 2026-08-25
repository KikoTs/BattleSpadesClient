#include "battlespades/frontend/ugc_editor_menu.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <ranges>
#include <unordered_set>

namespace battlespades::frontend {
namespace {

constexpr std::int32_t scale{MainMenuModel::subpixels_per_pixel};
constexpr std::int32_t browser_row_x{66};
constexpr std::int32_t browser_row_y{155};
constexpr std::int32_t browser_row_width{320};
constexpr std::int32_t browser_row_height{25};

constexpr std::array<std::string_view, 9U> maps{
    "DesertBaseplate",
    "LunarBaseplate",
    "MountainBaseplate",
    "GrasslandBaseplate",
    "Templebaseplate",
    "UrbanBaseplate",
    "MarshTemplate",
    "SnowyBaseplate",
    "WaterBaseplate",
};

constexpr std::array<std::string_view, 9U> modes{
    "zom", "tdm", "dia", "oc", "dem", "mh", "vip", "ctf", "tc",
};

constexpr std::array<std::string_view, 9U> mode_labels{
    "ZOMBIE_MODE_TITLE",
    "TDM_TITLE",
    "DIAMOND_MINE_TITLE",
    "OCCUPATION_MODE_TITLE",
    "DEMOLITION_TITLE",
    "MULTIHILL_TITLE",
    "VIP_MODE_TITLE",
    "CTF_AND_CLASSIC_CTF",
    "TC_TITLE",
};

constexpr std::array<std::string_view, 6U> prefab_labels{
    "UGC_PREFAB_SET_LUNAR",
    "UGC_PREFAB_SET_DESERT",
    "UGC_PREFAB_SET_GRASSLAND",
    "UGC_PREFAB_SET_MOUNTAIN",
    "UGC_PREFAB_SET_TEMPLE",
    "UGC_PREFAB_SET_URBAN",
};

constexpr std::array<std::uint8_t, 12U> maximum_players{
    2U, 4U, 6U, 8U, 10U, 12U, 14U, 16U, 18U, 20U, 22U, 24U,
};

// constants_gamemode.MODE_* values accepted by retail's UGC dropdown.
constexpr std::array<std::uint8_t, 9U> ugc_target_modes{
    2U, 6U, 5U, 4U, 1U, 3U, 7U, 8U, 9U,
};

[[nodiscard]] constexpr ui::Rect pixels(std::int32_t x,
                                        std::int32_t y,
                                        std::int32_t width,
                                        std::int32_t height) noexcept {
    return ui::Rect{x * scale, y * scale, width * scale, height * scale};
}

[[nodiscard]] constexpr ui::Widget widget(std::uint32_t id,
                                          ui::Rect bounds,
                                          bool enabled = true) noexcept {
    return ui::Widget{ui::WidgetId{id}, bounds, {true, enabled, true}};
}

[[nodiscard]] constexpr std::size_t wrapped(std::size_t current,
                                            std::size_t count,
                                            int direction) noexcept {
    if (count == 0U || direction == 0) {
        return current;
    }
    if (direction > 0) {
        return (current + 1U) % count;
    }
    return (current + count - 1U) % count;
}

template <typename Range, typename Value>
[[nodiscard]] std::size_t index_or_zero(const Range& range, const Value& value) noexcept {
    const auto found = std::ranges::find(range, value);
    return found == std::end(range)
               ? 0U
               : static_cast<std::size_t>(std::distance(std::begin(range), found));
}

[[nodiscard]] constexpr std::uint8_t default_prefab_for_map(std::size_t index) noexcept {
    constexpr std::array<std::uint8_t, 9U> defaults{1U, 0U, 3U, 2U, 4U, 5U, 1U, 1U, 1U};
    return index < defaults.size() ? defaults[index] : 1U;
}

} // namespace

bool UgcEditorLobbyRecord::full() const noexcept {
    return maximum_members == 0U || member_count >= maximum_members;
}

UgcEditorBrowserModel::UgcEditorBrowserModel()
    : controls_{
          UgcEditorBrowserControl{widget(1U, pixels(54, 541, 120, 32)),
                                  UgcEditorBrowserControlKind::back,
                                  "BACK"},
          UgcEditorBrowserControl{widget(2U, pixels(242, 114, 130, 24), false),
                                  UgcEditorBrowserControlKind::source_filter,
                                  "OPEN"},
          UgcEditorBrowserControl{widget(3U, pixels(405, 456, 162, 50), false),
                                  UgcEditorBrowserControlKind::join_lobby,
                                  "UGC_SQUADS_MENU_JOIN"},
          UgcEditorBrowserControl{widget(4U, pixels(575, 456, 162, 50)),
                                  UgcEditorBrowserControlKind::new_lobby,
                                  "UGC_SQUADS_MENU_NEW_LOBBY"},
      } {
    rebuild_focus();
}

std::span<const UgcEditorBrowserControl> UgcEditorBrowserModel::controls() const noexcept {
    return controls_;
}

std::span<const UgcEditorLobbyRecord> UgcEditorBrowserModel::lobbies() const noexcept {
    return lobbies_;
}

std::optional<std::size_t> UgcEditorBrowserModel::selected_index() const noexcept {
    if (!selected_lobby_id_.has_value()) {
        return std::nullopt;
    }
    const auto found =
        std::ranges::find(lobbies_, *selected_lobby_id_, &UgcEditorLobbyRecord::lobby_id);
    if (found == lobbies_.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(lobbies_.begin(), found));
}

const UgcEditorLobbyRecord* UgcEditorBrowserModel::selected_lobby() const noexcept {
    const auto index = selected_index();
    return index.has_value() ? &lobbies_[*index] : nullptr;
}

std::size_t UgcEditorBrowserModel::first_visible_row() const noexcept {
    return first_visible_row_;
}

UgcEditorLobbySource UgcEditorBrowserModel::source() const noexcept {
    return source_;
}

UgcEditorDiscoveryState UgcEditorBrowserModel::discovery_state() const noexcept {
    return discovery_state_;
}

std::string_view UgcEditorBrowserModel::status_localization_key() const noexcept {
    switch (discovery_state_) {
    case UgcEditorDiscoveryState::offline: return "MATCHMAKING_OFFLINE";
    case UgcEditorDiscoveryState::idle: return "MATCHMAKING_READY_TO_REFRESH";
    case UgcEditorDiscoveryState::refreshing: return "SEARCHING_FOR_UGC_LOBBIES";
    case UgcEditorDiscoveryState::error: return "MATCHMAKING_UNAVAILABLE";
    case UgcEditorDiscoveryState::ready:
        return lobbies_.empty() ? std::string_view{"NO_UGC_LOBBIES_FOUND"}
                                : std::string_view{};
    }
    return {};
}

void UgcEditorBrowserModel::set_network_available(bool available) noexcept {
    if (!available) {
        discovery_state_ = UgcEditorDiscoveryState::offline;
        lobbies_.clear();
        selected_lobby_id_.reset();
        first_visible_row_ = 0U;
    } else if (discovery_state_ == UgcEditorDiscoveryState::offline) {
        discovery_state_ = UgcEditorDiscoveryState::idle;
    }
    update_control_states();
}

std::optional<UgcEditorBrowserIntent> UgcEditorBrowserModel::request_refresh() noexcept {
    if (discovery_state_ == UgcEditorDiscoveryState::offline ||
        discovery_state_ == UgcEditorDiscoveryState::refreshing) {
        return std::nullopt;
    }
    discovery_state_ = UgcEditorDiscoveryState::refreshing;
    update_control_states();
    return UgcEditorBrowserIntent{UgcEditorBrowserIntentKind::refresh_lobbies, source_, {}};
}

bool UgcEditorBrowserModel::complete_refresh(std::vector<UgcEditorLobbyRecord> lobbies,
                                             bool success) {
    if (discovery_state_ != UgcEditorDiscoveryState::refreshing) {
        return false;
    }
    lobbies_.clear();
    selected_lobby_id_.reset();
    first_visible_row_ = 0U;
    if (!success) {
        discovery_state_ = UgcEditorDiscoveryState::error;
        update_control_states();
        return true;
    }

    std::unordered_set<std::string> identifiers;
    identifiers.reserve(std::min(maximum_lobbies, lobbies.size()));
    lobbies_.reserve(std::min(maximum_lobbies, lobbies.size()));
    for (auto& lobby : lobbies) {
        if (lobbies_.size() >= maximum_lobbies) {
            break;
        }
        if (!valid_lobby(lobby) || !identifiers.insert(lobby.lobby_id).second) {
            continue;
        }
        lobbies_.push_back(std::move(lobby));
    }
    discovery_state_ = UgcEditorDiscoveryState::ready;
    repair_selection();
    update_control_states();
    return true;
}

std::optional<UgcEditorBrowserIntent>
UgcEditorBrowserModel::set_source(UgcEditorLobbySource source) noexcept {
    source_ = source;
    controls_[1U].localization_key = localization_key(source_);
    lobbies_.clear();
    selected_lobby_id_.reset();
    first_visible_row_ = 0U;
    if (discovery_state_ != UgcEditorDiscoveryState::offline) {
        discovery_state_ = UgcEditorDiscoveryState::idle;
    }
    update_control_states();
    return request_refresh();
}

std::optional<UgcEditorBrowserIntent> UgcEditorBrowserModel::cycle_source() noexcept {
    return set_source(source_ == UgcEditorLobbySource::open ? UgcEditorLobbySource::friends
                                                            : UgcEditorLobbySource::open);
}

bool UgcEditorBrowserModel::select_lobby(std::size_t index) noexcept {
    if (index >= lobbies_.size()) {
        return false;
    }
    selected_lobby_id_ = lobbies_[index].lobby_id;
    if (index < first_visible_row_) {
        first_visible_row_ = index;
    } else if (index >= first_visible_row_ + visible_lobby_rows) {
        first_visible_row_ = index - visible_lobby_rows + 1U;
    }
    update_control_states();
    return true;
}

bool UgcEditorBrowserModel::select_visible_row(std::size_t row) noexcept {
    return row < visible_lobby_rows && select_lobby(first_visible_row_ + row);
}

void UgcEditorBrowserModel::scroll_rows(int delta) noexcept {
    const auto maximum = lobbies_.size() > visible_lobby_rows
                             ? lobbies_.size() - visible_lobby_rows
                             : 0U;
    if (delta > 0) {
        const auto amount = static_cast<std::size_t>(delta);
        first_visible_row_ = std::min(maximum, first_visible_row_ + amount);
    } else if (delta < 0) {
        const auto amount = static_cast<std::size_t>(-static_cast<std::int64_t>(delta));
        first_visible_row_ = amount > first_visible_row_ ? 0U : first_visible_row_ - amount;
    }
}

void UgcEditorBrowserModel::pointer_move(std::optional<ui::Point> point) noexcept {
    hovered_ = point.has_value() ? hit_control(*point) : std::nullopt;
}

void UgcEditorBrowserModel::pointer_press(std::optional<ui::Point> point) noexcept {
    pointer_down_ = point.has_value();
    pointer_move(point);
}

std::optional<UgcEditorBrowserIntent>
UgcEditorBrowserModel::pointer_release(std::optional<ui::Point> point) noexcept {
    const auto was_down = pointer_down_;
    pointer_down_ = false;
    pointer_move(point);
    if (!was_down || !point.has_value()) {
        return std::nullopt;
    }
    if (const auto row = hit_lobby_row(*point); row.has_value()) {
        static_cast<void>(select_visible_row(*row));
        return std::nullopt;
    }
    if (const auto id = hit_control(*point); id.has_value()) {
        const auto index = control_index(*id);
        if (index.has_value() && controls_[*index].widget.state.enabled) {
            static_cast<void>(focus_.set_focused(*id));
            return activate(controls_[*index].kind);
        }
    }
    return std::nullopt;
}

std::optional<UgcEditorBrowserIntent>
UgcEditorBrowserModel::handle(ui::InputEvent event) noexcept {
    if (!event.triggers_action()) {
        return std::nullopt;
    }
    switch (event.action) {
    case ui::InputAction::navigate_up:
        static_cast<void>(focus_.move(ui::FocusDirection::up));
        break;
    case ui::InputAction::navigate_down:
        static_cast<void>(focus_.move(ui::FocusDirection::down));
        break;
    case ui::InputAction::navigate_left:
        static_cast<void>(focus_.move(ui::FocusDirection::left));
        break;
    case ui::InputAction::navigate_right:
        static_cast<void>(focus_.move(ui::FocusDirection::right));
        break;
    case ui::InputAction::focus_next:
        static_cast<void>(focus_.advance());
        break;
    case ui::InputAction::focus_previous:
        static_cast<void>(focus_.advance(true));
        break;
    case ui::InputAction::activate:
        if (const auto focused = focus_.focused(); focused.has_value()) {
            const auto index = control_index(*focused);
            if (index.has_value()) {
                return activate(controls_[*index].kind);
            }
        }
        break;
    case ui::InputAction::cancel:
        return UgcEditorBrowserIntent{UgcEditorBrowserIntentKind::back_to_ugc_select,
                                      source_,
                                      {}};
    }
    return std::nullopt;
}

WidgetVisualState UgcEditorBrowserModel::visual_state(ui::WidgetId id) const noexcept {
    const auto index = control_index(id);
    if (!index.has_value() || !controls_[*index].widget.state.enabled) {
        return WidgetVisualState::disabled;
    }
    if (pointer_down_ && hovered_ == id) {
        return WidgetVisualState::pressed;
    }
    if (hovered_ == id) {
        return WidgetVisualState::hovered;
    }
    if (focus_.focused() == id) {
        return WidgetVisualState::focused;
    }
    return WidgetVisualState::normal;
}

std::optional<std::size_t>
UgcEditorBrowserModel::control_index(ui::WidgetId id) const noexcept {
    const auto found = std::ranges::find_if(
        controls_, [id](const auto& control) { return control.widget.id == id; });
    return found == controls_.end()
               ? std::nullopt
               : std::optional{static_cast<std::size_t>(std::distance(controls_.begin(), found))};
}

std::optional<ui::WidgetId>
UgcEditorBrowserModel::hit_control(ui::Point point) const noexcept {
    const auto found = std::ranges::find_if(controls_, [&](const auto& control) {
        return control.widget.state.visible && control.widget.bounds.contains(point);
    });
    return found == controls_.end() ? std::nullopt : std::optional{found->widget.id};
}

std::optional<std::size_t>
UgcEditorBrowserModel::hit_lobby_row(ui::Point point) const noexcept {
    if (!pixels(browser_row_x,
                browser_row_y,
                browser_row_width,
                browser_row_height * static_cast<std::int32_t>(visible_lobby_rows))
             .contains(point)) {
        return std::nullopt;
    }
    const auto row = static_cast<std::size_t>(
        (point.y / scale - browser_row_y) / browser_row_height);
    return first_visible_row_ + row < lobbies_.size() ? std::optional{row} : std::nullopt;
}

std::optional<UgcEditorBrowserIntent>
UgcEditorBrowserModel::activate(UgcEditorBrowserControlKind kind) noexcept {
    switch (kind) {
    case UgcEditorBrowserControlKind::back:
        return UgcEditorBrowserIntent{UgcEditorBrowserIntentKind::back_to_ugc_select,
                                      source_,
                                      {}};
    case UgcEditorBrowserControlKind::source_filter:
        return cycle_source();
    case UgcEditorBrowserControlKind::join_lobby:
        if (const auto* selected = selected_lobby();
            selected != nullptr && selected->open && !selected->full()) {
            return UgcEditorBrowserIntent{UgcEditorBrowserIntentKind::join_lobby,
                                          source_,
                                          selected->lobby_id};
        }
        return std::nullopt;
    case UgcEditorBrowserControlKind::new_lobby:
        return UgcEditorBrowserIntent{UgcEditorBrowserIntentKind::create_lobby, source_, {}};
    }
    return std::nullopt;
}

bool UgcEditorBrowserModel::valid_lobby(const UgcEditorLobbyRecord& lobby) const noexcept {
    return !lobby.lobby_id.empty() && !lobby.name.empty() && lobby.maximum_members > 0U &&
           lobby.member_count <= lobby.maximum_members;
}

void UgcEditorBrowserModel::update_control_states() noexcept {
    controls_[1U].widget.state.enabled = discovery_state_ != UgcEditorDiscoveryState::offline &&
                                          discovery_state_ != UgcEditorDiscoveryState::refreshing;
    const auto* selected = selected_lobby();
    controls_[2U].widget.state.enabled = discovery_state_ == UgcEditorDiscoveryState::ready &&
                                          selected != nullptr && selected->open &&
                                          !selected->full();
    rebuild_focus();
}

void UgcEditorBrowserModel::rebuild_focus() noexcept {
    std::array<ui::Widget, control_count> widgets{};
    std::ranges::transform(controls_, widgets.begin(), &UgcEditorBrowserControl::widget);
    static_cast<void>(focus_.set_widgets(widgets));
}

void UgcEditorBrowserModel::repair_selection() noexcept {
    selected_lobby_id_ = lobbies_.empty() ? std::nullopt
                                          : std::optional{lobbies_.front().lobby_id};
}

std::string_view localization_key(UgcEditorLobbySource source) noexcept {
    return source == UgcEditorLobbySource::friends ? "FRIENDS" : "OPEN";
}

UgcEditorLobbyModel::UgcEditorLobbyModel()
    : settings_{
          UgcEditorSettingDefinition{UgcEditorSettingId::privacy, "PRIVACY", false},
          UgcEditorSettingDefinition{UgcEditorSettingId::maximum_players,
                                     "MAX_PLAYERS",
                                     false},
          UgcEditorSettingDefinition{UgcEditorSettingId::map, "MAP", false},
          UgcEditorSettingDefinition{UgcEditorSettingId::prefab_set, "PREFAB_SET", false},
          UgcEditorSettingDefinition{UgcEditorSettingId::ugc_mode, "MODE", false},
          UgcEditorSettingDefinition{UgcEditorSettingId::map_title, "UGC_MAP_TITLE", true},
      },
      controls_{
          UgcEditorLobbyControl{widget(101U, pixels(54, 541, 170, 32)),
                                UgcEditorLobbyControlKind::back,
                                std::nullopt,
                                "LEAVE_LOBBY"},
          UgcEditorLobbyControl{widget(102U, pixels(296, 461, 80, 30)),
                                UgcEditorLobbyControlKind::invite,
                                std::nullopt,
                                "INVITE"},
          UgcEditorLobbyControl{widget(103U, pixels(405, 456, 332, 50)),
                                UgcEditorLobbyControlKind::start,
                                std::nullopt,
                                "START_GAME"},
          UgcEditorLobbyControl{widget(110U, pixels(411, 145, 320, 42)),
                                UgcEditorLobbyControlKind::setting,
                                UgcEditorSettingId::privacy,
                                "PRIVACY"},
          UgcEditorLobbyControl{widget(111U, pixels(411, 187, 320, 42)),
                                UgcEditorLobbyControlKind::setting,
                                UgcEditorSettingId::maximum_players,
                                "MAX_PLAYERS"},
          UgcEditorLobbyControl{widget(112U, pixels(411, 229, 320, 42)),
                                UgcEditorLobbyControlKind::setting,
                                UgcEditorSettingId::map,
                                "MAP"},
          UgcEditorLobbyControl{widget(113U, pixels(411, 271, 320, 42)),
                                UgcEditorLobbyControlKind::setting,
                                UgcEditorSettingId::prefab_set,
                                "PREFAB_SET"},
          UgcEditorLobbyControl{widget(114U, pixels(411, 313, 320, 42)),
                                UgcEditorLobbyControlKind::setting,
                                UgcEditorSettingId::ugc_mode,
                                "MODE"},
          UgcEditorLobbyControl{widget(115U, pixels(411, 355, 320, 42)),
                                UgcEditorLobbyControlKind::setting,
                                UgcEditorSettingId::map_title,
                                "UGC_MAP_TITLE"},
      } {
    rebuild_focus();
}

const UgcEditorConfiguration& UgcEditorLobbyModel::configuration() const noexcept {
    return configuration_;
}

std::span<const UgcEditorLobbyControl> UgcEditorLobbyModel::controls() const noexcept {
    return controls_;
}

std::span<const UgcEditorSettingDefinition> UgcEditorLobbyModel::settings() const noexcept {
    return settings_;
}

std::string UgcEditorLobbyModel::value_text(UgcEditorSettingId setting) const {
    switch (setting) {
    case UgcEditorSettingId::privacy:
        switch (configuration_.privacy) {
        case UgcEditorPrivacy::invite_only: return "INVITE";
        case UgcEditorPrivacy::friends_only: return "FRIENDS";
        case UgcEditorPrivacy::open: return "OPEN";
        }
        break;
    case UgcEditorSettingId::maximum_players:
        return std::to_string(configuration_.maximum_players);
    case UgcEditorSettingId::map:
        return configuration_.map_name;
    case UgcEditorSettingId::prefab_set:
        return std::string{prefab_labels[std::min<std::size_t>(configuration_.prefab_set,
                                                              prefab_labels.size() - 1U)]};
    case UgcEditorSettingId::ugc_mode: {
        const auto index = index_or_zero(modes, std::string_view{configuration_.ugc_mode});
        return std::string{mode_labels[index]};
    }
    case UgcEditorSettingId::map_title:
        return configuration_.map_title;
    }
    return {};
}

bool UgcEditorLobbyModel::title_editing() const noexcept {
    return title_editing_;
}

bool UgcEditorLobbyModel::cycle(UgcEditorSettingId setting, int direction) noexcept {
    if (direction == 0 || title_editing_) {
        return false;
    }
    switch (setting) {
    case UgcEditorSettingId::privacy: {
        const auto current = static_cast<std::size_t>(configuration_.privacy);
        configuration_.privacy = static_cast<UgcEditorPrivacy>(wrapped(current, 3U, direction));
        return true;
    }
    case UgcEditorSettingId::maximum_players: {
        const auto current = index_or_zero(maximum_players, configuration_.maximum_players);
        configuration_.maximum_players =
            maximum_players[wrapped(current, maximum_players.size(), direction)];
        return true;
    }
    case UgcEditorSettingId::map: {
        const auto current = index_or_zero(maps, std::string_view{configuration_.map_name});
        const auto next = wrapped(current, maps.size(), direction);
        configuration_.map_name = maps[next];
        configuration_.prefab_set = default_prefab_for_map(next);
        return true;
    }
    case UgcEditorSettingId::prefab_set:
        configuration_.prefab_set = static_cast<std::uint8_t>(
            wrapped(configuration_.prefab_set, prefab_labels.size(), direction));
        return true;
    case UgcEditorSettingId::ugc_mode: {
        const auto current = index_or_zero(modes, std::string_view{configuration_.ugc_mode});
        configuration_.ugc_mode = modes[wrapped(current, modes.size(), direction)];
        return true;
    }
    case UgcEditorSettingId::map_title:
        return begin_title_edit();
    }
    return false;
}

bool UgcEditorLobbyModel::begin_title_edit() noexcept {
    if (title_editing_) {
        return false;
    }
    title_before_edit_ = configuration_.map_title;
    title_editing_ = true;
    return true;
}

bool UgcEditorLobbyModel::append_title_text(std::string_view ascii_text) {
    if (!title_editing_) {
        return false;
    }
    bool changed{};
    for (const unsigned char character : ascii_text) {
        if (configuration_.map_title.size() >= maximum_title_code_units) {
            break;
        }
        if (character >= 32U && character <= 126U) {
            configuration_.map_title.push_back(static_cast<char>(character));
            changed = true;
        }
    }
    return changed;
}

bool UgcEditorLobbyModel::erase_title_character() noexcept {
    if (!title_editing_ || configuration_.map_title.empty()) {
        return false;
    }
    configuration_.map_title.pop_back();
    return true;
}

bool UgcEditorLobbyModel::commit_title_edit() noexcept {
    if (!title_editing_) {
        return false;
    }
    if (configuration_.map_title.empty()) {
        configuration_.map_title = title_before_edit_;
    }
    title_before_edit_.clear();
    title_editing_ = false;
    return true;
}

void UgcEditorLobbyModel::cancel_title_edit() noexcept {
    if (!title_editing_) {
        return;
    }
    configuration_.map_title = title_before_edit_;
    title_before_edit_.clear();
    title_editing_ = false;
}

void UgcEditorLobbyModel::pointer_move(std::optional<ui::Point> point) noexcept {
    hovered_ = point.has_value() ? hit_control(*point) : std::nullopt;
}

void UgcEditorLobbyModel::pointer_press(std::optional<ui::Point> point) noexcept {
    pointer_down_ = point.has_value();
    pointer_move(point);
}

std::optional<UgcEditorLobbyIntent>
UgcEditorLobbyModel::pointer_release(std::optional<ui::Point> point) noexcept {
    const auto was_down = pointer_down_;
    pointer_down_ = false;
    pointer_move(point);
    if (!was_down || !point.has_value()) {
        return std::nullopt;
    }
    const auto id = hit_control(*point);
    const auto index = id.has_value() ? control_index(*id) : std::nullopt;
    if (!index.has_value()) {
        return std::nullopt;
    }
    static_cast<void>(focus_.set_focused(*id));
    return activate(controls_[*index]);
}

std::optional<UgcEditorLobbyIntent> UgcEditorLobbyModel::handle(ui::InputEvent event) noexcept {
    if (!event.triggers_action()) {
        return std::nullopt;
    }
    if (title_editing_) {
        if (event.action == ui::InputAction::activate) {
            static_cast<void>(commit_title_edit());
        } else if (event.action == ui::InputAction::cancel) {
            cancel_title_edit();
        }
        return std::nullopt;
    }

    switch (event.action) {
    case ui::InputAction::navigate_up:
        static_cast<void>(focus_.move(ui::FocusDirection::up));
        break;
    case ui::InputAction::navigate_down:
        static_cast<void>(focus_.move(ui::FocusDirection::down));
        break;
    case ui::InputAction::navigate_left:
        if (const auto setting = focused_setting(); setting.has_value()) {
            static_cast<void>(cycle(*setting, -1));
        } else {
            static_cast<void>(focus_.move(ui::FocusDirection::left));
        }
        break;
    case ui::InputAction::navigate_right:
        if (const auto setting = focused_setting(); setting.has_value()) {
            static_cast<void>(cycle(*setting, 1));
        } else {
            static_cast<void>(focus_.move(ui::FocusDirection::right));
        }
        break;
    case ui::InputAction::focus_next:
        static_cast<void>(focus_.advance());
        break;
    case ui::InputAction::focus_previous:
        static_cast<void>(focus_.advance(true));
        break;
    case ui::InputAction::activate:
        if (const auto focused = focus_.focused(); focused.has_value()) {
            const auto index = control_index(*focused);
            if (index.has_value()) {
                return activate(controls_[*index]);
            }
        }
        break;
    case ui::InputAction::cancel:
        return UgcEditorLobbyIntent{UgcEditorLobbyIntentKind::leave_lobby, configuration_};
    }
    return std::nullopt;
}

WidgetVisualState UgcEditorLobbyModel::visual_state(ui::WidgetId id) const noexcept {
    const auto index = control_index(id);
    if (!index.has_value() || !controls_[*index].widget.state.enabled) {
        return WidgetVisualState::disabled;
    }
    if (pointer_down_ && hovered_ == id) {
        return WidgetVisualState::pressed;
    }
    if (hovered_ == id) {
        return WidgetVisualState::hovered;
    }
    if (focus_.focused() == id) {
        return WidgetVisualState::focused;
    }
    return WidgetVisualState::normal;
}

std::optional<std::size_t>
UgcEditorLobbyModel::control_index(ui::WidgetId id) const noexcept {
    const auto found = std::ranges::find_if(
        controls_, [id](const auto& control) { return control.widget.id == id; });
    return found == controls_.end()
               ? std::nullopt
               : std::optional{static_cast<std::size_t>(std::distance(controls_.begin(), found))};
}

std::optional<ui::WidgetId>
UgcEditorLobbyModel::hit_control(ui::Point point) const noexcept {
    const auto found = std::ranges::find_if(controls_, [&](const auto& control) {
        return control.widget.state.visible && control.widget.bounds.contains(point);
    });
    return found == controls_.end() ? std::nullopt : std::optional{found->widget.id};
}

std::optional<UgcEditorLobbyIntent>
UgcEditorLobbyModel::activate(const UgcEditorLobbyControl& control) noexcept {
    switch (control.kind) {
    case UgcEditorLobbyControlKind::back:
        return UgcEditorLobbyIntent{UgcEditorLobbyIntentKind::leave_lobby, configuration_};
    case UgcEditorLobbyControlKind::invite:
        return UgcEditorLobbyIntent{UgcEditorLobbyIntentKind::invite_friends, configuration_};
    case UgcEditorLobbyControlKind::start:
        if (!configuration_.map_title.empty()) {
            return UgcEditorLobbyIntent{UgcEditorLobbyIntentKind::start_editor, configuration_};
        }
        return std::nullopt;
    case UgcEditorLobbyControlKind::setting:
        if (!control.setting.has_value()) {
            return std::nullopt;
        }
        if (*control.setting == UgcEditorSettingId::map_title) {
            static_cast<void>(begin_title_edit());
        } else {
            static_cast<void>(cycle(*control.setting, 1));
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<UgcEditorSettingId> UgcEditorLobbyModel::focused_setting() const noexcept {
    const auto focused = focus_.focused();
    const auto index = focused.has_value() ? control_index(*focused) : std::nullopt;
    return index.has_value() ? controls_[*index].setting : std::nullopt;
}

void UgcEditorLobbyModel::rebuild_focus() noexcept {
    std::array<ui::Widget, control_count> widgets{};
    std::ranges::transform(controls_, widgets.begin(), &UgcEditorLobbyControl::widget);
    static_cast<void>(focus_.set_widgets(widgets));
}

std::span<const std::string_view> ugc_editor_maps() noexcept {
    return maps;
}

std::span<const std::string_view> ugc_editor_modes() noexcept {
    return modes;
}

std::span<const std::string_view> ugc_editor_mode_labels() noexcept {
    return mode_labels;
}

std::span<const std::string_view> ugc_editor_prefab_labels() noexcept {
    return prefab_labels;
}

bool UgcIngameSettingsModel::open(bool host,
                                  UgcIngameSettingsDraft current,
                                  std::vector<std::string> valid_skydomes) {
    if (!host || valid_skydomes.empty() || current.map_title.empty() ||
        current.map_title.size() > maximum_title_code_units) {
        return false;
    }
    valid_skydomes.erase(
        std::remove_if(valid_skydomes.begin(), valid_skydomes.end(), [](const auto& name) {
            return name.empty() || name.size() > 63U || !name.ends_with(".txt") ||
                   name.find('/') != std::string::npos || name.find('\\') != std::string::npos;
        }),
        valid_skydomes.end());
    std::ranges::sort(valid_skydomes);
    valid_skydomes.erase(std::unique(valid_skydomes.begin(), valid_skydomes.end()),
                         valid_skydomes.end());
    if (valid_skydomes.empty()) return false;
    if (std::ranges::find(valid_skydomes, current.skydome) == valid_skydomes.end()) {
        current.skydome = valid_skydomes.front();
    }
    if (std::ranges::find(ugc_target_modes, current.target_mode) == ugc_target_modes.end()) {
        current.target_mode = 6U;
    }
    current.water[3U] = 255U;
    opening_ = current;
    draft_ = std::move(current);
    skydomes_ = std::move(valid_skydomes);
    focused_ = 0U;
    visible_ = true;
    title_editing_ = false;
    return true;
}

void UgcIngameSettingsModel::cancel() noexcept {
    draft_ = opening_;
    visible_ = false;
    title_editing_ = false;
}

std::optional<UgcIngameSettingsDraft> UgcIngameSettingsModel::apply() noexcept {
    if (!visible_ || draft_.map_title.empty()) return std::nullopt;
    visible_ = false;
    title_editing_ = false;
    opening_ = draft_;
    return draft_;
}

bool UgcIngameSettingsModel::visible() const noexcept { return visible_; }
bool UgcIngameSettingsModel::title_editing() const noexcept { return title_editing_; }
UgcIngameSettingField UgcIngameSettingsModel::focused_field() const noexcept {
    return static_cast<UgcIngameSettingField>(focused_);
}
const UgcIngameSettingsDraft& UgcIngameSettingsModel::draft() const noexcept { return draft_; }
std::span<const std::string> UgcIngameSettingsModel::valid_skydomes() const noexcept {
    return skydomes_;
}

bool UgcIngameSettingsModel::set_focused_field(UgcIngameSettingField field) noexcept {
    const auto index = static_cast<std::size_t>(field);
    if (!visible_ || title_editing_ || index >= field_count) return false;
    const bool changed = focused_ != index;
    focused_ = index;
    return changed;
}

void UgcIngameSettingsModel::move_focus(int direction) noexcept {
    if (!visible_ || title_editing_ || direction == 0) return;
    focused_ = wrapped(focused_, field_count, direction);
}

bool UgcIngameSettingsModel::adjust_focused(int direction) noexcept {
    if (!visible_ || title_editing_ || direction == 0) return false;
    const auto delta = direction > 0 ? 1 : -1;
    switch (focused_field()) {
    case UgcIngameSettingField::skydome: {
        const auto index = index_or_zero(skydomes_, draft_.skydome);
        draft_.skydome = skydomes_[wrapped(index, skydomes_.size(), direction)];
        return true;
    }
    case UgcIngameSettingField::water_red:
    case UgcIngameSettingField::water_green:
    case UgcIngameSettingField::water_blue: {
        const auto channel = focused_ - 1U;
        const auto value = static_cast<int>(draft_.water[channel]) + delta;
        draft_.water[channel] = static_cast<std::uint8_t>(std::clamp(value, 0, 255));
        return true;
    }
    case UgcIngameSettingField::target_mode: {
        const auto index = index_or_zero(ugc_target_modes, draft_.target_mode);
        draft_.target_mode = ugc_target_modes[wrapped(index, ugc_target_modes.size(), direction)];
        return true;
    }
    case UgcIngameSettingField::map_title:
    case UgcIngameSettingField::map_preview:
        return false;
    }
    return false;
}

bool UgcIngameSettingsModel::begin_title_edit() noexcept {
    if (!visible_ || focused_field() != UgcIngameSettingField::map_title) return false;
    title_editing_ = true;
    return true;
}

bool UgcIngameSettingsModel::append_title_text(std::string_view ascii_text) {
    if (!title_editing_ || ascii_text.empty() ||
        draft_.map_title.size() + ascii_text.size() > maximum_title_code_units) {
        return false;
    }
    if (!std::ranges::all_of(ascii_text, [](unsigned char value) {
            return value >= 32U && value <= 126U;
        })) {
        return false;
    }
    draft_.map_title.append(ascii_text);
    return true;
}

bool UgcIngameSettingsModel::erase_title_character() noexcept {
    if (!title_editing_ || draft_.map_title.empty()) return false;
    draft_.map_title.pop_back();
    return true;
}

void UgcIngameSettingsModel::finish_title_edit() noexcept {
    if (!draft_.map_title.empty()) title_editing_ = false;
}

std::string_view ugc_ingame_mode_name(std::uint8_t mode) noexcept {
    switch (mode) {
    case 1U: return "dem";
    case 2U: return "zom";
    case 3U: return "mh";
    case 4U: return "oc";
    case 5U: return "dia";
    case 6U: return "tdm";
    case 7U: return "vip";
    case 8U: return "ctf";
    case 9U: return "tc";
    default: return "tdm";
    }
}

} // namespace battlespades::frontend
