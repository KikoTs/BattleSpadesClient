#include "battlespades/frontend/friends_lobby_presentation.hpp"
#include "battlespades/frontend/menu_status.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace battlespades::frontend {
namespace {

using ui::ColorRgba8;
using ui::DrawRect;
using ui::DrawSpace;
using ui::HorizontalTextAlignment;
using ui::SpriteDrawCommand;
using ui::SpriteSizing;
using ui::TextDrawCommand;
using ui::TextFit;
using ui::TextLayout;
using ui::TextTransform;
using ui::TextureAnchor;
using ui::TextureFilter;
using ui::VerticalTextAlignment;

constexpr double scale{static_cast<double>(FriendsLobbyMenuModel::subpixels_per_pixel)};
constexpr ColorRgba8 white{255U, 255U, 255U, 255U};
constexpr ColorRgba8 cream{244U, 236U, 187U, 255U};
constexpr ColorRgba8 gold{215U, 189U, 83U, 255U};
constexpr ColorRgba8 black{20U, 20U, 12U, 255U};
constexpr ColorRgba8 panel{27U, 30U, 1U, 255U};
constexpr ColorRgba8 row_light{57U, 53U, 44U, 255U};
constexpr ColorRgba8 row_dark{24U, 21U, 14U, 255U};
constexpr ColorRgba8 green{174U, 209U, 77U, 255U};
constexpr ColorRgba8 amber{239U, 180U, 76U, 255U};
constexpr ColorRgba8 red{239U, 115U, 87U, 255U};
constexpr ColorRgba8 muted{161U, 157U, 127U, 255U};

constexpr std::array<std::array<std::string_view, 3U>, 4U> button_assets{{
    {{"png/ui/common_elements/buttons/button_large_left.png",
      "png/ui/common_elements/buttons/button_large_mid.png",
      "png/ui/common_elements/buttons/button_large_right.png"}},
    {{"png/ui/common_elements/buttons/button_large_hover_left.png",
      "png/ui/common_elements/buttons/button_large_hover_mid.png",
      "png/ui/common_elements/buttons/button_large_hover_right.png"}},
    {{"png/ui/common_elements/buttons/button_large_press_left.png",
      "png/ui/common_elements/buttons/button_large_press_mid.png",
      "png/ui/common_elements/buttons/button_large_press_right.png"}},
    {{"png/ui/common_elements/buttons/button_large_left_desat.png",
      "png/ui/common_elements/buttons/button_large_mid_desat.png",
      "png/ui/common_elements/buttons/button_large_right_desat.png"}},
}};
constexpr std::array<std::array<std::string_view, 3U>, 3U> primary_assets{{
    {{"png/ui/common_elements/buttons/button_large_start_left_default.png",
      "png/ui/common_elements/buttons/button_large_start_mid_default.png",
      "png/ui/common_elements/buttons/button_large_start_right_default.png"}},
    {{"png/ui/common_elements/buttons/button_large_start_left_hover.png",
      "png/ui/common_elements/buttons/button_large_start_mid_hover.png",
      "png/ui/common_elements/buttons/button_large_start_right_hover.png"}},
    {{"png/ui/common_elements/buttons/button_large_start_left_press.png",
      "png/ui/common_elements/buttons/button_large_start_mid_press.png",
      "png/ui/common_elements/buttons/button_large_start_right_press.png"}},
}};

[[nodiscard]] DrawRect draw_rect(ui::Rect value) noexcept {
    return {static_cast<double>(value.x) / scale, static_cast<double>(value.y) / scale,
            static_cast<double>(value.width) / scale, static_cast<double>(value.height) / scale};
}

[[nodiscard]] SpriteDrawCommand sprite(std::string_view asset, DrawRect destination,
                                       ColorRgba8 tint = white,
                                       std::uint16_t intensity = 1'000U,
                                       DrawSpace space = DrawSpace::design_pixels,
                                       SpriteSizing sizing = SpriteSizing::stretch) {
    return {std::string{asset}, destination, space, TextureFilter::linear,
            TextureAnchor::top_left, 1.0, sizing, {tint, intensity, 1'000U}};
}

void solid(ui::DrawList& list, DrawRect bounds, ColorRgba8 tint) {
    list.push(sprite(friends_lobby_assets::white_pixel, bounds, tint));
}

[[nodiscard]] TextDrawCommand text(std::string_view value, DrawRect bounds, double size,
                                   ColorRgba8 tint = cream,
                                   HorizontalTextAlignment alignment = HorizontalTextAlignment::left,
                                   std::string_view font = friends_lobby_assets::body_font) {
    return {std::string{value}, std::string{font}, bounds, DrawSpace::design_pixels,
            size, 0.0, 1U, alignment, VerticalTextAlignment::retail_center,
            font == friends_lobby_assets::title_font ? TextTransform::uppercase : TextTransform::preserve,
            TextFit::shrink_to_fit, {tint, 1'000U, 1'000U}};
}

void bounded_text(ui::DrawList& list, std::string_view value, DrawRect bounds,
                  double size = 13.0, ColorRgba8 tint = cream, std::uint8_t lines = 1U,
                  HorizontalTextAlignment alignment = HorizontalTextAlignment::left,
                  std::string_view font = friends_lobby_assets::body_font) {
    auto command = text(value, bounds, size, tint, alignment, font);
    command.layout = TextLayout::bounded_wrapped_lines;
    command.maximum_lines = lines;
    command.fit = TextFit::none;
    command.vertical_alignment = VerticalTextAlignment::top;
    list.push(std::move(command));
}

void panel_surface(ui::DrawList& list, DrawRect bounds) {
    solid(list, bounds, panel);
    list.push(sprite(friends_lobby_assets::panel, bounds));
}

void selected_row(ui::DrawList& list, DrawRect bounds, bool selected, std::size_t index) {
    solid(list, bounds, index % 2U == 0U ? row_light : row_dark);
    if (selected) {
        list.push(sprite(friends_lobby_assets::selection_line, bounds));
        list.push(sprite(friends_lobby_assets::selection_glow, bounds));
    }
}

void button(ui::DrawList& list, const FriendsLobbyMenuModel& model,
            FriendsLobbyControl control, DrawRect bounds, std::string_view label,
            bool enabled, bool primary = false) {
    const auto state = !enabled ? 3U : model.control_pressed(control) ? 2U
                                     : model.control_hovered(control) ? 1U : 0U;
    const auto& assets = primary && enabled ? primary_assets[state] : button_assets[state];
    const auto cap = std::floor(bounds.height * 36.0 / 58.0);
    const std::array destinations{
        DrawRect{bounds.x, bounds.y, cap + 1.0, bounds.height},
        DrawRect{bounds.x + cap, bounds.y, bounds.width - cap * 2.0 + 1.0, bounds.height},
        DrawRect{bounds.x + bounds.width - cap, bounds.y, cap, bounds.height}};
    for (std::size_t index{}; index < assets.size(); ++index)
        list.push(sprite(assets[index], destinations[index], white, enabled ? 1'000U : 700U));
    list.push(text(label, {bounds.x + 9.0, bounds.y + 3.0, bounds.width - 18.0, bounds.height - 6.0},
                   bounds.height < 40.0 ? 13.0 : 15.0, enabled ? black : row_dark,
                   HorizontalTextAlignment::center, friends_lobby_assets::title_font));
}

struct FriendState final { std::string_view label; ColorRgba8 color; };

[[nodiscard]] FriendState friend_state(const FriendsLobbyFriend& value) noexcept {
    if (value.relationship == "pending")
        return value.direction == "incoming" ? FriendState{"INCOMING", gold}
                                               : FriendState{"REQUEST SENT", muted};
    if (value.relationship == "search" || value.relationship == "none") return {"FOUND", gold};
    if (value.presence == "offline") return {"OFFLINE", muted};
    if (!value.current_server_id.empty() || value.presence == "in_game") return {"IN GAME", green};
    if (!value.current_lobby_id.empty()) return {"IN LOBBY", green};
    return {"ONLINE", green};
}

[[nodiscard]] std::string_view phase_label(FriendsLobbyPhase phase) noexcept {
    switch (phase) {
    case FriendsLobbyPhase::starting: return "Starting the match...";
    case FriendsLobbyPhase::waiting_for_host: return "Waiting for the host to start the match.";
    case FriendsLobbyPhase::joining: return "Joining the match...";
    case FriendsLobbyPhase::reconnecting: return "Reconnecting to AoSPlay...";
    case FriendsLobbyPhase::error: return "Please retry the action when it becomes available.";
    case FriendsLobbyPhase::idle: return {};
    }
    return {};
}

} // namespace

ui::DrawList FriendsLobbyPresentation::build(
    const FriendsLobbyMenuModel& model, const FriendsLobbyPresentationContext& context) const {
    if (!context.window.is_valid() || context.background_opacity_per_mille > 1'000U)
        throw std::invalid_argument{"invalid friends/lobby presentation context"};

    ui::DrawList list;
    list.reserve(160U);
    const auto& layout = model.layout();
    auto background = sprite(friends_lobby_assets::background,
        {0.0, 0.0, static_cast<double>(context.window.width), static_cast<double>(context.window.height)},
        white, 1'000U, DrawSpace::window_pixels, SpriteSizing::cover);
    background.modulation.opacity_per_mille = context.background_opacity_per_mille;
    list.push(std::move(background));
    list.push(sprite(friends_lobby_assets::frame, draw_rect(layout.frame)));
    list.push(text("FRIENDS", draw_rect(layout.title), 40.0, cream,
                   HorizontalTextAlignment::center, friends_lobby_assets::title_font));

    const auto tabs = draw_rect(layout.tabs);
    // Pending counts on the tabs: an incoming request or invitation used to be
    // invisible until the player happened to open the right tab.
    const auto counted = [](std::string_view label, std::size_t count) {
        return count == 0U ? std::string{label}
                           : std::string{label} + " (" + std::to_string((std::min)(count, std::size_t{99U})) + ")";
    };
    const std::array labels{std::string{"FRIENDS"},
                            counted("REQUESTS", model.incoming_request_count()),
                            counted("INVITES", model.snapshot().invitations.size())};
    const auto tab_width = tabs.width / 3.0;
    for (std::size_t index{}; index < labels.size(); ++index) {
        const bool selected = static_cast<std::size_t>(model.tab()) == index;
        const DrawRect bounds{tabs.x + tab_width * static_cast<double>(index), tabs.y,
                              tab_width - 2.0, tabs.height};
        list.push(sprite(selected ? friends_lobby_assets::active_tab : friends_lobby_assets::inactive_tab,
                         bounds));
        const bool pending = index > 0U && labels[index].find('(') != std::string::npos;
        list.push(text(labels[index], bounds, 15.0, selected || pending ? gold : cream,
                       HorizontalTextAlignment::center, friends_lobby_assets::tab_font));
    }

    const auto search = draw_rect(layout.search_field);
    solid(list, search, black);
    solid(list, {search.x + 1.0, search.y + search.height - 2.0, search.width - 2.0, 1.0},
          model.search_focused() ? gold : muted);
    const auto search_label = model.search_text().empty() ? std::string{"SEARCH PLAYER..."}
                                                         : std::string{model.search_text()};
    bounded_text(list, search_label, {search.x + 9.0, search.y + 6.0, search.width - 18.0, 23.0},
                 14.0, model.search_text().empty() ? muted : cream);
    button(list, model, FriendsLobbyControl::search, draw_rect(layout.search_button), "SEARCH",
           model.service_available() && !model.search_text().empty() && !model.busy());

    const FriendsLobbyFriend* selected_friend{};
    const FriendsLobbyInvitation* selected_invitation{};
    for (const auto& value : model.snapshot().friends)
        if (value.id == model.selected_friend_id()) { selected_friend = &value; break; }
    for (const auto& value : model.snapshot().invitations)
        if (value.id == model.selected_invitation_id()) { selected_invitation = &value; break; }

    const auto friends = draw_rect(layout.friends_list);
    panel_surface(list, friends);
    const auto visible = model.visible_friend_indices();
    const auto first = model.first_visible_friend_row();
    const auto count = std::min<std::size_t>(first < visible.size() ? visible.size() - first : 0U,
                                            FriendsLobbyMenuModel::visible_rows);
    const auto row_height = friends.height / static_cast<double>(FriendsLobbyMenuModel::visible_rows);
    for (std::size_t row{}; row < count; ++row) {
        const auto& value = model.snapshot().friends[visible[first + row]];
        const DrawRect bounds{friends.x + 2.0, friends.y + row_height * static_cast<double>(row) + 1.0,
                              friends.width - 4.0, row_height - 1.0};
        const bool selected = value.id == model.selected_friend_id();
        selected_row(list, bounds, selected, row);
        const auto state = friend_state(value);
        solid(list, {bounds.x + 8.0, bounds.y + 11.0, 6.0, 6.0}, state.color);
        bounded_text(list, value.name, {bounds.x + 23.0, bounds.y + 4.0, bounds.width - 128.0, 23.0},
                     15.0, selected ? white : cream);
        list.push(text(state.label, {bounds.x + bounds.width - 99.0, bounds.y, 91.0, bounds.height},
                       11.0, state.color, HorizontalTextAlignment::right));
    }
    if (count == 0U) {
        const bool invites = model.tab() == FriendsLobbyTab::invitations;
        list.push(sprite(friends_lobby_assets::friends_icon,
                         {friends.x + 145.0, friends.y + 30.0, 50.0, 48.0}));
        const auto heading = invites ? "LOBBY INVITATIONS"
                           : !model.search_text().empty() ? "FIND A PLAYER"
                           : model.tab() == FriendsLobbyTab::requests ? "NO FRIEND REQUESTS"
                                                                     : "PLAY WITH FRIENDS";
        bounded_text(list, heading, {friends.x + 18.0, friends.y + 96.0, friends.width - 36.0, 48.0},
                     19.0, cream, 2U, HorizontalTextAlignment::center, friends_lobby_assets::title_font);
        const bool other_lobby_invite = selected_invitation && model.snapshot().lobby &&
            selected_invitation->lobby_id != model.snapshot().lobby->id;
        const std::string detail = invites && other_lobby_invite
            ? "Open your current lobby and leave it before accepting another invitation."
            : invites && selected_invitation
            ? "From " + selected_invitation->inviter_name + ". Accept to join their lobby."
            : invites ? "Select an invitation on the right, then choose Accept or Decline."
            : !model.search_text().empty() ? "Choose Search to find this player on AoSPlay."
            : model.tab() == FriendsLobbyTab::requests ? "New friend requests will appear here."
            : "Search for a player above and send a friend request. Select an online friend to invite or join.";
        bounded_text(list, detail, {friends.x + 28.0, friends.y + 145.0, friends.width - 56.0, 78.0},
                     13.0, muted, 4U, HorizontalTextAlignment::center);
    }

    const auto lobby = draw_rect(layout.lobby_panel);
    panel_surface(list, lobby);
    list.push(sprite(friends_lobby_assets::panel_header,
                     {lobby.x + 8.0, lobby.y + 7.0, lobby.width - 16.0, 34.0}));
    list.push(text("LOBBY INVITES", {lobby.x + 18.0, lobby.y + 7.0, lobby.width - 71.0, 34.0},
                   19.0, cream, HorizontalTextAlignment::left, friends_lobby_assets::tab_font));
    list.push(text(std::to_string(model.snapshot().invitations.size()),
                   {lobby.x + lobby.width - 50.0, lobby.y + 7.0, 32.0, 34.0},
                   16.0, cream, HorizontalTextAlignment::right, friends_lobby_assets::title_font));
    const auto invitations = model.visible_invitation_indices();
    const auto first_invite = model.first_visible_invitation_row();
    const auto count_invites = std::min<std::size_t>(
        first_invite < invitations.size() ? invitations.size() - first_invite : 0U,
        FriendsLobbyMenuModel::visible_invitation_rows);
    for (std::size_t row{}; row < count_invites; ++row) {
        const auto& value = model.snapshot().invitations[invitations[first_invite + row]];
        const DrawRect bounds{lobby.x + 10.0, lobby.y + 53.0 + 38.0 * static_cast<double>(row),
                              lobby.width - 20.0, 36.0};
        const bool selected = value.id == model.selected_invitation_id();
        selected_row(list, bounds, selected, row);
        bounded_text(list, value.lobby_name, {bounds.x + 8.0, bounds.y, bounds.width - 16.0, 20.0},
                     14.0, selected ? white : cream);
        bounded_text(list, "From " + value.inviter_name,
                     {bounds.x + 8.0, bounds.y + 18.0, bounds.width - 16.0, 17.0}, 11.0, muted);
    }
    if (count_invites == 0U) {
        bounded_text(list, "NO LOBBY INVITES", {lobby.x + 24.0, lobby.y + 95.0, lobby.width - 48.0, 52.0},
                     21.0, cream, 2U, HorizontalTextAlignment::center, friends_lobby_assets::title_font);
        const auto detail = model.snapshot().lobby
            ? "Your lobby is ready. Select an online friend and choose Invite to Lobby."
            : "Create a lobby to invite friends, or join when a friend sends you an invitation.";
        bounded_text(list, detail, {lobby.x + 28.0, lobby.y + 150.0, lobby.width - 56.0, 92.0},
                     14.0, muted, 4U, HorizontalTextAlignment::center);
        if (model.snapshot().lobby) {
            const auto& active = *model.snapshot().lobby;
            bounded_text(list, active.name, {lobby.x + 26.0, lobby.y + 258.0, lobby.width - 52.0, 25.0},
                         16.0, gold, 1U, HorizontalTextAlignment::center);
            list.push(text(std::to_string(active.members.size()) + " / " + std::to_string(active.maximum_members) + " PLAYERS",
                           {lobby.x + 26.0, lobby.y + 286.0, lobby.width - 52.0, 23.0},
                           12.0, muted, HorizontalTextAlignment::center));
        }
    }
    const std::string selected_label = selected_friend ? "Selected: " + selected_friend->name
                                     : selected_invitation ? "Invitation from " + selected_invitation->inviter_name
                                     : "Select a friend or an invitation";
    bounded_text(list, selected_label, {lobby.x + 12.0, lobby.y + lobby.height - 25.0,
                                        lobby.width - 24.0, 20.0}, 12.0, gold);

    const auto primary = model.primary_button();
    const auto secondary = model.secondary_button();
    button(list, model, FriendsLobbyControl::primary, draw_rect(layout.primary_button),
           primary.label, primary.intent.has_value(), true);
    button(list, model, FriendsLobbyControl::secondary, draw_rect(layout.secondary_button),
           secondary.label, secondary.intent.has_value());
    std::string_view heading{"FRIENDS & INVITES"};
    std::string_view message{"Select a friend to invite or join, or create a lobby."};
    auto accent = gold;
    if (!model.error().empty()) {
        heading = "ACTION NEEDS ATTENTION";
        message = model.error();
        accent = red;
    } else if (const auto operation = model.operation()) {
        heading = "WORKING";
        switch (operation->intent.kind) {
        case FriendsLobbyActionKind::create_lobby: message = "Creating your lobby..."; break;
        case FriendsLobbyActionKind::search: message = "Searching for players..."; break;
        case FriendsLobbyActionKind::send_friend_request: message = "Sending the friend request..."; break;
        case FriendsLobbyActionKind::accept_friend_request: message = "Accepting the friend request..."; break;
        case FriendsLobbyActionKind::invite_friend: message = "Sending the lobby invitation..."; break;
        case FriendsLobbyActionKind::join_game:
        case FriendsLobbyActionKind::join_friend_lobby:
        case FriendsLobbyActionKind::accept_lobby_invite: message = "Joining your friends..."; break;
        default: message = "Saving your changes..."; break;
        }
    } else if (!model.service_available() || !model.connected()) {
        heading = "RECONNECTING";
        message = model.service_status().empty() ? "Connection unavailable. Retrying automatically." : model.service_status();
        accent = amber;
    } else if (const auto phase = phase_label(model.phase()); !phase.empty()) {
        heading = "LOBBY STATUS";
        message = phase;
    }
    append_menu_status(list, {56, 464, 340, 58}, heading, message, accent);
    const auto back = draw_rect(layout.back_button);
    const auto back_color = model.control_hovered(FriendsLobbyControl::back) ? cream : gold;
    list.push(sprite(friends_lobby_assets::back_icon, {back.x + 2.5, back.y + 3.5, 25.0, 25.0},
                     white, model.control_hovered(FriendsLobbyControl::back) ? 1'000U : 700U));
    list.push(text("BACK", {back.x + 35.0, back.y, back.width - 35.0, back.height},
                   20.0, back_color, HorizontalTextAlignment::left, friends_lobby_assets::tab_font));
    const bool online = model.service_available() && model.connected();
    list.push(text(online ? "CONNECTED" : "RECONNECTING...",
                   {548.0, 543.0, 190.0, 25.0}, 12.0, online ? green : amber,
                   HorizontalTextAlignment::right));
    return list;
}

} // namespace battlespades::frontend
