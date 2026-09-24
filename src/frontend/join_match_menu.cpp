#include "battlespades/frontend/join_match_menu.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iterator>
#include <limits>
#include <ranges>
#include <tuple>
#include <utility>

namespace battlespades::frontend {
namespace {

using ui::Rect;
using ui::Widget;
using ui::WidgetId;

constexpr std::int32_t scale{MainMenuModel::subpixels_per_pixel};

[[nodiscard]] constexpr Rect retail_text_button(std::int32_t x,
                                                std::int32_t retail_top_y,
                                                std::int32_t width,
                                                std::int32_t height) noexcept {
    return Rect{x * scale,
                (MainMenuModel::reference_height_pixels - retail_top_y) * scale,
                width * scale,
                height * scale};
}

[[nodiscard]] constexpr Rect retail_bottom_left(std::int32_t x,
                                                std::int32_t y,
                                                std::int32_t width,
                                                std::int32_t height) noexcept {
    return Rect{x * scale,
                (MainMenuModel::reference_height_pixels - y - height) * scale,
                width * scale,
                height * scale};
}

// InputServer.py uses the full-width edit box and Connect row below it. The
// native Favourites extension shares that row instead of overflowing the
// retail three-button frame with a fourth stacked control.
constexpr Rect direct_connect_input = retail_bottom_left(267, 325, 270, 100);
constexpr Rect direct_connect_button = retail_text_button(269, 308, 127, 58);
constexpr Rect direct_favourite_button = retail_text_button(404, 308, 127, 58);
constexpr Rect direct_back_button = retail_bottom_left(248, 32, 78, 26);

[[nodiscard]] constexpr Widget widget(std::uint32_t id, Rect bounds) noexcept {
    return Widget{WidgetId{id}, bounds, {}};
}

[[nodiscard]] bool retail_hit_test(Rect bounds, ui::Point point) noexcept {
    const auto left = static_cast<std::int64_t>(bounds.x);
    const auto top = static_cast<std::int64_t>(bounds.y);
    const auto right = left + static_cast<std::int64_t>(bounds.width);
    const auto bottom = top + static_cast<std::int64_t>(bounds.height);
    const auto x = static_cast<std::int64_t>(point.x);
    const auto y = static_cast<std::int64_t>(point.y);
    return x > left && x < right && y > top && y < bottom;
}

[[nodiscard]] bool same_server(const ServerBrowserEntry& left,
                               const ServerBrowserEntry& right) noexcept {
    // A friend's match found through Steam has no address, so two of them would
    // otherwise look like the same row and collapse into one.
    if (left.steam_host_id != 0U || right.steam_host_id != 0U) {
        if (left.address.empty() || right.address.empty()) {
            return left.steam_host_id == right.steam_host_id;
        }
    }
    return left.game_port == right.game_port && left.address == right.address;
}

[[nodiscard]] char ascii_lower(char value) noexcept {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

[[nodiscard]] bool ascii_less(std::string_view left, std::string_view right) noexcept {
    return std::lexicographical_compare(
        left.begin(), left.end(), right.begin(), right.end(), [](char lhs, char rhs) {
            return ascii_lower(lhs) < ascii_lower(rhs);
        });
}

[[nodiscard]] auto players_sort_key(const ServerBrowserEntry& server) noexcept {
    // Recovered ListGrid packs current/max as count * 256 + max.
    return static_cast<std::uint32_t>(server.players) * 256U + server.maximum_players;
}

[[nodiscard]] constexpr bool uses_region(ServerBrowserSource source) noexcept {
    // Only Official exposes region tabs. All/Community must not silently
    // inherit an invisible filter from a previously selected Official tab.
    return source == ServerBrowserSource::official;
}

[[nodiscard]] bool same_region(std::string_view left, std::string_view right) noexcept {
    const auto canonical = [](char character) {
        return character == '-' ? '_' : ascii_lower(character);
    };
    return std::ranges::equal(left, right, {}, canonical, canonical);
}

[[nodiscard]] constexpr std::uint64_t next_generation(std::uint64_t generation) noexcept {
    return generation == std::numeric_limits<std::uint64_t>::max() ? 1U : generation + 1U;
}

} // namespace

JoinMatchMenuModel::JoinMatchMenuModel()
    : controls_{
          JoinMatchControl{widget(1U, retail_text_button(269, 434, 262, 58)),
                           JoinMatchRoute::server_browser,
                           JoinMatchControlKind::text_button,
                           "SERVER_BROWSER",
                           {}},
          JoinMatchControl{widget(2U, retail_text_button(269, 371, 262, 58)),
                           JoinMatchRoute::direct_connect,
                           JoinMatchControlKind::text_button,
                           "BUTTON_CONNECT_IP",
                           {}},
          JoinMatchControl{widget(3U, retail_text_button(269, 308, 262, 58)),
                           JoinMatchRoute::random_match,
                           JoinMatchControlKind::text_button,
                           "PUBLIC_MATCH",
                           {}},
          // Spades 24 px measures BACK at 47 px; +5 px pad +25 px icon =78 px.
          JoinMatchControl{widget(4U, retail_bottom_left(248, 32, 78, 26)),
                           JoinMatchRoute::select_menu,
                           JoinMatchControlKind::navigation_item,
                           "BACK",
                           join_match_assets::back_icon},
      } {
    rebuild_focus();
}

std::span<const JoinMatchControl> JoinMatchMenuModel::controls() const noexcept {
    return controls_;
}

std::optional<WidgetId> JoinMatchMenuModel::hovered() const noexcept {
    return hovered_;
}

std::optional<WidgetId> JoinMatchMenuModel::focused() const noexcept {
    return focus_.focused();
}

void JoinMatchMenuModel::pointer_move(std::optional<ui::Point> point) noexcept {
    hovered_ = point.has_value() ? hit_test(*point) : std::nullopt;
}

void JoinMatchMenuModel::pointer_press(std::optional<ui::Point> point) noexcept {
    pointer_move(point);
    pointer_down_ = true;
    navigation_item_armed_ = false;
    if (hovered_.has_value()) {
        const auto index = index_of(*hovered_);
        navigation_item_armed_ =
            index.has_value() && controls_[*index].kind == JoinMatchControlKind::navigation_item;
    }
}

std::optional<JoinMatchRoute>
JoinMatchMenuModel::pointer_release(std::optional<ui::Point> point) noexcept {
    pointer_move(point);
    std::optional<JoinMatchRoute> route;
    if (pointer_down_ && hovered_.has_value()) {
        const auto index = index_of(*hovered_);
        if (index.has_value()) {
            const auto& control = controls_[*index];
            if (control.widget.state.enabled && control.widget.state.visible &&
                (control.kind != JoinMatchControlKind::navigation_item || navigation_item_armed_)) {
                route = control.route;
                static_cast<void>(focus_.set_focused(control.widget.id));
            }
        }
    }
    pointer_down_ = false;
    navigation_item_armed_ = false;
    return route;
}

std::optional<JoinMatchRoute> JoinMatchMenuModel::handle(ui::InputEvent event) noexcept {
    if (!event.triggers_action()) {
        return std::nullopt;
    }
    using ui::FocusDirection;
    using ui::InputAction;
    switch (event.action) {
    case InputAction::navigate_up:
        static_cast<void>(focus_.move(FocusDirection::up));
        break;
    case InputAction::navigate_down:
        static_cast<void>(focus_.move(FocusDirection::down));
        break;
    case InputAction::navigate_left:
        static_cast<void>(focus_.move(FocusDirection::left));
        break;
    case InputAction::navigate_right:
        static_cast<void>(focus_.move(FocusDirection::right));
        break;
    case InputAction::focus_next:
        static_cast<void>(focus_.advance());
        break;
    case InputAction::focus_previous:
        static_cast<void>(focus_.advance(true));
        break;
    case InputAction::activate:
        return focused_route();
    case InputAction::cancel:
        return JoinMatchRoute::select_menu;
    }
    return std::nullopt;
}

void JoinMatchMenuModel::set_online_routes_enabled(bool enabled) noexcept {
    for (std::size_t index = 0U; index < 3U; ++index) {
        controls_[index].widget.state.enabled = enabled;
    }
    rebuild_focus();
}

WidgetVisualState JoinMatchMenuModel::visual_state(WidgetId id) const noexcept {
    const auto index = index_of(id);
    if (!index.has_value()) {
        return WidgetVisualState::disabled;
    }
    const auto& control = controls_[*index];
    if (!control.widget.state.enabled || !control.widget.state.visible) {
        return WidgetVisualState::disabled;
    }
    if (pointer_down_ && hovered_ == id &&
        (control.kind != JoinMatchControlKind::navigation_item || navigation_item_armed_)) {
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

std::optional<std::size_t> JoinMatchMenuModel::index_of(WidgetId id) const noexcept {
    const auto iterator = std::ranges::find(
        controls_, id, [](const JoinMatchControl& control) { return control.widget.id; });
    if (iterator == controls_.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(controls_.begin(), iterator));
}

std::optional<WidgetId> JoinMatchMenuModel::hit_test(ui::Point point) const noexcept {
    for (auto iterator = controls_.rbegin(); iterator != controls_.rend(); ++iterator) {
        if (iterator->widget.state.visible && retail_hit_test(iterator->widget.bounds, point)) {
            return iterator->widget.id;
        }
    }
    return std::nullopt;
}

std::optional<JoinMatchRoute> JoinMatchMenuModel::focused_route() const noexcept {
    if (!focus_.focused().has_value()) {
        return std::nullopt;
    }
    const auto index = index_of(*focus_.focused());
    if (!index.has_value() || !controls_[*index].widget.state.enabled ||
        !controls_[*index].widget.state.visible) {
        return std::nullopt;
    }
    return controls_[*index].route;
}

void JoinMatchMenuModel::rebuild_focus() noexcept {
    std::array<Widget, control_count> widgets{};
    std::ranges::transform(
        controls_, widgets.begin(), [](const JoinMatchControl& control) { return control.widget; });
    static_cast<void>(focus_.set_widgets(widgets));
}

void DirectConnectMenuModel::pointer_move(std::optional<ui::Point> point) noexcept {
    pointer_ = point;
}

void DirectConnectMenuModel::pointer_press(std::optional<ui::Point> point) noexcept {
    pointer_ = point;
    pointer_down_ = point.has_value();
    armed_action_.reset();
    if (!point.has_value()) {
        return;
    }
    if (retail_hit_test(direct_connect_input, *point)) {
        input_focused_ = true;
        return;
    }

    input_focused_ = false;
    if (retail_hit_test(direct_connect_button, *point) && !endpoint_.empty()) {
        armed_action_ = DirectConnectActionKind::connect;
    } else if (retail_hit_test(direct_favourite_button, *point) && !endpoint_.empty()) {
        armed_action_ = DirectConnectActionKind::add_favourite;
    } else if (retail_hit_test(direct_back_button, *point)) {
        armed_action_ = DirectConnectActionKind::back;
    }
}

std::optional<DirectConnectAction>
DirectConnectMenuModel::pointer_release(std::optional<ui::Point> point) noexcept {
    pointer_ = point;
    const auto was_down = std::exchange(pointer_down_, false);
    const auto armed = std::exchange(armed_action_, std::nullopt);
    if (!point.has_value()) {
        return std::nullopt;
    }
    if (retail_hit_test(direct_connect_input, *point)) {
        input_focused_ = true;
        return std::nullopt;
    }
    input_focused_ = false;
    if (!was_down || !armed.has_value()) {
        return std::nullopt;
    }
    if (*armed == DirectConnectActionKind::connect &&
        retail_hit_test(direct_connect_button, *point)) {
        return submit();
    }
    if (*armed == DirectConnectActionKind::add_favourite &&
        retail_hit_test(direct_favourite_button, *point)) {
        return submit_favourite();
    }
    if (*armed == DirectConnectActionKind::back &&
        retail_hit_test(direct_back_button, *point)) {
        return DirectConnectAction{DirectConnectActionKind::back, {}};
    }
    return std::nullopt;
}

bool DirectConnectMenuModel::append_character(char character) {
    return append_text(std::string_view{&character, 1U});
}

bool DirectConnectMenuModel::append_text(std::string_view text) {
    const auto valid = [](char character) {
        return (character >= 'a' && character <= 'z') ||
               (character >= 'A' && character <= 'Z') ||
               (character >= '0' && character <= '9') ||
               character == '.' || character == '-' || character == ':';
    };
    if (!input_focused_ || text.empty() ||
        text.size() > maximum_endpoint_bytes - endpoint_.size() ||
        !std::ranges::all_of(text, valid)) {
        return false;
    }
    endpoint_.append(text);
    error_.clear();
    return true;
}

bool DirectConnectMenuModel::paste_text(std::string_view text) {
    if (!input_focused_) return false;
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return false;
    text = text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1U);
    if (append_text(text)) return true;
    set_error("Paste a host or IP address with an optional :port (up to 255 characters).");
    return false;
}

bool DirectConnectMenuModel::erase_character() noexcept {
    if (!input_focused_ || endpoint_.empty())
        return false;
    endpoint_.pop_back();
    error_.clear();
    return true;
}

void DirectConnectMenuModel::clear() noexcept {
    endpoint_.clear();
    error_.clear();
}

void DirectConnectMenuModel::focus_input() noexcept {
    input_focused_ = true;
}

bool DirectConnectMenuModel::input_focused() const noexcept {
    return input_focused_;
}

std::string_view DirectConnectMenuModel::endpoint() const noexcept {
    return endpoint_;
}

std::optional<DirectConnectAction> DirectConnectMenuModel::submit() const {
    if (endpoint_.empty())
        return std::nullopt;
    return DirectConnectAction{DirectConnectActionKind::connect, endpoint_};
}

std::optional<DirectConnectAction> DirectConnectMenuModel::submit_favourite() const {
    if (endpoint_.empty())
        return std::nullopt;
    return DirectConnectAction{DirectConnectActionKind::add_favourite, endpoint_};
}

void DirectConnectMenuModel::set_error(std::string message) {
    error_ = std::move(message);
    message_is_error_ = true;
    input_focused_ = true;
}

void DirectConnectMenuModel::set_notice(std::string message) {
    error_ = std::move(message);
    message_is_error_ = false;
    input_focused_ = true;
}

std::string_view DirectConnectMenuModel::error() const noexcept {
    return error_;
}

bool DirectConnectMenuModel::message_is_error() const noexcept {
    return message_is_error_;
}

WidgetVisualState DirectConnectMenuModel::connect_state() const noexcept {
    if (endpoint_.empty())
        return WidgetVisualState::disabled;
    if (!pointer_.has_value() || !retail_hit_test(direct_connect_button, *pointer_)) {
        return WidgetVisualState::normal;
    }
    return pointer_down_ && armed_action_ == DirectConnectActionKind::connect
               ? WidgetVisualState::pressed
               : WidgetVisualState::hovered;
}

WidgetVisualState DirectConnectMenuModel::favourite_state() const noexcept {
    if (endpoint_.empty())
        return WidgetVisualState::disabled;
    if (!pointer_.has_value() || !retail_hit_test(direct_favourite_button, *pointer_)) {
        return WidgetVisualState::normal;
    }
    return pointer_down_ && armed_action_ == DirectConnectActionKind::add_favourite
               ? WidgetVisualState::pressed
               : WidgetVisualState::hovered;
}

WidgetVisualState DirectConnectMenuModel::back_state() const noexcept {
    if (!pointer_.has_value() || !retail_hit_test(direct_back_button, *pointer_)) {
        return WidgetVisualState::normal;
    }
    return pointer_down_ && armed_action_ == DirectConnectActionKind::back
               ? WidgetVisualState::pressed
               : WidgetVisualState::hovered;
}

bool DirectConnectMenuModel::input_hovered() const noexcept {
    return pointer_.has_value() && retail_hit_test(direct_connect_input, *pointer_);
}

std::string ServerBrowserEntry::identifier() const {
    // A friend's match found through Steam has no address of its own: the
    // host's Steam id is the address, spelled the way Direct Connect takes it.
    if (address.empty() && steam_host_id != 0U) {
        return "steam:" + std::to_string(steam_host_id);
    }
    return "aos://" + address + ':' + std::to_string(game_port);
}

void ServerBrowserModel::replace_servers(std::vector<ServerBrowserEntry> servers) {
    servers_.clear();
    visible_indices_.clear();
    selected_identifier_.reset();
    first_visible_row_ = 0U;
    refreshing_ = false;
    refresh_generation_ = next_generation(refresh_generation_);
    servers_.reserve(servers.size());
    for (auto& server : servers) {
        static_cast<void>(upsert(std::move(server)));
    }
    rebuild_visible();
}

bool ServerBrowserModel::upsert(ServerBrowserEntry server) {
    // A row needs somewhere to go: either an AoSPlay endpoint, or the Steam id
    // of a friend hosting, which the loader dials over Valve's relays. Dropping
    // the second kind here hid every friend's match without saying anything.
    const bool addressable = !server.address.empty() && server.game_port != 0U;
    if ((!addressable && server.steam_host_id == 0U) || !server.compatible) {
        return false;
    }
    const auto iterator =
        std::ranges::find_if(servers_, [&server](const ServerBrowserEntry& existing) {
            return same_server(existing, server);
        });
    if (iterator == servers_.end()) {
        servers_.push_back(std::move(server));
    } else {
        const auto retained_favourite = iterator->favourite;
        *iterator = std::move(server);
        iterator->favourite = iterator->favourite || retained_favourite;
    }
    rebuild_visible();
    return true;
}

void ServerBrowserModel::clear() noexcept {
    servers_.clear();
    visible_indices_.clear();
    selected_identifier_.reset();
    first_visible_row_ = 0U;
    refreshing_ = false;
    refresh_generation_ = next_generation(refresh_generation_);
}

std::span<const ServerBrowserEntry> ServerBrowserModel::servers() const noexcept {
    return servers_;
}

std::span<const std::size_t> ServerBrowserModel::visible_indices() const noexcept {
    return visible_indices_;
}

const ServerBrowserEntry* ServerBrowserModel::selected() const noexcept {
    if (!selected_identifier_.has_value()) {
        return nullptr;
    }
    const auto iterator = std::ranges::find_if(servers_, [this](const ServerBrowserEntry& server) {
        return server.identifier() == *selected_identifier_;
    });
    return iterator == servers_.end() ? nullptr : &*iterator;
}

std::optional<std::size_t> ServerBrowserModel::selected_visible_row() const noexcept {
    if (!selected_identifier_.has_value()) {
        return std::nullopt;
    }
    for (std::size_t row = 0U; row < visible_indices_.size(); ++row) {
        if (servers_[visible_indices_[row]].identifier() == *selected_identifier_) {
            return row;
        }
    }
    return std::nullopt;
}

bool ServerBrowserModel::select_visible_row(std::size_t row) noexcept {
    if (row >= visible_indices_.size()) {
        return false;
    }
    selected_identifier_ = servers_[visible_indices_[row]].identifier();
    return true;
}

void ServerBrowserModel::clear_selection() noexcept {
    selected_identifier_.reset();
}

void ServerBrowserModel::set_source(ServerBrowserSource source) {
    if (source_ == source) {
        return;
    }
    source_ = source;
    first_visible_row_ = 0U;
    if (refreshing_) {
        refreshing_ = false;
        refresh_generation_ = next_generation(refresh_generation_);
    }
    rebuild_visible();
}

void ServerBrowserModel::cycle_source(int direction) {
    const auto count = static_cast<int>(source_count);
    auto index = static_cast<int>(source_);
    const auto normalized = direction >= 0 ? 1 : -1;
    index = (index + normalized + count) % count;
    set_source(static_cast<ServerBrowserSource>(index));
}

ServerBrowserSource ServerBrowserModel::source() const noexcept {
    return source_;
}

void ServerBrowserModel::set_region(ServerBrowserRegion region) noexcept {
    if (region_ == region) {
        return;
    }
    region_ = region;
    if (refreshing_) {
        refreshing_ = false;
        refresh_generation_ = next_generation(refresh_generation_);
    }
}

void ServerBrowserModel::cycle_region(int direction) noexcept {
    const auto count = static_cast<int>(region_count);
    auto index = static_cast<int>(region_);
    const auto normalized = direction >= 0 ? 1 : -1;
    index = (index + normalized + count) % count;
    set_region(static_cast<ServerBrowserRegion>(index));
}

ServerBrowserRegion ServerBrowserModel::region() const noexcept {
    return region_;
}

bool ServerBrowserModel::region_tabs_visible() const noexcept {
    return uses_region(source_);
}

ServerBrowserRefreshRequest ServerBrowserModel::begin_refresh() {
    refresh_generation_ = next_generation(refresh_generation_);
    refreshing_ = true;
    servers_.clear();
    visible_indices_.clear();
    selected_identifier_.reset();
    first_visible_row_ = 0U;
    return ServerBrowserRefreshRequest{
        refresh_generation_, source_, uses_region(source_) ? std::optional{region_} : std::nullopt};
}

bool ServerBrowserModel::accept_response(std::uint64_t generation, ServerBrowserEntry server) {
    if (!refreshing_ || generation != refresh_generation_) {
        return false;
    }
    return upsert(std::move(server));
}

bool ServerBrowserModel::finish_refresh(std::uint64_t generation) noexcept {
    if (!refreshing_ || generation != refresh_generation_) {
        return false;
    }
    refreshing_ = false;
    return true;
}

bool ServerBrowserModel::refreshing() const noexcept {
    return refreshing_;
}

std::uint64_t ServerBrowserModel::refresh_generation() const noexcept {
    return refresh_generation_;
}

void ServerBrowserModel::set_show_full_servers(bool value) {
    show_full_servers_ = value;
    rebuild_visible();
}

void ServerBrowserModel::set_show_empty_servers(bool value) {
    show_empty_servers_ = value;
    rebuild_visible();
}

bool ServerBrowserModel::show_full_servers() const noexcept {
    return show_full_servers_;
}

bool ServerBrowserModel::show_empty_servers() const noexcept {
    return show_empty_servers_;
}

void ServerBrowserModel::select_sort_column(ServerSortColumn column) {
    if (sort_column_ == column) {
        sort_descending_ = !sort_descending_;
    } else {
        sort_column_ = column;
        sort_descending_ = false;
    }
    sort_visible();
}

ServerSortColumn ServerBrowserModel::sort_column() const noexcept {
    return sort_column_;
}

bool ServerBrowserModel::sort_descending() const noexcept {
    return sort_descending_;
}

bool ServerBrowserModel::toggle_selected_favourite() noexcept {
    if (!selected_identifier_.has_value()) {
        return false;
    }
    const auto iterator = std::ranges::find_if(servers_, [this](const ServerBrowserEntry& server) {
        return server.identifier() == *selected_identifier_;
    });
    if (iterator == servers_.end()) {
        return false;
    }
    iterator->favourite = !iterator->favourite;
    if (source_ == ServerBrowserSource::favourites) {
        rebuild_visible();
    }
    return true;
}

bool ServerBrowserModel::can_connect() const noexcept {
    const auto* server = selected();
    return server != nullptr && server->compatible && server->content_owned;
}

std::optional<ServerConnectRequest> ServerBrowserModel::connect_request() const {
    const auto* server = selected();
    if (server == nullptr || !can_connect()) {
        return std::nullopt;
    }
    ServerConnectRequest request{server->identifier(),
                                 server->address,
                                 server->game_port,
                                 server->map,
                                 server->mode_id,
                                 server->texture_skin,
                                 server->classic,
                                 server->identity_server_id,
                                 server->identity_ticket};
    // Carried separately from the endpoint: a listing may offer both, and the
    // loader prefers Valve's relays while keeping the endpoint as the fallback.
    request.steam_host_id = server->steam_host_id;
    return request;
}

std::optional<ServerConnectRequest>
ServerBrowserModel::activate_visible_row(std::size_t row, std::uint8_t click_count) {
    if (row >= visible_indices_.size()) {
        return std::nullopt;
    }
    static_cast<void>(select_visible_row(row));
    if (click_count != 2U) {
        return std::nullopt;
    }
    return connect_request();
}

void ServerBrowserModel::set_visible_row_capacity(std::size_t row_count) noexcept {
    visible_row_capacity_ = std::max<std::size_t>(1U, row_count);
    clamp_scroll();
}

std::size_t ServerBrowserModel::visible_row_capacity() const noexcept {
    return visible_row_capacity_;
}

void ServerBrowserModel::set_first_visible_row(std::size_t row) noexcept {
    first_visible_row_ = std::min(row, maximum_first_visible_row());
}

void ServerBrowserModel::scroll_rows(int delta) noexcept {
    if (delta < 0) {
        const auto magnitude = static_cast<std::uint64_t>(-static_cast<std::int64_t>(delta));
        first_visible_row_ = magnitude > first_visible_row_
                                 ? 0U
                                 : first_visible_row_ - static_cast<std::size_t>(magnitude);
        return;
    }
    const auto maximum = maximum_first_visible_row();
    const auto magnitude = static_cast<std::size_t>(delta);
    first_visible_row_ =
        magnitude > maximum - first_visible_row_ ? maximum : first_visible_row_ + magnitude;
}

void ServerBrowserModel::scroll_to_fraction(double fraction) noexcept {
    if (!std::isfinite(fraction)) {
        return;
    }
    const auto clamped = std::clamp(fraction, 0.0, 1.0);
    first_visible_row_ =
        static_cast<std::size_t>(clamped * static_cast<double>(maximum_first_visible_row()));
}

std::size_t ServerBrowserModel::first_visible_row() const noexcept {
    return first_visible_row_;
}

std::size_t ServerBrowserModel::maximum_first_visible_row() const noexcept {
    return visible_indices_.size() > visible_row_capacity_
               ? visible_indices_.size() - visible_row_capacity_
               : 0U;
}

double ServerBrowserModel::scroll_fraction() const noexcept {
    const auto maximum = maximum_first_visible_row();
    return maximum == 0U ? 0.0
                         : static_cast<double>(first_visible_row_) / static_cast<double>(maximum);
}

bool ServerBrowserModel::visible(const ServerBrowserEntry& server) const noexcept {
    if (!server.compatible) {
        return false;
    }
    if (!show_full_servers_ && server.maximum_players > 0U &&
        server.players == server.maximum_players) {
        return false;
    }
    if (!show_empty_servers_ && server.players == 0U) {
        return false;
    }
    if (uses_region(source_) && !server.region.empty() &&
        !same_region(server.region, localization_key(region_))) return false;
    switch (source_) {
    case ServerBrowserSource::all:
        return true;
    case ServerBrowserSource::official:
        return server.official;
    case ServerBrowserSource::community:
        return !server.official && !server.local;
    case ServerBrowserSource::favourites:
        return server.favourite;
    case ServerBrowserSource::history:
        return server.in_history;
    case ServerBrowserSource::friends:
        return server.friend_hosted;
    case ServerBrowserSource::local:
        return server.local;
    }
    return false;
}

void ServerBrowserModel::rebuild_visible() {
    visible_indices_.clear();
    visible_indices_.reserve(servers_.size());
    for (std::size_t index = 0U; index < servers_.size(); ++index) {
        if (visible(servers_[index])) {
            visible_indices_.push_back(index);
        }
    }
    sort_visible();
    preserve_or_clear_selection();
    clamp_scroll();
}

void ServerBrowserModel::sort_visible() {
    const auto key_less = [this](std::size_t left_index, std::size_t right_index) {
        const auto& left = servers_[left_index];
        const auto& right = servers_[right_index];
        switch (sort_column_) {
        case ServerSortColumn::name:
            return ascii_less(left.name, right.name);
        case ServerSortColumn::players:
            return players_sort_key(left) < players_sort_key(right);
        case ServerSortColumn::map:
            return ascii_less(left.map, right.map);
        case ServerSortColumn::mode:
            return ascii_less(left.mode, right.mode);
        case ServerSortColumn::ping:
            return left.ping_milliseconds < right.ping_milliseconds;
        }
        return false;
    };
    std::stable_sort(visible_indices_.begin(),
                     visible_indices_.end(),
                     [this, &key_less](std::size_t left, std::size_t right) {
                         return sort_descending_ ? key_less(right, left) : key_less(left, right);
                     });
}

void ServerBrowserModel::preserve_or_clear_selection() {
    if (!selected_identifier_.has_value()) {
        return;
    }
    const auto visible_selected = std::ranges::any_of(visible_indices_, [this](std::size_t index) {
        return servers_[index].identifier() == *selected_identifier_;
    });
    if (!visible_selected) {
        selected_identifier_.reset();
    }
}

void ServerBrowserModel::clamp_scroll() noexcept {
    first_visible_row_ = std::min(first_visible_row_, maximum_first_visible_row());
}

std::string_view localization_key(ServerBrowserSource source) noexcept {
    switch (source) {
    case ServerBrowserSource::all:
        return "INTERNET_ALL";
    case ServerBrowserSource::official:
        return "INTERNET_OFFICIAL";
    case ServerBrowserSource::community:
        return "INTERNET_USER";
    case ServerBrowserSource::favourites:
        return "FAVORITES";
    case ServerBrowserSource::history:
        return "HISTORY";
    case ServerBrowserSource::friends:
        return "FRIENDS";
    case ServerBrowserSource::local:
        return "LOCAL";
    }
    return "INTERNET_ALL";
}

std::string_view localization_key(ServerBrowserRegion region) noexcept {
    switch (region) {
    case ServerBrowserRegion::us_west:
        return "US_WEST";
    case ServerBrowserRegion::us_east:
        return "US_EAST";
    case ServerBrowserRegion::europe:
        return "EUROPE";
    case ServerBrowserRegion::australia:
        return "AUSTRALIA";
    }
    return "US_WEST";
}

namespace join_match_assets {

std::span<const MainMenuAsset> required() noexcept {
    constexpr auto texture = MainMenuAssetKind::texture;
    constexpr auto font = MainMenuAssetKind::font;
    constexpr auto music = MainMenuAssetKind::music;
    constexpr auto sound = MainMenuAssetKind::sound;
    constexpr auto linear = TextureSampling::linear;
    constexpr auto nearest = TextureSampling::nearest;
    constexpr auto no_sampling = TextureSampling::not_applicable;
    constexpr auto top_left = TextureAnchor::top_left;
    constexpr auto center = TextureAnchor::center;
    constexpr auto no_anchor = TextureAnchor::not_applicable;

    static constexpr std::array assets{
        MainMenuAsset{main_menu_assets::background, texture, linear, top_left, 0.6},
        MainMenuAsset{main_menu_assets::splash, texture, linear, center, 0.6},
        MainMenuAsset{three_button_frame, texture, linear, center, 0.6},
        MainMenuAsset{small_navigation_frame, texture, linear, center, 0.6},
        MainMenuAsset{main_menu_assets::button_left, texture, nearest, top_left, 0.6},
        MainMenuAsset{main_menu_assets::button_middle, texture, nearest, top_left, 0.6},
        MainMenuAsset{main_menu_assets::button_right, texture, nearest, top_left, 0.6},
        MainMenuAsset{"png/ui/common_elements/buttons/button_large_hover_left.png",
                      texture,
                      nearest,
                      top_left,
                      0.6},
        MainMenuAsset{"png/ui/common_elements/buttons/button_large_hover_mid.png",
                      texture,
                      nearest,
                      top_left,
                      0.6},
        MainMenuAsset{"png/ui/common_elements/buttons/button_large_hover_right.png",
                      texture,
                      nearest,
                      top_left,
                      0.6},
        MainMenuAsset{"png/ui/common_elements/buttons/button_large_press_left.png",
                      texture,
                      nearest,
                      top_left,
                      0.6},
        MainMenuAsset{"png/ui/common_elements/buttons/button_large_press_mid.png",
                      texture,
                      nearest,
                      top_left,
                      0.6},
        MainMenuAsset{"png/ui/common_elements/buttons/button_large_press_right.png",
                      texture,
                      nearest,
                      top_left,
                      0.6},
        MainMenuAsset{back_icon, texture, linear, center, 0.64},
        MainMenuAsset{
            "png/ui/common_elements/frames/ui_frame_large.png", texture, linear, center, 0.64},
        MainMenuAsset{server_content_frame, texture, linear, center, 0.64},
        MainMenuAsset{server_tab_frame, texture, linear, center, 0.64},
        MainMenuAsset{favourite_star, texture, linear, center, 0.64},
        MainMenuAsset{favourite_star_button, texture, linear, center, 0.64},
        MainMenuAsset{map_placeholder, texture, linear, top_left, 0.64},
        MainMenuAsset{sort_up, texture, linear, top_left, 0.64},
        MainMenuAsset{sort_down, texture, linear, top_left, 0.64},
        MainMenuAsset{scroll_up, texture, nearest, center, 1.0},
        MainMenuAsset{scroll_down, texture, nearest, center, 1.0},
        MainMenuAsset{scroll_thumb_top, texture, linear, top_left, 1.0},
        MainMenuAsset{scroll_thumb_middle, texture, linear, top_left, 1.0},
        MainMenuAsset{scroll_thumb_bottom, texture, linear, top_left, 1.0},
        MainMenuAsset{"png/ui/common_elements/scroll_bar/scroll_bar_arrow_left.png",
                      texture,
                      nearest,
                      center,
                      1.0},
        MainMenuAsset{"png/ui/common_elements/scroll_bar/scroll_bar_arrow_right.png",
                      texture,
                      nearest,
                      center,
                      1.0},
        MainMenuAsset{
            "png/ui/common_elements/buttons/button_square.png", texture, nearest, center, 0.6},
        MainMenuAsset{"png/ui/common_elements/tick.png", texture, linear, top_left, 0.64},
        MainMenuAsset{main_menu_assets::button_font, font, no_sampling, no_anchor, 1.0},
        MainMenuAsset{"fonts/Edo.ttf", font, no_sampling, no_anchor, 1.0},
        MainMenuAsset{"fonts/A750-Sans-Medium.ttf", font, no_sampling, no_anchor, 1.0},
        MainMenuAsset{main_menu_assets::confirmation_sound, sound, no_sampling, no_anchor, 1.0},
        MainMenuAsset{main_menu_assets::back_sound, sound, no_sampling, no_anchor, 1.0},
        MainMenuAsset{"sounds/menu_scrollA.ogg", sound, no_sampling, no_anchor, 1.0},
        MainMenuAsset{main_menu_assets::music, music, no_sampling, no_anchor, 1.0},
    };
    return assets;
}

} // namespace join_match_assets
} // namespace battlespades::frontend
