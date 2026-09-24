#include "battlespades/frontend/favorite_server_store.hpp"
#include "battlespades/frontend/frontend_shell.hpp"
#include "battlespades/frontend/join_match_menu.hpp"
#include "battlespades/frontend/join_match_presentation.hpp"
#include "battlespades/frontend/loading_screen.hpp"

#include <array>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using battlespades::frontend::DirectConnectActionKind;
using battlespades::frontend::DirectConnectMenuModel;
using battlespades::frontend::FavoriteServerStore;
using battlespades::frontend::FrontendShellModel;
using battlespades::frontend::JoinMatchMenuModel;
using battlespades::frontend::JoinMatchPresentation;
using battlespades::frontend::JoinMatchPresentationContext;
using battlespades::frontend::JoinMatchRoute;
using battlespades::frontend::NavigationDirection;
using battlespades::frontend::ServerBrowserEntry;
using battlespades::frontend::ServerBrowserModel;
using battlespades::frontend::ServerBrowserPresentation;
using battlespades::frontend::ServerBrowserPresentationContext;
using battlespades::frontend::ServerBrowserRegion;
using battlespades::frontend::ServerBrowserSource;
using battlespades::frontend::ServerSortColumn;
using battlespades::frontend::WidgetVisualState;
using battlespades::ui::DrawList;
using battlespades::ui::DrawRect;
using battlespades::ui::InputAction;
using battlespades::ui::InputEvent;
using battlespades::ui::InputPhase;
using battlespades::ui::Point;
using battlespades::ui::Rect;
using battlespades::ui::ScreenId;
using battlespades::ui::SpriteDrawCommand;
using battlespades::ui::TextDrawCommand;
using battlespades::ui::WidgetId;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

template <typename Command>
[[nodiscard]] const Command& command_as(const DrawList& list, std::size_t index) {
    if (index >= list.size()) {
        throw std::runtime_error{"draw command index is out of range"};
    }
    const auto* command = std::get_if<Command>(&list.commands()[index]);
    if (command == nullptr) {
        throw std::runtime_error{"draw command has the wrong type"};
    }
    return *command;
}

ServerBrowserEntry server(std::string name,
                          std::string address,
                          std::uint16_t port,
                          std::uint16_t ping,
                          std::uint16_t players,
                          std::uint16_t maximum) {
    ServerBrowserEntry result;
    result.name = std::move(name);
    result.address = std::move(address);
    result.game_port = port;
    result.query_port = static_cast<std::uint16_t>(port + 1U);
    result.ping_milliseconds = ping;
    result.map = "Castle Wars";
    result.mode = "Team Deathmatch";
    result.mode_id = "tdm";
    result.players = players;
    result.maximum_players = maximum;
    return result;
}

void join_geometry_and_routes_match_retail() {
    JoinMatchMenuModel menu;
    const auto controls = menu.controls();
    expect(controls.size() == 4U, "Join Match must expose three routes and Back");
    expect(controls[0].widget.bounds == Rect{2'152, 1'328, 2'096, 464},
           "Server Browser must occupy the recovered top button bounds");
    expect(controls[1].widget.bounds == Rect{2'152, 1'832, 2'096, 464},
           "Custom Match must occupy the recovered middle button bounds");
    expect(controls[2].widget.bounds == Rect{2'152, 2'336, 2'096, 464},
           "Random Match must occupy the recovered bottom button bounds");
    expect(controls[3].widget.bounds == Rect{1'984, 4'336, 624, 208},
           "Back must retain its retail navigation-bar hit target");

    menu.pointer_press(Point{3'200, 1'560});
    expect(menu.pointer_release(Point{3'200, 1'560}) == JoinMatchRoute::server_browser,
           "top button must route to Server Browser");
    menu.pointer_press(Point{3'200, 2'064});
    expect(menu.pointer_release(Point{3'200, 2'064}) == JoinMatchRoute::direct_connect,
           "middle button must route to retail direct IP connect");
    menu.pointer_press(Point{3'200, 2'568});
    expect(menu.pointer_release(Point{3'200, 2'568}) == JoinMatchRoute::random_match,
           "bottom button must route to Random Match");

    menu.pointer_press(Point{2'100, 4'440});
    expect(menu.pointer_release(Point{2'100, 4'440}) == JoinMatchRoute::select_menu,
           "navigation item must route back to Select Menu");
    expect(menu.handle(InputEvent{InputAction::cancel, InputPhase::pressed}) ==
               JoinMatchRoute::select_menu,
           "semantic Cancel must preserve the same back route");
}

void retail_pointer_arming_and_disabled_state_are_explicit() {
    JoinMatchMenuModel menu;
    menu.pointer_press(std::nullopt);
    menu.pointer_move(Point{3'200, 1'560});
    expect(menu.visual_state(WidgetId{1U}) == WidgetVisualState::pressed,
           "retail text buttons arm on any delivered press and support drag-in");
    expect(menu.pointer_release(Point{3'200, 1'560}) == JoinMatchRoute::server_browser,
           "drag-in release must activate a retail text button");

    menu.set_online_routes_enabled(false);
    menu.pointer_press(Point{3'200, 1'560});
    expect(!menu.pointer_release(Point{3'200, 1'560}).has_value(),
           "invalid retail data must disable every online route");
    expect(menu.visual_state(WidgetId{1U}) == WidgetVisualState::disabled,
           "disabled online routes must be renderable");
    expect(menu.focused() == WidgetId{4U}, "focus must repair to the enabled Back item");
}

void join_branch_uses_shared_forward_and_back_slide_contract() {
    FrontendShellModel shell;
    expect(shell.start(ScreenId{1U}), "Select Menu must start");
    expect(shell.navigate(ScreenId{2U}), "Join Match must navigate forward");
    expect(shell.active_offset() == 1.0 && shell.previous_offset() == 0.0,
           "Join Match must enter from the right while Select Menu begins in place");
    for (int tick = 0; tick < 7; ++tick) {
        shell.tick();
    }
    expect(shell.accepts_input(), "retail input gate must open after seven fixed ticks");
    expect(shell.navigate(ScreenId{3U}), "Server Browser must navigate forward");
    expect(shell.active_offset() == 1.0 && shell.previous_offset() == 0.0,
           "nested child screen must use the same forward slide");
    expect(shell.navigate(ScreenId{2U}, NavigationDirection::back),
           "Server Browser Back must navigate to cached Join Match");
    expect(shell.active_offset() == -1.0 && shell.previous_offset() == 0.0,
           "Back must reverse both slide directions");
}

void direct_connect_accepts_retail_host_and_port_input() {
    DirectConnectMenuModel direct;
    direct.focus_input();
    for (const char character : std::string{"127.0.0.1:27015"}) {
        expect(direct.append_character(character),
               "direct connect must accept the retail IPv4:PORT alphabet");
    }
    const auto action = direct.submit();
    expect(action.has_value() && action->kind == DirectConnectActionKind::connect &&
               action->endpoint == "127.0.0.1:27015",
           "direct connect must preserve the explicitly entered local server port");
    const auto favourite = direct.submit_favourite();
    expect(favourite.has_value() && favourite->kind == DirectConnectActionKind::add_favourite &&
               favourite->endpoint == "127.0.0.1:27015",
           "direct connect Add must emit a favourite request without joining");
    const auto direct_layer =
        battlespades::frontend::DirectConnectPresentation{}.build_layer(direct);
    const auto has_add_button =
        std::ranges::any_of(direct_layer.commands(), [](const auto& command) {
            const auto* text = std::get_if<TextDrawCommand>(&command);
            return text != nullptr && text->localization_key == "FAVORITE" &&
                   text->destination == DrawRect{418.0, 296.0, 99.0, 50.0};
        });
    expect(has_add_button,
           "direct-IP screen must fit its Favourite action beside Connect inside the retail frame");

    direct.pointer_press(Point{2'240, 2'480});
    expect(!direct.pointer_release(Point{3'360, 2'480}).has_value(),
           "dragging from Connect to Favourite must not activate a different control");
    direct.pointer_press(Point{3'360, 2'480});
    const auto pointer_favourite = direct.pointer_release(Point{3'360, 2'480});
    expect(pointer_favourite.has_value() &&
               pointer_favourite->kind == DirectConnectActionKind::add_favourite,
           "the compact Favourite button must preserve its typed action");

    direct.set_notice("Added to Favorites");
    expect(!direct.message_is_error(), "successful persistence must not render as a red error");
    direct.set_error("Invalid endpoint");
    expect(direct.message_is_error(), "invalid endpoints must retain the error presentation");
    expect(!direct.append_character('/'),
           "direct connect must reject URL/path characters before endpoint parsing");
    expect(direct.erase_character() && direct.endpoint() == "127.0.0.1:2701",
           "backspace must update the focused endpoint deterministically");
}

void direct_connect_committed_text_and_paste_preserve_endpoints() {
    DirectConnectMenuModel direct;
    expect(direct.append_text("127.0.0.1") && direct.append_text(":") &&
               direct.append_text("27015"), "committed colon text must reach the address");
    expect(direct.endpoint() == "127.0.0.1:27015", "port separator must be retained");
    direct.clear();
    expect(direct.paste_text(" \tPlay.example.net:32887\r\n") &&
               direct.endpoint() == "Play.example.net:32887",
           "paste must preserve host and port while trimming outer whitespace");
    expect(!direct.paste_text("bad/path") &&
               direct.endpoint() == "Play.example.net:32887",
           "a rejected paste must not silently change the destination");
    expect(!direct.paste_text("one\ntwo") && !direct.append_text("\xc3\xa9"),
           "embedded newlines and unsupported non-ASCII host names must be rejected atomically");
    direct.clear();
    expect(direct.append_text(std::string(254U, 'a')), "allow input up to the byte limit");
    expect(!direct.paste_text("bc") && direct.endpoint().size() == 254U,
           "overlong paste must not truncate into a different hostname");
    expect(direct.append_text("z") && !direct.append_text("z"), "enforce exact byte limit");
    direct.pointer_press(Point{0, 0});
    expect(!direct.paste_text("127.0.0.1") && !direct.append_text(":"),
           "unfocused fields must ignore text and paste");
}

void direct_connect_favourites_are_durable_and_fail_closed() {
    const auto directory =
        std::filesystem::temp_directory_path() / "battlespades-favorite-server-store-test";
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    FavoriteServerStore store{directory / "server_favorites.txt"};

    const std::set<std::string, std::less<>> expected{"aos://127.0.0.1:27015",
                                                      "aos://play.example.net:32887"};
    expect(store.save(expected), "favourite store must persist a canonical set");
    expect(store.load() == expected, "Favourites page must survive a clean process restart");

    {
        std::ofstream output{store.path(), std::ios::app};
        output << "not-a-server\n"
               << "aos://bad port:99999\n"
               << "aos://127.0.0.1:27015\n";
    }
    expect(store.load() == expected,
           "malformed and duplicate disk rows must fail closed without hiding valid favourites");
    expect(!FavoriteServerStore::valid_identifier("aos://127.0.0.1:0") &&
               !FavoriteServerStore::valid_identifier("https://127.0.0.1:27015"),
           "direct Add must reject zero ports and non-AoS schemes");
    std::filesystem::remove_all(directory, error);
}

void retail_server_metadata_resolvers_cover_live_modes_and_authored_maps() {
    using battlespades::frontend::resolve_server_map_preview_asset;
    using battlespades::frontend::resolve_server_mode;

    expect(resolve_server_map_preview_asset("BlockNess") ==
                   "png/ui/game_loading/map_previews/blockness.png" &&
               resolve_server_map_preview_asset("Double Dragon") ==
                   "png/ui/game_loading/map_previews/doubledragon.png" &&
               resolve_server_map_preview_asset("SpookyMansion") ==
                   "png/ui/game_loading/map_previews/spookymansion.png",
           "live AoSPlay map spellings must resolve to authored retail preview assets");
    expect(resolve_server_map_preview_asset("Unknown UGC map").empty(),
           "unknown maps must request the retail placeholder instead of a fabricated asset");

    // Canonical codes exported by BattleSpades' current mode registry. Arena
    // is its MODE_NORMAL extension and Classic CTF uses the stock CTF wire row
    // plus InitialInfo.classic once connected.
    constexpr std::array<std::string_view, 11U> live_mode_codes{
        "ctf", "cctf", "tdm", "arena", "vip", "zom", "mh", "tc", "dia", "dem", "oc"};
    for (const auto code : live_mode_codes) {
        const auto presentation = resolve_server_mode(code, false);
        expect(presentation.code == code && !presentation.title_key.empty() &&
                   !presentation.description_key.empty(),
               "every live BattleSpades mode must resolve before connecting");
    }

    const auto zombies = resolve_server_mode("ZOM", false);
    const auto arena = resolve_server_mode("arena", false);
    const auto classic_ctf = resolve_server_mode("cctf", false);
    const auto unknown = resolve_server_mode("not-a-mode", true);
    expect(zombies.title_key == "ZOMBIE_MODE_TITLE" &&
               zombies.description_key == "ZOMBIE_MODE_DESCRIPTION",
           "Zombie discovery metadata must use the retail title and description");
    expect(arena.code == "arena" && arena.title_key == "ARENA",
           "BattleSpades Arena discovery metadata must not masquerade as TDM");
    expect(resolve_server_mode("ARENA").code == "arena" &&
               resolve_server_mode("nor").code == "nor" &&
               battlespades::frontend::resolve_protocol168_mode(0U).code == "nor",
           "shared Arena titles must not shadow exact discovery codes or change the normal wire ordinal");
    expect(classic_ctf.code == "cctf" && classic_ctf.title_key == "CLASSIC_CTF_TITLE" &&
               classic_ctf.classic,
           "Classic CTF must preserve its distinct mode title and classic loading contract");
    expect(unknown.code == "tdm" && !unknown.classic,
           "unknown mode identifiers must fail closed to the ordinary TDM presentation");
}

void server_browser_deduplicates_filters_and_cycles_sources() {
    ServerBrowserModel browser;
    auto alpha = server("Alpha", "10.0.0.1", 32887U, 80U, 0U, 32U);
    alpha.official = true;
    auto bravo = server("Bravo", "10.0.0.2", 32887U, 30U, 32U, 32U);
    auto charlie = server("Charlie", "192.168.1.2", 32887U, 50U, 5U, 16U);
    charlie.local = true;
    charlie.favourite = true;
    auto delta = server("Delta", "10.0.0.4", 32887U, 15U, 7U, 16U);
    delta.friend_hosted = true;

    browser.replace_servers({alpha, bravo, charlie, delta});
    expect(browser.servers().size() == 4U, "all compatible unique servers must be retained");
    expect(browser.visible_indices().size() == 4U, "default All source shows every server");
    expect(browser.servers()[browser.visible_indices()[0]].name == "Delta",
           "initial retail Ping column sorts ascending");

    auto replacement = alpha;
    replacement.name = "Alpha Updated";
    replacement.ping_milliseconds = 9U;
    expect(browser.upsert(replacement), "valid duplicate response should update in place");
    expect(browser.servers().size() == 4U,
           "address/port deduplication must prevent duplicate rows");
    expect(browser.servers()[browser.visible_indices()[0]].name == "Alpha Updated",
           "updated ping must immediately re-sort visible rows");

    browser.set_show_full_servers(false);
    expect(browser.visible_indices().size() == 3U, "Full Servers unchecked hides full rows");
    browser.set_show_empty_servers(false);
    expect(browser.visible_indices().size() == 2U, "Empty Servers unchecked hides empty rows");
    browser.set_show_full_servers(true);
    browser.set_show_empty_servers(true);

    browser.set_source(ServerBrowserSource::official);
    expect(browser.visible_indices().size() == 1U,
           "Official source must retain only official discovery results");
    browser.set_source(ServerBrowserSource::favourites);
    expect(browser.visible_indices().size() == 1U &&
               browser.servers()[browser.visible_indices()[0]].name == "Charlie",
           "Favourites source must retain user-starred rows regardless of locality");
    browser.set_source(ServerBrowserSource::all);
    browser.cycle_source(-1);
    expect(browser.source() == ServerBrowserSource::local,
           "source selector must wrap left from All to Local");
    browser.cycle_source(1);
    expect(browser.source() == ServerBrowserSource::all,
           "source selector must wrap right from Local to All");
}

void server_browser_preserves_identity_and_emits_safe_loading_handoff() {
    ServerBrowserModel browser;
    auto first = server("First", "127.0.0.1", 32887U, 80U, 4U, 32U);
    first.map = "City Of Chicago";
    first.mode_id = "ctf";
    first.texture_skin = "mafia";
    first.classic = true;
    auto second = server("Second", "127.0.0.2", 32887U, 20U, 3U, 32U);
    second.content_owned = false;
    browser.replace_servers({first, second});

    expect(browser.select_visible_row(1U), "higher-ping First row should be selectable");
    expect(browser.selected() != nullptr && browser.selected()->name == "First",
           "selection must resolve by stable address/port identity");
    browser.select_sort_column(ServerSortColumn::name);
    expect(browser.selected() != nullptr && browser.selected()->name == "First",
           "sorting must preserve the selected server identity");
    const auto request = browser.connect_request();
    expect(request.has_value(), "compatible owned selection must produce a handoff");
    expect(request->identifier == "aos://127.0.0.1:32887" && request->host == "127.0.0.1" &&
               request->port == 32887U && request->expected_map == "City Of Chicago" &&
               request->expected_mode == "ctf" && request->expected_skin == "mafia" &&
               request->expected_classic,
           "LoadingMenu handoff must retain every recovered expectation field");

    expect(browser.toggle_selected_favourite(), "selected row should toggle Favourite");
    expect(browser.selected()->favourite, "favourite state must update the selected row");
    browser.set_source(ServerBrowserSource::community);
    expect(browser.selected() != nullptr,
           "selection remains when the selected row survives a source filter");
    browser.set_source(ServerBrowserSource::official);
    expect(browser.selected() == nullptr,
           "selection must clear when the active filter hides its row");

    browser.set_source(ServerBrowserSource::all);
    expect(browser.select_visible_row(1U), "unowned Second row should be selectable");
    expect(!browser.can_connect() && !browser.connect_request().has_value(),
           "missing DLC/content ownership must fail closed before loading");
}

void server_browser_refresh_requests_reject_stale_discovery_callbacks() {
    ServerBrowserModel browser;
    browser.set_region(ServerBrowserRegion::europe);
    const auto internet = browser.begin_refresh();
    expect(internet.source == ServerBrowserSource::all &&
                !internet.region.has_value() && browser.refreshing(),
           "All Servers discovery must not apply an invisible region filter");

    auto first = server("Current", "10.0.0.1", 32887U, 10U, 1U, 32U);
    expect(browser.accept_response(internet.generation, first),
           "current-generation discovery callbacks must be accepted");
    browser.set_source(ServerBrowserSource::friends);
    expect(!browser.refreshing(), "changing source must cancel the previous in-flight request");
    expect(!browser.accept_response(internet.generation, first),
           "late callbacks from a cancelled source must fail closed");

    const auto friends = browser.begin_refresh();
    expect(friends.source == ServerBrowserSource::friends && !friends.region.has_value(),
           "friends discovery must not leak an irrelevant internet region");
    auto friend_server = server("Friend", "10.0.0.2", 32887U, 20U, 2U, 32U);
    friend_server.friend_hosted = true;
    expect(browser.accept_response(friends.generation, friend_server),
           "active friends result must be accepted");
    expect(!browser.finish_refresh(internet.generation),
           "a stale completion callback must not end the active request");
    expect(browser.finish_refresh(friends.generation) && !browser.refreshing(),
           "the matching completion callback must settle the request exactly once");

    browser.set_source(ServerBrowserSource::official);
    expect(browser.region_tabs_visible(), "retail region tabs appear only for Official");
    browser.cycle_region(1);
    expect(browser.region() == ServerBrowserRegion::australia,
           "region selector must wrap and preserve retail ordering");
    expect(battlespades::frontend::localization_key(browser.region()) == "AUSTRALIA",
           "region labels must stay localization keys rather than English model text");
}

/**
 * A friend's match found through Steam has no address of its own, so the row
 * must still be joinable and must carry the host id the loader dials.
 */
void a_steam_only_row_is_joinable_by_its_host_id() {
    battlespades::frontend::ServerBrowserModel browser;
    battlespades::frontend::ServerBrowserEntry steam_row;
    steam_row.name = "KikoTs";
    steam_row.map = "Ancient Egypt, TDM";
    steam_row.mode_id = "tdm";
    steam_row.steam_host_id = 76561198158362762ULL;
    steam_row.friend_hosted = true;
    battlespades::frontend::ServerBrowserEntry relay_row;
    relay_row.name = "Official TDM";
    relay_row.address = "203.0.113.7";
    relay_row.game_port = 32887U;
    relay_row.mode_id = "tdm";
    relay_row.steam_host_id = 76561198298183214ULL;
    browser.replace_servers({steam_row, relay_row});

    // The browser sorts its rows, so each is found by what it is rather than
    // by the order it was added in.
    std::optional<battlespades::frontend::ServerConnectRequest> steam_request;
    std::optional<battlespades::frontend::ServerConnectRequest> relay_request;
    for (std::size_t row{}; row < browser.visible_indices().size(); ++row) {
        expect(browser.select_visible_row(row), "every listed row must be selectable");
        auto request = browser.connect_request();
        expect(request.has_value(), "every listed row must be able to connect");
        if (request->identifier.starts_with("steam:")) steam_request = std::move(request);
        else relay_request = std::move(request);
    }

    expect(steam_request.has_value(), "a Steam row without an address must still connect");
    expect(steam_request->identifier == "steam:76561198158362762",
           "an address-less row identifies itself by its host id");
    expect(steam_request->steam_host_id == 76561198158362762ULL,
           "the host id must reach the loader, which has nothing else to dial");
    expect(steam_request->host.empty(), "a Steam row offers no AoSPlay endpoint");

    expect(relay_request.has_value(), "a listed server must connect");
    expect(relay_request->identifier == "aos://203.0.113.7:32887",
           "a row with an address keeps identifying itself by it");
    expect(relay_request->steam_host_id == 76561198298183214ULL,
           "a listing may offer both routes, and the loader prefers Steam");
    expect(relay_request->host == "203.0.113.7" && relay_request->port == 32887U,
           "the endpoint stays available as the fallback");
}

void server_browser_double_click_and_scroll_are_bounded() {
    ServerBrowserModel browser;
    std::vector<ServerBrowserEntry> entries;
    for (std::uint16_t index = 0U; index < 22U; ++index) {
        entries.push_back(server("Server " + std::to_string(index),
                                 "10.0.1." + std::to_string(index + 1U),
                                 32887U,
                                 static_cast<std::uint16_t>(index + 1U),
                                 1U,
                                 32U));
    }
    browser.replace_servers(std::move(entries));
    expect(browser.visible_row_capacity() == 15U && browser.maximum_first_visible_row() == 7U,
           "retail browser must expose a fixed 15-row viewport over 22 rows");

    browser.set_first_visible_row(999U);
    expect(browser.first_visible_row() == 7U, "absolute scroll requests must clamp to the tail");
    browser.scroll_rows(-3);
    expect(browser.first_visible_row() == 4U, "wheel-up must move by bounded rows");
    browser.scroll_rows(100);
    expect(browser.first_visible_row() == 7U, "wheel-down must not pass the final full page");
    browser.scroll_to_fraction(0.5);
    expect(browser.first_visible_row() == 3U && browser.scroll_fraction() > 0.4 &&
               browser.scroll_fraction() < 0.5,
           "scrollbar dragging must map a normalized position into the bounded row range");
    browser.set_visible_row_capacity(0U);
    expect(browser.visible_row_capacity() == 1U,
           "a malformed zero-row viewport must fail closed to one visible row");
    browser.set_visible_row_capacity(15U);

    expect(!browser.activate_visible_row(0U, 1U).has_value(),
           "the first row click must select without connecting");
    const auto handoff = browser.activate_visible_row(0U, 2U);
    expect(handoff.has_value() && handoff->identifier == "aos://10.0.1.1:32887",
           "a platform-reported second click must connect the selected row");
    expect(!browser.activate_visible_row(0U, 3U).has_value(),
           "a third click must not be misinterpreted as another double-click");
    expect(!browser.activate_visible_row(999U, 2U).has_value(),
           "an out-of-range double-click must fail closed");

    auto blocked = server("Missing DLC", "10.9.9.9", 32887U, 65'000U, 1U, 32U);
    blocked.content_owned = false;
    expect(browser.upsert(blocked), "unowned fixture must remain visible for purchase UI");
    const auto blocked_row = browser.visible_indices().size() - 1U;
    expect(!browser.activate_visible_row(blocked_row, 2U).has_value(),
           "double-click must still fail closed for unowned content");
}

void join_presentation_is_complete_and_transition_composable() {
    JoinMatchMenuModel menu;
    JoinMatchPresentation presentation;
    const auto layer = presentation.build_layer(menu);
    expect(layer.size() == JoinMatchPresentation::layer_command_count,
           "Join Match layer command count must remain characterized");
    expect(command_as<SpriteDrawCommand>(layer, 0U).destination ==
               DrawRect{231.0, 129.5, 339.0, 253.0},
           "three-button frame must resolve its truncated center anchor exactly");
    expect(command_as<SpriteDrawCommand>(layer, 1U).destination ==
               DrawRect{230.0, 520.0, 340.0, 68.0},
           "small navigation frame must preserve retail geometry");
    expect(command_as<TextDrawCommand>(layer, 5U).localization_key == "SERVER_BROWSER",
           "top button must request the correct localized label");
    expect(command_as<SpriteDrawCommand>(layer, 16U).destination ==
               DrawRect{265.75, -6.75, 293.25, 193.5},
           "splash transform must match the Select Menu reconstruction");
    for (const auto& command : layer.commands()) {
        const auto design_space = std::visit(
            [](const auto& value) {
                using Value = std::remove_cvref_t<decltype(value)>;
                if constexpr (std::is_same_v<Value, battlespades::ui::PlayerNamePlateDrawRequest>) {
                    return false;
                } else {
                    return value.space == battlespades::ui::DrawSpace::design_pixels;
                }
            },
            command);
        expect(design_space,
               "transition layer must contain only translatable design-space commands");
    }

    const auto complete = presentation.build(menu, JoinMatchPresentationContext{});
    expect(complete.size() == JoinMatchPresentation::complete_command_count,
           "standalone build must add exactly one stationary background command");
    expect(command_as<SpriteDrawCommand>(complete, 0U).space ==
               battlespades::ui::DrawSpace::window_pixels,
           "complete frame background must stay in window space");

    menu.pointer_press(Point{3'200, 1'560});
    const auto pressed = presentation.build_layer(menu);
    expect(command_as<SpriteDrawCommand>(pressed, 2U).asset_id.find("press_left") !=
               std::string::npos,
           "pressed route must select retail pressed slices");
    expect(command_as<TextDrawCommand>(pressed, 5U).destination.y == 172.0,
           "pressed route moves only text down by two pixels");
}

void server_browser_presentation_reflects_rows_filters_and_selection() {
    ServerBrowserModel browser;
    auto entry = server("BattleSpades EU", "88.80.155.252", 32887U, 42U, 12U, 32U);
    entry.map = "Castle Wars";
    entry.mode = "Capture The Flag";
    entry.favourite = true;
    browser.replace_servers({entry});
    expect(browser.select_visible_row(0U), "fixture server should select");

    ServerBrowserPresentationContext context;
    context.status_text = "Received 1 server";
    const auto layer = ServerBrowserPresentation{}.build_layer(browser, context);
    bool saw_name{};
    bool saw_map{};
    bool saw_connect{};
    bool saw_preview{};
    for (const auto& command : layer.commands()) {
        if (const auto* text_command = std::get_if<TextDrawCommand>(&command)) {
            saw_name = saw_name || text_command->localization_key == "BattleSpades EU";
            saw_map = saw_map || text_command->localization_key == "Castle Wars";
            saw_connect = saw_connect || text_command->localization_key == "CONNECT";
        } else if (const auto* sprite_command = std::get_if<SpriteDrawCommand>(&command)) {
            saw_preview =
                saw_preview || sprite_command->asset_id ==
                                   battlespades::frontend::join_match_assets::map_placeholder;
        }
    }
    expect(saw_name && saw_map && saw_connect && saw_preview,
           "browser layer must render selected list data, Connect, and fallback map art");
}

void server_browser_presentation_exposes_retail_sort_scroll_and_button_states() {
    ServerBrowserModel browser;
    std::vector<ServerBrowserEntry> entries;
    for (std::uint16_t index = 0U; index < 18U; ++index) {
        entries.push_back(server("Node " + std::to_string(index),
                                 "172.16.0." + std::to_string(index + 1U),
                                 32887U,
                                 index,
                                 1U,
                                 32U));
    }
    browser.replace_servers(std::move(entries));
    browser.set_source(ServerBrowserSource::official);
    // Source filtering deliberately leaves no rows until the typed adapter
    // returns official entries; restore one characterized official result.
    auto official = server("Official", "172.16.1.1", 32887U, 5U, 2U, 32U);
    official.official = true;
    expect(browser.upsert(official), "official fixture must be accepted");

    ServerBrowserPresentationContext context;
    const auto no_selection = ServerBrowserPresentation{}.build_layer(browser, context);
    std::size_t selected_region_frames{};
    bool saw_sort_up{};
    bool saw_scroll_up{};
    bool favourite_disabled{};
    for (const auto& command : no_selection.commands()) {
        if (const auto* sprite_command = std::get_if<SpriteDrawCommand>(&command)) {
            selected_region_frames += sprite_command->asset_id ==
                                      battlespades::frontend::join_match_assets::server_tab_frame;
            saw_sort_up = saw_sort_up || sprite_command->asset_id ==
                                             battlespades::frontend::join_match_assets::sort_up;
            saw_scroll_up =
                saw_scroll_up ||
                sprite_command->asset_id == battlespades::frontend::join_match_assets::scroll_up;
        } else if (const auto* text_command = std::get_if<TextDrawCommand>(&command)) {
            favourite_disabled =
                favourite_disabled || (text_command->localization_key == "FAVORITE" &&
                                       text_command->modulation.intensity_per_mille == 700U);
        }
    }
    expect(selected_region_frames == 1U,
           "only the active retail region receives the tab-name frame");
    expect(saw_sort_up && saw_scroll_up,
           "list presentation must expose the active sort sign and scrollbar controls");
    expect(favourite_disabled, "Favourite must render disabled until a concrete row is selected");

    expect(browser.select_visible_row(0U), "official result must be selectable");
    browser.select_sort_column(ServerSortColumn::ping);
    const auto selected = ServerBrowserPresentation{}.build_layer(browser, context);
    bool saw_sort_down{};
    bool favourite_enabled{};
    for (const auto& command : selected.commands()) {
        if (const auto* sprite_command = std::get_if<SpriteDrawCommand>(&command)) {
            saw_sort_down =
                saw_sort_down ||
                sprite_command->asset_id == battlespades::frontend::join_match_assets::sort_down;
        } else if (const auto* text_command = std::get_if<TextDrawCommand>(&command)) {
            favourite_enabled =
                favourite_enabled || (text_command->localization_key == "FAVORITE" &&
                                      text_command->modulation.intensity_per_mille == 1'000U);
        }
    }
    expect(saw_sort_down, "retail descending sort must swap to filter_arrow_down");
    expect(favourite_enabled, "Favourite must return to full intensity when a row is selected");
}

void public_regions_match_wire_names_without_hidden_filters() {
    ServerBrowserModel browser;
    auto eu = server("EU", "192.0.2.1", 27015U, 30U, 2U, 24U);
    auto west = server("US West", "192.0.2.2", 27015U, 90U, 2U, 24U);
    auto east = server("US East", "192.0.2.3", 27015U, 70U, 2U, 24U);
    eu.region = "europe"; west.region = "us-west"; east.region = "US_EAST";
    eu.official = west.official = east.official = true;
    const auto populate = [&] {
        const auto request = browser.begin_refresh();
        for (const auto& entry : {eu, west, east}) {
            expect(browser.accept_response(request.generation, entry), "Region fixture rejected");
        }
        expect(browser.finish_refresh(request.generation), "Region fixture did not finish");
    };
    browser.set_region(ServerBrowserRegion::europe); populate();
    expect(browser.visible_indices().size() == 3U, "All Servers hid a different region");
    browser.set_source(ServerBrowserSource::official);
    for (const auto region : {ServerBrowserRegion::us_west, ServerBrowserRegion::us_east,
                              ServerBrowserRegion::europe}) {
        browser.set_region(region); populate();
        expect(browser.visible_indices().size() == 1U, "Official region alias did not match");
    }
    browser.set_source(ServerBrowserSource::all); populate();
    expect(browser.visible_indices().size() == 3U, "Leaving Official retained its region filter");
    eu.official = west.official = east.official = false;
    browser.set_source(ServerBrowserSource::community); populate();
    expect(browser.visible_indices().size() == 3U && !browser.region_tabs_visible(),
           "Community discovery retained an invisible region filter");
}

struct TestCase final {
    std::string_view name;
    std::function<void()> body;
};

} // namespace

int main() {
    const std::vector<TestCase> tests{
        {"direct_connect_committed_text_and_paste_preserve_endpoints",
         direct_connect_committed_text_and_paste_preserve_endpoints},
        {"public_regions_match_wire_names_without_hidden_filters", public_regions_match_wire_names_without_hidden_filters},
        {"join_geometry_and_routes_match_retail", join_geometry_and_routes_match_retail},
        {"retail_pointer_arming_and_disabled_state_are_explicit",
         retail_pointer_arming_and_disabled_state_are_explicit},
        {"join_branch_uses_shared_forward_and_back_slide_contract",
         join_branch_uses_shared_forward_and_back_slide_contract},
        {"direct_connect_accepts_retail_host_and_port_input",
         direct_connect_accepts_retail_host_and_port_input},
        {"direct_connect_favourites_are_durable_and_fail_closed",
         direct_connect_favourites_are_durable_and_fail_closed},
        {"retail_server_metadata_resolvers_cover_live_modes_and_authored_maps",
         retail_server_metadata_resolvers_cover_live_modes_and_authored_maps},
        {"server_browser_deduplicates_filters_and_cycles_sources",
         server_browser_deduplicates_filters_and_cycles_sources},
        {"server_browser_preserves_identity_and_emits_safe_loading_handoff",
         server_browser_preserves_identity_and_emits_safe_loading_handoff},
        {"server_browser_refresh_requests_reject_stale_discovery_callbacks",
         server_browser_refresh_requests_reject_stale_discovery_callbacks},
        {"a_steam_only_row_is_joinable_by_its_host_id",
         a_steam_only_row_is_joinable_by_its_host_id},
        {"server_browser_double_click_and_scroll_are_bounded",
         server_browser_double_click_and_scroll_are_bounded},
        {"join_presentation_is_complete_and_transition_composable",
         join_presentation_is_complete_and_transition_composable},
        {"server_browser_presentation_reflects_rows_filters_and_selection",
         server_browser_presentation_reflects_rows_filters_and_selection},
        {"server_browser_presentation_exposes_retail_sort_scroll_and_button_states",
         server_browser_presentation_exposes_retail_sort_scroll_and_button_states},
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
