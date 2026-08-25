#include "battlespades/frontend/friends_lobby_presentation.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <string_view>

namespace battlespades::frontend {
namespace {

using ui::ColorModulation;
using ui::ColorRgba8;
using ui::DrawRect;
using ui::DrawSpace;
using ui::HorizontalTextAlignment;
using ui::SpriteDrawCommand;
using ui::SpriteSizing;
using ui::TextDrawCommand;
using ui::TextFit;
using ui::TextTransform;
using ui::TextureAnchor;
using ui::TextureFilter;
using ui::VerticalTextAlignment;

constexpr double scale{static_cast<double>(FriendsLobbyMenuModel::subpixels_per_pixel)};
constexpr ColorRgba8 white{255U, 255U, 255U, 255U};
constexpr ColorRgba8 cream{241U, 225U, 147U, 255U};
constexpr ColorRgba8 gold{214U, 184U, 58U, 255U};
constexpr ColorRgba8 black{6U, 8U, 6U, 255U};
constexpr ColorRgba8 panel{28U, 34U, 25U, 255U};
constexpr ColorRgba8 panel_alt{39U, 47U, 34U, 255U};
constexpr ColorRgba8 navy{31U, 74U, 96U, 255U};
constexpr ColorRgba8 green{64U, 190U, 78U, 255U};
constexpr ColorRgba8 amber{239U, 164U, 45U, 255U};
constexpr ColorRgba8 red{211U, 75U, 57U, 255U};
constexpr ColorRgba8 muted{155U, 160U, 143U, 255U};

[[nodiscard]] DrawRect draw_rect(ui::Rect value) noexcept {
    return {static_cast<double>(value.x) / scale,
            static_cast<double>(value.y) / scale,
            static_cast<double>(value.width) / scale,
            static_cast<double>(value.height) / scale};
}

[[nodiscard]] ColorModulation color(ColorRgba8 value,
                                    std::uint16_t opacity = 1'000U) noexcept {
    return {value, 1'000U, opacity};
}

[[nodiscard]] SpriteDrawCommand sprite(std::string_view asset,
                                       DrawRect destination,
                                       ColorRgba8 tint = white,
                                       std::uint16_t opacity = 1'000U,
                                       DrawSpace space = DrawSpace::design_pixels,
                                       SpriteSizing sizing = SpriteSizing::stretch) {
    return {std::string{asset}, destination, space, TextureFilter::nearest,
            TextureAnchor::top_left, 1.0, sizing, color(tint, opacity)};
}

void solid(ui::DrawList& list,
           DrawRect bounds,
           ColorRgba8 tint,
           std::uint16_t opacity = 1'000U) {
    list.push(sprite(friends_lobby_assets::white_pixel, bounds, tint, opacity));
}

[[nodiscard]] TextDrawCommand text(std::string_view value,
                                   DrawRect bounds,
                                   double size,
                                   ColorRgba8 tint = cream,
                                   HorizontalTextAlignment alignment =
                                       HorizontalTextAlignment::left,
                                   std::string_view font = friends_lobby_assets::body_font) {
    return {std::string{value}, std::string{font}, bounds, DrawSpace::design_pixels,
            size, 0.0, 1U, alignment, VerticalTextAlignment::retail_center,
            TextTransform::preserve, TextFit::shrink_to_fit, color(tint)};
}

void border(ui::DrawList& list, DrawRect bounds, ColorRgba8 tint, double width = 2.0) {
    solid(list, {bounds.x, bounds.y, bounds.width, width}, tint);
    solid(list, {bounds.x, bounds.y + bounds.height - width, bounds.width, width}, tint);
    solid(list, {bounds.x, bounds.y, width, bounds.height}, tint);
    solid(list, {bounds.x + bounds.width - width, bounds.y, width, bounds.height}, tint);
}

void button(ui::DrawList& list,
            DrawRect bounds,
            std::string_view label,
            bool enabled,
            ColorRgba8 tint = gold) {
    solid(list, bounds, enabled ? tint : panel_alt, enabled ? 1'000U : 800U);
    border(list, bounds, enabled ? cream : muted, 1.0);
    list.push(text(label, {bounds.x + 8.0, bounds.y + 2.0, bounds.width - 16.0,
                           bounds.height - 4.0},
                   18.0, enabled ? black : muted, HorizontalTextAlignment::center,
                   friends_lobby_assets::title_font));
}

[[nodiscard]] std::string_view phase_label(FriendsLobbyPhase phase) noexcept {
    switch (phase) {
    case FriendsLobbyPhase::starting: return "STARTING MATCH...";
    case FriendsLobbyPhase::waiting_for_host: return "WAITING FOR HOST...";
    case FriendsLobbyPhase::joining: return "JOINING MATCH...";
    case FriendsLobbyPhase::reconnecting: return "RECONNECTING TO AOSPLAY...";
    case FriendsLobbyPhase::error: return "ACTION NEEDS ATTENTION";
    case FriendsLobbyPhase::idle: return {};
    }
    return {};
}

} // namespace

ui::DrawList FriendsLobbyPresentation::build(
    const FriendsLobbyMenuModel& model,
    const FriendsLobbyPresentationContext& context) const {
    if (context.window.width <= 0 || context.window.height <= 0 ||
        context.background_opacity_per_mille > 1'000U) {
        throw std::invalid_argument{"invalid friends/lobby presentation context"};
    }

    ui::DrawList list;
    list.reserve(120U);
    const auto& layout = model.layout();
    list.push(sprite(friends_lobby_assets::background,
                     {0.0, 0.0, static_cast<double>(context.window.width),
                      static_cast<double>(context.window.height)},
                     white, context.background_opacity_per_mille,
                     DrawSpace::window_pixels, SpriteSizing::cover));

    const auto frame = draw_rect(layout.frame);
    solid(list, frame, black, 930U);
    border(list, frame, gold, 3.0);
    const auto title = draw_rect(layout.title);
    list.push(sprite(friends_lobby_assets::friends_icon,
                     {title.x + 6.0, title.y + 4.0, 44.0, 44.0}));
    list.push(text("FRIENDS",
                   {title.x + 54.0, title.y, title.width - 54.0, title.height},
                   34.0, cream, HorizontalTextAlignment::center,
                   friends_lobby_assets::title_font));

    const auto tabs = draw_rect(layout.tabs);
    constexpr std::array labels{std::string_view{"FRIENDS"},
                                std::string_view{"REQUESTS"},
                                std::string_view{"INVITES"}};
    const auto tab_width = tabs.width / 3.0;
    for (std::size_t index{}; index < labels.size(); ++index) {
        const auto selected = static_cast<std::size_t>(model.tab()) == index;
        const DrawRect bounds{tabs.x + tab_width * static_cast<double>(index), tabs.y,
                              tab_width, tabs.height};
        solid(list, bounds, selected ? navy : panel_alt);
        border(list, bounds, selected ? cream : muted, 1.0);
        list.push(text(labels[index], bounds, 14.0, selected ? cream : muted,
                       HorizontalTextAlignment::center,
                       friends_lobby_assets::title_font));
    }

    const auto search = draw_rect(layout.search_field);
    solid(list, search, panel);
    border(list, search, model.search_focused() ? cream : muted, 1.0);
    const auto search_label = model.search_text().empty()
                                  ? std::string{"SEARCH PLAYER..."}
                                  : std::string{model.search_text()};
    list.push(text(search_label, {search.x + 8.0, search.y, search.width - 16.0,
                                  search.height}, 15.0,
                   model.search_text().empty() ? muted : white));
    button(list, draw_rect(layout.search_button), "+", model.service_available() &&
                                                          !model.search_text().empty() &&
                                                          !model.busy());

    const auto friends = draw_rect(layout.friends_list);
    solid(list, friends, panel, 970U);
    border(list, friends, gold, 1.0);
    const auto visible = model.visible_friend_indices();
    const auto rows = model.snapshot().friends;
    const auto row_height = friends.height /
                            static_cast<double>(FriendsLobbyMenuModel::visible_rows);
    const auto count = std::min<std::size_t>(visible.size(),
                                             model.first_visible_friend_row() >= visible.size()
                                                 ? 0U
                                                 : FriendsLobbyMenuModel::visible_rows);
    for (std::size_t row{}; row < count; ++row) {
        const auto source_row = model.first_visible_friend_row() + row;
        if (source_row >= visible.size()) break;
        const auto& value = rows[visible[source_row]];
        const DrawRect bounds{friends.x + 1.0,
                              friends.y + row_height * static_cast<double>(row) + 1.0,
                              friends.width - 2.0, row_height - 2.0};
        const auto selected = value.id == model.selected_friend_id();
        solid(list, bounds, selected ? navy : (row % 2U == 0U ? panel_alt : panel));
        const auto online = value.presence != "offline";
        solid(list, {bounds.x + 8.0, bounds.y + 10.0, 9.0, 9.0},
              online ? green : muted);
        list.push(text(value.name,
                       {bounds.x + 25.0, bounds.y, bounds.width - 118.0, bounds.height},
                       16.0, selected ? cream : white));
        list.push(text(online ? "ONLINE" : "OFFLINE",
                       {bounds.x + bounds.width - 86.0, bounds.y, 78.0, bounds.height},
                       11.0, online ? green : muted, HorizontalTextAlignment::right));
    }
    if (count == 0U) {
        list.push(text(model.tab() == FriendsLobbyTab::requests
                           ? "NO FRIEND REQUESTS"
                           : "NO FRIENDS HERE YET",
                       friends, 16.0, muted, HorizontalTextAlignment::center));
    }

    const auto lobby = draw_rect(layout.lobby_panel);
    solid(list, lobby, panel, 970U);
    border(list, lobby, gold, 1.0);
    solid(list, {lobby.x + 1.0, lobby.y + 1.0, lobby.width - 2.0, 46.0}, navy);
    list.push(text("LOBBY INVITATIONS",
                   {lobby.x + 8.0, lobby.y + 1.0, lobby.width - 16.0, 46.0},
                   20.0, cream, HorizontalTextAlignment::center,
                   friends_lobby_assets::title_font));
    const auto invitations = model.visible_invitation_indices();
    const auto remaining_invites = model.first_visible_invitation_row() < invitations.size()
                                       ? invitations.size() -
                                             model.first_visible_invitation_row()
                                       : 0U;
    const auto count_invites = std::min<std::size_t>(remaining_invites, 7U);
    for (std::size_t row{}; row < count_invites; ++row) {
        const auto source_row = model.first_visible_invitation_row() + row;
        const auto& invitation = model.snapshot().invitations[invitations[source_row]];
        const DrawRect bounds{lobby.x + 10.0, lobby.y + 53.0 + 38.0 * row,
                              lobby.width - 20.0, 36.0};
        const auto selected = invitation.id == model.selected_invitation_id();
        solid(list, bounds, selected ? navy : (row % 2U == 0U ? panel_alt : panel));
        list.push(text(invitation.lobby_name,
                       {bounds.x + 8.0, bounds.y, bounds.width - 16.0, 20.0},
                       15.0, selected ? cream : white));
        list.push(text("FROM " + invitation.inviter_name,
                       {bounds.x + 8.0, bounds.y + 17.0, bounds.width - 16.0, 16.0},
                       10.0, muted));
    }
    if (count_invites == 0U) {
        list.push(text(model.snapshot().lobby.has_value()
                           ? "SELECT AN ONLINE FRIEND TO INVITE"
                           : "CREATE A LOBBY OR WAIT FOR AN INVITE",
                       {lobby.x + 25.0, lobby.y + 95.0, lobby.width - 50.0, 80.0},
                       15.0, muted, HorizontalTextAlignment::center));
    }

    std::string primary{model.snapshot().lobby.has_value() ? "LOBBY ACTIVE" : "CREATE LOBBY"};
    bool primary_enabled = model.service_available() && !model.busy() &&
                           !model.snapshot().lobby.has_value();
    std::string secondary{"DECLINE"};
    bool secondary_enabled = model.service_available() && !model.busy() &&
                             !model.selected_invitation_id().empty();
    const auto selected_friend = std::ranges::find(
        model.snapshot().friends, model.selected_friend_id(), &FriendsLobbyFriend::id);
    if (selected_friend != model.snapshot().friends.end()) {
        if (selected_friend->relationship == "pending" &&
            selected_friend->direction == "incoming") {
            primary = "ACCEPT REQUEST";
            secondary = "DECLINE REQUEST";
            secondary_enabled = true;
        } else if (model.snapshot().lobby.has_value() &&
                   selected_friend->presence != "offline") {
            primary = "INVITE TO LOBBY";
            secondary = "REMOVE FRIEND";
            secondary_enabled = true;
        } else {
            primary = "FRIEND SELECTED";
            primary_enabled = false;
            secondary = "REMOVE FRIEND";
            secondary_enabled = true;
        }
    } else if (!model.snapshot().lobby.has_value() &&
               !model.selected_invitation_id().empty()) {
        primary = "ACCEPT INVITE";
    }
    button(list, draw_rect(layout.primary_button), primary, primary_enabled);
    button(list, draw_rect(layout.secondary_button), secondary, secondary_enabled, red);

    const auto phase = phase_label(model.phase());
    if (!phase.empty()) {
        list.push(text(phase, {lobby.x + 15.0, lobby.y + lobby.height - 48.0,
                               lobby.width - 30.0, 32.0}, 14.0,
                       model.phase() == FriendsLobbyPhase::error ? red : amber,
                       HorizontalTextAlignment::center,
                       friends_lobby_assets::title_font));
    }
    if (!model.error().empty()) {
        list.push(text(model.error(), {frame.x + 170.0, frame.y + frame.height - 42.0,
                                       frame.width - 340.0, 30.0}, 12.0, red,
                       HorizontalTextAlignment::center));
    } else if (!model.service_available()) {
        const auto message = model.service_status().empty()
                                 ? std::string_view{"AOSPLAY IS OFFLINE - RETRYING SAFELY"}
                                 : model.service_status();
        list.push(text(message, {frame.x + 170.0, frame.y + frame.height - 42.0,
                                 frame.width - 340.0, 30.0}, 12.0, amber,
                       HorizontalTextAlignment::center));
    }

    const auto back = draw_rect(layout.back_button);
    list.push(sprite(friends_lobby_assets::back_icon,
                     {back.x, back.y + 3.0, 24.0, 24.0}, muted));
    list.push(text("BACK", {back.x + 28.0, back.y, back.width - 28.0, back.height},
                   20.0, gold, HorizontalTextAlignment::left,
                   friends_lobby_assets::title_font));
    return list;
}

} // namespace battlespades::frontend
