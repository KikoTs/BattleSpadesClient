#include "battlespades/frontend/quick_play_menu.hpp"
#include "battlespades/frontend/quick_play_presentation.hpp"

#include <exception>
#include <filesystem>
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

using battlespades::frontend::QuickPlayBackIntent;
using battlespades::frontend::QuickPlayBuyIntent;
using battlespades::frontend::QuickPlayDirectStartIntent;
using battlespades::frontend::QuickPlayFixedControl;
using battlespades::frontend::QuickPlayMenuModel;
using battlespades::frontend::QuickPlayPlaylistStartIntent;
using battlespades::frontend::QuickPlayPresentation;
using battlespades::frontend::QuickPlaySearchIntent;
using battlespades::frontend::QuickPlaySearchState;
using battlespades::frontend::QuickPlayServerResponse;
using battlespades::frontend::WidgetVisualState;
using battlespades::ui::DrawList;
using battlespades::ui::DrawRect;
using battlespades::ui::DrawSpace;
using battlespades::ui::InputAction;
using battlespades::ui::InputEvent;
using battlespades::ui::InputPhase;
using battlespades::ui::Point;
using battlespades::ui::SpriteDrawCommand;
using battlespades::ui::TextDrawCommand;

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

QuickPlayServerResponse server(std::uint32_t playlist_id,
                               std::string address,
                               double ping,
                               std::uint16_t players,
                               std::uint16_t maximum = 32U) {
    QuickPlayServerResponse result;
    result.playlist_id = playlist_id;
    result.name = "BattleSpades EU";
    result.address = std::move(address);
    result.game_port = 32'887U;
    result.query_port = 32'888U;
    result.ping_seconds = ping;
    result.map = "CastleWars";
    result.mode = "Capture the Flag";
    result.mode_id = "ctf";
    result.players = players;
    result.maximum_players = maximum;
    result.texture_skin = "default";
    return result;
}

void playlist_catalog_matches_retail_files_and_order() {
    const auto definitions = battlespades::frontend::quick_play_playlist_definitions();
    expect(definitions.size() == 11U,
           "Tutorial and UGC must be filtered from thirteen shipped playlist files");
    expect(definitions[0].id == 1U && definitions[0].name_key == "RANDOM",
           "Random playlist must be promoted to the first UI row");
    expect(definitions[1].id == 3U && definitions[1].name_key == "CTF_TITLE",
           "localized English sort must put Capture the Flag after Random");
    expect(definitions[2].id == 2U && definitions[2].classic &&
               definitions[2].rules.size() == 4U,
           "Classic CTF must preserve its file ID, flag, and four default rules");
    expect(definitions[8].id == 8U && definitions[8].mafia_content &&
               definitions[9].id == 12U && definitions[9].mafia_content,
           "Territory Control and VIP must retain Mafia ownership gating");
    expect(definitions[10].id == 13U && definitions[10].rules.size() == 1U &&
               definitions[10].rules[0].value == "60",
           "Zombie must preserve its crate-spawn rule");
}

void no_network_fails_closed_without_trapping_back_or_buy() {
    QuickPlayMenuModel menu{false};
    expect(menu.search_state() == QuickPlaySearchState::unavailable,
           "no discovery adapter must be an explicit unavailable state");
    expect(!menu.begin_search().has_value() && !menu.refresh_enabled(),
           "Refresh cannot start an implicit network operation");
    expect(!menu.activate_primary().has_value(),
           "owned Random row cannot start matchmaking without a network adapter");
    expect(menu.select_row(8U), "Mafia playlist fixture should select");
    const auto buy = menu.activate_primary();
    expect(buy.has_value() && std::holds_alternative<QuickPlayBuyIntent>(*buy),
           "unowned content must still expose the store intent offline");
    const auto back = menu.handle(InputEvent{InputAction::cancel, InputPhase::pressed});
    expect(back.has_value() && std::holds_alternative<QuickPlayBackIntent>(*back),
           "Back must remain usable in the safe no-network state");
}

void discovery_is_generation_checked_and_reproduces_server_priority() {
    QuickPlayMenuModel menu{true, 7U};
    menu.set_network_available(true);
    const auto first = menu.begin_search();
    expect(first.has_value() && menu.search_state() == QuickPlaySearchState::searching,
           "enabled adapter must receive a typed public search intent");
    expect(!menu.begin_search().has_value(), "Refresh is locked while discovery is active");

    auto malformed = server(1U, "", 0.02, 3U);
    expect(!menu.accept_server(*first, std::move(malformed)),
           "malformed endpoint data must fail closed");
    expect(menu.accept_server(*first, server(1U, "10.0.0.1", 0.01, 32U)),
           "matching full server remains part of the retail ping baseline");
    expect(menu.selected().chosen_server() == nullptr,
           "full servers must never become the Quick Play target");
    expect(menu.accept_server(*first, server(1U, "10.0.0.2", 0.50, 0U)),
           "empty server should be accepted");
    expect(menu.selected().chosen_server()->address == "10.0.0.2",
           "empty server is fallback when it is the only joinable result");
    expect(menu.accept_server(*first, server(1U, "10.0.0.3", 0.20, 8U)),
           "populated server should be accepted");
    expect(menu.selected().chosen_server()->address == "10.0.0.3",
           "populated high-ping server outranks an empty high-ping server");
    expect(menu.accept_server(*first, server(1U, "10.0.0.4", 0.02, 4U)),
           "low-ping populated server should be accepted");
    expect(menu.selected().chosen_server()->address == "10.0.0.4" &&
               menu.selected().displayed_ping_milliseconds == 20U &&
               menu.selected().displayed_players == "4/32",
           "low-ping populated result must win and populate both list columns");

    expect(menu.finish_search(*first) && menu.refresh_enabled(),
           "completion unlocks Refresh");
    const auto second = menu.begin_search();
    expect(second.has_value() && second->generation != first->generation,
           "each refresh must advance the callback generation");
    expect(!menu.accept_server(*first, server(1U, "10.0.0.5", 0.01, 2U)) &&
               !menu.finish_search(*first),
           "late callbacks from the previous Steam query must be discarded");
}

void start_requires_a_completed_search_and_preserves_join_identity() {
    QuickPlayMenuModel menu;
    menu.set_network_available(true);
    const auto request = menu.begin_search();
    expect(request.has_value(), "search fixture should start");

    expect(!menu.activate_primary(), "in-flight search cannot start a phantom connection");

    auto concrete = server(1U, "88.80.155.252", 0.042, 12U);
    concrete.name = "Public CTF";
    concrete.map = "CastleWars";
    concrete.mode_id = "ctf";
    concrete.texture_skin = "mafia";
    concrete.classic = true;
    concrete.identity_server_id = "88.80.155.252:32887";
    concrete.identity_ticket = true;
    expect(menu.accept_server(*request, std::move(concrete)), "direct fixture should be admitted");
    expect(!menu.activate_primary(), "partial discovery cannot start before completion");
    expect(menu.finish_search(*request), "direct fixture search must finish");
    const auto direct = menu.activate_primary();
    const auto* loading =
        direct.has_value() ? std::get_if<QuickPlayDirectStartIntent>(&*direct) : nullptr;
    expect(loading != nullptr && loading->identifier == "aos://88.80.155.252:32887" &&
               loading->server_name == "Public CTF" && loading->expected_map == "CastleWars" &&
               loading->expected_mode == "ctf" && loading->expected_skin == "mafia" &&
               loading->expected_classic && loading->identity_ticket &&
               loading->identity_server_id == "88.80.155.252:32887",
           "concrete choice must preserve every LoadingMenu expectation field");
}

void public_discovery_routes_playlists_and_replaces_stale_results() {
    QuickPlayMenuModel menu;
    menu.set_network_available(true);
    const auto request = *menu.begin_search();
    auto response = server(0U, "127.0.0.1", 0.02, 2U);
    response.map = "Castle Wars";
    expect(menu.accept_public_server(request, response) == 2U,
           "CastleWars CTF must populate Random and CTF");
    response.players = 32U;
    expect(menu.accept_public_server(request, response) == 2U &&
               menu.selected().server_responses.size() == 1U &&
               menu.selected().chosen_server() == nullptr,
           "duplicate endpoint becoming full must replace its previous joinable response");
    response.classic = true;
    response.mode_id = "cctf";
    response.map = "Classic";
    response.players = 1U;
    expect(menu.accept_public_server(request, response) == 1U &&
               menu.rows()[2U].chosen_server() != nullptr,
           "Classic CTF cannot leak into normal playlists");
    response.mode_id = "ugc";
    expect(menu.accept_public_server(request, response) == 0U,
           "editor sessions cannot enter public game playlists");
    response.classic = false;
    response.map = "CastleWars";
    response.mode_id = "unknown";
    expect(menu.accept_public_server(request, response) == 0U,
           "unknown modes cannot become TDM through a presentation fallback");
    expect(menu.finish_search(request) && !menu.activate_primary(),
           "a completed full/empty playlist cannot start a connection");
    expect(menu.select_row(2U) && menu.activate_primary().has_value(),
           "another playlist with a valid server remains joinable");
    const auto refresh = *menu.begin_search();
    expect(menu.accept_public_server(refresh, server(0U, "127.0.0.2", 0.04, 2U)) == 2U,
           "refresh may accept partial responses");
    expect(menu.fail_search(refresh) && menu.rows()[0U].server_responses.empty(),
           "a failed search must discard partial results");
    menu.set_network_available(false);
    menu.set_network_available(true);
    const auto reopened = *menu.begin_search();
    expect(!menu.finish_search(refresh) &&
               menu.accept_public_server(refresh, server(0U, "127.0.0.3", 0.01, 2U)) == 0U &&
               menu.finish_search(reopened),
           "leaving and reopening must reject callbacks from the old screen visit");
}

void pointer_contract_emits_typed_refresh_primary_and_back() {
    QuickPlayMenuModel menu;
    menu.set_network_available(true);

    // Retail TextButton arms on press even before the cursor drags into it.
    menu.pointer_press(std::nullopt);
    const auto refresh = menu.pointer_release(Point{100 * 8, 480 * 8});
    expect(refresh.has_value() && std::holds_alternative<QuickPlaySearchIntent>(*refresh),
           "drag-in Refresh release must emit a typed search request");
    expect(menu.visual_state(QuickPlayFixedControl::refresh) == WidgetVisualState::disabled,
           "Refresh must render disabled while its query is active");

    menu.pointer_press(Point{100 * 8, (175 + 24 + 12) * 8});
    expect(!menu.pointer_release(Point{100 * 8, (175 + 24 + 12) * 8}).has_value() &&
               menu.selected_row() == 1U,
           "playlist row releases update selection without emitting a route");

    menu.pointer_press(Point{450 * 8, 480 * 8});
    const auto start = menu.pointer_release(Point{450 * 8, 480 * 8});
    expect(!start, "primary stays disabled while searching without a concrete server");
    const auto request = std::get<QuickPlaySearchIntent>(*refresh);
    expect(menu.accept_server(request, server(3U, "127.0.0.1", 0.02, 2U)) &&
               menu.finish_search(request), "pointer fixture discovery must complete");
    menu.pointer_press(Point{450 * 8, 480 * 8});
    const auto join = menu.pointer_release(Point{450 * 8, 480 * 8});
    expect(join && std::holds_alternative<QuickPlayDirectStartIntent>(*join),
           "primary must join the selected discovered server after completion");

    menu.pointer_press(std::nullopt);
    expect(!menu.pointer_release(Point{80 * 8, 556 * 8}).has_value(),
           "NavigationBar Back cannot be drag-armed like TextButton");
    menu.pointer_press(Point{80 * 8, 556 * 8});
    const auto back = menu.pointer_release(Point{80 * 8, 556 * 8});
    expect(back.has_value() && std::holds_alternative<QuickPlayBackIntent>(*back),
           "press-and-release over Back emits the reverse-slide intent");
}

void presentation_is_complete_slide_composable_and_asset_backed() {
    QuickPlayMenuModel menu;
    const auto layer = QuickPlayPresentation{}.build_layer(menu);
    expect(layer.size() > 40U, "Quick Play layer must contain the panels, rows, preview, and controls");
    expect(command_as<SpriteDrawCommand>(layer, 0U).destination ==
               DrawRect{25.0, 5.5, 750.0, 589.0},
           "large centered frame must preserve recovered 0.64-scale geometry");
    expect(command_as<SpriteDrawCommand>(layer, 1U).destination ==
               DrawRect{56.0, 95.0, 340.0, 354.0},
           "left playlist panel must preserve recovered coordinates");
    expect(command_as<TextDrawCommand>(layer, 5U).localization_key == "PUBLIC_MATCH",
           "screen title must retain its localization key");
    for (const auto& command : layer.commands()) {
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
        expect(design_space, "build_layer must be fully translatable by FrontendShell");
    }

    const auto complete = QuickPlayPresentation{}.build(menu);
    expect(complete.size() == layer.size() + 1U &&
               command_as<SpriteDrawCommand>(complete, 0U).space == DrawSpace::window_pixels,
           "standalone build adds exactly one stationary window-space background");
    const auto definitions = battlespades::frontend::quick_play_playlist_definitions();
    expect(battlespades::frontend::quick_play_mode_image_asset(definitions[0]) ==
               battlespades::frontend::quick_play_assets::random_mode_image &&
               battlespades::frontend::quick_play_mode_image_asset(definitions[2]).find(
                   "letterbox_classic") != std::string_view::npos,
           "preview art must distinguish Random and Classic CTF");

#ifdef AOS_TEST_ASSET_ROOT
    const std::filesystem::path root{AOS_TEST_ASSET_ROOT};
#else
    const std::filesystem::path root{"assets/original"};
#endif
    for (const auto& asset : battlespades::frontend::quick_play_assets::required()) {
        if (asset.kind == battlespades::frontend::MainMenuAssetKind::music ||
            asset.kind == battlespades::frontend::MainMenuAssetKind::sound ||
            asset.kind == battlespades::frontend::MainMenuAssetKind::texture ||
            asset.kind == battlespades::frontend::MainMenuAssetKind::font) {
            expect(std::filesystem::is_regular_file(root / asset.path),
                   "every declared Quick Play dependency must exist in the preserved catalog");
        }
    }
}

struct TestCase final {
    std::string_view name;
    std::function<void()> body;
};

} // namespace

int main() {
    const std::vector<TestCase> tests{
        {"playlist_catalog_matches_retail_files_and_order",
         playlist_catalog_matches_retail_files_and_order},
        {"no_network_fails_closed_without_trapping_back_or_buy",
         no_network_fails_closed_without_trapping_back_or_buy},
        {"discovery_is_generation_checked_and_reproduces_server_priority",
         discovery_is_generation_checked_and_reproduces_server_priority},
        {"start_requires_a_completed_search_and_preserves_join_identity",
         start_requires_a_completed_search_and_preserves_join_identity},
        {"public_discovery_routes_playlists_and_replaces_stale_results",
         public_discovery_routes_playlists_and_replaces_stale_results},
        {"pointer_contract_emits_typed_refresh_primary_and_back",
         pointer_contract_emits_typed_refresh_primary_and_back},
        {"presentation_is_complete_slide_composable_and_asset_backed",
         presentation_is_complete_slide_composable_and_asset_backed},
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
