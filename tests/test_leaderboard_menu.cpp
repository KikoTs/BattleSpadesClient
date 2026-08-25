#include "battlespades/frontend/leaderboard_menu.hpp"
#include "battlespades/frontend/leaderboard_presentation.hpp"

#include <exception>
#include <functional>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using namespace battlespades::frontend;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

LeaderboardRow general_row(std::uint32_t rank, std::string name, std::string total) {
    return {rank, rank, std::move(name), {std::move(total), "4", "2", "2.000", "1", "0"}};
}

LeaderboardRow tdm_row(std::uint32_t rank, std::string name) {
    return {rank, rank, std::move(name), {"100", "2", "1", "7", "3", "1", "4"}};
}

[[nodiscard]] const battlespades::ui::TextDrawCommand*
find_text(const battlespades::ui::DrawList& draw, std::string_view key) {
    const auto found = std::ranges::find_if(draw.commands(), [key](const auto& command) {
        const auto* text = std::get_if<battlespades::ui::TextDrawCommand>(&command);
        return text != nullptr && text->localization_key == key;
    });
    return found == draw.commands().end() ? nullptr
                                          : std::get_if<battlespades::ui::TextDrawCommand>(&*found);
}

[[nodiscard]] const battlespades::ui::SpriteDrawCommand*
find_sprite(const battlespades::ui::DrawList& draw, std::string_view asset) {
    const auto found = std::ranges::find_if(draw.commands(), [asset](const auto& command) {
        const auto* sprite = std::get_if<battlespades::ui::SpriteDrawCommand>(&command);
        return sprite != nullptr && sprite->asset_id == asset;
    });
    return found == draw.commands().end()
               ? nullptr
               : std::get_if<battlespades::ui::SpriteDrawCommand>(&*found);
}

[[nodiscard]] const battlespades::ui::SpriteDrawCommand*
find_solid(const battlespades::ui::DrawList& draw,
           battlespades::ui::DrawRect destination,
           battlespades::ui::ColorRgba8 fill) {
    const auto found = std::ranges::find_if(draw.commands(), [&](const auto& command) {
        const auto* sprite = std::get_if<battlespades::ui::SpriteDrawCommand>(&command);
        return sprite != nullptr &&
               sprite->asset_id == leaderboard_presentation_assets::white_pixel &&
               sprite->destination == destination && sprite->modulation.color == fill;
    });
    return found == draw.commands().end()
               ? nullptr
               : std::get_if<battlespades::ui::SpriteDrawCommand>(&*found);
}

void definitions_recover_all_retail_types_and_columns() {
    const auto definitions = leaderboard_definitions();
    expect(definitions.size() == 10U, "retail exposes ten leaderboard filters");
    expect(definitions.front().column_keys.size() == 8U,
           "General has rank/name plus six value columns");
    expect(definitions[7].filter_key == "ZOMBIE_MODE_TITLE" &&
               definitions[7].column_keys.size() == 7U,
           "Zombie leaderboard definition must retain its dedicated stats");
}

void request_lock_cache_and_stale_callback_are_deterministic() {
    LeaderboardMenuModel model;
    const auto request = model.take_request();
    expect(request.has_value(), "initial General/Global view must request data");
    expect(!model.take_request().has_value(), "a request may only be taken once");
    expect(!model.select_scope(LeaderboardScope::friends),
           "retail selectors are disabled while connecting");

    std::vector rows{general_row(2U, "Bravo", "10"), general_row(1U, "Alpha", "20")};
    expect(model.complete(*request, rows), "matching response must populate cache");
    expect(model.state() == LeaderboardLoadState::ready && model.rows().size() == 2U,
           "completed data must be visible");
    expect(model.toggle_dropdown(LeaderboardDropdown::type) &&
               model.open_dropdown() == LeaderboardDropdown::type,
           "ready type selector must open its retail DropBoxControl list");
    expect(model.toggle_dropdown(LeaderboardDropdown::scope) &&
               model.open_dropdown() == LeaderboardDropdown::scope,
           "opening scope must transfer the single retail dropdown focus");
    expect(model.close_dropdown() &&
               model.open_dropdown() == LeaderboardDropdown::none,
           "clicking away must close the focused retail dropdown");
    expect(model.select_scope(LeaderboardScope::friends), "ready selectors should switch scope");
    const auto friends_request = model.take_request();
    expect(friends_request.has_value(), "uncached scope must request once");
    expect(!model.complete(*request, rows), "late response from old scope must be ignored");
    expect(model.fail(*friends_request), "current failed response must unlock the menu");
    expect(model.state() == LeaderboardLoadState::unavailable,
           "failure must render an empty unavailable table");

    expect(model.select_scope(LeaderboardScope::global), "cached Global scope must reopen");
    expect(model.state() == LeaderboardLoadState::ready && !model.take_request().has_value(),
           "cache hit must not duplicate HTTP work");
}

void sorting_reproduces_name_ascending_and_numeric_descending_defaults() {
    LeaderboardMenuModel model;
    const auto request = *model.take_request();
    expect(
        model.complete(request, {general_row(2U, "Bravo", "10"), general_row(1U, "Alpha", "20")}),
        "fixture must load");
    expect(model.sort_by(1U), "Name column should sort");
    expect(model.rows().front().player_name == "Alpha" &&
               model.sort_direction() == LeaderboardSortDirection::ascending,
           "first Name click sorts ascending");
    expect(model.sort_by(2U), "Total column should sort");
    expect(model.rows().front().player_name == "Alpha" &&
               model.sort_direction() == LeaderboardSortDirection::descending,
           "first numeric click sorts descending");
}

void row_selection_and_hover_follow_retail_list_panel_identity() {
    LeaderboardMenuModel model;
    const auto request = *model.take_request();
    expect(
        model.complete(request, {general_row(2U, "Bravo", "10"),
                                 general_row(1U, "Alpha", "20")}),
        "row interaction fixture must load");
    expect(model.select_row(0U) && model.selected_row() == 0U,
           "retail row click must select in place rather than activate another menu");
    expect(model.hover_row(1U) && model.hovered_row() == 1U,
           "retail pointer motion must retain an independent hovered row");
    expect(model.sort_by(1U), "name sort must succeed");
    expect(model.selected_row() == 1U && model.hovered_row() == 0U,
           "sorting must move selected and hovered row objects with their player identity");
    expect(model.select_row(1U),
           "clicking the already-selected retail row remains a handled selection");
    expect(model.hover_row(std::nullopt) && !model.hovered_row().has_value(),
           "leaving the grid must clear only the hover state");
    expect(model.select_type(LeaderboardType::team_deathmatch) &&
               !model.selected_row().has_value() && !model.hovered_row().has_value(),
           "a refreshed leaderboard recreates rows and clears old interaction flags");
}

void row_highlights_render_in_retail_background_selection_text_order() {
    LeaderboardMenuModel model;
    const auto request = *model.take_request();
    expect(model.complete(request, {general_row(1U, "Alpha", "20"),
                                    general_row(2U, "Bravo", "10")}),
           "row highlight fixture must load");
    expect(model.hover_row(0U) && model.select_row(1U),
           "hover and selection fixtures must be accepted");
    const auto draw = LeaderboardPresentation{}.build(model);
    const auto* hovered = find_solid(
        draw,
        battlespades::ui::DrawRect{40.0, 175.0, 720.0, 25.0},
        battlespades::ui::ColorRgba8{175U, 172U, 161U, 255U});
    const auto* selection =
        find_sprite(draw, leaderboard_presentation_assets::selection_line);
    const auto* glow = find_sprite(draw, leaderboard_presentation_assets::selection_glow);
    expect(hovered != nullptr,
           "hovered row must use ListPanelItemBase.hovered_colour across the full row");
    expect(selection != nullptr &&
               selection->destination ==
                   battlespades::ui::DrawRect{40.0, 200.0, 720.0, 25.0} &&
               glow != nullptr &&
               glow->destination ==
                   battlespades::ui::DrawRect{22.0, 196.25, 756.0, 32.5},
           "selected row must use the recovered green line and 5%/30% expanded glow");
}

void presentation_uses_binary_recovered_geometry_and_loading_copy() {
    const auto layout = leaderboard_classic_layout();
    expect(layout.frame == battlespades::ui::DrawRect{2.0, 24.0, 796.0, 552.0},
           "1328x921 frame must retain truncated 0.6 centered geometry");
    expect(layout.type_dropdown == battlespades::ui::DrawRect{30.0, 110.0, 300.0, 20.0},
           "type selector coordinates must convert from bottom-left exactly");
    expect(layout.table == battlespades::ui::DrawRect{30.0, 150.0, 740.0, 350.0},
           "list-area header must convert retail y=425 to top-left y=150");
    LeaderboardMenuModel model;
    const auto draw = LeaderboardPresentation{}.build(model);
    expect(draw.size() > 8U, "loading leaderboard should be a complete draw list");
    const auto* title = find_text(draw, "LEADERBOARD");
    expect(title != nullptr && title->preferred_font_asset == "fonts/Spades.ttf" &&
               title->requested_font_size_pixels == 46.0,
           "leaderboard title must use retail title_font (ALDO/Spades 46)");
    const auto* left_cap =
        find_sprite(draw, leaderboard_presentation_assets::red_header_left);
    const auto* sort_arrow =
        find_sprite(draw, leaderboard_presentation_assets::filter_down_white);
    expect(left_cap != nullptr &&
               left_cap->destination ==
                   battlespades::ui::DrawRect{38.0, 150.0, 25.0, 25.0} &&
               sort_arrow != nullptr &&
               sort_arrow->destination ==
                   battlespades::ui::DrawRect{79.0, 159.0, 8.0, 7.0},
           "global_scale=0.64 header caps and sort arrows must keep truncated retail geometry");
    expect(find_sprite(draw, leaderboard_presentation_assets::square_button) != nullptr &&
               find_sprite(draw, leaderboard_presentation_assets::back_icon) != nullptr,
           "drop-down and navigation controls must use retail image widgets");
    const auto* filter = find_text(draw, "GENERAL");
    const auto* loading = find_text(draw, "CONNECTING_PLEASE_WAIT");
    const auto* selector_arrow =
        find_sprite(draw, leaderboard_presentation_assets::arrow_down);
    expect(filter != nullptr && filter->preferred_font_asset == "fonts/Spades.ttf" &&
               filter->requested_font_size_pixels == 14.0 &&
               filter->vertical_alignment ==
                   battlespades::ui::VerticalTextAlignment::retail_center &&
               filter->fit ==
                   battlespades::ui::TextFit::retail_width_scale &&
               filter->modulation.color ==
                   battlespades::ui::ColorRgba8{244U, 236U, 187U, 255U} &&
               loading != nullptr && loading->preferred_font_asset == "fonts/Edo.ttf" &&
               loading->destination.y == 300.0 &&
               loading->vertical_alignment ==
                   battlespades::ui::VerticalTextAlignment::baseline &&
               loading->fit == battlespades::ui::TextFit::none &&
               selector_arrow != nullptr &&
               selector_arrow->modulation.color ==
                   battlespades::ui::ColorRgba8{86U, 86U, 86U, 255U},
           "retail dropdown metric center, direct loading baseline and disabled tint must remain exact");
}

void row_names_use_retail_ellipsis_before_ordinary_width_validation() {
    LeaderboardMenuModel model;
    const auto request = *model.take_request();
    expect(model.complete(request, {general_row(1U, "ABCDEFGHIJKLMNO", "10")}),
           "name fitting fixture must load");

    LeaderboardPresentationContext context;
    context.measure_row_text = [](std::string_view value, double size) {
        expect(size == 11.0, "retail name fitting must use small_standard_ui_font at 11px");
        return static_cast<double>(value.size()) * 10.0;
    };
    const auto draw = LeaderboardPresentation{}.build(model, context);
    const auto* fitted = find_text(draw, "ABCDEFGHIJK...");
    expect(fitted != nullptr && find_text(draw, "ABCDEFGHIJKLMNO") == nullptr &&
               fitted->preferred_font_asset == "fonts/A750-Sans-Medium.ttf" &&
               fitted->vertical_alignment ==
                   battlespades::ui::VerticalTextAlignment::retail_center &&
               fitted->fit ==
                   battlespades::ui::TextFit::retail_width_scale,
           "LeaderboardListPanel.populate must truncate against 143px with literal periods before drawing");
}

void dropdown_lists_replace_click_to_cycle_and_overlay_the_grid() {
    LeaderboardMenuModel model;
    const auto request = *model.take_request();
    expect(model.complete(request, {general_row(1U, "Alpha", "10")}),
           "dropdown fixture must unlock selectors");
    expect(model.toggle_dropdown(LeaderboardDropdown::type),
           "type title click must open rather than cycle");
    const auto draw = LeaderboardPresentation{}.build(model);
    const auto* second_option = find_text(draw, "TDM_TITLE");
    const auto* selection =
        find_sprite(draw, leaderboard_presentation_assets::selection_line);
    const auto* glow =
        find_sprite(draw, leaderboard_presentation_assets::selection_glow);
    expect(second_option != nullptr &&
               second_option->destination ==
                   battlespades::ui::DrawRect{44.0, 151.0, 272.0, 20.0} &&
               second_option->preferred_font_asset ==
                   "fonts/A750-Sans-Medium.ttf",
           "type options must open one pixel below the title in retail row geometry");
    expect(selection != nullptr &&
               selection->destination ==
                   battlespades::ui::DrawRect{30.0, 131.0, 300.0, 20.0} &&
               glow != nullptr &&
               glow->destination ==
                   battlespades::ui::DrawRect{22.5, 128.0, 315.0, 26.0},
           "current dropdown option must retain retail highlight line and glow");
    expect(model.select_type(LeaderboardType::team_deathmatch) &&
               model.open_dropdown() == LeaderboardDropdown::none &&
               model.state() == LeaderboardLoadState::loading,
           "choosing an option must close the list and request that exact leaderboard");
}

void horizontal_stat_window_pins_rank_and_name_and_scrolls_like_retail() {
    LeaderboardMenuModel model;
    const auto general_request = *model.take_request();
    expect(model.complete(general_request, {general_row(1U, "Alpha", "10")}),
           "General fixture must load before selectors unlock");
    expect(model.select_type(LeaderboardType::team_deathmatch), "TDM selector must activate");
    const auto request = *model.take_request();
    expect(model.complete(request, {tdm_row(1U, "Alpha")}), "TDM fixture must load");

    LeaderboardPresentationContext context;
    context.measure_header_text = [](std::string_view key, double) {
        return key == "LEADERBOARD_RANK" ? 24.0 : 100.0;
    };
    auto grid = leaderboard_grid_layout(model, context);
    expect(grid.shows_horizontal_scrollbar && grid.visible_stat_columns == 4U &&
               grid.visible_rows == 11U,
           "overflow must enable the retail four-column viewport and max_index-1 row count");
    expect(grid.columns.size() == 6U && grid.columns[0].source_index == 0U &&
               grid.columns[1].source_index == 1U && grid.columns[2].source_index == 2U,
           "Rank and Name stay pinned while the first stat window begins at Total");
    expect(model.scroll_stat_columns(1, grid.visible_stat_columns),
           "right-arrow action must advance the stat window");
    grid = leaderboard_grid_layout(model, context);
    expect(grid.columns[0].source_index == 0U && grid.columns[1].source_index == 1U &&
               grid.columns[2].source_index == 3U,
           "horizontal scrolling must never move Rank or Name");
    const auto draw = LeaderboardPresentation{}.build(model, context);
    const auto* horizontal_mid =
        find_sprite(draw, leaderboard_presentation_assets::scrollbar_hmid);
    const auto* horizontal_cap =
        find_sprite(draw, leaderboard_presentation_assets::scrollbar_left);
    expect(horizontal_mid != nullptr && horizontal_cap != nullptr &&
               horizontal_cap->destination.width == 3.75,
           "overflowing stats must use the retail 0.6 texture then 20/16 bar scaling");
}

void row_capacity_and_vertical_scrollbar_follow_horizontal_overflow() {
    LeaderboardMenuModel model;
    const auto request = *model.take_request();
    std::vector<LeaderboardRow> rows;
    for (std::uint32_t index = 1U; index <= 20U; ++index) {
        rows.push_back(general_row(index, "Player" + std::to_string(index), "10"));
    }
    expect(model.complete(request, std::move(rows)), "large fixture must load");
    const auto grid = leaderboard_grid_layout(model);
    expect(grid.shows_vertical_scrollbar && grid.visible_rows == 12U &&
               grid.header.width == 688.0,
           "row overflow must reserve the retail 22-pixel vertical control and 688-pixel row");
    expect(model.scroll_rows(99, grid.visible_rows) && model.first_visible_row() == 8U,
           "row scrolling must clamp against the actual rendered capacity");
    const auto draw = LeaderboardPresentation{}.build(model);
    const auto* vertical_top =
        find_sprite(draw, leaderboard_presentation_assets::scrollbar_top);
    expect(vertical_top != nullptr && vertical_top->destination.height == 3.75 &&
               find_sprite(draw, leaderboard_presentation_assets::scrollbar_bottom) != nullptr,
           "long leaderboards must render the 3.75px retail vertical bevels, not fabricated 20px caps");
}

void persona_projection_matches_retail_friend_name_override() {
    std::vector rows{general_row(7U, "StaleLocal", "10"),
                     general_row(8U, "StaleFriend", "20"),
                     general_row(9U, "ServerName", "30")};
    const std::array personas{
        LeaderboardPersona{7U, "FriendAlias"},
        LeaderboardPersona{8U, "CurrentFriend"},
        LeaderboardPersona{0U, "Invalid"},
    };
    apply_retail_leaderboard_personas(rows, 7U, "SignedInName", personas);
    expect(rows[0U].player_name == "SignedInName" &&
               rows[1U].player_name == "CurrentFriend" &&
               rows[2U].player_name == "ServerName",
           "retail persona projection must prefer local identity, then friends, then server text");
}

struct TestCase final {
    std::string_view name;
    std::function<void()> body;
};

} // namespace

int main() {
    const std::vector<TestCase> tests{
        {"definitions_recover_all_retail_types_and_columns",
         definitions_recover_all_retail_types_and_columns},
        {"request_lock_cache_and_stale_callback_are_deterministic",
         request_lock_cache_and_stale_callback_are_deterministic},
        {"sorting_reproduces_name_ascending_and_numeric_descending_defaults",
         sorting_reproduces_name_ascending_and_numeric_descending_defaults},
        {"row_selection_and_hover_follow_retail_list_panel_identity",
         row_selection_and_hover_follow_retail_list_panel_identity},
        {"row_highlights_render_in_retail_background_selection_text_order",
         row_highlights_render_in_retail_background_selection_text_order},
        {"presentation_uses_binary_recovered_geometry_and_loading_copy",
         presentation_uses_binary_recovered_geometry_and_loading_copy},
        {"row_names_use_retail_ellipsis_before_ordinary_width_validation",
         row_names_use_retail_ellipsis_before_ordinary_width_validation},
        {"dropdown_lists_replace_click_to_cycle_and_overlay_the_grid",
         dropdown_lists_replace_click_to_cycle_and_overlay_the_grid},
        {"horizontal_stat_window_pins_rank_and_name_and_scrolls_like_retail",
         horizontal_stat_window_pins_rank_and_name_and_scrolls_like_retail},
        {"row_capacity_and_vertical_scrollbar_follow_horizontal_overflow",
         row_capacity_and_vertical_scrollbar_follow_horizontal_overflow},
        {"persona_projection_matches_retail_friend_name_override",
         persona_projection_matches_retail_friend_name_override},
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
    return failures == 0U ? 0 : 1;
}
