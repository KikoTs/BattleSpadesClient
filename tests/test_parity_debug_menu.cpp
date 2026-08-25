#include "battlespades/frontend/parity_debug_menu.hpp"
#include "battlespades/frontend/parity_debug_presentation.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using battlespades::frontend::ParityCatalogKind;
using battlespades::frontend::ParityDebugAction;
using battlespades::frontend::ParityDebugFilter;
using battlespades::frontend::ParityDebugMenuModel;
using battlespades::frontend::ParityDebugPresentation;
using battlespades::ui::InputAction;
using battlespades::ui::InputEvent;
using battlespades::ui::InputPhase;
using battlespades::ui::Point;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

void catalog_accounts_for_routes_components_widgets_and_services() {
    const auto catalog = battlespades::frontend::retail_frontend_catalog();
    expect(catalog.size() >= 100U,
           "generated parity catalog must expose the complete retail UI vocabulary");
    const auto has_id = [&](std::string_view id) {
        return std::ranges::any_of(catalog, [&](const auto& entry) { return entry.id == id; });
    };
    expect(has_id("select_menu") && has_id("create_match_lobby") &&
               has_id("ugc_editor_lobbies") && has_id("player_profile") &&
               has_id("loading"),
           "all critical retail routes must be directly inspectable");
    expect(has_id("widgetTypes.TextButton") && has_id("listRowTypes.SquadFriendListItem"),
           "core widgets and specialized rows must be cataloged by name");
    expect(has_id("widgetTypes.UGCObjectivesListPanel") &&
               has_id("widgetTypes.HelpPanel") &&
               has_id("listRowTypes.OwnableItemBase") &&
               has_id("listRowTypes.MultiColumnPanelItem") &&
               has_id("listRowTypes.UGCObjectiveListItem"),
           "frontend-adjacent authored panels and row types must not disappear from the dump");
    const auto find_id = [&](std::string_view id)
        -> const battlespades::frontend::ParityCatalogEntry* {
        const auto found = std::ranges::find_if(
            catalog, [&](const auto& entry) { return entry.id == id; });
        return found == catalog.end() ? nullptr : &*found;
    };
    const auto* resolution = find_id("change_resolution");
    const auto* ugc_mode = find_id("create_match_lobby.ugc_mode");
    expect(resolution != nullptr && resolution->native_fixture &&
               resolution->route == "change_resolution",
           "the implemented resolution confirmation must open as a native fixture");
    expect(ugc_mode != nullptr && ugc_mode->route == "ugc_editor_lobby/mode" &&
               ugc_mode->parent_screen == "ugc_editor_lobby",
           "generated catalog entries must preserve route and parent metadata independently");
    expect(std::ranges::all_of(catalog, [](const auto& entry) {
               return entry.kind != ParityCatalogKind::screen || !entry.route.empty();
           }),
           "every screen must expose its recovered stable route to the parity browser");
    for (const auto kind : {ParityCatalogKind::screen,
                            ParityCatalogKind::component,
                            ParityCatalogKind::widget,
                            ParityCatalogKind::service}) {
        expect(std::ranges::any_of(catalog,
                                   [kind](const auto& entry) { return entry.kind == kind; }),
               "every debug filter must have at least one entry");
    }
}

void browser_filters_scrolls_and_emits_typed_actions() {
    ParityDebugMenuModel model;
    expect(model.filter() == ParityDebugFilter::all,
           "parity browser must open on the complete inventory");
    expect(model.filtered_indices().size() ==
               battlespades::frontend::retail_frontend_catalog().size(),
           "All filter must not hide inventory rows");
    expect(model.selected_entry() != nullptr && model.selected_entry()->id == "select_menu",
           "the root retail screen must be the deterministic first selection");

    model.pointer_press(Point{450, 480});
    const auto open = model.pointer_release(Point{450, 480});
    expect(open.has_value() && open->action == ParityDebugAction::open_native_fixture &&
               open->entry_id == "select_menu",
           "Open Native must emit the selected fixture id without routing itself");

    model.pointer_press(Point{650, 480});
    const auto hitboxes = model.pointer_release(Point{650, 480});
    expect(hitboxes.has_value() && hitboxes->action == ParityDebugAction::toggle_hitboxes &&
               model.show_hitboxes(),
           "hitbox overlay must be an explicit toggle action");

    model.cycle_filter(1);
    expect(model.filter() == ParityDebugFilter::screens,
           "right filter cycle must enter the Screens inventory");
    expect(std::ranges::all_of(model.filtered_indices(), [](std::size_t index) {
               return battlespades::frontend::retail_frontend_catalog()[index].kind ==
                      ParityCatalogKind::screen;
           }),
           "screen filtering must not leak components/widgets/services");

    const auto before = model.selected_filtered_row();
    static_cast<void>(model.handle(
        InputEvent{InputAction::navigate_down, InputPhase::pressed}));
    expect(model.selected_filtered_row().has_value() &&
               model.selected_filtered_row() != before,
           "keyboard navigation must advance the selected catalog row");
    model.scroll_rows(1000);
    expect(model.first_visible_row() + ParityDebugMenuModel::visible_rows <=
               model.filtered_indices().size(),
           "catalog scrolling must remain bounded");
}

void presentation_exposes_metadata_and_detail_paging() {
    ParityDebugMenuModel model;
    const auto initial_lines = model.detail_line_count();
    expect(initial_lines > 11U,
           "SelectMenu fixture must expose enough controls/assets to exercise detail paging");
    model.scroll_details(5);
    expect(model.detail_first_row() == 5U,
           "detail pane must scroll independently from the catalog list");

    ParityDebugPresentation presentation;
    const auto list = presentation.build(model);
    expect(!list.empty() && list.size() > 50U,
           "parity browser must produce a complete two-panel draw list");
}

} // namespace

int main() {
    try {
        catalog_accounts_for_routes_components_widgets_and_services();
        std::cout << "[PASS] catalog_accounts_for_routes_components_widgets_and_services\n";
        browser_filters_scrolls_and_emits_typed_actions();
        std::cout << "[PASS] browser_filters_scrolls_and_emits_typed_actions\n";
        presentation_exposes_metadata_and_detail_paging();
        std::cout << "[PASS] presentation_exposes_metadata_and_detail_paging\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
