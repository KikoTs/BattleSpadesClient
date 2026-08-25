#include "battlespades/frontend/custom_match_menu.hpp"
#include "battlespades/frontend/custom_match_presentation.hpp"

#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace battlespades::frontend;
using battlespades::ui::DrawList;
using battlespades::ui::DrawRect;
using battlespades::ui::DrawSpace;
using battlespades::ui::InputAction;
using battlespades::ui::InputEvent;
using battlespades::ui::InputPhase;
using battlespades::ui::Point;
using battlespades::ui::Rect;
using battlespades::ui::SpriteDrawCommand;
using battlespades::ui::TextDrawCommand;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] CustomMatchLobbyRecord lobby(std::string id,
                                           std::string name,
                                           std::uint16_t members = 2U,
                                           std::uint16_t maximum = 24U) {
    CustomMatchLobbyRecord result;
    result.lobby_id = std::move(id);
    result.name = std::move(name);
    result.member_count = members;
    result.maximum_members = maximum;
    result.ping_milliseconds = 42U;
    result.game_info = {"Team Deathmatch", "Castle Wars"};
    result.game_rules = {"15 Minutes"};
    return result;
}

[[nodiscard]] bool has_text(const DrawList& list, std::string_view key) {
    for (const auto& command : list.commands()) {
        if (const auto* value = std::get_if<TextDrawCommand>(&command);
            value != nullptr && value->localization_key == key) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool has_sprite(const DrawList& list, std::string_view asset) {
    for (const auto& command : list.commands()) {
        if (const auto* value = std::get_if<SpriteDrawCommand>(&command);
            value != nullptr && value->asset_id == asset) {
            return true;
        }
    }
    return false;
}

void retail_geometry_and_host_boundary_are_explicit() {
    const auto layout = custom_match_classic_layout();
    expect(layout.frame == DrawRect{25.0, 5.0, 750.0, 589.0} &&
               layout.list_panel == DrawRect{56.0, 95.0, 340.0, 413.0} &&
               layout.source_filter == DrawRect{242.0, 114.0, 130.0, 24.0} &&
               layout.preview_panel == DrawRect{401.0, 95.0, 340.0, 354.0} &&
               layout.join_button == DrawRect{405.0, 456.0, 332.0, 50.0},
           "ListPreview, filter, preview and full-width Join geometry must match retail");

    CustomMatchMenuModel model;
    const auto controls = model.controls();
    expect(controls.size() == 3U,
           "MatchSquadsMenu has Back, source filter and Join but no visible Create button");
    expect(controls[0U].widget.bounds == Rect{432, 4'328, 624, 256} &&
               controls[1U].widget.bounds == Rect{1'936, 912, 1'040, 192} &&
               controls[2U].widget.bounds == Rect{3'240, 3'648, 2'656, 400},
           "model hit targets must use the recovered eighth-pixel geometry");
    expect(!model.request_create().has_value(),
           "offline root must not emit a phantom create request");
    model.set_network_available(true);
    expect(model.request_create() ==
               CustomMatchIntent{CustomMatchIntentKind::create_lobby,
                                 CustomMatchSource::open,
                                 {}},
           "host flow must still have a typed create boundary outside the visible retail list");
}

void discovery_is_bounded_deduplicated_and_honest() {
    CustomMatchMenuModel model;
    expect(model.discovery_state() == CustomMatchDiscoveryState::offline &&
               model.lobbies().empty() &&
               model.status_localization_key() == "MATCHMAKING_OFFLINE",
           "default state must disclose that no network adapter is connected");
    expect(!model.request_refresh().has_value(),
           "offline screen must not claim that discovery started");

    model.set_network_available(true);
    const auto request = model.request_refresh();
    expect(request == CustomMatchIntent{CustomMatchIntentKind::refresh_lobbies,
                                        CustomMatchSource::open,
                                        {}},
           "first online refresh must identify the default Open source");
    expect(!model.request_refresh().has_value(),
           "duplicate in-flight refreshes must be bounded");

    auto duplicate = lobby("one", "Duplicate");
    auto malformed = lobby("", "Missing Identifier");
    expect(model.complete_refresh(
               {lobby("one", "One"), std::move(duplicate), std::move(malformed)}, true),
           "matching refresh callback must complete");
    expect(model.discovery_state() == CustomMatchDiscoveryState::ready &&
               model.lobbies().size() == 1U &&
               model.selected_lobby() != nullptr && model.selected_lobby()->name == "One" &&
               model.status_localization_key().empty(),
           "first valid unique row must be retained and selected without fake placeholders");

    expect(model.request_refresh().has_value(), "ready list must permit an explicit refresh");
    expect(model.complete_refresh({}, false) && model.lobbies().empty() &&
               model.selected_lobby() == nullptr &&
               model.discovery_state() == CustomMatchDiscoveryState::error &&
               model.status_localization_key() == "MATCHMAKING_UNAVAILABLE",
           "failed refresh must clear stale join targets and disclose the error");
    expect(!model.complete_refresh({lobby("late", "Late")}, true),
           "stale callbacks without an active transaction must be ignored");
}

void join_intent_matches_retail_authorization_gates() {
    CustomMatchMenuModel model;
    model.set_network_available(true);
    static_cast<void>(model.request_refresh());
    auto missing_details = lobby("details", "Missing Details");
    missing_details.game_info.clear();
    missing_details.game_rules.clear();
    auto full = lobby("full", "Full", 24U, 24U);
    auto unowned = lobby("unowned", "Unowned");
    unowned.content_owned = false;
    expect(model.complete_refresh(
               {std::move(missing_details), std::move(full), std::move(unowned),
                lobby("ready", "Ready")},
               true),
           "join fixture should load");

    expect(!model.can_join() && !model.request_join().has_value(),
           "retail requires preview details before enabling Join");
    expect(model.select_lobby(1U) && !model.can_join(),
           "full lobbies must fail closed");
    expect(model.select_lobby(2U) && model.selected_requires_purchase() && !model.can_join(),
           "unowned lobby content must not enter the loading path");
    expect(model.select_lobby(3U) && model.can_join(),
           "open, owned lobby with details should enable Join");
    expect(model.request_join() ==
               CustomMatchIntent{CustomMatchIntentKind::join_lobby,
                                 CustomMatchSource::open,
                                 "ready"},
           "Join must carry only the selected opaque lobby identity");

    model.set_network_available(false);
    expect(model.lobbies().empty() && !model.request_join().has_value(),
           "network loss must invalidate every previous join request");
}

void source_changes_repopulate_and_input_emits_typed_intents() {
    CustomMatchMenuModel model;
    model.set_network_available(true);
    static_cast<void>(model.request_refresh());
    expect(model.complete_refresh({lobby("open", "Open Lobby")}, true),
           "Open list should load");

    const auto friends_request = model.cycle_source(1);
    expect(friends_request ==
               CustomMatchIntent{CustomMatchIntentKind::refresh_lobbies,
                                 CustomMatchSource::friends,
                                 {}} &&
               model.source() == CustomMatchSource::friends && model.lobbies().empty() &&
               model.discovery_state() == CustomMatchDiscoveryState::refreshing,
           "source switch must clear Open rows and enumerate Friends transactionally");
    expect(model.complete_refresh({lobby("friend", "Friend Lobby")}, true),
           "Friends list should load");

    expect(model.handle(InputEvent{InputAction::cancel, InputPhase::pressed}) ==
               CustomMatchIntent{CustomMatchIntentKind::back_to_join_match,
                                 CustomMatchSource::friends,
                                 {}},
           "semantic Cancel must emit the recovered Back route");
    expect(model.handle(InputEvent{InputAction::activate, InputPhase::pressed}) ==
               CustomMatchIntent{CustomMatchIntentKind::join_lobby,
                                 CustomMatchSource::friends,
                                 "friend"},
           "semantic Activate must join the selected lobby");

    model.pointer_press(Point{500, 4'450});
    expect(model.pointer_release(Point{500, 4'450}) ==
               CustomMatchIntent{CustomMatchIntentKind::back_to_join_match,
                                 CustomMatchSource::friends,
                                 {}},
           "recovered Back hit target must emit a typed route");
}

void selection_scrolling_and_lobby_back_routes_are_stable() {
    CustomMatchMenuModel model;
    model.set_network_available(true);
    static_cast<void>(model.request_refresh());
    std::vector<CustomMatchLobbyRecord> rows;
    for (std::size_t index = 0U; index < 20U; ++index) {
        rows.push_back(lobby("lobby-" + std::to_string(index),
                             "Lobby " + std::to_string(index)));
    }
    expect(model.complete_refresh(std::move(rows), true), "twenty-row fixture should load");
    expect(model.select_lobby(15U) && model.first_visible_row() == 3U,
           "selection below the viewport must reveal the minimum thirteen-row window");
    model.scroll_rows(100);
    expect(model.first_visible_row() == 7U,
           "scrolling must clamp to the final full retail page");
    expect(model.select_visible_row(12U) && model.selected_index() == 19U,
           "visible row selection must map through the scroll offset");

    const CustomMatchLobbyRootState owner{"host", CustomMatchLobbyRole::owner};
    const CustomMatchLobbyRootState member{"joined", CustomMatchLobbyRole::member};
    expect(owner.back_route() == CustomMatchLobbyBackRoute::select_menu &&
               member.back_route() == CustomMatchLobbyBackRoute::custom_match_list &&
               owner.title_localization_key() == "MATCH_LOBBY" &&
               owner.settings_title_localization_key() == "MATCH_SETTINGS",
           "lobby root must preserve retail owner/member back destinations and titles");
}

void presentation_is_complete_transition_safe_and_never_invents_rows() {
    CustomMatchMenuModel model;
    CustomMatchPresentation presentation;
    const auto offline = presentation.build_layer(model);
    expect(has_sprite(offline, custom_match_assets::frame) &&
               has_sprite(offline, custom_match_assets::panel) &&
               has_sprite(offline, custom_match_assets::panel_header) &&
               has_text(offline, "SQUAD_LIST") && has_text(offline, "AVAILABLE_SQUADS") &&
               has_text(offline, "MATCHMAKING_OFFLINE") && has_text(offline, "JOIN_SQUAD") &&
               !has_text(offline, "CREATE_SQUAD"),
           "offline root must retain exact retail chrome without a fake lobby or Create button");
    for (const auto& command : offline.commands()) {
        const auto design_space = std::visit(
            [](const auto& value) {
                using Value = std::remove_cvref_t<decltype(value)>;
                if constexpr (std::is_same_v<Value,
                                             battlespades::ui::PlayerNamePlateDrawRequest>) {
                    return false;
                } else {
                    return value.space == DrawSpace::design_pixels;
                }
            },
            command);
        expect(design_space,
               "build_layer must contain only design-space commands for shared slides");
    }

    model.set_network_available(true);
    static_cast<void>(model.request_refresh());
    auto selected = lobby("selected", "Todor's Lobby");
    selected.friend_count = 2U;
    expect(model.complete_refresh({std::move(selected)}, true), "presentation row should load");
    const auto populated = presentation.build_layer(model);
    expect(has_text(populated, "Todor's Lobby") && has_text(populated, "(2 FRIENDS)") &&
               has_text(populated, "2/24") && has_text(populated, "GAME_INFO") &&
               has_text(populated, "GAME_RULES") && has_text(populated, "Team Deathmatch") &&
               has_text(populated, "Castle Wars"),
           "selected row and preview must expose the supplied lobby data exactly");

    const auto complete = presentation.build(model);
    const auto* background = std::get_if<SpriteDrawCommand>(&complete.commands().front());
    expect(background != nullptr && background->asset_id == main_menu_assets::background &&
               background->space == DrawSpace::window_pixels,
           "standalone build must prepend one stationary covered background");
}

void presentation_rejects_invalid_context_and_declares_assets() {
    const CustomMatchMenuModel model;
    bool rejected{};
    try {
        static_cast<void>(CustomMatchPresentation{}.build(
            model, CustomMatchPresentationContext{{0, 600}, 1'000U}));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected, "zero-sized presentation context must fail before drawing");

    const auto assets = custom_match_assets::required();
    expect(assets.size() >= 20U && assets.front().path == main_menu_assets::background,
           "preloader manifest must include the complete Custom Match asset set");
}

struct TestCase final {
    std::string_view name;
    std::function<void()> body;
};

} // namespace

int main() {
    const std::vector<TestCase> tests{
        {"retail_geometry_and_host_boundary_are_explicit",
         retail_geometry_and_host_boundary_are_explicit},
        {"discovery_is_bounded_deduplicated_and_honest",
         discovery_is_bounded_deduplicated_and_honest},
        {"join_intent_matches_retail_authorization_gates",
         join_intent_matches_retail_authorization_gates},
        {"source_changes_repopulate_and_input_emits_typed_intents",
         source_changes_repopulate_and_input_emits_typed_intents},
        {"selection_scrolling_and_lobby_back_routes_are_stable",
         selection_scrolling_and_lobby_back_routes_are_stable},
        {"presentation_is_complete_transition_safe_and_never_invents_rows",
         presentation_is_complete_transition_safe_and_never_invents_rows},
        {"presentation_rejects_invalid_context_and_declares_assets",
         presentation_rejects_invalid_context_and_declares_assets},
    };

    std::size_t failures{};
    for (const auto& test : tests) {
        try {
            test.body();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
        }
    }
    std::cout << tests.size() - failures << '/' << tests.size() << " tests passed\n";
    return failures == 0U ? 0 : 1;
}
