#include "battlespades/frontend/friends_lobby_menu.hpp"
#include "battlespades/frontend/friends_lobby_presentation.hpp"

#include <algorithm>
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

[[nodiscard]] std::optional<FriendsLobbyIntent> click(
    FriendsLobbyMenuModel& model,
    ui::Point point,
    std::chrono::steady_clock::time_point now) {
    model.pointer_press(point, now);
    return model.pointer_release(point, now);
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
    model.set_tab(FriendsLobbyTab::friends);
    expect(model.selected_friend_id() == "a", "Reselecting the active tab must preserve the selected friend");

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
    expect(std::ranges::any_of(list.commands(), [](const auto& command) {
        const auto* image = std::get_if<ui::SpriteDrawCommand>(&command);
        return image && image->asset_id == "png/ui/common_elements/frames/ui_frame_large.png";
    }), "Friends must use the bundled retail menu frame");
    expect(std::ranges::any_of(list.commands(), [](const auto& command) {
        const auto* label = std::get_if<ui::TextDrawCommand>(&command);
        return label && label->localization_key == "FRIENDS" &&
               label->preferred_font_asset == "fonts/Spades.ttf" &&
               label->transform == ui::TextTransform::uppercase;
    }), "Friends heading must retain the retail title typeface");
    expect(std::ranges::any_of(list.commands(), [](const auto& command) {
        const auto* label = std::get_if<ui::TextDrawCommand>(&command);
        return label && label->localization_key == "SEARCH";
    }), "Profile lookup must have an explicit Search button label");
    for (const auto& command : list.commands()) {
        std::visit([](const auto& value) {
            if constexpr (requires { value.destination; }) {
                expect(value.destination.is_valid(), "draw command has invalid geometry");
            }
        }, command);
    }

    const std::string unavailable =
        "The friends service is reconnecting. Invitations and your current lobby are preserved while we retry.";
    model.set_service_status(false, unavailable);
    const auto offline = FriendsLobbyPresentation{}.build(model, {});
    const auto notice = std::ranges::find_if(offline.commands(), [&](const auto& command) {
        const auto* text = std::get_if<ui::TextDrawCommand>(&command);
        return text != nullptr && text->localization_key == unavailable;
    });
    expect(notice != offline.commands().end(), "offline friends status must remain visible");
    const auto& body = std::get<ui::TextDrawCommand>(*notice);
    constexpr auto scale = FriendsLobbyMenuModel::subpixels_per_pixel;
    expect(body.layout == ui::TextLayout::bounded_wrapped_lines && body.maximum_lines == 2U &&
               body.destination.x + body.destination.width < model.layout().primary_button.x / scale &&
               body.destination.y + body.destination.height < model.layout().back_button.y / scale,
           "wrapped friends status must stay clear of action buttons and Back");
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
    const auto open = model.pointer_release(primary, now + 200ms);
    expect(open && open->kind == FriendsLobbyActionKind::open_lobby && open->target_id == "l",
           "Friends must provide a route back to the existing Match Lobby");
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

void backend_profile_matches_are_not_hidden_by_display_name_filtering() {
    FriendsLobbyMenuModel model;
    const auto now = std::chrono::steady_clock::now();
    model.set_identity("self");
    model.set_service_status(true, {});
    model.enter(now);

    const auto search = model.layout().search_field;
    const ui::Point search_point{search.x + 4, search.y + 4};
    model.pointer_press(search_point, now + 200ms);
    static_cast<void>(model.pointer_release(search_point, now + 210ms));
    expect(model.append_search_text("login_name"), "search fixture must accept text");
    model.set_search_results({
        {"profile", "Completely Different Nickname", "online", "none", "none", {}, {}},
    });
    expect(model.visible_friend_indices().size() == 1U,
           "an authoritative username/ID match must remain visible even when its nickname differs");
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

void friend_and_invitation_buttons_emit_the_exact_backend_contract() {
    FriendsLobbyMenuModel model;
    const auto now = std::chrono::steady_clock::now();
    model.set_identity("self");
    model.set_service_status(true, {});
    model.enter(now);
    FriendsLobbySnapshot snapshot;
    snapshot.friends = {
        {"incoming", "Incoming", "online", "pending", "incoming", {}, {}},
        {"outgoing", "Outgoing", "online", "pending", "outgoing", {}, {}},
    };
    snapshot.invitations = {{"invite", "lobby", "Builders", "Friend"}};
    model.apply_snapshot(snapshot, now);
    model.set_tab(FriendsLobbyTab::requests);

    const auto list = model.layout().friends_list;
    const auto row_height = list.height /
                            static_cast<std::int32_t>(FriendsLobbyMenuModel::visible_rows);
    const ui::Point first{list.x + 10, list.y + row_height / 2};
    const ui::Point second{list.x + 10, list.y + row_height + row_height / 2};
    static_cast<void>(click(model, first, now + 200ms));
    const auto accept = click(model, center(model.layout().primary_button), now + 210ms);
    expect(accept.has_value() &&
               accept->kind == FriendsLobbyActionKind::accept_friend_request &&
               accept->target_id == "incoming",
           "incoming friend requests must emit Accept for the requester id");

    static_cast<void>(click(model, second, now + 220ms));
    const auto cancel = click(model, center(model.layout().secondary_button), now + 230ms);
    expect(cancel.has_value() && cancel->kind == FriendsLobbyActionKind::remove_friend &&
               cancel->target_id == "outgoing",
           "outgoing requests must emit Remove/Cancel, never Decline");

    model.set_tab(FriendsLobbyTab::invitations);
    const auto lobby = model.layout().lobby_panel;
    const ui::Point invitation{lobby.x + 20 * FriendsLobbyMenuModel::subpixels_per_pixel, lobby.y + 60 * FriendsLobbyMenuModel::subpixels_per_pixel};
    static_cast<void>(click(model, invitation, now + 240ms));
    const auto join = click(model, center(model.layout().primary_button), now + 250ms);
    expect(join.has_value() &&
               join->kind == FriendsLobbyActionKind::accept_lobby_invite &&
               join->target_id == "invite",
           "lobby invitations must join by stable invitation id");
    const auto decline = click(model, center(model.layout().secondary_button), now + 260ms);
    expect(decline.has_value() &&
               decline->kind == FriendsLobbyActionKind::decline_lobby_invite &&
               decline->target_id == "invite",
           "lobby invitation decline must retain the stable invitation id");
}

void inviting_without_a_lobby_creates_and_invites_atomically() {
    FriendsLobbyMenuModel model;
    const auto now = std::chrono::steady_clock::now();
    model.set_identity("self");
    model.set_service_status(true, {});
    model.enter(now);

    FriendsLobbySnapshot snapshot;
    snapshot.friends = {
        {"friend", "Offline Friend", "offline", "accepted", "accepted", {}, {}},
    };
    model.apply_snapshot(std::move(snapshot), now);
    const auto list = model.layout().friends_list;
    const auto row_height = list.height /
                            static_cast<std::int32_t>(FriendsLobbyMenuModel::visible_rows);
    static_cast<void>(click(model, {list.x + 10, list.y + row_height / 2}, now + 200ms));
    const auto invite = click(model, center(model.layout().primary_button), now + 210ms);
    expect(invite.has_value() &&
               invite->kind == FriendsLobbyActionKind::create_lobby &&
               invite->target_id == "friend",
           "an accepted friend must create a lobby and invite in one operation");

    const auto presentation = FriendsLobbyPresentation{}.build(model, {});
    expect(std::ranges::any_of(presentation.commands(), [](const auto& command) {
               const auto* text = std::get_if<ui::TextDrawCommand>(&command);
               return text != nullptr && text->localization_key == "CREATE + INVITE";
           }),
           "the primary button must explain the atomic create-and-invite flow");
}

void accepted_friends_route_to_their_authoritative_lobby_or_server() {
    FriendsLobbyMenuModel model;
    const auto now = std::chrono::steady_clock::now();
    model.set_identity("self");
    model.set_service_status(true, {});
    model.enter(now);

    FriendsLobbySnapshot snapshot;
    snapshot.friends = {
        {"playing", "Playing", "in_game", "accepted", "accepted", "lobby-a", "relay-a"},
        {"waiting", "Waiting", "in_lobby", "accepted", "accepted", "lobby-b", {}},
        {"online", "Online", "online", "accepted", "accepted", {}, {}},
    };
    model.apply_snapshot(snapshot, now);
    const auto list = model.layout().friends_list;
    const auto row_height = list.height /
                            static_cast<std::int32_t>(FriendsLobbyMenuModel::visible_rows);

    static_cast<void>(click(model, {list.x + 10, list.y + row_height / 2}, now + 200ms));
    const auto join_game = click(model, center(model.layout().primary_button), now + 210ms);
    expect(join_game.has_value() && join_game->kind == FriendsLobbyActionKind::join_friend_lobby &&
               join_game->target_id == "lobby-a",
           "Join Friend must establish lobby membership before following its published server");

    static_cast<void>(click(model,
                            {list.x + 10, list.y + row_height + row_height / 2},
                            now + 230ms));
    const auto join_lobby = click(model, center(model.layout().primary_button), now + 240ms);
    expect(join_lobby.has_value() &&
               join_lobby->kind == FriendsLobbyActionKind::join_friend_lobby &&
               join_lobby->target_id == "lobby-b",
           "Join Friend must use the authoritative lobby identifier before a server exists");

    model.apply_snapshot(snapshot, now + 250ms);
    static_cast<void>(click(model,
                            {list.x + 10, list.y + row_height * 2 + row_height / 2},
                            now + 260ms));
    const auto create = click(model, center(model.layout().primary_button), now + 270ms);
    expect(create.has_value() && create->kind == FriendsLobbyActionKind::create_lobby &&
               create->target_id == "online",
           "an online accepted friend without a lobby must use atomic Create + Invite");
}

void authoritative_snapshots_complete_lost_write_responses() {
    const auto now = std::chrono::steady_clock::now();

    FriendsLobbyMenuModel friend_model;
    friend_model.set_identity("self");
    friend_model.set_service_status(true, {});
    friend_model.enter(now);
    FriendsLobbySnapshot incoming;
    incoming.friends = {
        {"requester", "Requester", "online", "pending", "incoming", {}, {}}};
    friend_model.apply_snapshot(incoming, now);
    const auto accept = friend_model.begin(
        {FriendsLobbyActionKind::accept_friend_request, "requester", {}}, now);
    expect(accept.has_value(), "friend acceptance fixture must begin");
    incoming.friends.front().relationship = "accepted";
    incoming.friends.front().direction = "accepted";
    friend_model.apply_snapshot(incoming, now + 1ms);
    expect(!friend_model.busy() && friend_model.error().empty(),
           "an accepted authoritative friendship must complete a lost Accept response");

    FriendsLobbyMenuModel create_model;
    create_model.set_identity("self");
    create_model.set_service_status(true, {});
    create_model.enter(now);
    const auto create = create_model.begin(
        {FriendsLobbyActionKind::create_lobby, {}, {}}, now);
    expect(create.has_value(), "lobby creation fixture must begin");
    FriendsLobbySnapshot created;
    created.lobby = FriendsLobby{"lobby", "self", "Lobby", "forming", {}, 8U,
                                  {{"self", "Self", "in_lobby", false}}};
    create_model.apply_snapshot(created, now + 1ms);
    expect(!create_model.busy() && create_model.snapshot().lobby.has_value(),
           "an authoritative membership must complete a lost Create response");

    const auto start = create_model.begin(
        {FriendsLobbyActionKind::start_lobby, {}, {}}, now + 2ms);
    expect(start.has_value(), "lobby start fixture must begin");
    created.lobby->state = "starting";
    create_model.apply_snapshot(created, now + 3ms);
    expect(!create_model.busy() &&
               create_model.phase() == FriendsLobbyPhase::waiting_for_host,
           "the authoritative starting state must complete a lost Start response");

    const auto leave = create_model.begin(
        {FriendsLobbyActionKind::leave_lobby, {}, {}}, now + 4ms);
    expect(leave.has_value(), "lobby leave fixture must begin");
    created.lobby.reset();
    create_model.apply_snapshot(created, now + 5ms);
    expect(!create_model.busy() && !create_model.snapshot().lobby.has_value(),
           "an absent authoritative membership must complete a lost Leave response");
}

void action_state_and_hit_testing_stay_in_sync() {
    FriendsLobbyMenuModel model;
    const auto now = std::chrono::steady_clock::now();
    model.set_identity("self"); model.set_service_status(true, {}); model.enter(now);
    auto snapshot = fixture();
    snapshot.lobby = FriendsLobby{"l", "self", "Builders", "forming", {}, 8U, {}};
    model.apply_snapshot(snapshot, now);
    const auto list = model.layout().friends_list;
    static_cast<void>(click(model, {list.x + 20, list.y + list.height / 18}, now + 200ms));
    expect(model.primary_button().label == "INVITE TO LOBBY" && model.primary_button().intent.has_value(),
           "An online friend in an active lobby must have a visibly enabled Invite");
    const auto operation = model.begin(*model.primary_button().intent, now + 210ms);
    expect(operation && !model.primary_button().intent && !model.secondary_button().intent,
           "Both action buttons must disable immediately while a write is pending");
    expect(!click(model, center(model.layout().primary_button), now + 220ms), "Busy Invite accepted another click");
    expect(model.complete(operation->generation, true, {}, now + 300ms), "Invite did not complete");
    model.apply_snapshot(snapshot, now + 301ms);
    expect(!click(model, center(model.layout().primary_button), now + 310ms),
           "Background snapshots must not disarm double-click protection");
    model.set_service_status(false, "Offline");
    expect(!model.primary_button().intent && !model.secondary_button().intent &&
           !click(model, center(model.layout().secondary_button), now + 1s), "Offline actions remain enabled");
    model.set_service_status(true, {});
    model.pointer_press(center(model.layout().primary_button), now + 2s);
    snapshot.friends[0].current_server_id = "new-game";
    model.apply_snapshot(snapshot, now + 2s);
    expect(!model.pointer_release(center(model.layout().primary_button), now + 2s),
           "An Invite pressed before refresh must not become Join Game on release");
}

void invitation_pages_reach_the_last_row_and_exclude_the_header() {
    FriendsLobbyMenuModel model;
    const auto now = std::chrono::steady_clock::now();
    model.set_service_status(true, {}); model.enter(now); model.set_tab(FriendsLobbyTab::invitations);
    FriendsLobbySnapshot snapshot;
    for (unsigned i{}; i < 10U; ++i)
        snapshot.invitations.push_back({std::to_string(i), "l", "Lobby", "Friend"});
    model.apply_snapshot(snapshot, now);
    const auto panel = model.layout().lobby_panel;
    static_cast<void>(click(model, {panel.x + 20, panel.y + 10}, now + 200ms));
    expect(model.selected_invitation_id().empty(), "Invitation heading selects an invisible row");
    expect(model.scroll_rows(100) && model.first_visible_invitation_row() == 3U,
           "Invitation scrolling must use its seven rendered rows");
    constexpr auto scale = FriendsLobbyMenuModel::subpixels_per_pixel;
    static_cast<void>(click(model, {panel.x + 20 * scale, panel.y + (53 + 6 * 38 + 18) * scale}, now + 1s));
    expect(model.selected_invitation_id() == "9", "Last invitation cannot be selected");
}

void search_results_cannot_resurrect_removed_friendships() {
    FriendsLobbyMenuModel model;
    const auto now = std::chrono::steady_clock::now();
    model.set_identity("self"); model.set_service_status(true, {}); model.enter(now);
    auto snapshot = fixture();
    model.apply_snapshot(snapshot, now);
    static_cast<void>(click(model, center(model.layout().search_field), now + 200ms));
    expect(model.append_search_text("Alpha"), "Search fixture must accept text");
    model.set_search_results({snapshot.friends.front()});
    const auto remove = model.begin({FriendsLobbyActionKind::remove_friend, "a", {}}, now + 210ms);
    expect(remove.has_value(), "Remove fixture must start");
    std::erase_if(snapshot.friends, [](const auto& row) { return row.id == "a"; });
    model.apply_snapshot(snapshot, now + 250ms);
    expect(!model.busy(), "An old search result must not prevent removal reconciliation");
    const auto former = std::ranges::find(model.snapshot().friends, "a", &FriendsLobbyFriend::id);
    expect(former != model.snapshot().friends.end() && former->relationship == "search",
           "Removed friendship may remain discoverable but cannot retain friend permissions");
    expect(model.erase_search_code_point(), "Backspace must edit the query");
    expect(std::ranges::find(model.snapshot().friends, "a", &FriendsLobbyFriend::id) ==
               model.snapshot().friends.end(),
           "Backspace must invalidate all prior-query search results");
    model.clear_search();
    expect(model.snapshot().friends.size() == snapshot.friends.size(),
           "Clearing a query must restore only authoritative friends");
}

void lookup_results_are_bound_to_the_current_generation_and_query() {
    FriendsLobbyMenuModel model;
    const auto now = std::chrono::steady_clock::now();
    model.set_identity("self"); model.set_service_status(true, {}); model.enter(now);
    static_cast<void>(click(model, center(model.layout().search_field), now + 200ms));
    expect(model.append_search_text("login"), "Lookup fixture query must be entered");
    const auto old = model.begin({FriendsLobbyActionKind::search, {}, "login"}, now + 210ms);
    expect(old.has_value(), "Old lookup must begin");
    model.leave(); model.enter(now + 300ms);
    const auto current = model.begin({FriendsLobbyActionKind::search, {}, "login"}, now + 400ms);
    expect(current.has_value(), "A reopened menu must start a distinct lookup");
    const std::vector<FriendsLobbyFriend> rows{{"profile", "Different nickname", "online", "none"}};
    expect(!model.apply_search_results(old->generation, "login", rows, now + 500ms),
           "A closed menu's lookup must never replace current results");
    expect(!model.apply_search_results(current->generation, "other", rows, now + 500ms),
           "A response for another query must never replace current results");
    model.set_tab(FriendsLobbyTab::requests);
    expect(model.apply_search_results(current->generation, "login", rows, now + 500ms) &&
               model.tab() == FriendsLobbyTab::friends && model.visible_friend_indices().size() == 1U,
           "A successful lookup must reveal matching profiles even from Requests");
    expect(model.complete(current->generation, true, {}, now + 600ms), "Lookup must finish");
    const auto late = model.begin({FriendsLobbyActionKind::search, {}, "login"}, now + 1s);
    expect(late && !model.apply_search_results(late->generation, "login", rows, now + 14s) &&
               !model.busy() && !model.error().empty(),
           "Expired lookup results must be rejected even before the next model tick");
    model.set_identity("another-account");
    expect(model.search_text().empty() && model.snapshot().friends.empty(),
           "Switching accounts must clear the previous account's search state");
}

void membership_reconciliation_requires_the_requested_lobby() {
    FriendsLobbyMenuModel model;
    const auto now = std::chrono::steady_clock::now();
    model.set_identity("self"); model.set_service_status(true, {}); model.enter(now);
    auto snapshot = fixture();
    model.apply_snapshot(snapshot, now);
    const auto join = model.begin({FriendsLobbyActionKind::accept_lobby_invite, "i", {}}, now);
    expect(join.has_value(), "Invite fixture must begin");
    snapshot.lobby = FriendsLobby{"other", "other-host", "Other", "forming"};
    model.apply_snapshot(snapshot, now + 1s);
    expect(model.busy(), "Unrelated membership must not complete an invitation acceptance");
    snapshot.lobby->id = "l";
    snapshot.lobby->owner_id = "self";
    snapshot.invitations.clear();
    model.apply_snapshot(snapshot, now + 2s);
    expect(!model.busy(), "The requested membership must settle even after its invite disappears");
    const auto start = model.begin({FriendsLobbyActionKind::start_lobby, {}, {}}, now + 3s);
    expect(start.has_value(), "Start fixture must begin");
    snapshot.lobby->id = "another";
    snapshot.lobby->state = "starting";
    model.apply_snapshot(snapshot, now + 4s);
    expect(model.busy(), "Another lobby's start must not complete the pending start");
    expect(!model.complete(start->generation, true, {}, now + 34s) && !model.busy(),
           "Late direct completions must respect operation deadlines without requiring tick first");
}

void list_refresh_cannot_redirect_a_pressed_row() {
    FriendsLobbyMenuModel model;
    const auto now = std::chrono::steady_clock::now();
    model.set_service_status(true, {}); model.enter(now);
    auto snapshot = fixture();
    snapshot.invitations.push_back({"second", "l2", "Other Lobby", "Beta"});
    model.apply_snapshot(snapshot, now);
    const auto list = model.layout().friends_list;
    const ui::Point first{list.x + 20, list.y + list.height / 18};
    model.pointer_press(first, now + 200ms);
    std::swap(snapshot.friends[0], snapshot.friends[1]);
    model.apply_snapshot(snapshot, now + 210ms);
    static_cast<void>(model.pointer_release(first, now + 220ms));
    expect(model.selected_friend_id().empty(), "A refreshed row cannot select a different friend mid-click");
    model.set_tab(FriendsLobbyTab::invitations);
    const auto panel = model.layout().lobby_panel;
    constexpr auto scale = FriendsLobbyMenuModel::subpixels_per_pixel;
    const ui::Point invitation{panel.x + 20 * scale, panel.y + 60 * scale};
    model.pointer_press(invitation, now + 300ms);
    std::swap(snapshot.invitations[0], snapshot.invitations[1]);
    model.apply_snapshot(snapshot, now + 310ms);
    static_cast<void>(model.pointer_release(invitation, now + 320ms));
    expect(model.selected_invitation_id().empty(), "A refreshed row cannot select a different invitation mid-click");
}

void invite_guards_and_rejoin_actions_follow_membership() {
    FriendsLobbyMenuModel model;
    const auto now = std::chrono::steady_clock::now();
    model.set_identity("self"); model.set_service_status(true, {}); model.enter(now);
    auto snapshot = fixture();
    snapshot.lobby = FriendsLobby{"l", "self", "Full lobby", "forming", {}, 1U,
                                  {{"self", "Me", "online", false}}};
    model.apply_snapshot(snapshot, now);
    const auto list = model.layout().friends_list;
    static_cast<void>(click(model, {list.x + 20, list.y + list.height / 18}, now + 200ms));
    expect(model.primary_button().label == "LOBBY FULL" && !model.primary_button().intent,
           "Full lobbies must not offer an invite that the server cannot accept");
    snapshot.lobby->maximum_members = 8U;
    snapshot.lobby->members.push_back({"a", "Alpha", "online", false});
    model.apply_snapshot(snapshot, now + 300ms);
    expect(model.primary_button().label == "ALREADY IN LOBBY" && !model.primary_button().intent,
           "Existing members must not receive duplicate invite actions");
    snapshot.lobby->members.pop_back();
    snapshot.lobby->state = "in_game";
    snapshot.lobby->server_id = "relay";
    model.apply_snapshot(snapshot, now + 400ms);
    expect(model.primary_button().intent &&
               model.primary_button().intent->kind == FriendsLobbyActionKind::invite_friend,
           "A running match must still allow invitations when it has room");
    model.set_tab(FriendsLobbyTab::requests);
    expect(model.primary_button().intent &&
               model.primary_button().intent->kind == FriendsLobbyActionKind::join_game &&
               model.primary_button().intent->target_id == "relay",
           "An existing member must be able to rejoin the lobby's published game");
    snapshot.lobby->state = "forming"; snapshot.lobby->server_id.clear();
    model.apply_snapshot(snapshot, now + 500ms);
    model.set_service_status(false, "Offline");
    expect(model.primary_button().intent &&
               model.primary_button().intent->kind == FriendsLobbyActionKind::open_lobby,
           "A cached lobby must remain reachable while social reconnects");
}

void retail_button_feedback_and_long_names_stay_bounded() {
    FriendsLobbyMenuModel model;
    const auto now = std::chrono::steady_clock::now();
    model.set_service_status(true, {}); model.enter(now);
    FriendsLobbySnapshot snapshot;
    const std::string long_name(180U, 'W');
    snapshot.friends = {{"long", long_name, "in_game", "accepted", {}, {}, "server"}};
    model.apply_snapshot(snapshot, now);
    const auto button = center(model.layout().primary_button);
    model.pointer_press(button, now + 200ms);
    expect(model.control_hovered(FriendsLobbyControl::primary) &&
               model.control_pressed(FriendsLobbyControl::primary),
           "Enabled retail buttons must expose both hover and pressed feedback");
    const auto pressed = FriendsLobbyPresentation{}.build(model, {});
    expect(std::ranges::any_of(pressed.commands(), [](const auto& command) {
        const auto* image = std::get_if<ui::SpriteDrawCommand>(&command);
        return image && image->asset_id ==
            "png/ui/common_elements/buttons/button_large_start_mid_press.png";
    }), "Primary action must render the bundled pressed button slices");
    const auto name = std::ranges::find_if(pressed.commands(), [&](const auto& command) {
        const auto* label = std::get_if<ui::TextDrawCommand>(&command);
        return label && label->localization_key == long_name;
    });
    expect(name != pressed.commands().end(), "Long player names must remain present");
    const auto& label = std::get<ui::TextDrawCommand>(*name);
    expect(label.fit == ui::TextFit::none && label.layout == ui::TextLayout::bounded_wrapped_lines &&
               label.transform == ui::TextTransform::preserve &&
               label.maximum_lines == 1U && label.requested_font_size_pixels >= 14.0 &&
               label.destination.x + label.destination.width < 300.0,
           "Long names must clip within their column at readable size, clear of presence labels");
    static_cast<void>(model.pointer_release(button, now + 210ms));
    model.set_service_status(false, "Reconnecting");
    const auto offline = FriendsLobbyPresentation{}.build(model, {});
    expect(std::ranges::any_of(offline.commands(), [](const auto& command) {
        const auto* image = std::get_if<ui::SpriteDrawCommand>(&command);
        return image && image->asset_id ==
            "png/ui/common_elements/buttons/button_large_mid_desat.png";
    }), "Unavailable buttons must use the bundled disabled texture treatment");
}

void keyboard_selection_reveals_rows_and_wheel_targets_its_panel() {
    FriendsLobbyMenuModel model;
    const auto now = std::chrono::steady_clock::now();
    model.set_identity("self");
    model.set_service_status(true, {});
    model.enter(now);
    FriendsLobbySnapshot snapshot;
    for (unsigned index{}; index < 12U; ++index)
        snapshot.friends.push_back({"friend-" + std::to_string(index), "Player " + std::to_string(index), "online"});
    for (unsigned index{}; index < 10U; ++index)
        snapshot.invitations.push_back({"invite-" + std::to_string(index), "lobby", "Lobby", "Host"});
    model.apply_snapshot(snapshot, now);
    static_cast<void>(click(model, center(model.layout().search_field), now + 200ms));
    expect(model.search_focused(), "search fixture must own text entry");
    expect(model.move_selection(1) && model.selected_friend_id() == "friend-0" && !model.search_focused(),
           "keyboard list navigation must select a player and leave search text entry");
    for (unsigned index{}; index < 10U; ++index) static_cast<void>(model.move_selection(1));
    expect(model.selected_friend_id() == "friend-10" && model.first_visible_friend_row() == 2U,
           "keyboard navigation must reveal the selected row beyond the first page");
    const auto primary = model.primary_button();
    expect(primary.intent && primary.intent->target_id == "friend-10",
           "keyboard selection must feed the same primary action as a mouse selection");

    expect(model.scroll_rows_at(2, center(model.layout().lobby_panel)) &&
               model.first_visible_invitation_row() == 2U && model.first_visible_friend_row() == 2U &&
               model.tab() == FriendsLobbyTab::friends,
           "wheel over invitations must scroll that visible panel without changing the Friends tab");
    expect(model.scroll_rows_at(-1, center(model.layout().friends_list)) &&
               model.first_visible_friend_row() == 1U && model.first_visible_invitation_row() == 2U,
           "wheel over friends must not move invitations");
    expect(!model.scroll_rows_at(1, center(model.layout().back_button)),
           "wheel over navigation must not move either list");

    expect(model.cycle_tab(1) && model.tab() == FriendsLobbyTab::requests &&
               model.selected_friend_id().empty(), "keyboard tab changes must clear the old target");
    expect(!model.move_selection(1), "an empty request list must not select an invisible player");
    expect(model.cycle_tab(1) && model.tab() == FriendsLobbyTab::invitations && model.move_selection(1),
           "keyboard navigation must reach invitations");
    for (unsigned index{}; index < 9U; ++index) static_cast<void>(model.move_selection(1));
    expect(model.selected_invitation_id() == "invite-9" && model.first_visible_invitation_row() == 3U &&
               !model.move_selection(1), "keyboard invitation navigation must reveal and clamp at its final row");
    const auto invitation_action = model.primary_button();
    expect(invitation_action.intent && invitation_action.intent->kind == FriendsLobbyActionKind::accept_lobby_invite &&
               invitation_action.intent->target_id == "invite-9", "keyboard Accept must target the selected invitation");
    expect(model.cycle_tab(1) && model.tab() == FriendsLobbyTab::friends &&
               model.cycle_tab(-1) && model.tab() == FriendsLobbyTab::invitations,
           "tab cycling must wrap in both directions");
}

void search_results_keep_pending_relationship_actions_visible() {
    FriendsLobbyMenuModel model;
    model.set_identity("self");
    model.set_service_status(true, {});
    model.set_tab(FriendsLobbyTab::requests);
    model.set_search_results({
        {"incoming", "Incoming player", "online", "pending", "incoming"},
        {"outgoing", "Outgoing player", "offline", "pending", "outgoing"}});
    expect(model.tab() == FriendsLobbyTab::friends && model.visible_friend_indices().size() == 2U,
           "search must display matched profiles even when they already have a pending request");
    expect(model.move_selection(1), "incoming search result must be selectable");
    const auto incoming = model.primary_button();
    expect(incoming.intent && incoming.intent->kind == FriendsLobbyActionKind::accept_friend_request &&
               model.secondary_button().intent &&
               model.secondary_button().intent->kind == FriendsLobbyActionKind::decline_friend_request,
           "incoming lookup profiles must expose Accept and Decline, not another Add Friend action");
    expect(model.move_selection(1), "outgoing search result must be selectable");
    expect(model.primary_button().label == "REQUEST SENT" && !model.primary_button().intent &&
               model.secondary_button().intent &&
               model.secondary_button().intent->kind == FriendsLobbyActionKind::remove_friend,
           "outgoing lookup profiles must expose pending state and Cancel Request");
}

} // namespace

int main() {
    try {
        keyboard_selection_reveals_rows_and_wheel_targets_its_panel();
        search_results_keep_pending_relationship_actions_visible();
        search_results_cannot_resurrect_removed_friendships();
        lookup_results_are_bound_to_the_current_generation_and_query();
        membership_reconciliation_requires_the_requested_lobby();
        list_refresh_cannot_redirect_a_pressed_row();
        invite_guards_and_rejoin_actions_follow_membership();
        retail_button_feedback_and_long_names_stay_bounded();
        action_state_and_hit_testing_stay_in_sync();
        invitation_pages_reach_the_last_row_and_exclude_the_header();
        selection_survives_reorder_and_bad_rows();
        operations_timeout_and_reject_stale_results();
        leave_and_reenter_cannot_restore_waiting_state();
        replacement_buttons_swallow_double_clicks();
        disconnect_retains_lobby_and_recovers();
        presentation_is_complete_and_valid();
        friends_surface_never_duplicates_the_match_lobby();
        background_refresh_does_not_hide_an_action_error();
        long_friend_lists_scroll_with_stable_rows();
        backend_profile_matches_are_not_hidden_by_display_name_filtering();
        server_publication_only_completes_the_start_operation();
        friend_and_invitation_buttons_emit_the_exact_backend_contract();
        inviting_without_a_lobby_creates_and_invites_atomically();
        accepted_friends_route_to_their_authoritative_lobby_or_server();
        authoritative_snapshots_complete_lost_write_responses();
        std::cout << "friends/lobby tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
