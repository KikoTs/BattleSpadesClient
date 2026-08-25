#include "battlespades/frontend/ugc_editor_menu.hpp"
#include "battlespades/frontend/ugc_editor_presentation.hpp"

#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using namespace battlespades::frontend;
using battlespades::ui::DrawList;
using battlespades::ui::DrawRect;
using battlespades::ui::InputAction;
using battlespades::ui::InputEvent;
using battlespades::ui::InputPhase;
using battlespades::ui::Point;
using battlespades::ui::Rect;
using battlespades::ui::SpriteDrawCommand;
using battlespades::ui::TextDrawCommand;

constexpr std::int32_t scale{MainMenuModel::subpixels_per_pixel};

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] InputEvent pressed(InputAction action) noexcept {
    return InputEvent{action, InputPhase::pressed};
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

[[nodiscard]] bool has_sprite_at(const DrawList& list,
                                 std::string_view asset,
                                 DrawRect destination) {
    for (const auto& command : list.commands()) {
        if (const auto* value = std::get_if<SpriteDrawCommand>(&command);
            value != nullptr && value->asset_id == asset &&
            value->destination == destination) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] UgcEditorLobbyRecord lobby(std::string id,
                                          std::string name,
                                          std::uint16_t members = 2U,
                                          std::uint16_t maximum = 12U) {
    return UgcEditorLobbyRecord{
        std::move(id), std::move(name), members, maximum, 42U, true};
}

void browser_recovers_retail_route_and_geometry() {
    const UgcEditorBrowserModel model;
    const auto controls = model.controls();
    expect(controls.size() == 4U &&
               controls[0U].kind == UgcEditorBrowserControlKind::back &&
               controls[0U].localization_key == "BACK" &&
               controls[1U].kind == UgcEditorBrowserControlKind::source_filter &&
               controls[2U].localization_key == "UGC_SQUADS_MENU_JOIN" &&
               controls[3U].localization_key == "UGC_SQUADS_MENU_NEW_LOBBY",
           "UGCSquadsMenu must expose Back, source filter, Join and New Lobby");
    expect(controls[0U].widget.bounds == Rect{54 * scale, 541 * scale, 120 * scale, 32 * scale} &&
               controls[1U].widget.bounds ==
                   Rect{242 * scale, 114 * scale, 130 * scale, 24 * scale} &&
               controls[2U].widget.bounds ==
                   Rect{405 * scale, 456 * scale, 162 * scale, 50 * scale} &&
               controls[3U].widget.bounds ==
                   Rect{575 * scale, 456 * scale, 162 * scale, 50 * scale},
           "browser hit boxes must match the recovered BaseSquadsMenu geometry");

    const auto layout = ugc_editor_browser_classic_layout();
    expect(layout.frame == DrawRect{25.0, 5.0, 750.0, 589.0} &&
               layout.list_panel == DrawRect{56.0, 95.0, 340.0, 413.0} &&
               layout.preview_panel == DrawRect{401.0, 95.0, 340.0, 354.0} &&
               layout.new_lobby_button == DrawRect{575.0, 456.0, 162.0, 50.0},
           "browser presentation must retain the retail 800x600 list/preview layout");
}

void browser_discovery_is_bounded_and_new_lobby_works_offline() {
    UgcEditorBrowserModel model;
    expect(model.discovery_state() == UgcEditorDiscoveryState::offline &&
               model.status_localization_key() == "MATCHMAKING_OFFLINE" &&
               !model.request_refresh().has_value(),
           "no adapter must be represented honestly as offline");

    model.pointer_press(Point{650 * scale, 480 * scale});
    const auto create = model.pointer_release(Point{650 * scale, 480 * scale});
    expect(create == UgcEditorBrowserIntent{UgcEditorBrowserIntentKind::create_lobby,
                                            UgcEditorLobbySource::open,
                                            {}},
           "bundled editor hosting must remain reachable without fake online discovery");

    model.set_network_available(true);
    const auto refresh = model.request_refresh();
    expect(refresh == UgcEditorBrowserIntent{UgcEditorBrowserIntentKind::refresh_lobbies,
                                             UgcEditorLobbySource::open,
                                             {}},
           "online transition must expose one typed Open-lobby refresh");
    expect(model.complete_refresh(
               {lobby("one", "One"), lobby("one", "Duplicate"), lobby("", "Bad")}, true),
           "active refresh must accept a callback");
    expect(model.lobbies().size() == 1U && model.selected_lobby() != nullptr &&
               model.selected_lobby()->name == "One",
           "malformed and duplicate lobby rows must be discarded deterministically");

    model.pointer_press(Point{480 * scale, 480 * scale});
    const auto join = model.pointer_release(Point{480 * scale, 480 * scale});
    expect(join == UgcEditorBrowserIntent{UgcEditorBrowserIntentKind::join_lobby,
                                          UgcEditorLobbySource::open,
                                          "one"},
           "Join must carry only the selected lobby identifier");

    const auto back = model.handle(pressed(InputAction::cancel));
    expect(back == UgcEditorBrowserIntent{UgcEditorBrowserIntentKind::back_to_ugc_select,
                                          UgcEditorLobbySource::open,
                                          {}},
           "Cancel must return to UGC Select instead of entering Loading");
}

void lobby_exposes_only_the_six_retail_ugc_settings() {
    UgcEditorLobbyModel model;
    const auto definitions = model.settings();
    expect(definitions.size() == 6U &&
               definitions[0U].id == UgcEditorSettingId::privacy &&
               definitions[1U].id == UgcEditorSettingId::maximum_players &&
               definitions[2U].id == UgcEditorSettingId::map &&
               definitions[3U].id == UgcEditorSettingId::prefab_set &&
               definitions[4U].id == UgcEditorSettingId::ugc_mode &&
               definitions[5U].id == UgcEditorSettingId::map_title &&
               definitions[5U].editable_text,
           "UGCSquadLobbyMenu must enable Privacy, Max Players, Map, Prefab Set, UGC Mode and title");
    expect(model.configuration() == UgcEditorConfiguration{} &&
               model.value_text(UgcEditorSettingId::privacy) == "OPEN" &&
               model.value_text(UgcEditorSettingId::maximum_players) == "12" &&
               model.value_text(UgcEditorSettingId::ugc_mode) == "TDM_TITLE" &&
               model.value_text(UgcEditorSettingId::prefab_set) == "UGC_PREFAB_SET_DESERT",
           "defaults must reproduce the UGC playlist and DEFAULT_MATCH_SETTINGS transaction");
    expect(ugc_editor_maps().size() == 9U && ugc_editor_modes().size() == 9U &&
               ugc_editor_prefab_labels().size() == 6U,
           "all retail baseplates, editable modes and prefab sets must remain inspectable");

    expect(model.cycle(UgcEditorSettingId::map, 1) &&
               model.configuration().map_name == "LunarBaseplate" &&
               model.configuration().prefab_set == 0U,
           "map selection must restore the baseplate's retail default prefab set");
    expect(model.cycle(UgcEditorSettingId::maximum_players, 1) &&
               model.configuration().maximum_players == 14U,
           "Max Players must use the recovered even 2..24 list");
    expect(model.cycle(UgcEditorSettingId::ugc_mode, -1) &&
               model.configuration().ugc_mode == "zom",
           "UGC mode selection must wrap across the recovered mode catalog");
}

void title_editing_is_transactional_and_bounded() {
    UgcEditorLobbyModel model;
    expect(model.begin_title_edit(), "title row must enter an explicit edit transaction");
    while (model.erase_title_character()) {
    }
    expect(model.append_title_text("My Castle\n") &&
               model.configuration().map_title == "My Castle",
           "authored title input must accept printable text and reject control characters");
    expect(model.append_title_text("-12345678901234567890") &&
               model.configuration().map_title.size() ==
                   UgcEditorLobbyModel::maximum_title_code_units,
           "retail title storage must remain bounded to 19 code units");
    model.cancel_title_edit();
    expect(!model.title_editing() && model.configuration().map_title == "DesertBaseplate",
           "Cancel must restore the pre-edit title atomically");

    expect(model.begin_title_edit(), "second title edit must start after cancellation");
    while (model.erase_title_character()) {
    }
    expect(model.append_title_text("Arena") && model.commit_title_edit() &&
               model.configuration().map_title == "Arena",
           "Commit must preserve a valid non-empty authored title");
}

void start_is_the_only_transition_to_loading_boundary() {
    UgcEditorLobbyModel model;
    model.pointer_press(Point{570 * scale, 480 * scale});
    const auto start = model.pointer_release(Point{570 * scale, 480 * scale});
    expect(start.has_value() && start->kind == UgcEditorLobbyIntentKind::start_editor &&
               start->configuration == model.configuration(),
           "Start Game must emit the complete immutable editor configuration");

    const auto leave = model.handle(pressed(InputAction::cancel));
    expect(leave.has_value() && leave->kind == UgcEditorLobbyIntentKind::leave_lobby,
           "lobby Cancel must return to UGCSquadsMenu without launching the editor");

    model.pointer_press(Point{330 * scale, 475 * scale});
    const auto invite = model.pointer_release(Point{330 * scale, 475 * scale});
    expect(invite.has_value() && invite->kind == UgcEditorLobbyIntentKind::invite_friends,
           "Invite must remain a typed platform boundary");
}

void presentations_show_real_browser_and_lobby_not_fake_loading() {
    UgcEditorBrowserModel browser;
    const auto browser_frame = UgcEditorBrowserPresentation{}.build(browser);
    expect(has_sprite(browser_frame, main_menu_assets::background) &&
               has_sprite(browser_frame, ugc_editor_assets::frame) &&
               has_text(browser_frame, "UGC_SQUADS_MENU_TITLE") &&
               has_text(browser_frame, "UGC_OPEN_LOBBIES") &&
               has_text(browser_frame, "UGC_SQUADS_MENU_NEW_LOBBY") &&
               !has_text(browser_frame, "INITIALISING_MAP") &&
               !has_text(browser_frame, "SYNCING_MAP"),
           "Map Editor must render UGCSquadsMenu and never masquerade as Loading");

    UgcEditorLobbyModel lobby_model;
    const auto lobby_frame = UgcEditorLobbyPresentation{}.build(
        lobby_model, UgcEditorPresentationContext{{800, 600}, 1'000U, "Kiko"});
    expect(has_text(lobby_frame, "UGC_SQUADS_LOBBY_TITLE") &&
               has_text(lobby_frame, "UGC_SETTINGS") &&
               has_text(lobby_frame, "PRIVACY") &&
               has_text(lobby_frame, "MAX_PLAYERS") &&
               has_text(lobby_frame, "PREFAB_SET") &&
               has_text(lobby_frame, "UGC_MAP_TITLE") &&
               has_text(lobby_frame, "START_GAME") && has_text(lobby_frame, "Kiko"),
           "UGC lobby frame must expose the recovered host, controls and six settings");

    bool rejected{};
    try {
        static_cast<void>(UgcEditorLobbyPresentation{}.build(
            lobby_model, UgcEditorPresentationContext{{0, 600}, 1'000U, "Kiko"}));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected, "invalid presentation extents must fail before partial drawing");
}

void all_authored_assets_exist() {
#ifdef AOS_TEST_ASSET_ROOT
    const std::filesystem::path root{AOS_TEST_ASSET_ROOT};
    for (const auto& asset : ugc_editor_assets::required()) {
        expect(std::filesystem::is_regular_file(root / asset.path),
               std::string{"missing UGC editor asset: "} + std::string{asset.path});
    }
#endif
}

void ingame_settings_are_host_only_atomic_and_cancelable() {
    UgcIngameSettingsModel model;
    UgcIngameSettingsDraft current;
    current.skydome = "Tokyo.txt";
    current.water = {12U, 22U, 38U, 255U};
    current.target_mode = 8U;
    current.map_title = "Neon Capture";
    expect(!model.open(false, current, {"Tokyo.txt", "Classic_B.txt"}),
           "editor guests must not open the host UGC settings");
    expect(model.open(true, current, {"Tokyo.txt", "Classic_B.txt", "../bad.txt"}),
           "the editor host must open UGC settings with safe skydomes");
    expect(model.valid_skydomes().size() == 2U && model.visible(),
           "unsafe skydomes must be removed before the dropdown is exposed");

    static_cast<void>(model.adjust_focused(1));
    model.move_focus(1);
    static_cast<void>(model.adjust_focused(1));
    expect(model.draft().water[0U] == 13U,
           "water red must be independently adjustable in authored RGBA order");
    model.cancel();
    expect(!model.visible() && model.draft() == current,
           "Cancel must restore the complete opening snapshot");

    expect(model.open(true, current, {"Tokyo.txt", "Classic_B.txt"}),
           "host settings must reopen after cancel");
    for (std::size_t index{}; index < 5U; ++index) model.move_focus(1);
    expect(model.focused_field() == UgcIngameSettingField::map_title &&
               model.begin_title_edit(),
           "Map Title row must enter bounded edit mode");
    while (model.erase_title_character()) {}
    expect(model.append_title_text("UGC Arena") && !model.append_title_text("01234567890123456789"),
           "title input must accept printable ASCII and preserve the retail 19-unit bound");
    model.finish_title_edit();
    const auto applied = model.apply();
    expect(applied.has_value() && applied->map_title == "UGC Arena" && !model.visible(),
           "Apply must return one immutable complete settings snapshot");
}

void ingame_settings_pointer_focus_selects_exact_rows() {
    UgcIngameSettingsModel model;
    UgcIngameSettingsDraft draft;
    draft.skydome = "Desert.txt";
    draft.map_title = "Pointer Test";
    expect(model.open(true, draft, {"Desert.txt"}),
           "host settings must open before pointer focus is accepted");
    expect(model.set_focused_field(UgcIngameSettingField::water_blue) &&
               model.focused_field() == UgcIngameSettingField::water_blue,
           "pointer focus must select the exact water-blue row");
    expect(!model.set_focused_field(UgcIngameSettingField::water_blue),
           "selecting the focused row must remain a no-op");
    expect(!model.begin_title_edit(), "non-title rows must not begin title editing");
    expect(model.set_focused_field(UgcIngameSettingField::map_title) &&
               model.begin_title_edit(),
           "pointer focus must make the title row editable");
    expect(!model.set_focused_field(UgcIngameSettingField::skydome) &&
               model.focused_field() == UgcIngameSettingField::map_title,
           "pointer focus must not escape an active title edit");
}

void ingame_settings_presentation_uses_retail_frames_rows_and_scroll() {
    const auto layout = ugc_ingame_settings_classic_layout();
    expect(layout == UgcIngameSettingsClassicLayout{
                         {133.0, 28.0, 534.0, 524.0},
                         {112.0, 10.0, 552.0, 579.0},
                         {250.0, 26.0, 300.0, 80.0},
                         {152.0, 105.0, 494.0, 351.0},
                         {614.0, 115.0, 22.0, 331.0},
                         {160.0, 439.0, 232.0, 41.0},
                         {403.0, 439.0, 232.0, 41.0}},
           "UGCSettings frame, title, list and TextButtons must retain retail 800x600 geometry");

    UgcIngameSettingsModel model;
    UgcIngameSettingsDraft draft;
    draft.skydome = "Classic_B.txt";
    draft.water = {18U, 67U, 94U, 255U};
    draft.target_mode = 6U;
    draft.map_title = "Parity Settings";
    expect(model.open(true, draft, {"Classic_B.txt", "Tokyo.txt"}),
           "presentation fixture must open the host-only UGCSettings model");

    const UgcIngameSettingsPresentation presentation;
    const auto frame = presentation.build_layer(model);
    expect(has_sprite_at(frame,
                         ugc_editor_assets::settings_outer_frame,
                         layout.outer_frame) &&
               has_sprite_at(frame,
                             ugc_editor_assets::settings_content_frame,
                             layout.content_frame) &&
               has_sprite(frame, ugc_editor_assets::settings_row) && has_text(frame, "SETTINGS") &&
               has_text(frame, "SKY") && has_text(frame, "WATER") &&
               has_text(frame, "CONFIG") && has_text(frame, "CANCEL") &&
               has_text(frame, "APPLY") && !has_text(frame, "ESC CANCEL") &&
               !has_text(frame, "ARROWS CHANGE  |  TITLE: ENTER THEN TYPE"),
           "UGCSettings must use retail frame/control art without fabricated helper captions");
    expect(presentation.field_bounds(model, UgcIngameSettingField::skydome) ==
               DrawRect{162.0, 143.0, 442.0, 32.0} &&
               presentation.field_bounds(model, UgcIngameSettingField::water_red) ==
                   DrawRect{162.0, 205.0, 442.0, 32.0} &&
               presentation.field_bounds(model, UgcIngameSettingField::map_title) ==
                   DrawRect{162.0, 403.0, 442.0, 32.0} &&
               !presentation.field_bounds(model, UgcIngameSettingField::map_preview).has_value(),
           "initial expanded list must expose exactly retail's first ten visible rows");

    for (std::size_t index{}; index < 6U; ++index) model.move_focus(1);
    expect(model.focused_field() == UgcIngameSettingField::map_preview &&
               presentation.field_bounds(model, UgcIngameSettingField::map_preview) ==
                   DrawRect{162.0, 409.0, 442.0, 32.0} &&
               presentation.field_bounds(model, UgcIngameSettingField::skydome) ==
                   DrawRect{162.0, 115.0, 442.0, 32.0},
           "focusing Map Preview must advance the same one-row retail viewport");

    model.cancel();
    expect(presentation.build_layer(model).empty(),
           "closed UGCSettings must emit no stale in-game overlay commands");
}

struct TestCase final {
    std::string_view name;
    std::function<void()> body;
};

} // namespace

int main() {
    const std::vector<TestCase> tests{
        {"browser_recovers_retail_route_and_geometry", browser_recovers_retail_route_and_geometry},
        {"browser_discovery_is_bounded_and_new_lobby_works_offline",
         browser_discovery_is_bounded_and_new_lobby_works_offline},
        {"lobby_exposes_only_the_six_retail_ugc_settings",
         lobby_exposes_only_the_six_retail_ugc_settings},
        {"title_editing_is_transactional_and_bounded", title_editing_is_transactional_and_bounded},
        {"start_is_the_only_transition_to_loading_boundary",
         start_is_the_only_transition_to_loading_boundary},
        {"presentations_show_real_browser_and_lobby_not_fake_loading",
         presentations_show_real_browser_and_lobby_not_fake_loading},
        {"all_authored_assets_exist", all_authored_assets_exist},
        {"ingame_settings_are_host_only_atomic_and_cancelable",
         ingame_settings_are_host_only_atomic_and_cancelable},
        {"ingame_settings_pointer_focus_selects_exact_rows",
         ingame_settings_pointer_focus_selects_exact_rows},
        {"ingame_settings_presentation_uses_retail_frames_rows_and_scroll",
         ingame_settings_presentation_uses_retail_frames_rows_and_scroll},
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
