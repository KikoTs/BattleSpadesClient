#include "battlespades/frontend/friends_lobby_menu.hpp"
#include "battlespades/frontend/friends_lobby_presentation.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>

namespace {

using namespace battlespades;
using namespace battlespades::frontend;
using namespace std::chrono_literals;

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

[[nodiscard]] ui::Point center(ui::Rect bounds) noexcept {
    return {bounds.x + bounds.width / 2, bounds.y + bounds.height / 2};
}

FriendsLobbySnapshot fixture() {
    FriendsLobbySnapshot value;
    value.cursor = "4";
    value.friends = {{"a", "Alpha", "online", "accepted", {}, {}},
                     {"b", "Beta", "offline", "accepted", {}, {}},
                     {"r", "Request", "online", "pending", "incoming", {}}};
    value.invitations = {{"i", "l", "Builders", "Alpha"}};
    return value;
}

void selection_survives_reorder_and_bad_rows() {
    FriendsLobbyMenuModel model;
    const auto now = std::chrono::steady_clock::now();
    model.set_identity("self");
    model.set_service_status(true, {});
    model.enter(now);
    model.apply_snapshot(fixture(), now);
    const auto list = model.layout().friends_list;
    const ui::Point first{list.x + 20,
                          list.y + list.height /
                                       static_cast<std::int32_t>(
                                           FriendsLobbyMenuModel::visible_rows) /
                                       2};
    model.pointer_press(first, now + 200ms);
    expect(!model.pointer_release(first, now + 200ms).has_value(),
           "row selection must not execute a destructive action");
    expect(model.selected_friend_id() == "a", "first friend must select by stable id");

    auto changed = fixture();
    std::swap(changed.friends[0], changed.friends[1]);
    changed.friends.push_back(changed.friends.front());
    changed.friends.push_back({{}, "invalid", "online", "accepted", {}, {}});
    model.apply_snapshot(std::move(changed), now + 250ms);
    expect(model.selected_friend_id() == "a", "selection must survive a reordered snapshot");
    expect(model.snapshot().friends.size() == 3U,
           "invalid and duplicate endpoint rows must be discarded");
}

void operations_timeout_and_reject_stale_results() {
    FriendsLobbyMenuModel model;
    const auto now = std::chrono::steady_clock::now();
    model.set_identity("self");
    model.set_service_status(true, {});
    model.enter(now);
    auto snapshot = fixture();
    snapshot.lobby = FriendsLobby{"l", "self", "Builders", "idle", {}, 8U,
                                  {{"self", "Me", "online", false}}};
    model.apply_snapshot(std::move(snapshot), now);

    auto operation = model.begin({FriendsLobbyActionKind::start_lobby, {}, {}}, now);
    expect(operation.has_value() && model.phase() == FriendsLobbyPhase::starting,
           "start must enter a bounded starting state");
    expect(!model.complete(operation->generation + 1U, true, {}, now + 1s),
           "a stale callback must not change the current operation");
    model.tick(now + 31s);
    expect(!model.busy() && model.phase() == FriendsLobbyPhase::error && !model.error().empty(),
           "a hung host request must time out into a retryable error");
    expect(!model.complete(operation->generation, true, {}, now + 32s),
           "a completion arriving after timeout must be ignored");
}

void leave_and_reenter_cannot_restore_waiting_state() {
    FriendsLobbyMenuModel model;
    const auto now = std::chrono::steady_clock::now();
    model.set_identity("self");
    model.set_service_status(true, {});
    model.enter(now);
    auto snapshot = fixture();
    snapshot.lobby = FriendsLobby{"l", "self", "Builders", "idle", {}, 8U,
                                  {{"self", "Me", "online", false}}};
    model.apply_snapshot(std::move(snapshot), now);
    const auto operation = model.begin({FriendsLobbyActionKind::start_lobby, {}, {}}, now);
    expect(operation.has_value(), "start fixture must begin");
    model.leave();
    model.enter(now + 1s);
    expect(!model.busy() && model.phase() == FriendsLobbyPhase::idle,
           "reopening a cached lobby must not inherit stale Waiting For Host state");
    expect(!model.complete(operation->generation, true, {}, now + 2s),
           "callbacks from a closed screen must be generation rejected");
}

void replacement_buttons_swallow_double_clicks() {
    FriendsLobbyMenuModel model;
    const auto now = std::chrono::steady_clock::now();
    model.set_identity("self");
    model.set_service_status(true, {});
    model.enter(now);
    auto snapshot = fixture();
    snapshot.lobby = FriendsLobby{"l", "self", "Builders", "idle", {}, 8U,
                                  {{"self", "Me", "online", false}}};
    model.apply_snapshot(std::move(snapshot), now);
    const auto operation = model.begin({FriendsLobbyActionKind::start_lobby, {}, {}}, now);
    expect(operation.has_value(), "start fixture must begin");
    expect(model.complete(operation->generation, true, {}, now + 100ms),
           "start completion must apply");
    const auto point = center(model.layout().primary_button);
    model.pointer_press(point, now + 150ms);
    expect(!model.pointer_release(point, now + 150ms).has_value(),
           "the replacement button must be disarmed for the second click");
}

void disconnect_retains_lobby_and_recovers() {
    FriendsLobbyMenuModel model;
    const auto now = std::chrono::steady_clock::now();
    model.set_identity("self");
    model.set_service_status(true, {});
    model.enter(now);
    auto snapshot = fixture();
    snapshot.lobby = FriendsLobby{"l", "self", "Builders", "idle", "host:32887", 8U,
                                  {{"self", "Me", "online", false}}};
    model.apply_snapshot(snapshot, now);
    model.set_service_status(false, "Network unavailable - retrying");
    expect(model.phase() == FriendsLobbyPhase::idle ||
               model.phase() == FriendsLobbyPhase::reconnecting,
           "transport loss must remain a representable state");
    expect(model.snapshot().lobby.has_value() && model.snapshot().lobby->id == "l",
           "transport loss must not erase the last authoritative lobby");
    model.set_service_status(true, {});
    expect(model.phase() == FriendsLobbyPhase::idle,
           "successful polling must recover without rebuilding the menu");
}

void presentation_is_complete_and_valid() {
    FriendsLobbyMenuModel model;
    const auto now = std::chrono::steady_clock::now();
    model.set_identity("self");
    model.set_service_status(true, {});
    model.enter(now);
    model.apply_snapshot(fixture(), now);
    const auto list = FriendsLobbyPresentation{}.build(model, {});
    expect(list.size() > 40U, "friends screen must render a complete two-pane composition");
    for (const auto& command : list.commands()) {
        std::visit([](const auto& value) {
            if constexpr (requires { value.destination; }) {
                expect(value.destination.is_valid(), "draw command has invalid geometry");
            }
        }, command);
    }
}

void friends_surface_never_duplicates_the_match_lobby() {
    FriendsLobbyMenuModel model;
    const auto now = std::chrono::steady_clock::now();
    model.set_identity("self");
    model.set_service_status(true, {});
    model.enter(now);
    auto snapshot = fixture();
    snapshot.lobby = FriendsLobby{"l", "self", "Builders", "idle", {}, 8U,
                                  {{"self", "Me", "online", false}}};
    model.apply_snapshot(std::move(snapshot), now);
    expect(model.visible_invitation_indices().size() == 1U,
           "Friends must keep showing invitations while a Match Lobby is active");

    const auto list = FriendsLobbyPresentation{}.build(model, {});
    bool title_present{};
    bool duplicate_title_present{};
    bool duplicate_roster_present{};
    for (const auto& command : list.commands()) {
        const auto* text = std::get_if<ui::TextDrawCommand>(&command);
        if (text == nullptr) continue;
        title_present = title_present || text->localization_key == "FRIENDS";
        duplicate_title_present = duplicate_title_present ||
                                  text->localization_key == "FRIENDS & LOBBY";
        duplicate_roster_present = duplicate_roster_present ||
                                   text->localization_key == "Me";
    }
    expect(title_present && !duplicate_title_present && !duplicate_roster_present,
           "Friends must remain a social/invitation page, not render a second lobby roster");

    const auto primary = center(model.layout().primary_button);
    model.pointer_press(primary, now + 200ms);
    expect(!model.pointer_release(primary, now + 200ms).has_value(),
           "an active lobby must be opened and controlled only by Match Lobby");
}

void background_refresh_does_not_hide_an_action_error() {
    FriendsLobbyMenuModel model;
    const auto now = std::chrono::steady_clock::now();
    model.set_identity("self");
    model.set_service_status(true, {});
    model.enter(now);
    model.apply_snapshot(fixture(), now);
    const auto operation = model.begin({FriendsLobbyActionKind::search, {}, "Nobody"}, now);
    expect(operation.has_value(), "search fixture must begin");
    expect(model.complete(operation->generation, false, "No exact player match.", now + 1s),
           "search error must complete");
    model.apply_snapshot(fixture(), now + 2s);
    expect(model.phase() == FriendsLobbyPhase::error &&
               model.error() == "No exact player match.",
           "a background poll must not erase a user-action error before it is readable");
}

void long_friend_lists_scroll_with_stable_rows() {
    FriendsLobbyMenuModel model;
    const auto now = std::chrono::steady_clock::now();
    model.set_identity("self");
    model.set_service_status(true, {});
    model.enter(now);
    FriendsLobbySnapshot snapshot;
    for (std::size_t index{}; index < 20U; ++index) {
        snapshot.friends.push_back({"friend-" + std::to_string(index),
                                    "Friend " + std::to_string(index),
                                    "online", "accepted", {}, {}});
    }
    model.apply_snapshot(std::move(snapshot), now);
    expect(model.scroll_rows(50) && model.first_visible_friend_row() == 11U,
           "friend scrolling must clamp at the last full page");
    expect(!model.scroll_rows(1), "scrolling beyond the final page must be inert");
    expect(model.scroll_rows(-50) && model.first_visible_friend_row() == 0U,
           "friend scrolling must clamp at the first page");
}

void server_publication_only_completes_the_start_operation() {
    FriendsLobbyMenuModel model;
    const auto now = std::chrono::steady_clock::now();
    model.set_identity("self");
    model.set_service_status(true, {});
    model.enter(now);
    auto snapshot = fixture();
    snapshot.lobby = FriendsLobby{"l", "self", "Builders", "starting", {}, 8U,
                                  {{"self", "Me", "online", false}}};
    model.apply_snapshot(snapshot, now);
    const auto search = model.begin({FriendsLobbyActionKind::search, {}, "Alpha"}, now);
    expect(search.has_value(), "search fixture must begin");
    snapshot.lobby->server_id = "relay-id";
    model.apply_snapshot(snapshot, now + 1s);
    expect(model.operation().has_value() &&
               model.operation()->generation == search->generation,
           "server publication must not cancel an unrelated social action");
}

} // namespace

int main() {
    try {
        selection_survives_reorder_and_bad_rows();
        operations_timeout_and_reject_stale_results();
        leave_and_reenter_cannot_restore_waiting_state();
        replacement_buttons_swallow_double_clicks();
        disconnect_retains_lobby_and_recovers();
        presentation_is_complete_and_valid();
        friends_surface_never_duplicates_the_match_lobby();
        background_refresh_does_not_hide_an_action_error();
        long_friend_lists_scroll_with_stable_rows();
        server_publication_only_completes_the_start_operation();
        std::cout << "friends/lobby tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
