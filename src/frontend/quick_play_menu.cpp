#include "battlespades/frontend/quick_play_menu.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <utility>

namespace battlespades::frontend {
namespace {

constexpr std::array random_modes{
    std::string_view{"zom"}, std::string_view{"tdm"}, std::string_view{"dia"},
    std::string_view{"mh"}, std::string_view{"oc"}, std::string_view{"dem"},
    std::string_view{"ctf"},
};
constexpr std::array random_maps{
    std::string_view{"AncientEgypt"}, std::string_view{"ArcticBase"},
    std::string_view{"Atlantis"}, std::string_view{"BlockNess"},
    std::string_view{"BranCastle"}, std::string_view{"CastleWars"},
    std::string_view{"DoubleDragon"}, std::string_view{"DragonIsland"},
    std::string_view{"Frontier"}, std::string_view{"GreatWall"},
    std::string_view{"Invasion"}, std::string_view{"London"},
    std::string_view{"LunarBase"}, std::string_view{"MayanJungle"},
    std::string_view{"SpookyMansion"}, std::string_view{"TheColosseum"},
    std::string_view{"TokyoNeon"},
};

constexpr std::array ctf_modes{std::string_view{"ctf"}};
constexpr std::array ctf_maps{
    std::string_view{"Atlantis"}, std::string_view{"BlockNess"},
    std::string_view{"CastleWars"}, std::string_view{"DoubleDragon"},
    std::string_view{"Invasion"}, std::string_view{"TokyoNeon"},
};
constexpr std::array classic_maps{
    std::string_view{"Crossroads"}, std::string_view{"Hiesville"},
    std::string_view{"ToTheBridge"}, std::string_view{"Trenches"},
    std::string_view{"WinterValley"}, std::string_view{"WW1"},
    std::string_view{"Classic"},
};
constexpr std::array classic_rules{
    QuickPlayRule{"RULE_CTF_ENABLE_SHOOT_WITH_INTEL", "ON"},
    QuickPlayRule{"RULE_CTF_ENABLE_INTEL_AUTO_RETURN", "OFF"},
    QuickPlayRule{"RULE_ENABLE_WEAPON_CLASSIC_SMG", "OFF"},
    QuickPlayRule{"RULE_ENABLE_WEAPON_CLASSIC_SHOTGUN", "OFF"},
};

constexpr std::array demolition_modes{std::string_view{"dem"}};
constexpr std::array demolition_maps{
    std::string_view{"Atlantis"}, std::string_view{"BlockNess"},
    std::string_view{"CastleWars"}, std::string_view{"DoubleDragon"},
    std::string_view{"DragonIsland"}, std::string_view{"Frontier"},
    std::string_view{"GreatWall"}, std::string_view{"LunarBase"},
    std::string_view{"TokyoNeon"},
};

constexpr std::array diamond_modes{std::string_view{"dia"}};
constexpr std::array diamond_maps{
    std::string_view{"AncientEgypt"}, std::string_view{"ArcticBase"},
    std::string_view{"Atlantis"}, std::string_view{"BlockNess"},
    std::string_view{"BranCastle"}, std::string_view{"CastleWars"},
    std::string_view{"DoubleDragon"}, std::string_view{"DragonIsland"},
    std::string_view{"Frontier"}, std::string_view{"GreatWall"},
    std::string_view{"London"}, std::string_view{"LunarBase"},
    std::string_view{"MayanJungle"}, std::string_view{"SpookyMansion"},
    std::string_view{"TheColosseum"}, std::string_view{"TokyoNeon"},
};

constexpr std::array multihill_modes{std::string_view{"mh"}};
constexpr std::array multihill_maps{
    std::string_view{"AncientEgypt"}, std::string_view{"Atlantis"},
    std::string_view{"BlockNess"}, std::string_view{"BranCastle"},
    std::string_view{"CastleWars"}, std::string_view{"DoubleDragon"},
    std::string_view{"DragonIsland"}, std::string_view{"Frontier"},
    std::string_view{"GreatWall"}, std::string_view{"Invasion"},
    std::string_view{"London"}, std::string_view{"LunarBase"},
    std::string_view{"MayanJungle"}, std::string_view{"SpookyMansion"},
    std::string_view{"TheColosseum"},
};

constexpr std::array occupation_modes{std::string_view{"oc"}};
constexpr std::array occupation_maps{
    std::string_view{"AncientEgypt"}, std::string_view{"ArcticBase"},
    std::string_view{"Atlantis"}, std::string_view{"BlockNess"},
    std::string_view{"BranCastle"}, std::string_view{"DragonIsland"},
    std::string_view{"Frontier"}, std::string_view{"GreatWall"},
    std::string_view{"Invasion"}, std::string_view{"London"},
    std::string_view{"LunarBase"}, std::string_view{"MayanJungle"},
    std::string_view{"SpookyMansion"}, std::string_view{"TheColosseum"},
};

constexpr std::array tdm_modes{std::string_view{"tdm"}};
constexpr std::array tdm_maps{
    std::string_view{"AncientEgypt"}, std::string_view{"ArcticBase"},
    std::string_view{"Atlantis"}, std::string_view{"BlockNess"},
    std::string_view{"CastleWars"}, std::string_view{"DoubleDragon"},
    std::string_view{"DragonIsland"}, std::string_view{"Frontier"},
    std::string_view{"GreatWall"}, std::string_view{"Invasion"},
    std::string_view{"London"}, std::string_view{"LunarBase"},
    std::string_view{"MayanJungle"}, std::string_view{"SpookyMansion"},
    std::string_view{"TheColosseum"}, std::string_view{"TokyoNeon"},
};

constexpr std::array territory_modes{std::string_view{"tc"}};
constexpr std::array mafia_maps{std::string_view{"Alcatraz"},
                                std::string_view{"CityOfChicago"}};
constexpr std::array vip_modes{std::string_view{"vip"}};

constexpr std::array zombie_modes{std::string_view{"zom"}};
constexpr std::array zombie_maps{
    std::string_view{"AncientEgypt"}, std::string_view{"ArcticBase"},
    std::string_view{"Atlantis"}, std::string_view{"BlockNess"},
    std::string_view{"BranCastle"}, std::string_view{"CastleWars"},
    std::string_view{"DoubleDragon"}, std::string_view{"DragonIsland"},
    std::string_view{"Frontier"}, std::string_view{"GreatWall"},
    std::string_view{"Invasion"}, std::string_view{"London"},
    std::string_view{"MayanJungle"}, std::string_view{"SpookyMansion"},
    std::string_view{"TheColosseum"}, std::string_view{"TokyoNeon"},
};
constexpr std::array zombie_rules{QuickPlayRule{"RULE_CRATES_SPAWN_TIME", "60"}};

constexpr std::span<const QuickPlayRule> no_rules{};

// IDs are assigned by Python's sorted playlist-file load, while this array is
// in the English UI order produced by PlayListUIManager (Random first).
constexpr std::array definitions{
    QuickPlayPlaylistDefinition{1U, "RANDOM", random_modes, random_maps, no_rules, false, false},
    QuickPlayPlaylistDefinition{3U, "CTF_TITLE", ctf_modes, ctf_maps, no_rules, false, false},
    QuickPlayPlaylistDefinition{
        2U, "CLASSIC_CTF_TITLE", ctf_modes, classic_maps, classic_rules, true, false},
    QuickPlayPlaylistDefinition{4U,
                                "DEMOLITION_TITLE",
                                demolition_modes,
                                demolition_maps,
                                no_rules,
                                false,
                                false},
    QuickPlayPlaylistDefinition{
        5U, "DIAMOND_MINE_TITLE", diamond_modes, diamond_maps, no_rules, false, false},
    QuickPlayPlaylistDefinition{
        6U, "MULTIHILL_TITLE", multihill_modes, multihill_maps, no_rules, false, false},
    QuickPlayPlaylistDefinition{7U,
                                "OCCUPATION_MODE_TITLE",
                                occupation_modes,
                                occupation_maps,
                                no_rules,
                                false,
                                false},
    QuickPlayPlaylistDefinition{
        9U, "TDM_TITLE", tdm_modes, tdm_maps, no_rules, false, false},
    QuickPlayPlaylistDefinition{
        8U, "TC_TITLE", territory_modes, mafia_maps, no_rules, false, true},
    QuickPlayPlaylistDefinition{12U, "VIP_MODE_TITLE", vip_modes, mafia_maps, no_rules, false, true},
    QuickPlayPlaylistDefinition{
        13U, "ZOMBIE_MODE_TITLE", zombie_modes, zombie_maps, zombie_rules, false, false},
};

constexpr std::int32_t subpixels{MainMenuModel::subpixels_per_pixel};

[[nodiscard]] constexpr ui::Rect pixels(std::int32_t x,
                                        std::int32_t y,
                                        std::int32_t width,
                                        std::int32_t height) noexcept {
    return ui::Rect{x * subpixels, y * subpixels, width * subpixels, height * subpixels};
}

constexpr ui::Rect refresh_bounds{pixels(60, 456, 332, 50)};
constexpr ui::Rect primary_bounds{pixels(405, 456, 332, 50)};
constexpr ui::Rect back_bounds{pixels(54, 541, 78, 32)};
constexpr std::int32_t first_row_top{175};
constexpr std::int32_t row_height{24};
constexpr ui::Rect list_rows_bounds{pixels(66, first_row_top, 320, row_height)};

[[nodiscard]] bool strict_hit(ui::Rect bounds, ui::Point point) noexcept {
    const auto right = static_cast<std::int64_t>(bounds.x) + bounds.width;
    const auto bottom = static_cast<std::int64_t>(bounds.y) + bounds.height;
    return point.x > bounds.x && point.x < right && point.y > bounds.y && point.y < bottom;
}

[[nodiscard]] bool usable_response(const QuickPlayServerResponse& response) noexcept {
    return response.matching_version && !response.address.empty() && response.game_port != 0U &&
           response.maximum_players != 0U && response.players <= response.maximum_players &&
           std::isfinite(response.ping_seconds) && response.ping_seconds >= 0.0;
}

[[nodiscard]] std::string map_identity(std::string_view value) {
    std::string result;
    for (const char character : value) {
        if (character >= 'A' && character <= 'Z') {
            result.push_back(static_cast<char>(character - 'A' + 'a'));
        } else if ((character >= 'a' && character <= 'z') ||
                   (character >= '0' && character <= '9')) {
            result.push_back(character);
        }
    }
    return result;
}

[[nodiscard]] std::uint32_t ping_in_milliseconds(double seconds) noexcept {
    constexpr auto maximum = static_cast<double>(std::numeric_limits<std::uint32_t>::max());
    return static_cast<std::uint32_t>(std::clamp(seconds * 1'000.0, 0.0, maximum));
}

} // namespace

std::string QuickPlayServerResponse::identifier() const {
    return "aos://" + address + ':' + std::to_string(game_port);
}

const QuickPlayServerResponse* QuickPlayPlaylistRow::chosen_server() const noexcept {
    if (!chosen_server_index.has_value() || *chosen_server_index >= server_responses.size()) {
        return nullptr;
    }
    return &server_responses[*chosen_server_index];
}

QuickPlayMenuModel::QuickPlayMenuModel(bool mafia_content_owned, std::uint32_t random_seed)
    : mafia_content_owned_{mafia_content_owned}, random_{random_seed} {
    rows_.reserve(definitions.size());
    for (const auto& definition : definitions) {
        QuickPlayPlaylistRow row;
        row.definition = &definition;
        row.owned = !definition.mafia_content || mafia_content_owned_;
        rows_.push_back(std::move(row));
    }
}

std::span<const QuickPlayPlaylistRow> QuickPlayMenuModel::rows() const noexcept {
    return rows_;
}

std::size_t QuickPlayMenuModel::selected_row() const noexcept {
    return selected_row_;
}

const QuickPlayPlaylistRow& QuickPlayMenuModel::selected() const noexcept {
    return rows_[selected_row_];
}

bool QuickPlayMenuModel::select_row(std::size_t row) noexcept {
    if (row >= rows_.size()) {
        return false;
    }
    selected_row_ = row;
    return true;
}

void QuickPlayMenuModel::set_mafia_content_owned(bool owned) noexcept {
    mafia_content_owned_ = owned;
    for (auto& row : rows_) {
        row.owned = row.definition != nullptr &&
                    (!row.definition->mafia_content || mafia_content_owned_);
    }
}

void QuickPlayMenuModel::set_network_available(bool available) noexcept {
    if (network_available_ == available) {
        return;
    }
    network_available_ = available;
    active_search_.reset();
    clear_discovery();
    search_state_ = available ? QuickPlaySearchState::idle : QuickPlaySearchState::unavailable;
}

bool QuickPlayMenuModel::network_available() const noexcept {
    return network_available_;
}

QuickPlaySearchState QuickPlayMenuModel::search_state() const noexcept {
    return search_state_;
}

std::optional<QuickPlaySearchIntent> QuickPlayMenuModel::begin_search() noexcept {
    if (!network_available_ || search_state_ == QuickPlaySearchState::searching) {
        return std::nullopt;
    }
    clear_discovery();
    search_state_ = QuickPlaySearchState::searching;
    active_search_ = QuickPlaySearchIntent{next_generation_++, QuickPlayServerMode::public_match};
    return active_search_;
}

bool QuickPlayMenuModel::accept_server(QuickPlaySearchIntent request,
                                       QuickPlayServerResponse response) {
    if (!request_is_current(request) || !usable_response(response)) {
        return false;
    }
    const auto iterator = std::ranges::find_if(rows_, [&response](const auto& row) {
        return row.definition != nullptr && row.definition->id == response.playlist_id;
    });
    if (iterator == rows_.end()) {
        return false;
    }
    auto& responses = iterator->server_responses;
    const auto existing = std::ranges::find_if(responses, [&response](const auto& candidate) {
        return candidate.address == response.address && candidate.game_port == response.game_port;
    });
    if (existing != responses.end()) {
        *existing = std::move(response);
    } else {
        if (responses.size() >= 512U) {
            return false;
        }
        responses.push_back(std::move(response));
    }
    iterator->lowest_ping_seconds = std::ranges::min_element(
        responses, {}, &QuickPlayServerResponse::ping_seconds)->ping_seconds;
    choose_server(*iterator);
    return true;
}

std::size_t QuickPlayMenuModel::accept_public_server(QuickPlaySearchIntent request,
                                                     const QuickPlayServerResponse& response) {
    std::size_t accepted{};
    std::string_view mode = response.mode_id;
    if (mode == "cctf") mode = "ctf";
    if (mode == "occ") mode = "oc";
    const auto map = map_identity(response.map);
    for (const auto& definition : definitions) {
        if (definition.classic != response.classic ||
            std::ranges::find(definition.modes, mode) == definition.modes.end() ||
            !std::ranges::any_of(definition.maps, [&map](std::string_view candidate) {
                return map_identity(candidate) == map;
            })) {
            continue;
        }
        auto candidate = response;
        candidate.playlist_id = definition.id;
        accepted += accept_server(request, std::move(candidate)) ? 1U : 0U;
    }
    return accepted;
}

bool QuickPlayMenuModel::finish_search(QuickPlaySearchIntent request) noexcept {
    if (!request_is_current(request)) {
        return false;
    }
    active_search_.reset();
    search_state_ = QuickPlaySearchState::complete;
    return true;
}

bool QuickPlayMenuModel::fail_search(QuickPlaySearchIntent request) noexcept {
    if (!request_is_current(request)) {
        return false;
    }
    active_search_.reset();
    search_state_ = QuickPlaySearchState::failed;
    clear_discovery();
    return true;
}

bool QuickPlayMenuModel::refresh_enabled() const noexcept {
    return network_available_ && search_state_ != QuickPlaySearchState::searching;
}

QuickPlayPrimaryKind QuickPlayMenuModel::primary_kind() const noexcept {
    if (rows_.empty()) {
        return QuickPlayPrimaryKind::hidden;
    }
    return selected().owned ? QuickPlayPrimaryKind::start : QuickPlayPrimaryKind::buy;
}

bool QuickPlayMenuModel::primary_enabled() const noexcept {
    switch (primary_kind()) {
    case QuickPlayPrimaryKind::start:
        return network_available_ && search_state_ == QuickPlaySearchState::complete &&
               selected().chosen_server() != nullptr;
    case QuickPlayPrimaryKind::buy:
        return true;
    case QuickPlayPrimaryKind::hidden:
        return false;
    }
    return false;
}

std::optional<QuickPlayIntent> QuickPlayMenuModel::activate_primary() const {
    if (!primary_enabled()) {
        return std::nullopt;
    }
    if (primary_kind() == QuickPlayPrimaryKind::buy) {
        return QuickPlayBuyIntent{};
    }
    const auto& row = selected();
    if (const auto* server = row.chosen_server(); server != nullptr) {
        return QuickPlayDirectStartIntent{server->identifier(),
                                          QuickPlayServerMode::public_match,
                                          server->name,
                                          server->map,
                                          server->mode_id,
                                          server->texture_skin,
                                          server->classic,
                                          server->identity_server_id,
                                          server->identity_ticket};
    }
    return QuickPlayPlaylistStartIntent{QuickPlayServerMode::public_match, row.definition->id};
}

QuickPlayBackIntent QuickPlayMenuModel::back() const noexcept {
    return {};
}

void QuickPlayMenuModel::pointer_move(std::optional<ui::Point> point) noexcept {
    hovered_control_ = point.has_value() ? control_hit_test(*point) : std::nullopt;
    hovered_row_ = point.has_value() ? row_hit_test(*point) : std::nullopt;
}

void QuickPlayMenuModel::pointer_press(std::optional<ui::Point> point) noexcept {
    pointer_move(point);
    pointer_down_ = true;
    // Retail TextButton arms on every delivered press, then checks hover only
    // on release. NavigationBar instead requires press and release over Back.
    refresh_armed_ = refresh_enabled();
    primary_armed_ = primary_enabled();
    back_armed_ = hovered_control_ == QuickPlayFixedControl::back;
}

std::optional<QuickPlayIntent>
QuickPlayMenuModel::pointer_release(std::optional<ui::Point> point) {
    pointer_move(point);
    std::optional<QuickPlayIntent> intent;
    if (pointer_down_ && hovered_control_.has_value()) {
        switch (*hovered_control_) {
        case QuickPlayFixedControl::refresh:
            if (refresh_armed_) {
                if (const auto request = begin_search(); request.has_value()) {
                    intent = *request;
                }
            }
            break;
        case QuickPlayFixedControl::primary:
            if (primary_armed_) {
                intent = activate_primary();
            }
            break;
        case QuickPlayFixedControl::back:
            if (back_armed_) {
                intent = QuickPlayBackIntent{};
            }
            break;
        }
    } else if (pointer_down_ && hovered_row_.has_value()) {
        static_cast<void>(select_row(*hovered_row_));
    }
    pointer_down_ = false;
    refresh_armed_ = false;
    primary_armed_ = false;
    back_armed_ = false;
    return intent;
}

std::optional<QuickPlayIntent> QuickPlayMenuModel::handle(ui::InputEvent event) {
    if (!event.triggers_action()) {
        return std::nullopt;
    }
    using ui::InputAction;
    switch (event.action) {
    case InputAction::navigate_up:
    case InputAction::focus_previous:
        selected_row_ = selected_row_ == 0U ? rows_.size() - 1U : selected_row_ - 1U;
        break;
    case InputAction::navigate_down:
    case InputAction::focus_next:
        selected_row_ = (selected_row_ + 1U) % rows_.size();
        break;
    case InputAction::activate:
        return activate_primary();
    case InputAction::cancel:
        return QuickPlayBackIntent{};
    case InputAction::navigate_left:
    case InputAction::navigate_right:
        break;
    }
    return std::nullopt;
}

std::optional<std::size_t> QuickPlayMenuModel::hovered_row() const noexcept {
    return hovered_row_;
}

std::optional<QuickPlayFixedControl> QuickPlayMenuModel::hovered_control() const noexcept {
    return hovered_control_;
}

WidgetVisualState QuickPlayMenuModel::visual_state(QuickPlayFixedControl control) const noexcept {
    if (!control_visible(control) || !control_enabled(control)) {
        return WidgetVisualState::disabled;
    }
    const auto hovered = hovered_control_ == control;
    const auto pressed = pointer_down_ && hovered &&
                         ((control == QuickPlayFixedControl::refresh && refresh_armed_) ||
                          (control == QuickPlayFixedControl::primary && primary_armed_) ||
                          (control == QuickPlayFixedControl::back && back_armed_));
    if (pressed) {
        return WidgetVisualState::pressed;
    }
    return hovered ? WidgetVisualState::hovered : WidgetVisualState::normal;
}

std::optional<std::size_t> QuickPlayMenuModel::row_hit_test(ui::Point point) const noexcept {
    for (std::size_t row = 0U; row < rows_.size() && row < visible_playlist_rows; ++row) {
        auto bounds = list_rows_bounds;
        bounds.y += static_cast<std::int32_t>(row) * row_height * subpixels;
        if (strict_hit(bounds, point)) {
            return row;
        }
    }
    return std::nullopt;
}

std::optional<QuickPlayFixedControl>
QuickPlayMenuModel::control_hit_test(ui::Point point) const noexcept {
    if (strict_hit(primary_bounds, point) && control_visible(QuickPlayFixedControl::primary)) {
        return QuickPlayFixedControl::primary;
    }
    if (strict_hit(refresh_bounds, point)) {
        return QuickPlayFixedControl::refresh;
    }
    if (strict_hit(back_bounds, point)) {
        return QuickPlayFixedControl::back;
    }
    return std::nullopt;
}

bool QuickPlayMenuModel::control_enabled(QuickPlayFixedControl control) const noexcept {
    switch (control) {
    case QuickPlayFixedControl::refresh:
        return refresh_enabled();
    case QuickPlayFixedControl::primary:
        return primary_enabled();
    case QuickPlayFixedControl::back:
        return true;
    }
    return false;
}

bool QuickPlayMenuModel::control_visible(QuickPlayFixedControl control) const noexcept {
    return control != QuickPlayFixedControl::primary ||
           primary_kind() != QuickPlayPrimaryKind::hidden;
}

bool QuickPlayMenuModel::request_is_current(QuickPlaySearchIntent request) const noexcept {
    return network_available_ && search_state_ == QuickPlaySearchState::searching &&
           active_search_.has_value() && request == *active_search_;
}

void QuickPlayMenuModel::clear_discovery() noexcept {
    for (auto& row : rows_) {
        row.lowest_ping_seconds = 1'000.0;
        row.server_responses.clear();
        row.chosen_server_index.reset();
        row.displayed_ping_milliseconds.reset();
        row.displayed_players.clear();
    }
}

void QuickPlayMenuModel::choose_server(QuickPlayPlaylistRow& row) {
    std::vector<std::size_t> low_ping_populated;
    std::vector<std::size_t> populated;
    std::vector<std::size_t> low_ping_empty;
    std::vector<std::size_t> empty;
    const auto low_ping_threshold = row.lowest_ping_seconds * 3.0;
    for (std::size_t index = 0U; index < row.server_responses.size(); ++index) {
        const auto& server = row.server_responses[index];
        if (server.players > 0U && server.players < server.maximum_players) {
            (server.ping_seconds < low_ping_threshold ? low_ping_populated : populated)
                .push_back(index);
        } else if (server.players == 0U) {
            (server.ping_seconds < low_ping_threshold ? low_ping_empty : empty).push_back(index);
        }
        // Full servers are deliberately ignored by retail Quick Play.
    }

    const std::vector<std::size_t>* candidates{};
    if (!low_ping_populated.empty()) {
        candidates = &low_ping_populated;
    } else if (!low_ping_empty.empty()) {
        candidates = &low_ping_empty;
    } else if (!populated.empty()) {
        candidates = &populated;
    } else if (!empty.empty()) {
        candidates = &empty;
    }
    if (candidates == nullptr) {
        row.chosen_server_index.reset();
        row.displayed_ping_milliseconds.reset();
        row.displayed_players.clear();
        return;
    }

    std::uniform_int_distribution<std::size_t> distribution{0U, candidates->size() - 1U};
    row.chosen_server_index = (*candidates)[distribution(random_)];
    const auto& chosen = row.server_responses[*row.chosen_server_index];
    row.displayed_ping_milliseconds = ping_in_milliseconds(chosen.ping_seconds);
    row.displayed_players =
        std::to_string(chosen.players) + '/' + std::to_string(chosen.maximum_players);
}

std::span<const QuickPlayPlaylistDefinition> quick_play_playlist_definitions() noexcept {
    return definitions;
}

} // namespace battlespades::frontend
